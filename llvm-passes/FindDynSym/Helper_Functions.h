#pragma once
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
#include <optional>
#include <string.h>

struct ArgPosAndStruct {
    unsigned argIndex;
    std::string structName;
    const llvm::Argument* argPtr;
};

struct AllArgPosAndStruct {
    unsigned argIndex;
    std::string structName;
    const llvm::Argument* argPtr;
    bool isPointer;
};


std::string stripStructPrefix(llvm::StringRef name);

const llvm::Argument* getOriginArgumentImpl(const llvm::Value *V, std::unordered_set<const llvm::Value*>& Visited);
const llvm::Argument* getOriginArgument(const llvm::Value *V);

const llvm::Value *unwrapCasts(const llvm::Value *V);

std::set<std::string> findStructsForStoredArgument(const llvm::Function &F, const llvm::Argument *Arg);

const llvm::DIType *stripTypedefs(const llvm::DIType *Ty);
const llvm::DIType *stripToStruct(const llvm::DIType *Ty);
const llvm::DIType *stripPtrToStruct(const llvm::DIType *Ty);

//bool isFunctionPointerType(const llvm::DIType *Ty);

//std::set<llvm::Function*> collectCallbackMaybeFunctions(const llvm::Module &M);



//void printArgumentType(const llvm::DIType *Ty, llvm::raw_ostream &OS, int ArgIdx = -1);

/* THESE ARE USED FOR LOGGING THE STRUCTS THAT HOLD CALLBACK ARGUMENTS */
//void printDIType(const llvm::DIType *Ty, llvm::raw_ostream &OS, int Depth = 0);
//void reportStructStorageForFuncPtrArgs(const llvm::Function &F, const std::vector<std::pair<llvm::Argument*, unsigned>> &FPArgs);


/* THIS IS USED TO FIND POSSIBLE STRUCT ARGUMENTS TO A FUNCTION */
//std::vector<AllArgPosAndStruct> printStructArgTypes(const llvm::Function &F);
bool reportStructOriginOfIndirCalls(const llvm::CallInst *CB, const llvm::Function &F, const std::vector<ArgPosAndStruct>& structPtrArgs, const std::set<std::string> &AllowedStructNames);



std::optional<const llvm::DIType *> findDITypeForValue(const llvm::Value *V);
bool isFunctionPointerDIType(const llvm::DIType *Ty);