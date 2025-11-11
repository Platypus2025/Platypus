/*
 *  Pass responsible for logging
 *
 *  1) Structs in which CPTRs are stored
 *  2) Structs that are arguments to exported functions (we care about this only in DSOs)
 *  3) Calls that CBGs make to functions with argument a CPTR (if a function is external
 *     then this is useful for adding the corresponding CPTRs to the callback table of the external function)
 */

#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Attributes.h"
#include <fstream>
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Module.h"
#include <unordered_set>
#include "llvm/IR/Value.h"
#include "llvm/IR/Type.h"
#include <string.h>
#include "Helper_Functions.h"

using namespace llvm;
static std::set<std::string> SeenSymbols;


namespace {

struct LogStructs : public PassInfoMixin<LogStructs> {

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &) {
    std::set<Function*> callbackMaybeFns = collectCallbackMaybeFunctions(M);
    
    if (callbackMaybeFns.size() == 0) {
        errs() << "No annotated function\n";
    }

    for (Function *F : callbackMaybeFns) {
        if (!F) continue;
        if (F->isDeclaration()) continue;

        errs() << "Function: " << F->getName() << "\n";

        std::vector<std::pair<Argument*, unsigned>> FPArgs;
        if (DISubprogram *SP = F->getSubprogram())
        if (DISubroutineType *FnTy = SP->getType()) {
            auto Types = FnTy->getTypeArray();
            unsigned ActualArgIndex = 0;
            for (unsigned ArgIdx = 1; ArgIdx < Types.size(); ++ArgIdx, ++ActualArgIndex) {
                const Metadata *Meta = Types[ArgIdx];
                if (!Meta) continue;
                const DIType *ArgType = dyn_cast<DIType>(Meta);
                if (!isFunctionPointerType(ArgType)) continue;


                Argument *Arg = nullptr;
                unsigned argCount = 0;
                for (auto &A : F->args()) {
                    if (argCount == ActualArgIndex) { Arg = &A; break; }
                    ++argCount;
                }
                if (Arg)
                    FPArgs.emplace_back(Arg, ActualArgIndex);

                errs() << "  Arg #" << ActualArgIndex << " type: ";
                printDIType(ArgType, errs());
                errs() << "\n";
            }
        }
        reportStructStorageForFuncPtrArgs(*F, FPArgs);

        CBGcallsFunction(F);
    }

    for (auto &F : M) {
        if (F.hasInternalLinkage()) continue;
        LogAllStructArgTypes(F);
    }

    return PreservedAnalyses::all();
  }
};
}

PassPluginLibraryInfo getLogStructsPluginInfo() {
    return {LLVM_PLUGIN_API_VERSION, "LogStructs", LLVM_VERSION_STRING,
            [](PassBuilder &PB) {
                PB.registerPipelineParsingCallback(
                    [](StringRef Name, ModulePassManager &MPM,
                       ArrayRef<PassBuilder::PipelineElement>) {
                      if (Name == "log-structs") {

                        MPM.addPass(LogStructs());
                        return true;
                      }
                      return false;
                    });
                PB.registerOptimizerLastEPCallback([](ModulePassManager &MPM,
                                                      OptimizationLevel Level, ThinOrFullLTOPhase Phase) {

                    FunctionPassManager FPM;
                    MPM.addPass(LogStructs());

                    MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));
                });
            }};
}

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
    return getLogStructsPluginInfo();
}