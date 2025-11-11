#include "Helper_Functions.h"
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

std::string stripStructPrefix(llvm::StringRef name) {
    if (name.starts_with("struct."))
        return name.substr(7).str();
    if (name.starts_with("union."))
        return name.substr(6).str();
    if (name.starts_with("class."))
        return name.substr(6).str();
    return name.str();
}

const Argument* getOriginArgumentImpl(const Value *V, std::unordered_set<const Value*>& Visited) {
    if (!V) return nullptr;
    if (Visited.count(V)) return nullptr;
    Visited.insert(V);

    if (const auto *Arg = dyn_cast<Argument>(V))
        return Arg;
    if (const auto *BC = dyn_cast<BitCastInst>(V))
        return getOriginArgumentImpl(BC->getOperand(0), Visited);
    if (const auto *GEPI = dyn_cast<GetElementPtrInst>(V))
        return getOriginArgumentImpl(GEPI->getPointerOperand(), Visited);
    if (const auto *LI = dyn_cast<LoadInst>(V)) {
        const Value *ptr = LI->getPointerOperand();
        if (!ptr) return nullptr;
        if (const auto *A = dyn_cast<AllocaInst>(ptr)) {
            for (const auto *U : A->users()) {
                if (const auto *SI = dyn_cast<StoreInst>(U)) {
                    if (SI->getPointerOperand() == A) {
                        const Value *storedVal = SI->getValueOperand();
                        if (!storedVal) continue;
                        if (const Argument *Arg = getOriginArgumentImpl(storedVal, Visited))
                            return Arg;
                    }
                }
            }
        }
    }
    return nullptr;
}

const Argument* getOriginArgument(const Value *V) {
    std::unordered_set<const Value*> Visited;
    return getOriginArgumentImpl(V, Visited);
}

const Value *unwrapCasts(const Value *V) {
    while (auto *BC = dyn_cast<BitCastInst>(V))
        V = BC->getOperand(0);
    return V;
}

std::string getStructTypeNameFromDbg(const llvm::Value *V, const llvm::Function &F) {
    V = unwrapCasts(V);
    for (const auto &BB : F) {
        for (const auto &I : BB) {
            for (const auto &DVRref : llvm::filterDbgVars(I.getDbgRecordRange())) {
                const llvm::DbgVariableRecord &DVR = DVRref.get();
                const Value *dbgV = nullptr;
                if (DVR.isDbgAssign())
                    dbgV = DVR.getAddress();
                else if (DVR.isDbgValue())
                    dbgV = DVR.getValue();
                if (!dbgV) continue;
                if (unwrapCasts(dbgV) != V) continue;

                const llvm::DILocalVariable* Var = DVR.getVariable();
                if (!Var) continue;
                const llvm::DIType *Ty = Var->getType();

                const llvm::DIType *Base = Ty;
                while (Base) {
                    if (const auto *DT = llvm::dyn_cast<llvm::DIDerivedType>(Base)) {
                        unsigned Tag = DT->getTag();
                        if (Tag == llvm::dwarf::DW_TAG_pointer_type ||
                            Tag == llvm::dwarf::DW_TAG_typedef ||
                            Tag == llvm::dwarf::DW_TAG_const_type ||
                            Tag == llvm::dwarf::DW_TAG_restrict_type ||
                            Tag == llvm::dwarf::DW_TAG_volatile_type) {
                            Base = DT->getBaseType();
                            continue;
                        }
                    }
                    break;
                }
                if (auto *CT = llvm::dyn_cast_or_null<llvm::DICompositeType>(Base)) {
                    if (!CT->getName().empty())
                        return CT->getName().str();
                }
            }
        }
    }
    return "";
}



std::set<std::string> findStructsForStoredArgument(const Function &F, const Argument *Arg, std::set<std::string> &PointerGlobalVars) {
    std::set<std::string> StructNames;
    for (const auto &BB : F) {
        for (const auto &I : BB) {
            if (const auto *SI = dyn_cast<StoreInst>(&I)) {
                const Value *V = SI->getValueOperand();
                const Argument *Origin = getOriginArgument(V);
                if (Origin != Arg)
                    continue;

                const Value *Dest = SI->getPointerOperand();
                if (!Dest)
                    continue;

                if (const auto *GEP = dyn_cast<GetElementPtrInst>(Dest)) {
                    Type *PointeeType = GEP->getSourceElementType();
                    if (auto *ST = dyn_cast_or_null<StructType>(PointeeType)) {
                        std::string SName = (ST->hasName() ? ST->getName().str() : "<anon>");
                        StructNames.insert(SName);
                        continue;
                    }
                    if (PointeeType && PointeeType->isIntegerTy(8)) {
                        const Value *base = unwrapCasts(GEP->getPointerOperand());
                        if (!base) continue;
                        if (const AllocaInst *AI = dyn_cast<AllocaInst>(base)) {
                            if (Type *allocTy = AI->getAllocatedType()) {
                                if (const StructType *ST2 = dyn_cast<StructType>(allocTy)) {
                                    std::string SName = (ST2->hasName() ? ST2->getName().str() : "<anon>");
                                    StructNames.insert(SName);
                                }
                            }
                        }
                        else if (const GlobalVariable *GV = dyn_cast<GlobalVariable>(base)) {
                            Type *gvTy = GV->getValueType();
                            if (gvTy) {
                                if (const StructType *ST2 = dyn_cast<StructType>(gvTy)) {
                                    std::string SName = (ST2->hasName() ? ST2->getName().str() : "<anon>");
                                    StructNames.insert(SName);
                                }
                                else if (gvTy->isPointerTy()) {
                                    PointerGlobalVars.insert(GV->getName().str());
                                }
                            }
                        }
                    }
                }

                if (const GlobalVariable *GV = dyn_cast<GlobalVariable>(Dest)) {
                    Type *gvTy = GV->getValueType();
                    if (gvTy) {
                        if (gvTy->isStructTy()) {
                            const StructType *ST = dyn_cast<StructType>(gvTy);
                            if (ST && ST->hasName())
                                StructNames.insert(ST->getName().str());
                        } else if (gvTy->isPointerTy()) {
                            PointerGlobalVars.insert(GV->getName().str());
                        }
                    }
                }
                const Value *DbgBase = Dest;
                if (const GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(Dest)) {
                    DbgBase = unwrapCasts(GEP->getPointerOperand());
                }
                errs() << "Trying debug lookup for dest: "; Dest->print(errs()); errs() << "\n";
                std::string dbgStruct = getStructTypeNameFromDbg(DbgBase, F);
                if (!dbgStruct.empty()) {
                    std::string loggedStruct = dbgStruct;
                    if (!loggedStruct.starts_with("struct."))
                        loggedStruct = "struct." + loggedStruct;
                    StructNames.insert(loggedStruct);
                    errs() << "[DBGINFO STRUCT DETECTED] " << loggedStruct << " for variable used in store:\n";
                    DbgBase->print(errs()); errs() << "\n";
                }
            }
        }
    }
    return StructNames;
}

// bool reportStructOriginOfIndirCalls(const CallInst *CB, const Function &F, const std::vector<ArgPosAndStruct>& structPtrArgs, const std::set<std::string> &AllowedStructNames) {
//             if (!CB) return false;

//             const Value *Called = CB->getCalledOperand()->stripPointerCasts();
//             if (const Function *CF = dyn_cast<Function>(Called))
//                 return false;

//             if (const LoadInst *LI = dyn_cast<LoadInst>(Called)) {
//                 const Value *Ptr = LI->getPointerOperand();
//                 if (const GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(Ptr)) {
//                     const Value *GEPBase = GEP->getPointerOperand()->stripPointerCasts();

//                     // Check if GEP base is a function argument in indices
//                     for (const auto &info : structPtrArgs) {
//                         if (info.argPtr == GEPBase) {
//                             errs() << "LEMAO: ArgPos=" << info.argIndex
//                                    << ", structName=" << (info.structName.empty() ? "<anon>" : stripStructPrefix(info.structName))
//                                    << "\n";
//                             return true;
//                         }
//                     }

//                     Type *PointeeType = GEP->getSourceElementType();
//                     if (auto *ST = dyn_cast<StructType>(PointeeType)) {
//                         if (ST->hasName() && AllowedStructNames.count(stripStructPrefix(ST->getName()))) {
//                             errs() << "Indirect call in function " << F.getName()
//                                 << " uses field from struct: "
//                                 << (ST->hasName() ? stripStructPrefix(ST->getName()) : "<anon>") << "\n";
//                                 return true;
//                         }
//                     }
//                     else if (PointeeType->isIntegerTy(8)) {
//                         const Value *base = unwrapCasts(GEP->getPointerOperand());
//                         if (!base) return false;
//                         if (const AllocaInst *AI = dyn_cast<AllocaInst>(base)) {
//                             Type *allocTy = AI->getAllocatedType();
//                             if (const StructType *ST2 = dyn_cast<StructType>(allocTy)) {
//                                 if (ST2->hasName() && AllowedStructNames.count(stripStructPrefix(ST2->getName()))) {
//                                     errs() << "Indirect call in function " << F.getName()
//                                         << " uses field from struct: "
//                                         << (ST2->hasName() ? stripStructPrefix(ST2->getName()) : "<anon>") << "\n";
//                                         return true;
//                                 }
//                             }
//                         } else if (const GlobalVariable *GV = dyn_cast<GlobalVariable>(base)) {
//                             Type *gvTy = GV->getValueType();
//                             if (const StructType *ST2 = dyn_cast<StructType>(gvTy)) {
//                                 if (ST2->hasName() && AllowedStructNames.count(stripStructPrefix(ST2->getName()))) {
//                                     errs() << "Indirect call in function " << F.getName()
//                                         << " uses field from struct: "
//                                         << (ST2->hasName() ? stripStructPrefix(ST2->getName()) : "<anon>") << "\n";
//                                         return true;
//                                 }
//                             }
//                         }
//                     }
//                 }
//             }
//             return false;
//     //     }
//     // }
// }


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

void CBGcallsFunction(Function *F) {
    std::set<Value*> FArgs;
    for (auto &Arg : F->args())
        FArgs.insert(&Arg);

    for (BasicBlock &BB : *F) {
        for (Instruction &I : BB) {
            if (auto *CB = dyn_cast<CallBase>(&I)) {
                Function *Callee = CB->getCalledFunction();
                if (Callee && Callee->isDeclaration() && !Callee->isIntrinsic()) {
                    for (unsigned argIdx = 0; argIdx < CB->arg_size(); ++argIdx) {
                        if (isCalleeArgFunctionPointer(Callee, argIdx)) {
                            Value *Passed = CB->getArgOperand(argIdx);
                            if (FArgs.count(Passed)) {
                                getLogStream() << "Function : " << F->getName()
                                    << " : "
                                    << Callee->getName()
                                    << "\n";
                            }
                        }
                    }
                }
            }
        }
    }
}

void reportStructStorageForFuncPtrArgs(
    const Function &F,
    const std::vector<std::pair<Argument*, unsigned>> &FPArgs)
{
    for (auto &[Arg, ArgIndex] : FPArgs) {
        std::set<std::string> PointerGlobals;
        std::set<std::string> StructNames = findStructsForStoredArgument(F, Arg, PointerGlobals);

        for (const auto &N : StructNames) {
            getLogStream() << F.getName() << ": " << N << "\n";
        }

        for (const auto &N : PointerGlobals) {
            getLogStream() << N << "\n";
        }
    }
}


const DIType *stripTypedefs(const DIType *Ty) {
    while (Ty && Ty->getTag() == llvm::dwarf::DW_TAG_typedef) {
        const auto *DT = dyn_cast<DIDerivedType>(Ty);
        if (!DT)
            break;
        Ty = DT->getBaseType();
    }
    return Ty;
}

const DIType *stripToStruct(const DIType *Ty) {
    while (Ty && isa<DIDerivedType>(Ty)) {
        if (Ty->getTag() == llvm::dwarf::DW_TAG_pointer_type)
            break;
        Ty = cast<DIDerivedType>(Ty)->getBaseType();
    }
    return Ty;
}


const DIType *stripPtrToStruct(const DIType *Ty) {
    while (Ty && isa<DIDerivedType>(Ty)) {
        if (Ty->getTag() == llvm::dwarf::DW_TAG_pointer_type) {
            const DIType *Pointee = cast<DIDerivedType>(Ty)->getBaseType();
            return stripToStruct(Pointee);
        }
        Ty = cast<DIDerivedType>(Ty)->getBaseType();
    }
    return nullptr;
}

//std::vector<ArgPosAndStruct>
// printStructArgTypes(const llvm::Function &F, const std::set<std::string> &AllowedStructNames) {
//     std::vector<ArgPosAndStruct> result;
//     if (const llvm::DISubprogram *SP = F.getSubprogram()) {
//         if (const llvm::DISubroutineType *FnTy = SP->getType()) {
//             auto Types = FnTy->getTypeArray();
//             unsigned argIndex = 0;
//             for (unsigned i = 1; i < Types.size(); ++i, ++argIndex) {
//                 const llvm::Metadata *Meta = Types[i];
//                 if (!Meta) continue;
//                 const llvm::DIType *ArgTy = llvm::dyn_cast<llvm::DIType>(Meta);
//                 if (!ArgTy) continue;
//                 // Pointer to struct argument
//                 if (const llvm::DIType *StructTyPtr = stripPtrToStruct(ArgTy)) {
//                     if (const auto *CT = llvm::dyn_cast<llvm::DICompositeType>(StructTyPtr)) {
//                         if (CT->getTag() == llvm::dwarf::DW_TAG_structure_type) {
//                             std::string structName = CT->getName().str();
//                             // Only keep if allowed
//                             if (AllowedStructNames.count(structName)) {
//                                 errs() << structName << "\n";
//                                 if (argIndex < F.arg_size()) {
//                                     result.push_back({argIndex, structName, &*std::next(F.arg_begin(), argIndex)});
//                                 }
//                             }
//                         }
//                     }
//                 }
//             }
//         }
//     }
//     return result;
// }

bool structHasFunctionPointerField(const llvm::DICompositeType *CT, unsigned Depth = 0, std::unordered_set<const void*> *Visited = nullptr) {
    if (!CT || Depth > 8) return false;
    std::unordered_set<const void*> LocalVisited;
    if (!Visited)
        Visited = &LocalVisited;

    if (!Visited->insert(CT).second)
        return false;
    for (auto *Elem : CT->getElements()) {
        if (auto *Field = llvm::dyn_cast<llvm::DIDerivedType>(Elem)) {
            const llvm::DIType *FieldTy = Field->getBaseType();
            if (!FieldTy) continue;

            while (FieldTy && (FieldTy->getTag() == llvm::dwarf::DW_TAG_typedef || FieldTy->getTag() == llvm::dwarf::DW_TAG_const_type)) {
                auto *DT = llvm::dyn_cast<llvm::DIDerivedType>(FieldTy);
                if (!DT) break;
                FieldTy = DT->getBaseType();
            }

            if (isFunctionPointerType(FieldTy))
                return true;

            if (auto *PtrDT = llvm::dyn_cast<llvm::DIDerivedType>(FieldTy)) {
                if (PtrDT->getTag() == llvm::dwarf::DW_TAG_pointer_type) {
                    const llvm::DIType *Pointee = PtrDT->getBaseType();
                    const llvm::DIType *BaseStruct = Pointee;
                    while (BaseStruct && (BaseStruct->getTag() == llvm::dwarf::DW_TAG_typedef || BaseStruct->getTag() == llvm::dwarf::DW_TAG_const_type)) {
                        auto *DT = llvm::dyn_cast<llvm::DIDerivedType>(BaseStruct);
                        if (!DT) break;
                        BaseStruct = DT->getBaseType();
                    }
                    if (auto *SubStruct = llvm::dyn_cast_or_null<llvm::DICompositeType>(BaseStruct))
                        if (structHasFunctionPointerField(SubStruct, Depth+1, Visited))
                            return true;
                }
            }

            if (auto *SubStruct = llvm::dyn_cast<llvm::DICompositeType>(FieldTy))
                if (structHasFunctionPointerField(SubStruct, Depth+1, Visited))
                    return true;
        }
    }
    return false;
}

void LogAllStructArgTypes(const llvm::Function &F) {
    if(const llvm::DISubprogram *SP = F.getSubprogram()) {
        if(const llvm::DISubroutineType *FnTy = SP->getType()) {
            auto Types = FnTy->getTypeArray();
            unsigned idx = 0;
            for (auto AI = F.arg_begin(); AI != F.arg_end(); ++AI, ++idx) {
                if (idx + 1 >= Types.size()) break;
                const llvm::DIType *Ty = llvm::dyn_cast_or_null<llvm::DIType>(Types[idx+1]);
                if (!Ty) continue;

                if (auto *CT = llvm::dyn_cast<llvm::DICompositeType>(Ty)) {
                    if (CT->getTag() == llvm::dwarf::DW_TAG_structure_type &&
                        !CT->getName().empty() && structHasFunctionPointerField(CT)) {
                        getLogStream() << "struct." << CT->getName() << " " << F.getName() << "\n";
                        continue;
                    }
                }

                const llvm::DIType *MaybePtr = Ty;
                while (MaybePtr) {
                    auto *DT = llvm::dyn_cast<llvm::DIDerivedType>(MaybePtr);
                    if (!DT) break;
                    MaybePtr = DT->getBaseType();
                }
                if (MaybePtr) {
                    if (auto *CT = llvm::dyn_cast_or_null<llvm::DICompositeType>(MaybePtr)) {
                        if (CT->getTag() == llvm::dwarf::DW_TAG_structure_type &&
                            !CT->getName().empty() && structHasFunctionPointerField(CT)) {
                            getLogStream() << "struct." << CT->getName() << " " << F.getName() << "\n";
                        }
                    }
                }
            }
        }
    }
}


bool isFunctionPointerType(const DIType *Ty) {
    Ty = stripTypedefs(Ty);
    if (!Ty) return false;
    auto *Ptr = dyn_cast<DIDerivedType>(Ty);
    if (!Ptr || Ptr->getTag() != dwarf::DW_TAG_pointer_type) return false;

    const DIType *Base = Ptr->getBaseType();
    while (Base && Base->getTag() == llvm::dwarf::DW_TAG_typedef)
        Base = cast<DIDerivedType>(Base)->getBaseType();

    return Base && Base->getTag() == llvm::dwarf::DW_TAG_subroutine_type;
}

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

void printDIType(const DIType *Ty, raw_ostream &OS, int Depth) {
    if (!Ty) {
        OS << "unknown";
        return;
    }

    // Print indent
    for (int i = 0; i < Depth; ++i) OS << "  ";

    switch (Ty->getTag()) {
        case dwarf::DW_TAG_pointer_type: {
            OS << "*";
            if (const auto *DT = dyn_cast<DIDerivedType>(Ty)) {
                printDIType(DT->getBaseType(), OS, Depth+1);
            }
            break;
        }
        case dwarf::DW_TAG_typedef: {
            if (const auto *DT = dyn_cast<DIDerivedType>(Ty)) {
                OS << DT->getName() << " (typedef for ";
                printDIType(DT->getBaseType(), OS, Depth+1);
                OS << ")";
            }
            break;
        }
        case dwarf::DW_TAG_subroutine_type: {
            OS << "fnptr (";
            if (const auto *ST = dyn_cast<DISubroutineType>(Ty)) {
                auto types = ST->getTypeArray();
                if (types.size() > 0) {
                    OS << "returns ";
                    // --- Fix for possible NULL here:
                    if (types[0]) {
                        printDIType(dyn_cast<DIType>(types[0]), OS, Depth+1);
                    } else {
                        OS << "void";
                    }
                    OS << ", params: ";
                    for (unsigned i = 1; i < types.size(); ++i) {
                        if (types[i]) {
                            printDIType(dyn_cast<DIType>(types[i]), OS, Depth+1);
                        } else {
                            OS << "void";
                        }
                        if (i + 1 < types.size()) OS << ", ";
                    }
                }
            }
            OS << ")";
            break;
        }
        default: {
            if (auto *BT = dyn_cast<DIBasicType>(Ty)) {
                OS << BT->getName();
            } else if (auto *CT = dyn_cast<DICompositeType>(Ty)) {
                OS << CT->getName();
            } else if (auto *DT = dyn_cast<DIDerivedType>(Ty)) {
                OS << DT->getName() << " (derived from ";
                printDIType(DT->getBaseType(), OS, Depth+1);
                OS << ")";
            } else {
                OS << "(unknown type tag " << Ty->getTag() << ")";
            }
            break;
        }
    }
}

// void printArgumentType(const DIType *Ty, raw_ostream &OS, int ArgIdx) {
//     if (ArgIdx >= 0)
//         OS << "  Arg #" << ArgIdx << " type: ";
//     printDIType(Ty, OS, 0);
//     OS << "\n";
// }