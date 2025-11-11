#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/IR/Attributes.h"
#include <fstream>
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/Type.h"
#include <string.h>
#include "llvm/IR/DebugInfoMetadata.h"
#include "Helper_Functions.h"
#include <stack>
#include <set>
#include <map>


using namespace llvm;

static std::string LogFilePath = []{
    const char* p = std::getenv("LOGFILE_PATH");
    return p ? std::string(p) : "/tmp/dynsym.log";
}();


static raw_fd_ostream &getLogStream() {
    static std::error_code EC;
    static raw_fd_ostream fileStream(
        LogFilePath,
        EC,
        sys::fs::OF_Append);
    if (EC) {
        static bool warned = false;
        if (!warned) {
            llvm::errs() << "Warning: Could not open log file: " << EC.message() << "\n";
            warned = true;
        }
        return llvm::errs();
    }
    return fileStream;
}

void findAllFunctionsInValue(const Value *V, std::set<const Function*>& funcs, std::set<const Value*>* seen = nullptr) {
    if (!V) return;
    V = unwrapCasts(V);

    if (auto *F = dyn_cast<Function>(V)) {
        funcs.insert(F);
        return;
    }

    static thread_local std::set<const Value*> inner_seen;
    bool inner = false;
    if (!seen) { seen = &inner_seen; inner = true; seen->clear(); }
    if (!seen->insert(V).second)
        return;

    if (auto *PHI = dyn_cast<PHINode>(V)) {
        for (unsigned i = 0; i < PHI->getNumIncomingValues(); ++i)
            findAllFunctionsInValue(PHI->getIncomingValue(i), funcs, seen);
    }
    if (auto *SEL = dyn_cast<SelectInst>(V)) {
        findAllFunctionsInValue(SEL->getTrueValue(), funcs, seen);
        findAllFunctionsInValue(SEL->getFalseValue(), funcs, seen);
    }
    if (inner) seen->clear();
}


const DICompositeType *getBaseStructType(const DIType *Ty) {
    while (Ty) {
        if (auto *CT = dyn_cast<DICompositeType>(Ty)) return CT;
        if (auto *DT = dyn_cast<DIDerivedType>(Ty))
            Ty = DT->getBaseType();
        else
            break;
    }
    return nullptr;
}


std::string traceArgToStruct(const Value *V,
                             const std::map<const Value*, std::string> &ptrToStruct,
                             std::set<const Value*> &visited) {
    if (!V || visited.count(V)) return "";
    visited.insert(V);

    if (auto it = ptrToStruct.find(V); it != ptrToStruct.end())
        return it->second;

    if (auto *L = dyn_cast<LoadInst>(V))
        return traceArgToStruct(L->getPointerOperand(), ptrToStruct, visited);

    if (auto *G = dyn_cast<GetElementPtrInst>(V))
        return traceArgToStruct(G->getPointerOperand(), ptrToStruct, visited);

    if (auto *P = dyn_cast<PHINode>(V)) {
        for (auto &inc : P->incoming_values()) {
            auto res = traceArgToStruct(inc, ptrToStruct, visited);
            if (!res.empty()) return res;
        }
    }

    if (auto *S = dyn_cast<SelectInst>(V)) {
        auto res = traceArgToStruct(S->getTrueValue(), ptrToStruct, visited);
        if (!res.empty()) return res;
        return traceArgToStruct(S->getFalseValue(), ptrToStruct, visited);
    }

    return "";
}


bool isCalleeArgFunctionPointer(Function *callee, unsigned argIdx) {
    const DISubprogram *CalleeDI = callee->getSubprogram();
    if (!CalleeDI) return false;
    if (!CalleeDI->getType()) return false;
    DITypeRefArray Params = CalleeDI->getType()->getTypeArray();

    if (argIdx+1 >= Params.size()) return false;
    const DIType *paramTy = dyn_cast_or_null<DIType>(Params[argIdx+1]);
    if (!paramTy) return false;

    while (paramTy) {
        if (isa<DISubroutineType>(paramTy))
            return true;
        if (auto *DT = dyn_cast<DIDerivedType>(paramTy))
            paramTy = DT->getBaseType();
        else
            break;
    }
    return false;
}

std::string ensureStructPrefix(const std::string &name) {
    if (name.empty()) return "struct.<unknown>";
    if (name.size() >= 7 && name.substr(0, 7) == "struct.") return name;
    return "struct." + name;
}

const llvm::DIType *unwrapToComposite(const llvm::DIType *Ty) {
    while (Ty && llvm::isa<llvm::DIDerivedType>(Ty))
        Ty = llvm::cast<llvm::DIDerivedType>(Ty)->getBaseType();
    return Ty;
}

void logStructMemberFunctionsWithName(const llvm::Constant *C, const std::string &structName) {
    if (!C) return;

    if (auto *F = llvm::dyn_cast<llvm::Function>(C)) {
        getLogStream() << "Possible Struct Callback global: "
            << F->getName() << " : "
            << ensureStructPrefix(structName) << "\n";
        return;
    }
    if (auto *CS = llvm::dyn_cast<llvm::ConstantStruct>(C)) {
        for (unsigned i = 0; i < CS->getNumOperands(); ++i)
            logStructMemberFunctionsWithName(CS->getOperand(i), structName);
        return;
    }
    if (auto *CA = llvm::dyn_cast<llvm::ConstantArray>(C)) {
        for (unsigned i = 0; i < CA->getNumOperands(); ++i)
            logStructMemberFunctionsWithName(CA->getOperand(i), structName);
        return;
    }
    if (auto *CE = llvm::dyn_cast<llvm::ConstantExpr>(C)) {
        for (unsigned i = 0; i < CE->getNumOperands(); ++i)
            logStructMemberFunctionsWithName(CE->getOperand(i), structName);
        return;
    }
}

struct NonCallFuncPointerPass : public PassInfoMixin<NonCallFuncPointerPass> {
    PreservedAnalyses run(Module &M, ModuleAnalysisManager &) {

        struct FuncStoreLoc {
            Value *ptr;
            Function *func;
            bool isStruct;
        };

        std::vector<FuncStoreLoc> storedFuncPtrs;
        std::map<const Value*, std::pair<std::string, const Value*>> localsFromStruct;

        for (auto &F : M) {
            std::set<std::string> printed;

            for (auto &BB : F) {
                for (auto &I : BB) {
                    auto *store = dyn_cast<StoreInst>(&I);
                    if (store) {
                        
                        if (isa<Function>(store->getValueOperand())) {
                            auto *func = dyn_cast<Function>(store->getValueOperand());
                            Value *storePtr = store->getPointerOperand();
                            
                            bool isStruct = false;
                            if (auto *GEP = dyn_cast<GetElementPtrInst>(storePtr)) {
                                if (GEP->getSourceElementType()->isStructTy())
                                    isStruct = true;
                            }

                            storedFuncPtrs.push_back({storePtr, func, isStruct});

                            for (auto &DbgBB : F) {
                                for (auto &DbgI : DbgBB) {
                                    for (const auto &DVRref : llvm::filterDbgVars(DbgI.getDbgRecordRange())) {
                                        const llvm::DbgVariableRecord &DVR = DVRref.get();


                                        if (!(DVR.isDbgAssign() || DVR.isDbgValue())) continue;

                                        Value *RecordV = nullptr;
                                        if (DVR.isDbgAssign()) {
                                            RecordV = DVR.getAddress();
                                        } else {
                                            RecordV = DVR.getValue();
                                        }

                                        const Value *dbgBase = store->getPointerOperand();
                                        while (auto *GEP = dyn_cast<GetElementPtrInst>(dbgBase))
                                            dbgBase = GEP->getPointerOperand();
                                        dbgBase = unwrapCasts(dbgBase);
                                        if (unwrapCasts(RecordV) != dbgBase) continue;

                                        const llvm::DILocalVariable* Var = DVR.getVariable();


                                        if (Var) {
                                            const llvm::DIType *Ty = Var->getType();
                                            while (Ty && llvm::isa<llvm::DIDerivedType>(Ty)) {
                                                auto *DT = llvm::cast<llvm::DIDerivedType>(Ty);
                                                unsigned tag = DT->getTag();
                                                if (tag == llvm::dwarf::DW_TAG_pointer_type || tag == llvm::dwarf::DW_TAG_typedef)
                                                    Ty = DT->getBaseType();
                                                else
                                                    break;
                                            }

                                            if (const auto *CT = llvm::dyn_cast_or_null<llvm::DICompositeType>(Ty)) {
                                                if (CT->getTag() == llvm::dwarf::DW_TAG_structure_type) {
                                                    uint64_t ByteOffset = 0;
                                                    if (auto *GEP = dyn_cast<GetElementPtrInst>(store->getPointerOperand())) {
                                                        const DataLayout &DL = F.getParent()->getDataLayout();
                                                        APInt Offset(DL.getPointerSizeInBits(0), 0);
                                                        if (GEP->accumulateConstantOffset(DL, Offset)) {
                                                            ByteOffset = Offset.getZExtValue();
                                                        }
                                                    }

                                                    size_t fieldIndex = 0;
                                                    for (auto *Elt : CT->getElements()) {
                                                        if (auto *Member = llvm::dyn_cast<llvm::DIDerivedType>(Elt)) {
                                                            if (Member->getTag() != llvm::dwarf::DW_TAG_member)
                                                                continue;
                                                           
                                                            uint64_t FieldOffsetBits = Member->getOffsetInBits();
                                                            uint64_t FieldSizeBits   = Member->getSizeInBits();

                                                            if (ByteOffset * 8 >= FieldOffsetBits &&
                                                                ByteOffset * 8 < FieldOffsetBits + FieldSizeBits) {

                                                                const llvm::DIType *FieldTy = Member->getBaseType();
                                                                while (FieldTy && llvm::isa<llvm::DIDerivedType>(FieldTy)) {
                                                                    auto *DT = llvm::cast<llvm::DIDerivedType>(FieldTy);
                                                                    unsigned tag = DT->getTag();
                                                                    if (tag == llvm::dwarf::DW_TAG_typedef || tag == llvm::dwarf::DW_TAG_pointer_type) {
                                                                        FieldTy = DT->getBaseType();
                                                                    } else {
                                                                        break;
                                                                    }
                                                                }

                                                                if (FieldTy) {
                                                                    if (auto *CT = llvm::dyn_cast<llvm::DICompositeType>(FieldTy)) {
                                                                        if (CT->getTag() == llvm::dwarf::DW_TAG_structure_type) {

                                                                            std::string structName = CT->getName().str();
                                                                            getLogStream() << "Possible Struct Callback: "
                                                                                        << func->getName() << " : "
                                                                                        << ensureStructPrefix(structName) << " : "
                                                                                        << fieldIndex << "\n";
                                                                        }
                                                                    }
                                                                }
                                                                break;
                                                            }
                                                            fieldIndex++;
                                                        }
                                                    }
                                                }
                                            }
                                            const llvm::DIType *OuterTy = Var->getType();
                                            while (OuterTy && llvm::isa<llvm::DIDerivedType>(OuterTy))
                                                OuterTy = llvm::cast<llvm::DIDerivedType>(OuterTy)->getBaseType();
                                            if (const auto *CT = llvm::dyn_cast_or_null<llvm::DICompositeType>(OuterTy)) {
                                                if (CT->getTag() == llvm::dwarf::DW_TAG_structure_type &&
                                                    !CT->getName().empty()) {

                                                    unsigned fieldIndex = 0;
                                                    if (auto *GEP = dyn_cast<GetElementPtrInst>(store->getPointerOperand())) {
                                                        if (GEP->getNumIndices() >= 2) {
                                                            if (auto *CI = dyn_cast<ConstantInt>(GEP->getOperand(2))) {
                                                                fieldIndex = CI->getZExtValue();
                                                            }
                                                        }
                                                    }

                                                    getLogStream() << "Possible Struct Callback: "
                                                                << store->getValueOperand()->getName() << " : "
                                                                << ensureStructPrefix(std::string(CT->getName())) << " : " << fieldIndex << "\n";
                                                }
                                            }
                                        }

                                    }
                                }
                            }
                            if (auto *GEP = dyn_cast<GetElementPtrInst>(storePtr)) {
                                if (auto *ST = dyn_cast<StructType>(GEP->getSourceElementType())) {
                                    std::string structName;
                                    if (!ST->isLiteral() && ST->hasName()) {
                                        structName = ST->getName().str();
                                    }
                                    else {
                                        structName = "<unnamed>";
                                    }

                                    unsigned fieldIndex = 0;
                                    if (GEP->getNumIndices() >= 2) {
                                        if (auto *CI = dyn_cast<ConstantInt>(GEP->getOperand(2))) {
                                            fieldIndex = CI->getZExtValue();
                                        }
                                    }

                                    getLogStream() << "Possible Struct Callback: "
                                                << func->getName() << " : "
                                                << ensureStructPrefix(structName) << " : " << fieldIndex << "\n";
                                }
                            }
                        }

                        Value *val = store->getValueOperand();
                        Value *dst = store->getPointerOperand();

                        if (auto *ld = dyn_cast_or_null<LoadInst>(unwrapCasts(val))) {
                            const Value *loadedFrom = unwrapCasts(ld->getPointerOperand());
                            if (loadedFrom) {
                                auto it = localsFromStruct.find(loadedFrom);
                                if (it != localsFromStruct.end()) {
                                    localsFromStruct[dst] = { it->second.first, loadedFrom };
                                }
                                if (auto *gep = dyn_cast<GetElementPtrInst>(loadedFrom)) {
                                    if (auto *ST = dyn_cast<StructType>(gep->getSourceElementType())) {
                                        if (!ST->isLiteral() && ST->hasName())
                                            localsFromStruct[dst] = { ST->getName().str(), gep };
                                    }
                                }
                            }
                        }


                        if (val && localsFromStruct.count(val)) {
                            localsFromStruct[dst] = { localsFromStruct[val].first, val };
                        }


                        if (auto *gepDst = dyn_cast<GetElementPtrInst>(dst)) {
                            if (auto *ST = dyn_cast<StructType>(gepDst->getSourceElementType())) {
                                if (!ST->isLiteral() && ST->hasName())
                                    localsFromStruct[dst] = { ST->getName().str(), dst };
                            }
                        }
                    }

                    if (auto *call = dyn_cast<CallBase>(&I)) {
                        auto *callee = dyn_cast<Function>(call->getCalledOperand()->stripPointerCasts());

                        for (unsigned arg_pos = 0; arg_pos < call->arg_size(); ++arg_pos) {
                            Value *arg = call->getArgOperand(arg_pos);
                            if (auto *func = dyn_cast<Function>(arg)) {
                                if (call->getCalledOperand() == func) continue;
                                getLogStream() << (callee ? callee->getName() : "<unknown>")
                                                << ": " << func->getName() 
                                                << " at arg index " << arg_pos
                                                << "\n";
                            }
                        }
                    }
                }
            }

            for (auto &BB : F) {
                for (auto &I : BB) {
                    auto *call = dyn_cast<CallBase>(&I);
                    if (!call) continue;
                    Value *calleeOp = call->getCalledOperand();
                    if (!calleeOp) continue;
                    Value *callee = calleeOp->stripPointerCasts();

                    for (unsigned arg_pos = 0; arg_pos < call->arg_size(); ++arg_pos) {
                        Value *arg = call->getArgOperand(arg_pos);


                        if(call->getCalledFunction() && isCalleeArgFunctionPointer(call->getCalledFunction(), arg_pos)) {
                            const Value *base = unwrapCasts(arg);

                            if (auto *ld = dyn_cast<LoadInst>(base))
                                base = unwrapCasts(ld->getPointerOperand());

                            std::set<const Value*> visited;
                            std::map<const Value*, std::string> ptrToStruct;
                            for (auto &p : localsFromStruct)
                                ptrToStruct[p.first] = p.second.first;
                            std::string structName = traceArgToStruct(arg, ptrToStruct, visited);
                            if (!structName.empty()) {
                                getLogStream() << call->getCalledFunction()->getName()
                                            << ": arg " << arg_pos
                                            << " : may come from struct : "
                                            << ensureStructPrefix(structName) << "\n";
                            }    
                        }


                        if (call->getCalledFunction()) {
                            Value *base = arg;
                            if (auto *ld = dyn_cast<LoadInst>(arg))
                                base = ld->getPointerOperand();

                            if (auto *gep = dyn_cast<GetElementPtrInst>(base)) {
                                if (gep->getNumOperands() >= 1) {
                                    if (auto *ST = dyn_cast_or_null<StructType>(gep->getSourceElementType())) {
                                        std::string structName = ST->hasName() ? stripStructPrefix(ST->getName().str()) : "<unnamed>";
                                        getLogStream() << call->getCalledFunction()->getName()
                                                    << ": arg " << arg_pos
                                                    << " : may come from struct : "
                                                    << ensureStructPrefix(structName) << "\n";
                                    }
                                }
                            }
                        

                            for (auto &BB2 : F) {
                                for (auto &I2 : BB2) {
                                    if (auto *store = dyn_cast<StoreInst>(&I2)) {
                                        if (store->getPointerOperand() == base) {
                                            Value *storedVal = store->getValueOperand();
                                            if (!storedVal) continue;

                                            if (auto *Fptr = dyn_cast<Function>(storedVal)) {
                                                getLogStream() << call->getCalledFunction()->getName()
                                                            << ": " << Fptr->getName() 
                                                            << " at arg index " << arg_pos
                                                            << "\n";
                                            } 
                                            else if (auto *phi = dyn_cast<PHINode>(storedVal)) {
                                                for (unsigned i = 0; i < phi->getNumIncomingValues(); ++i) {
                                                    Value *inc = phi->getIncomingValue(i);
                                                    if (!inc) continue;

                                                    if (auto *Fptr = dyn_cast<Function>(inc)) {
                                                        getLogStream() << call->getCalledFunction()->getName()
                                                                    << ": " << Fptr->getName() 
                                                                    << " at arg index " << arg_pos
                                                                    << "\n";
                                                    } 
                                                    else if (auto *ld2 = dyn_cast<LoadInst>(inc)) {
                                                        Value *ptr = ld2->getPointerOperand();
                                                        if (!ptr) continue;
                                                        if (auto *GEP = dyn_cast<GetElementPtrInst>(ptr)) {
                                                            if (auto *ST = dyn_cast_or_null<StructType>(GEP->getSourceElementType())) {
                                                                std::string structName = ST->hasName() ? stripStructPrefix(ST->getName().str()) : "<unnamed>";
                                                                getLogStream() << call->getCalledFunction()->getName()
                                                                            << ": arg " << arg_pos
                                                                            << " : may come from struct : "
                                                                            << ensureStructPrefix(structName) << "\n";
                                                            } else {
                                                                getLogStream() << call->getCalledFunction()->getName()
                                                                            << ": arg " << arg_pos
                                                                            << " phi incoming load not from struct\n";
                                                            }
                                                        }
                                                    } 
                                                    else if (auto *GEP = dyn_cast<GetElementPtrInst>(inc)) {
                                                        if (auto *ST = dyn_cast_or_null<StructType>(GEP->getSourceElementType())) {
                                                            std::string structName = ST->hasName() ? stripStructPrefix(ST->getName().str()) : "<unnamed>";
                                                            getLogStream() << call->getCalledFunction()->getName()
                                                                        << ": arg " << arg_pos
                                                                        << " : may come from struct : "
                                                                        << ensureStructPrefix(structName) << "\n";
                                                        }
                                                    } 
                                                    else {
                                                        getLogStream() << call->getCalledFunction()->getName()
                                                                    << ": arg " << arg_pos
                                                                    << " phi incoming unknown type\n";
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        for (const auto& S : storedFuncPtrs) {
                            if (!S.ptr) continue;
                            Function *f = dyn_cast<Function>(callee);

                            if (unwrapCasts(S.ptr) == unwrapCasts(arg)) {
                                getLogStream() << (f ? f->getName() : "<unknown>")
                                << ": " << (S.func ? S.func->getName() : "<null_func>")
                                << " at arg index " << arg_pos << "\n";
                            }

                            if (auto *ld = dyn_cast<LoadInst>(unwrapCasts(arg))) {
                                Value *loadedFrom = const_cast<Value*>(ld->getPointerOperand());
                                if (unwrapCasts(S.ptr) == unwrapCasts(loadedFrom)) {
                                    getLogStream() << (f ? f->getName() : "<unknown>")
                                    << ": " << (S.func ? S.func->getName() : "<null_func>")
                                    << " at arg index " << arg_pos << "\n";
                                }

                                if (auto *GEP = dyn_cast<GetElementPtrInst>(unwrapCasts(loadedFrom))) {
                                    if (auto *ST = dyn_cast<StructType>(GEP->getSourceElementType())) {
                                        std::string structName;
                                        if (!ST->isLiteral() && ST->hasName()){
                                            structName = ST->getName().str();
                                        }
                                        else {
                                            structName = "<unnamed>";
                                        }
                                        getLogStream() << (f ? f->getName() : "<unknown>")
                                                    << ": arg " << arg_pos
                                                    << " : may come from struct : "
                                                    << ensureStructPrefix(structName) << "\n";
                                    }
                                }
                            }
                        }


                        std::set<const Function*> fptrs;
                        findAllFunctionsInValue(arg, fptrs);

                        Function *f = dyn_cast<Function>(callee);

                        for (auto *fptr : fptrs) {
                            if (!f || f != fptr) {
                                getLogStream() << (f ? f->getName() : "<unknown>")
                                    << ": " << (fptr ? fptr->getName() : "<null_func>")
                                    << " at arg index " << arg_pos << "\n";
                            }
                        }
                        // if (const Function *fptr = findFunctionInValue(arg)) {
                        //     llvm::errs() << "arg name: " 
                        //     << (arg->hasName() ? arg->getName() : "<unnamed>")
                        //     << ", type: " << *(arg->getType()) << "\n";
                        //     arg->print(llvm::errs());
                        //     llvm::errs() << "\n";
                        //     Function *f = dyn_cast<Function>(callee);
                        //     if (!f || f != fptr) {
                        //         getLogStream() << (f ? f->getName() : "<unknown>")
                        //         << ": " << (fptr ? fptr->getName() : "<null_func>")
                        //         << " at arg index1 " << arg_pos << "\n";
                        //     }
                        // }
                    }
                }
            }
        }


        for (auto &F : M) {
            for (auto &BB : F) {
                for (auto &I : BB) {
                    auto *call = dyn_cast<CallBase>(&I);
                    if (!call) continue;
                    Function *callee = call->getCalledFunction();
                    if (!callee || !callee->isDeclaration()) continue;
                    
                    for (unsigned i = 0; i < call->arg_size(); ++i) {
                        Value *Arg = call->getArgOperand(i);

                        const Value *basePtr = unwrapCasts(Arg);
                        if (auto *ld = dyn_cast<LoadInst>(basePtr))
                            basePtr = unwrapCasts(ld->getPointerOperand());

                        if (const GetElementPtrInst *gep = dyn_cast<GetElementPtrInst>(basePtr)) {
                            const Value *gepBase = unwrapCasts(gep->getPointerOperand());

                            for (auto &DbgBB : F) {
                                for (auto &DbgI : DbgBB) {
                                    for (const auto &DVRref : llvm::filterDbgVars(DbgI.getDbgRecordRange())) {
                                        const llvm::DbgVariableRecord &DVR = DVRref.get();
                                        if (!(DVR.isDbgAssign() || DVR.isDbgValue())) continue;
                                        const Value *RecordV = nullptr;
                                        if (DVR.isDbgAssign()) RecordV = DVR.getAddress();
                                        if (DVR.isDbgValue()) RecordV = DVR.getValue();
                                        if (unwrapCasts(RecordV) != gepBase) continue;

                                        const auto *Var = DVR.getVariable();
                                        if (!Var) continue;
                                        const DIType *Ty = Var->getType();
                                        const DICompositeType *StructTy = getBaseStructType(Ty);
                                        if (!StructTy) continue;

                                        StringRef StructName = StructTy->getName();
                                        std::string CleanName = stripStructPrefix(StructName);

                                        if (isCalleeArgFunctionPointer(callee, i)) {
                                            getLogStream() << "Argument in function " << F.getName() << " " << i << " to call : " 
                                                << (callee ? callee->getName() : "<unknown>")
                                                << " : " << CleanName
                                                << "\n";
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }


        for (const llvm::GlobalVariable &GV : M.globals()) {
            if (!GV.hasInitializer()) continue;
            std::string structNameDbg;

            if (auto *N = GV.getMetadata("dbg")) {
                if (auto *Expr = llvm::dyn_cast<llvm::DIGlobalVariableExpression>(N)) {
                    if (const auto *DIGV = Expr->getVariable()) {
                        const llvm::DIType *Ty = DIGV->getType();
                        Ty = unwrapToComposite(Ty);
                        if (Ty) {
                            if (auto *CT = llvm::dyn_cast<llvm::DICompositeType>(Ty)) {
                                structNameDbg = CT->getName().str();
                            }
                        }
                    }
                }
                else if (auto *MD = llvm::dyn_cast<llvm::MDNode>(N)) {
                    for (const auto &op : MD->operands()) {
                        if (!op) continue;
                        if (auto *Expr = llvm::dyn_cast<llvm::DIGlobalVariableExpression>(op)) {
                            if (const auto *DIGV = Expr->getVariable()) {
                                const llvm::DIType *Ty = DIGV->getType();
                                Ty = unwrapToComposite(Ty);
                                if (Ty) {
                                    if (auto *CT = llvm::dyn_cast<llvm::DICompositeType>(Ty)) {
                                        structNameDbg = CT->getName().str();
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
            }

            if (structNameDbg.empty()) {
                if (auto *Init = GV.getInitializer()) {
                    if (auto *CS = llvm::dyn_cast<llvm::ConstantStruct>(Init)) {
                        if (const llvm::StructType *ST = CS->getType()) {
                            if (ST->hasName())
                                structNameDbg = ST->getName().str();
                        }
                    }
                }
            }

            logStructMemberFunctionsWithName(GV.getInitializer(), structNameDbg);
        }


        return PreservedAnalyses::all();
    }
};


llvm::PassPluginLibraryInfo getFindDynSymPluginInfo() {
    return {LLVM_PLUGIN_API_VERSION, "FindDynSym", LLVM_VERSION_STRING,
        [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, ModulePassManager &MPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                    if (Name == "dyn-sym") {
                        MPM.addPass(NonCallFuncPointerPass());
                        return true;
                    }
                    return false;
                }
            );
            PB.registerOptimizerLastEPCallback([](ModulePassManager &MPM,
                                                      OptimizationLevel Level, ThinOrFullLTOPhase Phase) {
                MPM.addPass(NonCallFuncPointerPass());
            });
        }
    };
}

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
    return getFindDynSymPluginInfo();
}