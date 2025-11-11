#pragma once
#include "llvm/CodeGen/MachineFunctionPass.h"

namespace llvm {

struct X86InstrumentJmpPass : public MachineFunctionPass {
    static char ID;
    X86InstrumentJmpPass() : MachineFunctionPass(ID) {}
    bool runOnMachineFunction(MachineFunction &MF) override;
};
void initializeX86InstrumentJmpPassPass(PassRegistry &);

} // namespace llvm