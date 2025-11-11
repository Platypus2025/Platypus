#include "Helper_Functions.h"
using namespace llvm;


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


bool hasFuncPtrFieldRecursive(const llvm::DIType *Ty, const std::unordered_set<std::string>&, unsigned depth = 0) {
    if (!Ty || depth > 5) return false;

    if (Ty->getTag() == llvm::dwarf::DW_TAG_pointer_type) {
        if (auto *PtrTy = llvm::dyn_cast<llvm::DIDerivedType>(Ty)) {
            auto *Base = PtrTy->getBaseType();
            if (Base && Base->getTag() == llvm::dwarf::DW_TAG_subroutine_type) {
                errs() << std::string(depth*2, ' ') << "[FOUND] Function pointer field at depth " << depth << "\n";
                return true;
            }
        }
    }

    if (auto *CT = llvm::dyn_cast<llvm::DICompositeType>(Ty)) {
        errs() << std::string(depth*2, ' ') << "[DEBUG] Recursing into struct: " << CT->getName() << "\n";
        DINodeArray elements = CT->getElements();
        for (auto *Elem : elements) {
            if (auto *Field = llvm::dyn_cast<llvm::DIDerivedType>(Elem)) {
                errs() << std::string(depth*2, ' ') << "Field: " << Field->getName();
                if (const auto *FT = Field->getBaseType())
                    errs() << " (type: " << FT->getName() << ", tag: " << FT->getTag() << ")";
                errs() << "\n";
                // Recursively check ALL struct fields, not just those in the allowlist!
                if (hasFuncPtrFieldRecursive(Field->getBaseType(), {}, depth + 1))
                    return true;
            }
        }
    }

    return false;
}

static bool hasFunctionPointerField(const llvm::DICompositeType *CT) {
    if (!CT) return false;
    for (auto *Elem : CT->getElements()) {
        if (auto *Field = llvm::dyn_cast<llvm::DIDerivedType>(Elem)) {
            const llvm::DIType *FT = Field->getBaseType();
            if (FT && FT->getTag() == llvm::dwarf::DW_TAG_pointer_type) {
                if (auto *PtrTy = llvm::dyn_cast<llvm::DIDerivedType>(FT)) {
                    auto *Base = PtrTy->getBaseType();
                    if (Base && Base->getTag() == llvm::dwarf::DW_TAG_subroutine_type)
                        return true;
                }
            }
        }
    }
    return false;
}

const llvm::DILocalVariable* getDILocalVariableForAlloca(const llvm::AllocaInst* AI) {
    if (!AI) return nullptr;
    const llvm::Function *F = AI->getFunction();
    for (const auto &BB : *F) {
        for (const auto &I : BB) {
            if (auto *Dbg = llvm::dyn_cast<llvm::DbgDeclareInst>(&I)) {
                if (Dbg->getAddress() == AI)
                    return Dbg->getVariable();
            }
        }
    }
    return nullptr;
}

const DICompositeType* resolveStructDIType(const Value* V, const Function& F) {
    if (auto *Arg = dyn_cast<Argument>(V)) {
        if (const DISubprogram *SP = F.getSubprogram()) {
            unsigned argNum = Arg->getArgNo() + 1;
            for (auto *DIN : SP->getRetainedNodes()) {
                if (const auto *DILV = dyn_cast<DILocalVariable>(DIN)) {
                    if (DILV->getArg() == argNum) {
                        if (const DIType *T = stripPtrToStruct(DILV->getType()))
                            return dyn_cast<DICompositeType>(T);
                    }
                }
            }
        }
    } 
    if (auto *AI = dyn_cast<AllocaInst>(V)) {
        if (const DILocalVariable *DILV = getDILocalVariableForAlloca(AI)) {
            if (const DIType *T = stripPtrToStruct(DILV->getType()))
                return dyn_cast<DICompositeType>(T);
        }
    }
    return nullptr;
}


bool isValuePointerToAllowedStruct_ViaDbgRecord(const llvm::Value *V, const llvm::Function &F, const std::unordered_set<std::string> &AllowedStructNames) {
    V = unwrapCasts(V);
    for (const auto &BB : F) {
        for (const auto &I : BB) {
            for (const auto &DVRref : llvm::filterDbgVars(I.getDbgRecordRange())) {
                const llvm::DbgVariableRecord &DVR = DVRref.get();
                
                const Value *RecordV = nullptr;
                if (DVR.isDbgAssign() || DVR.isDbgDeclare())
                    RecordV = DVR.getAddress();
                else if (DVR.isDbgValue())
                    RecordV = DVR.getValue();
                else
                    continue;
                if (!RecordV)
                    continue;
                if (unwrapCasts(RecordV) != V)
                    continue;

                const llvm::DILocalVariable* Var = DVR.getVariable();
                if (!Var) continue;
                const llvm::DIType* Ty = Var->getType();
                const llvm::DIType* Tyy = Var->getType();
                
                while (Ty && (
                    Ty->getTag() == llvm::dwarf::DW_TAG_pointer_type ||
                    Ty->getTag() == llvm::dwarf::DW_TAG_const_type ||
                    Ty->getTag() == llvm::dwarf::DW_TAG_volatile_type ||
                    Ty->getTag() == llvm::dwarf::DW_TAG_restrict_type))
                {
                    Ty = llvm::cast<llvm::DIDerivedType>(Ty)->getBaseType();
                }

                llvm::errs() << "---- Type Chain for variable '" << Var->getName() << "' ----\n";
                const llvm::DIType* Current = Ty;
                while (Current) {
                    std::string tagName;
                    unsigned tag = Current->getTag();
                    switch (tag) {
                    case llvm::dwarf::DW_TAG_typedef: tagName = "typedef"; break;
                    case llvm::dwarf::DW_TAG_structure_type: tagName = "struct"; break;
                    case llvm::dwarf::DW_TAG_union_type: tagName = "union"; break;
                    case llvm::dwarf::DW_TAG_base_type: tagName = "base"; break;
                    default: tagName = "tag" + std::to_string(tag); break;
                    }
                    std::string tname = Current->getName().str();
                    if (tname.empty()) tname = "<unnamed>";
                    if (AllowedStructNames.count(tname)) {
                        llvm::errs() << "[CBITMASK] Matched allowed struct or typedef: " << tname << "\n";
                        llvm::errs() << "------------------------------------------\n";
                        return true;
                    }
                    llvm::errs() << "  [" << tagName << "]: " << tname << "\n";

                    if (auto *DT = llvm::dyn_cast<llvm::DIDerivedType>(Current)) {
                        Current = DT->getBaseType();
                    } else {
                        break;
                    }
                }
                llvm::errs() << "------------------------------------------\n";
                const llvm::DIType* StructTy = stripPtrToStruct(Tyy);
                if (auto* CT = llvm::dyn_cast_or_null<llvm::DICompositeType>(StructTy)) {
                    std::string structName = CT->getName().str();
                    if (AllowedStructNames.count(structName)) {
                        llvm::errs() << "[CBITMASK] Local variable (dbg.record) " << Var->getName()
                                     << " is ptr to allowed struct type " << structName << "\n";
                        return true;
                    }
                }
            }
        }
    }
    return false;
}


int resolveChainForAllowedStruct(const llvm::Value *V, const llvm::Function &F, const std::unordered_set<std::string> &AllowedStructNames, int depth = 0, int maxDepth = 8, std::unordered_set<const Value *> *VisitedPtr = nullptr) {

    std::unordered_set<const Value *> LocalVisited;
    std::unordered_set<const Value *> &Visited =
        VisitedPtr ? *VisitedPtr : LocalVisited;

    if (!V || depth > maxDepth) return 0;
    if (Visited.count(V)) return 0;
    Visited.insert(V);

    llvm::errs() << "[DEBUG] At pointer chain depth " << depth << ": ";
    V->print(llvm::errs()); llvm::errs() << "\n";

    if (isValuePointerToAllowedStruct_ViaDbgRecord(V, F, AllowedStructNames)) {
            return 1;
        }

    
    if (const llvm::DICompositeType* StructCT = resolveStructDIType(unwrapCasts(V), F)) {
        std::string structName = StructCT->getName().str();
        llvm::errs() << "[DEBUG]   Struct type: " << structName
            << (AllowedStructNames.count(structName) ? " [ALLOWED]\n" : " [not allowed]\n");
        if (AllowedStructNames.count(structName)) {
            llvm::errs() << "[rsoc] Matched allowed struct: " << structName << " at depth " << depth << "\n";
            return 1;
        }
    }

    // 1b. ONLY FOR GLIBC
    // if (const Argument *Arg = dyn_cast<Argument>(unwrapCasts(V))) {
    //     if (Arg->getName() == "pglob") {
    //         llvm::errs() << "[CBITMASK] Matched argument by name: 'pglob' at depth " << depth << "\n";
    //         return 1; // or instrument!
    //     }
    // }

    if (const llvm::LoadInst *LI = llvm::dyn_cast<llvm::LoadInst>(V)) {
        const llvm::Value *ptrOperand = LI->getPointerOperand();

        if (const auto *GEP = llvm::dyn_cast<llvm::GetElementPtrInst>(ptrOperand)) {
            if (const llvm::DICompositeType* StructCT = resolveStructDIType(unwrapCasts(GEP->getPointerOperand()), F)) {
                unsigned fieldIdx = 0;
                if (GEP->getNumIndices() >= 2) {
                    auto gepIdxIt = GEP->idx_begin();
                    ++gepIdxIt;
                    if (auto *CI = llvm::dyn_cast<llvm::ConstantInt>(gepIdxIt->get()))
                        fieldIdx = CI->getZExtValue();
                }
                auto elements = StructCT->getElements();
                if (fieldIdx < elements.size()) {
                    if (auto *Field = llvm::dyn_cast<llvm::DIDerivedType>(elements[fieldIdx])) {
                        if (const auto *FT = Field->getBaseType()) {
                            std::string fieldTypeName = FT->getName().str();
                            llvm::errs() << "[DEBUG]   GEP field type: " << fieldTypeName
                                << (AllowedStructNames.count(fieldTypeName) ? " [ALLOWED FIELD]\n" : "\n");
                            if (AllowedStructNames.count(fieldTypeName)) {
                                llvm::errs() << "[rsoc] Matched allowed struct field type: "
                                    << fieldTypeName << " at GEP field " << Field->getName() << " (depth " << depth << ")\n";
                                return 1;
                            }
                        }
                    }
                }
            }
        }
        
        if (const llvm::DICompositeType* StructCT = resolveStructDIType(unwrapCasts(ptrOperand), F)) {
            llvm::DINodeArray elements = StructCT->getElements();
            if (!elements.empty()) {
                if (auto *Field0 = llvm::dyn_cast<llvm::DIDerivedType>(elements[0])) {
                    if (const auto *FT0 = Field0->getBaseType()) {
                        std::string fieldTypeName = FT0->getName().str();
                        llvm::errs() << "[DEBUG]   Field 0 at load: " << fieldTypeName
                            << (AllowedStructNames.count(fieldTypeName) ? " [ALLOWED FIELD 0]\n" : "\n");
                        if (AllowedStructNames.count(fieldTypeName)) {
                            llvm::errs() << "[rsoc] Matched allowed field type for raw field 0: "
                                << fieldTypeName << " at load (depth " << depth << ")\n";
                            return 1;
                        }
                    }
                }
            }
        }

        if (isValuePointerToAllowedStruct_ViaDbgRecord(ptrOperand, F, AllowedStructNames)) {
            return 1;
        }

        return resolveChainForAllowedStruct(ptrOperand, F, AllowedStructNames, depth + 1, maxDepth, &Visited);
    }

    else if (const auto *PHI = llvm::dyn_cast<llvm::PHINode>(V)) {
        llvm::errs() << "[WARN] Could not map PHI: ";
        PHI->print(llvm::errs());
        llvm::errs() << "\nKnown DILocalVariable(s):\n";
        auto *Func = PHI->getFunction();

        for (const auto &BB : *Func) {
            for (const auto &Inst : BB) {
                for (const auto &DVRref : llvm::filterDbgVars(Inst.getDbgRecordRange())) {
                    const llvm::DbgVariableRecord &DVR = DVRref.get();
                    const llvm::DILocalVariable* Var = DVR.getVariable();
                    if (!Var) continue;

                    const llvm::DIType *Ty = Var->getType();
                    while (Ty && llvm::isa<llvm::DIDerivedType>(Ty)) {
                        Ty = llvm::cast<llvm::DIDerivedType>(Ty)->getBaseType();
                    }
                    if (const auto *CT = llvm::dyn_cast_or_null<llvm::DICompositeType>(Ty)) {
                        std::string structName = CT->getName().str();
                        llvm::errs() << "  [dbg.value] variable " << Var->getName()
                                    << " underlying type: " << structName << "\n";
                        if (AllowedStructNames.count(structName)) {
                            llvm::errs() << "[rsoc] PHI matches allowed struct: " << structName << "\n";
                            return 1;
                        }
                    }
                }
            }
        }
        for (unsigned i = 0; i < PHI->getNumIncomingValues(); ++i) {
            if (resolveChainForAllowedStruct(PHI->getIncomingValue(i), F, AllowedStructNames, depth + 1, maxDepth, &Visited))
                return 1;
        }
        return 0;
    }

    else if (const auto *SI = llvm::dyn_cast<llvm::SelectInst>(V)) {
        return resolveChainForAllowedStruct(SI->getTrueValue(), F, AllowedStructNames, depth + 1, maxDepth, &Visited)
            || resolveChainForAllowedStruct(SI->getFalseValue(), F, AllowedStructNames, depth + 1, maxDepth, &Visited);
    }

    else if (const auto *GEP = llvm::dyn_cast<llvm::GetElementPtrInst>(V)) {
        return resolveChainForAllowedStruct(GEP->getPointerOperand(), F, AllowedStructNames, depth + 1, maxDepth, &Visited);
    }
    else if (const auto *BC = llvm::dyn_cast<llvm::BitCastInst>(V)) {
        return resolveChainForAllowedStruct(BC->getOperand(0), F, AllowedStructNames, depth + 1, maxDepth, &Visited);
    }

    return 0;
}

int reportStructOriginOfIndirCalls(const CallInst *CB, const Function &F, const std::vector<ArgPosAndStruct>& structPtrArgs, const std::unordered_set<std::string> &AllowedStructNames) {
            if (!CB) return 0;

            const Value *Called = CB->getCalledOperand()->stripPointerCasts();
            if (const Function *CF = dyn_cast<Function>(Called))
                return 0;

            const Value *GEPBase = nullptr;    
            if (const LoadInst *LI1 = dyn_cast<LoadInst>(Called)) {
                const Value *Ptr1 = LI1->getPointerOperand();

                if (const LoadInst *LI2 = dyn_cast<LoadInst>(Ptr1)) {
                    const Value *Ptr2 = LI2->getPointerOperand();
                    if (const Argument *Arg = dyn_cast<Argument>(unwrapCasts(Ptr2))) {

                        if (const DISubprogram *SP = F.getSubprogram()) {
                            unsigned argNum = Arg->getArgNo() + 1;
                            for (auto *DIN : SP->getRetainedNodes()) {
                                if (const auto *DILV = dyn_cast<DILocalVariable>(DIN)) {
                                    if (DILV->getArg() != argNum) continue;
                                    const DIType *StructDI = stripPtrToStruct(DILV->getType());
                                    const auto *CT = dyn_cast_or_null<DICompositeType>(StructDI);
                                    if (!CT) continue;
                                    auto elements = CT->getElements();
                                    for (unsigned idx = 0; idx < elements.size(); ++idx) {
                                        if (const auto* Field = dyn_cast<DIDerivedType>(elements[idx])) {
                                            const DIType* fieldType = Field->getBaseType();
                                            if (const auto* NestedCT = dyn_cast<DICompositeType>(fieldType)) {
                                                std::string nestedStructName = NestedCT->getName().str();
                                                if (AllowedStructNames.count(nestedStructName) &&
                                                    hasFunctionPointerField(NestedCT))
                                                {
                                                    errs() << "[rsoc] Matched 2-level deref: "
                                                        << CT->getName() << "::" << Field->getName()
                                                        << " -> " << nestedStructName
                                                        << " (function pointer found inside via double load)\n";
                                                    return 1;
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                if (const Argument *Arg = dyn_cast<Argument>(unwrapCasts(Ptr1))) {
                    if (const DISubprogram *SP = F.getSubprogram()) {
                        unsigned argNum = Arg->getArgNo() + 1;
                        for (auto *DIN : SP->getRetainedNodes()) {
                            if (const auto *DILV = dyn_cast<DILocalVariable>(DIN)) {
                                if (DILV->getArg() != argNum) continue;
                                const DIType *StructDI = stripPtrToStruct(DILV->getType());
                                const auto *CT = dyn_cast_or_null<DICompositeType>(StructDI);
                                if (!CT) continue;
                                std::string structName = stripStructPrefix(CT->getName().str());
                                auto elements = CT->getElements();
                                for (unsigned idx = 0; idx < elements.size(); ++idx) {
                                    if (const auto *Field = dyn_cast<DIDerivedType>(elements[idx])) {
                                        const DIType* fieldType = Field->getBaseType();
                                        unsigned tag = fieldType ? fieldType->getTag() : 0;
                                        std::string fieldname = Field->getName().str();

                                        // if (F.getName() == "expand_builtin_function") {
                                        //     llvm::errs() << "[DEBUG] Struct: " << structName
                                        //                 << ", Field#" << idx
                                        //                 << " (" << fieldname << "), tag: " << tag << "\n";
                                        // }
                                        if (AllowedStructNames.count(structName)
                                            && fieldType
                                            && fieldType->getTag() == llvm::dwarf::DW_TAG_pointer_type) {
                                            if (const auto *PtrTy = dyn_cast<DIDerivedType>(fieldType)) {
                                                const DIType* base = PtrTy->getBaseType();
                                                if (base && base->getTag() == llvm::dwarf::DW_TAG_subroutine_type) {
                                                    llvm::errs() << "[MATCH] Will instrument: " << structName 
                                                                << "::" << fieldname << " (function pointer field)\n";
                                                    return 1;
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                            

                if (const GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(Ptr1)) {
                    GEPBase = GEP->getPointerOperand()->stripPointerCasts();
                    const Value *realArg = getOriginArgument(GEPBase);

                    for (const auto &info : structPtrArgs) {
                        if (info.argPtr == realArg) {
                            // errs() << "LEMAO: ArgPos=" << info.argIndex
                            //        << ", structName=" << (info.structName.empty() ? "<anon>" : stripStructPrefix(info.structName))
                            //        << "\n";
                            if (!info.structName.empty() && stripStructPrefix(info.structName) == "rtld_global_ro") {
                                return 2;
                            }
                            return 1;
                        }
                    }

                    Type *PointeeType = GEP->getSourceElementType();
                    if (auto *ST = dyn_cast<StructType>(PointeeType)) {
                        if (ST->hasName() && AllowedStructNames.count(stripStructPrefix(ST->getName()))) {
                            errs() << "Indirect call in function " << F.getName()
                                << " uses field from struct: "
                                << (ST->hasName() ? stripStructPrefix(ST->getName()) : "<anon>") << "\n";
                                if (ST->hasName() && stripStructPrefix(ST->getName()) == "rtld_global_ro")
                                    return 2;
                                return 1;
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
                                        if (ST2->hasName() && stripStructPrefix(ST2->getName()) == "rtld_global_ro")
                                            return 2;
                                        return 1;
                                }
                            }
                        } else if (const GlobalVariable *GV = dyn_cast<GlobalVariable>(base)) {
                            Type *gvTy = GV->getValueType();
                            if (GV->getName() == "rtld_global_ro") {
                                return 2;
                            }
                            if (const StructType *ST2 = dyn_cast<StructType>(gvTy)) {
                                if (ST2->hasName() && AllowedStructNames.count(stripStructPrefix(ST2->getName()))) {
                                    errs() << "Indirect call in function " << F.getName()
                                        << " uses field from struct: "
                                        << (ST2->hasName() ? stripStructPrefix(ST2->getName()) : "<anon>") << "\n";
                                        return 1;
                                }
                            }
                        }
                    }
                } else if (const ConstantExpr *CE = dyn_cast<ConstantExpr>(Ptr1)) {
                    if (CE->getOpcode() == Instruction::GetElementPtr) {
                        GEPBase = CE->getOperand(0)->stripPointerCasts();
                        const GlobalVariable *GV = dyn_cast<GlobalVariable>(GEPBase);
                        if (GV->getName() == "_rtld_global_ro")
                            return 2;
                    }
                }
            }


            errs() << "[DEBUG] --- Starting pointer chain analysis for call in " << F.getName() << " ---\n";
            errs() << "[DEBUG] Callee operand (depth 0): ";
            if (Called) Called->print(errs());
            errs() << "\n";

            if (resolveChainForAllowedStruct(Called, F, AllowedStructNames))
                return 1;
            return 0;
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

std::vector<ArgPosAndStruct>
printStructArgTypes(const llvm::Function &F, const std::unordered_set<std::string> &AllowedStructNames) {
    std::vector<ArgPosAndStruct> result;
    if (const llvm::DISubprogram *SP = F.getSubprogram()) {
        if (const llvm::DISubroutineType *FnTy = SP->getType()) {
            auto Types = FnTy->getTypeArray();
            unsigned argIndex = 0;
            for (unsigned i = 1; i < Types.size(); ++i, ++argIndex) {
                const llvm::Metadata *Meta = Types[i];
                if (!Meta) continue;
                const llvm::DIType *ArgTy = llvm::dyn_cast<llvm::DIType>(Meta);
                if (!ArgTy) continue;

                if (const llvm::DIType *StructTyPtr = stripPtrToStruct(ArgTy)) {
                    if (const auto *CT = llvm::dyn_cast<llvm::DICompositeType>(StructTyPtr)) {
                        if (CT->getTag() == llvm::dwarf::DW_TAG_structure_type) {
                            std::string structName = CT->getName().str();
                            if (AllowedStructNames.count(structName)) {
                                if (argIndex < F.arg_size()) {
                                    result.push_back({argIndex, structName, &*std::next(F.arg_begin(), argIndex)});
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return result;
}

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