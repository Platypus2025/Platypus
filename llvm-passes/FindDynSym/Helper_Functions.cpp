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

std::set<std::string> findStructsForStoredArgument(const Function &F, const Argument *Arg) {
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
                            if (Type *gvTy = GV->getValueType()) {
                                if (const StructType *ST2 = dyn_cast<StructType>(gvTy)) {
                                    std::string SName = (ST2->hasName() ? ST2->getName().str() : "<anon>");
                                    StructNames.insert(SName);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return StructNames;
}

bool reportStructOriginOfIndirCalls(const CallInst *CB, const Function &F, const std::vector<ArgPosAndStruct>& structPtrArgs, const std::set<std::string> &AllowedStructNames) {
            if (!CB) return false;

            const Value *Called = CB->getCalledOperand()->stripPointerCasts();
            if (const Function *CF = dyn_cast<Function>(Called))
                return false;

            if (const LoadInst *LI = dyn_cast<LoadInst>(Called)) {
                const Value *Ptr = LI->getPointerOperand();
                if (const GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(Ptr)) {
                    const Value *GEPBase = GEP->getPointerOperand()->stripPointerCasts();

                    for (const auto &info : structPtrArgs) {
                        if (info.argPtr == GEPBase) {
                            errs() << "FOUND: ArgPos=" << info.argIndex
                                   << ", structName=" << (info.structName.empty() ? "<anon>" : stripStructPrefix(info.structName))
                                   << "\n";
                            return true;
                        }
                    }

                    Type *PointeeType = GEP->getSourceElementType();
                    if (auto *ST = dyn_cast<StructType>(PointeeType)) {
                        if (ST->hasName() && AllowedStructNames.count(stripStructPrefix(ST->getName()))) {
                            errs() << "Indirect call in function " << F.getName()
                                << " uses field from struct: "
                                << (ST->hasName() ? stripStructPrefix(ST->getName()) : "<anon>") << "\n";
                                return true;
                        }
                    }
                    else if (PointeeType->isIntegerTy(8)) {
                        const Value *base = unwrapCasts(GEP->getPointerOperand());
                        if (!base) return false;
                        if (const AllocaInst *AI = dyn_cast<AllocaInst>(base)) {
                            Type *allocTy = AI->getAllocatedType();
                            if (const StructType *ST2 = dyn_cast<StructType>(allocTy)) {
                                if (ST2->hasName() && AllowedStructNames.count(stripStructPrefix(ST2->getName()))) {
                                    errs() << "Indirect call in function " << F.getName()
                                        << " uses field from struct: "
                                        << (ST2->hasName() ? stripStructPrefix(ST2->getName()) : "<anon>") << "\n";
                                        return true;
                                }
                            }
                        } else if (const GlobalVariable *GV = dyn_cast<GlobalVariable>(base)) {
                            Type *gvTy = GV->getValueType();
                            if (const StructType *ST2 = dyn_cast<StructType>(gvTy)) {
                                if (ST2->hasName() && AllowedStructNames.count(stripStructPrefix(ST2->getName()))) {
                                    errs() << "Indirect call in function " << F.getName()
                                        << " uses field from struct: "
                                        << (ST2->hasName() ? stripStructPrefix(ST2->getName()) : "<anon>") << "\n";
                                        return true;
                                }
                            }
                        }
                    }
                }
            }
            return false;
}


// void reportStructStorageForFuncPtrArgs(const Function &F, const std::vector<std::pair<Argument*, unsigned>> &FPArgs) {
//     for (auto &[Arg, ArgIndex] : FPArgs) {
        
//         std::set<std::string> StructNames = findStructsForStoredArgument(F, Arg);
        
//         errs() << "    Arg #" << ArgIndex << " ('" << Arg->getName() << "') ";
//         if (StructNames.empty())
//             continue;
//         else {
//             getLogStream() << "File:: " << F.getParent()->getName().str() << " Arg #" << ArgIndex << " ('" << Arg->getName() << "') ";
//             getLogStream() << "is stored in struct(s): ";
//             bool first = true;
//             for (const auto &N : StructNames) {
//                 if (!first) getLogStream() << ", ";
//                 getLogStream() << N;
//                 first = false;
//             }
//             getLogStream() << "\n";
//         }
//     }
// }


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

// std::vector<ArgPosAndStruct>
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


// std::vector<AllArgPosAndStruct>
// printStructArgTypes(const llvm::Function &F) {
//     std::vector<AllArgPosAndStruct> result;
//     if(const llvm::DISubprogram *SP = F.getSubprogram()) {
//         if(const llvm::DISubroutineType *FnTy = SP->getType()) {
//             auto Types = FnTy->getTypeArray();
//             unsigned idx = 0;
//             for (auto AI = F.arg_begin(); AI != F.arg_end(); ++AI, ++idx) {
//                 if(idx+1 >= Types.size()) break;
//                 const llvm::DIType *Ty = llvm::dyn_cast_or_null<llvm::DIType>(Types[idx+1]);
//                 if (!Ty) continue;

//                 // (1) Struct by value
//                 if(auto *CT = llvm::dyn_cast<llvm::DICompositeType>(Ty)) {
//                     if(CT->getTag() == llvm::dwarf::DW_TAG_structure_type) {
//                         result.push_back({idx, CT->getName().str(), &*AI, false});
//                         continue;
//                     }
//                 }

//                 // (2) Pointer (multi-level) to struct
//                 const llvm::DIType *MaybePtr = Ty;
//                 bool sawPointer = false;
//                 while (MaybePtr) { // <-- check for nullptr!
//                     auto *DT = llvm::dyn_cast<llvm::DIDerivedType>(MaybePtr);
//                     if (!DT) break;
//                     if (DT->getTag() == llvm::dwarf::DW_TAG_pointer_type)
//                         sawPointer = true;
//                     MaybePtr = DT->getBaseType();
//                 }
//                 if(MaybePtr) { // Don't dereference nullptr!
//                     if(auto *CT = llvm::dyn_cast_or_null<llvm::DICompositeType>(MaybePtr)) {
//                         if(CT->getTag() == llvm::dwarf::DW_TAG_structure_type) {
//                             result.push_back({idx, CT->getName().str(), &*AI, sawPointer});
//                         }
//                     }
//                 }
//             }
//         }
//     }
//     return result;
// }


// bool isFunctionPointerType(const DIType *Ty) {
//     Ty = stripTypedefs(Ty);
//     if (!Ty) return false;
//     auto *Ptr = dyn_cast<DIDerivedType>(Ty);
//     if (!Ptr || Ptr->getTag() != dwarf::DW_TAG_pointer_type) return false;
//     auto *Base = Ptr->getBaseType();
//     return Base && Base->getTag() == dwarf::DW_TAG_subroutine_type;
// }

// std::set<Function*> collectCallbackMaybeFunctions(const Module &M) {
//     std::set<Function*> callbackMaybeFns;
//     if (auto *GA = M.getGlobalVariable("llvm.global.annotations")) {
//         if (GA->hasInitializer()) {
//             if (auto *CA = dyn_cast<ConstantArray>(GA->getInitializer())) {
//                 for (unsigned i = 0; i < CA->getNumOperands(); ++i) {
//                     auto *Struct = dyn_cast<ConstantStruct>(CA->getOperand(i));
//                     if (!Struct) continue;
//                     auto *Fn = Struct->getOperand(0)->stripPointerCasts();
//                     auto *AnnoStr = Struct->getOperand(1);
//                     if (auto *AnnoGlobal = dyn_cast<GlobalVariable>(AnnoStr->stripPointerCasts())) {
//                         if (auto *AnnoCString = dyn_cast<ConstantDataArray>(AnnoGlobal->getInitializer())) {
//                             std::string anno = AnnoCString->getAsCString().str();
//                             if (anno == "callback_maybe") {
//                                 if (auto *F = dyn_cast<Function>(Fn)) {
//                                     llvm::errs() << "Inserting function: " << F->getName() << "\n";
//                                     callbackMaybeFns.insert(F);
//                                 }
//                             }
//                         }
//                     }
//                 }
//             }
//         }
//     }
//     return callbackMaybeFns;
// }

// void printDIType(const DIType *Ty, raw_ostream &OS, int Depth) {
//     if (!Ty) {
//         OS << "unknown";
//         return;
//     }

//     // Print indent
//     for (int i = 0; i < Depth; ++i) OS << "  ";

//     switch (Ty->getTag()) {
//         case dwarf::DW_TAG_pointer_type: {
//             OS << "*";
//             if (const auto *DT = dyn_cast<DIDerivedType>(Ty)) {
//                 printDIType(DT->getBaseType(), OS, Depth+1);
//             }
//             break;
//         }
//         case dwarf::DW_TAG_typedef: {
//             if (const auto *DT = dyn_cast<DIDerivedType>(Ty)) {
//                 OS << DT->getName() << " (typedef for ";
//                 printDIType(DT->getBaseType(), OS, Depth+1);
//                 OS << ")";
//             }
//             break;
//         }
//         case dwarf::DW_TAG_subroutine_type: {
//             OS << "fnptr (";
//             if (const auto *ST = dyn_cast<DISubroutineType>(Ty)) {
//                 auto types = ST->getTypeArray();
//                 if (types.size() > 0) {
//                     OS << "returns ";
//                     // --- Fix for possible NULL here:
//                     if (types[0]) {
//                         printDIType(dyn_cast<DIType>(types[0]), OS, Depth+1);
//                     } else {
//                         OS << "void";
//                     }
//                     OS << ", params: ";
//                     for (unsigned i = 1; i < types.size(); ++i) {
//                         if (types[i]) {
//                             printDIType(dyn_cast<DIType>(types[i]), OS, Depth+1);
//                         } else {
//                             OS << "void";
//                         }
//                         if (i + 1 < types.size()) OS << ", ";
//                     }
//                 }
//             }
//             OS << ")";
//             break;
//         }
//         default: {
//             if (auto *BT = dyn_cast<DIBasicType>(Ty)) {
//                 OS << BT->getName();
//             } else if (auto *CT = dyn_cast<DICompositeType>(Ty)) {
//                 OS << CT->getName();
//             } else if (auto *DT = dyn_cast<DIDerivedType>(Ty)) {
//                 OS << DT->getName() << " (derived from ";
//                 printDIType(DT->getBaseType(), OS, Depth+1);
//                 OS << ")";
//             } else {
//                 OS << "(unknown type tag " << Ty->getTag() << ")";
//             }
//             break;
//         }
//     }
// }

// void printArgumentType(const DIType *Ty, raw_ostream &OS, int ArgIdx) {
//     if (ArgIdx >= 0)
//         OS << "  Arg #" << ArgIdx << " type: ";
//     printDIType(Ty, OS, 0);
//     OS << "\n";
// }


/* ############################################################################## */

std::optional<const DIType *> findDITypeForValue(const Value *V) {
    V = unwrapCasts(V);

    if (auto *LI = dyn_cast<LoadInst>(V))
        return findDITypeForValue(LI->getPointerOperand());

    if (auto *Arg = dyn_cast<Argument>(V)) {
        auto &Entry = Arg->getParent()->getEntryBlock();
        for (auto &I : Entry) {
            if (auto *II = dyn_cast<IntrinsicInst>(&I)) {
                if (II->getIntrinsicID() == Intrinsic::dbg_declare) {
                    auto *DDI = cast<DbgDeclareInst>(II);
                    if (DDI->getAddress() == Arg) {
                        auto *Ty = DDI->getVariable()->getType();
                        if (Ty) return Ty;
                    }
                }
                if (II->getIntrinsicID() == Intrinsic::dbg_value) {
                    auto *DVI = cast<DbgValueInst>(II);
                    if (DVI->getValue() == Arg) {
                        auto *Ty = DVI->getVariable()->getType();
                        if (Ty) return Ty;
                    }
                }
            }
        }
    }

    if (auto *AI = dyn_cast<AllocaInst>(V)) {
        for (const User *U : AI->users()) {
            if (auto *II = dyn_cast<IntrinsicInst>(U)) {
                if (II->getIntrinsicID() == Intrinsic::dbg_declare) {
                    auto *DDI = cast<DbgDeclareInst>(II);
                    if (DDI->getAddress() == AI) {
                        auto *Ty = DDI->getVariable()->getType();
                        if (Ty) return Ty;
                    }
                }
                if (II->getIntrinsicID() == Intrinsic::dbg_value) {
                    auto *DVI = cast<DbgValueInst>(II);
                    if (DVI->getValue() == AI) {
                        auto *Ty = DVI->getVariable()->getType();
                        if (Ty) return Ty;
                    }
                }
            }
        }
    }

    for (const User *U : V->users()) {
        if (auto *II = dyn_cast<IntrinsicInst>(U)) {
            if (II->getIntrinsicID() == Intrinsic::dbg_declare) {
                auto *DDI = cast<DbgDeclareInst>(II);
                if (DDI->getAddress() == V) {
                    auto *Ty = DDI->getVariable()->getType();
                    if (Ty) return Ty;
                }
            }
            if (II->getIntrinsicID() == Intrinsic::dbg_value) {
                auto *DVI = cast<DbgValueInst>(II);
                if (DVI->getValue() == V) {
                    auto *Ty = DVI->getVariable()->getType();
                    if (Ty) return Ty;
                }
            }
        }
    }
    return std::nullopt;
}

bool isFunctionPointerDIType(const DIType *Ty) {
    if (!Ty)
        return false;
    while (auto *DT = dyn_cast<DIDerivedType>(Ty)) {
        if (DT->getTag() == dwarf::DW_TAG_typedef) {
            Ty = DT->getBaseType();
        } else if (DT->getTag() == dwarf::DW_TAG_pointer_type) {
            auto *Pointee = DT->getBaseType();
            if (Pointee && isa<DISubroutineType>(Pointee))
                return true;
            Ty = Pointee;
        } else {
            break;
        }
    }
    return isa<DISubroutineType>(Ty);
}