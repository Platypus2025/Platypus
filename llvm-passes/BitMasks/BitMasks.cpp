/*
 *  This PASS instruments the indirect calls with the OR / AND bitmasks
 */

#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/IR/Attributes.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/IR/InlineAsm.h"
#include <fstream>
#include "../../dso_callbacks.h"
#include "Helper_Functions.h"

using namespace llvm;

namespace {

static bool Modified = false;


std::set<Function*> collectCallbackMaybeFunctions(const Module &M) {
    std::set<Function*> callbackMaybeFns;
    if (auto *GA = M.getGlobalVariable("llvm.global.annotations")) {
        if (GA->hasInitializer()) {
            if (auto *CA = dyn_cast<ConstantArray>(GA->getInitializer())) {
                for (unsigned i = 0; i < CA->getNumOperands(); ++i) {
                    auto *Struct = dyn_cast<ConstantStruct>(CA->getOperand(i));
                    if (!Struct) continue;
                    auto *Fn = Struct->getOperand(0)->stripPointerCasts();
                    auto *AnnoStr = Struct->getOperand(1);
                    if (auto *AnnoGlobal = dyn_cast<GlobalVariable>(AnnoStr->stripPointerCasts())) {
                        if (auto *AnnoCString = dyn_cast<ConstantDataArray>(AnnoGlobal->getInitializer())) {
                            std::string anno = AnnoCString->getAsCString().str();
                            if (anno == "callback_maybe") {
                                if (auto *F = dyn_cast<Function>(Fn)) {
                                    llvm::errs() << "Inserting function: " << F->getName() << "\n";
                                    callbackMaybeFns.insert(F);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return callbackMaybeFns;
}


bool instrumentNormalCase(IRBuilder<> &Builder, CallInst *Call, Value *target, GlobalVariable *Ormask, GlobalVariable *Andmask) {
    Value *or_inst = Builder.CreateOr(
        Builder.CreatePtrToInt(target, Builder.getInt64Ty()),
        Builder.CreatePtrToInt(Ormask, Builder.getInt64Ty())
    );
    Value *and_inst = Builder.CreateAnd(or_inst, Builder.CreatePtrToInt(Andmask, Builder.getInt64Ty()));
    Value *ModifiedPtr = Builder.CreateIntToPtr(and_inst, target->getType());
    Call->setCalledOperand(ModifiedPtr);
    return true;
}

bool always_callback_table(IRBuilder<> &Builder, CallInst *Call, Value *target, u_int32_t table = 0) {
    Type *Int8PtrTy = PointerType::get(Type::getInt8Ty(Call->getContext()), 0);
    Value *CalleePtr = Builder.CreateBitCast(target, Int8PtrTy);

    Module *Mod = Call->getModule();
    std::vector<Type*> argTypes;
    std::vector<Value*> Args;
    for (auto &arg : Call->args()) {
        argTypes.push_back(arg->getType());
        Args.push_back(arg.get());
    }

    FunctionType *MovR11FTy = FunctionType::get(Builder.getVoidTy(), {Int8PtrTy}, false);

    InlineAsm* MovR11Asm = InlineAsm::get(
        MovR11FTy,
        "movq $0, %r11",
        "r",
        /*hasSideEffects=*/true
    );
    Builder.CreateCall(MovR11Asm, {CalleePtr});

    //FunctionType *FTy = FunctionType::get(Call->getType(), argTypes, false);
    FunctionType *FTy = Call->getFunctionType();
    FunctionCallee ExtFunc;
    if (table == 3) {
        ExtFunc = Mod->getOrInsertFunction("threadkey_callback_table", FTy);
    } else if (table == 2) {
        ExtFunc = Mod->getOrInsertFunction("fini_callback_table", FTy);
        llvm::errs() << "DID\n";
    }
    else {
        ExtFunc = Mod->getOrInsertFunction("callback_table", FTy);
    }

    CallInst *NewCall = Builder.CreateCall(ExtFunc, Args);
    if (!Call->getType()->isVoidTy()) {
        Call->replaceAllUsesWith(NewCall);
    }
    Call->eraseFromParent();
    return true;
}


struct BitmasksAddition : public PassInfoMixin<BitmasksAddition> {
    PreservedAnalyses run(Module &M, ModuleAnalysisManager &) {

        /* FOR THE SPEEDTEST IN SQLITE TESTSUITE */
        std::string identifier = M.getModuleIdentifier();
        std::vector<std::string> skip_substrings = {"speedtest"};

        for (const auto& sub : skip_substrings) {
            if (identifier.find(sub) != std::string::npos) {
                errs() << "SKIPPING SPEEDTEST INSTRUMENTATION\n";
                return PreservedAnalyses::all();
            }
        }
        /* END */

        Modified = false;
        std::set<Function*> callbackMaybeFns = collectCallbackMaybeFunctions(M);
        //logUnusedCallbackArgs(callbackMaybeFns);

        GlobalVariable *Ormask = M.getNamedGlobal("or_mask");
        if (!Ormask) {
            Ormask = new GlobalVariable(
                M, Type::getInt64Ty(M.getContext()), /*isConstant=*/false,
                GlobalValue::ExternalLinkage,
                /*Initializer=*/nullptr,
                "or_mask"
            );
        }
        GlobalVariable *Andmask = M.getNamedGlobal("and_mask");
        if (!Andmask) {
            Andmask = new GlobalVariable(
                M, Type::getInt64Ty(M.getContext()), false,
                GlobalValue::ExternalLinkage,
                nullptr,
                "and_mask"
            );
        }


        if (std::getenv("LOADER") && M.getSourceFileName().find("dl-call_fini") == std::string::npos) {
            // const char *l = std::getenv("LOADER");
            // if (l) {
            //     llvm::errs() << "LOADER env is set to: " << l << "\n";
            //     llvm::errs() << F.getName() << "\n";
            //     exit(0x123);
            // }
            // else
            //     llvm::errs() << "LOADER env is NOT set!\n";
            return PreservedAnalyses::all();
        }

        std::unordered_set<std::string> allowed = struct_names;

        for (auto &F: M) {
            if (F.getName() == "__getrandom_early_init"  || F.getName() == "main_init_once") continue;
            bool iscallback = false;
            if (callbackMaybeFns.count(&F)) {
                 llvm::errs() << "Instrumenting with callbacks: " << F.getName() << "\n";
                iscallback = true;
            }
            auto indices = printStructArgTypes(F, allowed);

            SmallPtrSet<Value*, 8> Args;
            for (auto &A : F.args())
                Args.insert(&A);

            std::vector<CallInst*> Candidates;
            for (auto &BB : F) {
                for (auto &Inst : BB) {
                    if (auto *Call = dyn_cast<CallInst>(&Inst)) {
                        if (!Call->getCalledFunction()) { // indirect
                            Value *target = Call->getCalledOperand();
                            if (!isa<InlineAsm>(target)) {
                                // if (Args.count(target)) {
                                //     Candidates.push_back(Call);
                                // }
                                Candidates.push_back(Call);
                            }
                        }
                    }

                    // else if (auto *IndBr = dyn_cast<IndirectBrInst>(&Inst)) {
                    //     IRBuilder<> Builder(IndBr);

                    //     // Get the original pointer to jump address
                    //     Value *origTarget = IndBr->getAddress();
                    //     // Or/And it
                    //     Value *or_val = Builder.CreateOr(
                    //         Builder.CreatePtrToInt(origTarget, Builder.getInt64Ty()),
                    //         Builder.CreatePtrToInt(Ormask, Builder.getInt64Ty())
                    //     );
                    //     Value *and_val = Builder.CreateAnd(
                    //         or_val,
                    //         Builder.CreatePtrToInt(Andmask, Builder.getInt64Ty())
                    //     );
                    //     Value *maskedTarget = Builder.CreateIntToPtr(and_val, origTarget->getType());

                    //     // Set the new address operand on IndirectBrInst
                    //     IndBr->setAddress(maskedTarget);

                    //     Modified = true; // If you're tracking this
                    // }


                }
            }

            for (CallInst *Call : Candidates) {
                Value *target = Call->getCalledOperand();

                /* SPECIAL CORNER CASES INSIDE GLIBC
                 * Instrument so that they always end to the callback table of libc
                 */
                if (F.getName().ends_with("__nptl_deallocate_tsd")) {
                    IRBuilder<> Builder(Call);
                    Modified |= always_callback_table(Builder, Call, target, 3);
                    continue;
                } else if (F.getName() == "_dl_call_fini" && std::getenv("LOADER")) {
                    llvm::errs() << "LOADER " << "\n";
                    IRBuilder<> Builder(Call);
                    Modified |= always_callback_table(Builder, Call, target, 2);
                    F.print(llvm::errs());
                    //exit(0x123);
                    continue;
                }
                else if (F.getName() == "__run_exit_handlers" || F.getName() == "__libc_start_main_impl" || F.getName() == "__libc_start_call_main" || F.getName() == "__GI___backtrace" || F.getName() == "backtrace_helper" || F.getName() == "start_thread" || F.getName() == "__cxa_finalize") {
                    IRBuilder<> Builder(Call);
                    Modified |= always_callback_table(Builder, Call, target);
                    continue;
                }

                int rtld_glob = reportStructOriginOfIndirCalls(Call, F, indices, allowed);

                if (rtld_glob == 2) {
                    continue;
                }

                GlobalVariable* GV1 = nullptr;
                if (rtld_glob == 0) {

                    if (auto *LI = dyn_cast<LoadInst>(target)) {
                        Value *ptr = LI->getPointerOperand();
                        GV1 = dyn_cast<GlobalVariable>(ptr->stripPointerCasts());
                    } else {
                        GV1 = dyn_cast<GlobalVariable>(target->stripPointerCasts());
                    }
                }

                if (iscallback || rtld_glob == 1 || (GV1 && global_names.count(GV1->getName().str()))) {


                     std::vector<Type*> ArgTypes;
                    // SmallVector<Value*, 8> Args;
                    for (auto &arg : Call->args()) {
                        ArgTypes.push_back(arg->getType());
                    }

                    IRBuilder<> Builder(Call);
                    Type *Int8PtrTy = PointerType::get(Type::getInt8Ty(Call->getContext()), 0);
                                Value *CalleePtr = Builder.CreateBitCast(target, Int8PtrTy);

                    FunctionType *MovFTy = FunctionType::get(Type::getVoidTy(Call->getContext()), {Int8PtrTy}, false);
                    InlineAsm *MovR11IA = InlineAsm::get(MovFTy, "movq $0, %r11", "r,~{r11}", true);
                    Builder.CreateCall(MovR11IA, {CalleePtr});

                    FunctionType *OrFTy = FunctionType::get(Int8PtrTy, {Int8PtrTy}, false);
                                InlineAsm *OrCmpAsm = InlineAsm::get(
                                    OrFTy,
                                    "orq or_mask(%rip), $0\n\tandq and_mask(%rip), $0",
                                    "=r,0", true
                                );
                    Value *OredPtr = Builder.CreateCall(OrCmpAsm, {CalleePtr});
                    Value *IsZero = Builder.CreateICmpEQ(CalleePtr, OredPtr);

                    FunctionType *FTy = FunctionType::get(Call->getType(), ArgTypes, false);
                    BasicBlock *OrigBlock = Call->getParent();

                    Function *Fun = OrigBlock->getParent();
                    BasicBlock *AfterCallBlock = OrigBlock->splitBasicBlock(Call, "aftercall");
                    BasicBlock *CallBlock = BasicBlock::Create(M.getContext(), "callblock", Fun);
                    BasicBlock *SkipBlock = BasicBlock::Create(M.getContext(), "skipblock", Fun);



                    IRBuilder<> builder(OrigBlock->getTerminator());
                    //builder.CreateCondBr(IsZero, CallBlock, SkipBlock);
                    BranchInst *Br = builder.CreateCondBr(IsZero, CallBlock, SkipBlock);
                    OrigBlock->getTerminator()->eraseFromParent();

                    llvm::MDBuilder MDB(M.getContext());

                    uint32_t likely = 2000, unlikely = 1;
                    llvm::MDNode *weights = MDB.createBranchWeights(likely, unlikely);
                    Br->setMetadata(llvm::LLVMContext::MD_prof, weights);

                    IRBuilder<> builderCall(CallBlock);
                    std::vector<Value *> Args;
                    for (auto &Arg : Call->args())
                        Args.push_back(Arg.get());

                    CallInst *CallInCallBlock = builderCall.CreateCall(Call->getFunctionType(), Call->getCalledOperand(), Args);
                    builderCall.CreateBr(AfterCallBlock);

                    IRBuilder<> builderSkip(SkipBlock);

                    FunctionCallee ExtFunc = M.getOrInsertFunction("callback_table", FTy);
                    CallInst *CallInSkipBlock = builderSkip.CreateCall(ExtFunc, Args);
                    //CallInst *CallInSkipBlock = builderSkip.CreateCall(FTy, GV, Args);
                    builderSkip.CreateBr(AfterCallBlock);

                    IRBuilder<> builderAfter(AfterCallBlock, AfterCallBlock->getFirstNonPHIIt());

                    if (!Call->getType()->isVoidTy()) {
                    PHINode *phi = builderAfter.CreatePHI(Call->getType(), 2, "mergedcall");
                    phi->addIncoming(CallInCallBlock, CallBlock);
                    phi->addIncoming(CallInSkipBlock, SkipBlock);
                    Call->replaceAllUsesWith(phi);
                    } else {

                    }
                    Call->eraseFromParent();

                    Modified = true;

                } else {
                    IRBuilder<> Builder(Call);
                    Modified |= instrumentNormalCase(Builder, Call, target, Ormask, Andmask);
                }
            }
        }

        return Modified ? PreservedAnalyses::none() : PreservedAnalyses::all();
    }
};

struct BitmasksAdditionPrinter : public PassInfoMixin<BitmasksAdditionPrinter> {
    PreservedAnalyses run(Function &F, FunctionAnalysisManager &) {
        return PreservedAnalyses::all();
        errs() << "*** Bitmask Addition PASS EXECUTING ***\n";
        if (Modified) {
            errs() << "Some instruction was replaced.\n";
        } else {
            errs() << "Nothing changed.\n";
        }
        return PreservedAnalyses::all();
    }
};
}

PassPluginLibraryInfo getBitmasksAdditionPluginInfo() {
    return {LLVM_PLUGIN_API_VERSION, "BitmasksAddition", LLVM_VERSION_STRING,
            [](PassBuilder &PB) {
                PB.registerPipelineParsingCallback(
                    [](StringRef Name, ModulePassManager &MPM,
                       ArrayRef<PassBuilder::PipelineElement>) {
                      if (Name == "bitmasks-addition") {
                        MPM.addPass(BitmasksAddition());
                        //MPM.addPass(BitmasksAdditionPrinter());
                        return true;
                      }
                      return false;
                    });
                PB.registerOptimizerLastEPCallback([](ModulePassManager &MPM,
                                                      OptimizationLevel Level, ThinOrFullLTOPhase Phase) {
                    FunctionPassManager FPM;
                    MPM.addPass(BitmasksAddition());
                    //MPM.addPass(BitmasksAdditionPrinter());

                    //MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));
                });
            }};
}

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
    return getBitmasksAdditionPluginInfo();
}
