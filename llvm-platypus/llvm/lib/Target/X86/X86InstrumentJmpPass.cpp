#include "X86InstrumentJmpPass.h"
#include "X86.h"
#include "X86InstrInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/InitializePasses.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/GlobalVariable.h"

using namespace llvm;

#define DEBUG_TYPE "x86-instrument-jmp"
char X86InstrumentJmpPass::ID = 0;

bool X86InstrumentJmpPass::runOnMachineFunction(MachineFunction &MF) {


  if (!std::getenv("PROTECT_JMP"))
        return false;

    bool Modified = false;

    const X86InstrInfo *TII =
        static_cast<const X86InstrInfo *>(MF.getSubtarget().getInstrInfo());
    MachineRegisterInfo &MRI = MF.getRegInfo();

    Module *M = MF.getFunction().getParent();

    // Global masks
    GlobalVariable *OrGV = M->getNamedGlobal("or_mask");
    if (!OrGV)
        OrGV = new GlobalVariable(*M, Type::getInt64Ty(M->getContext()), false,
                                  GlobalValue::ExternalLinkage, nullptr, "or_mask");

    GlobalVariable *AndGV = M->getNamedGlobal("and_mask");
    if (!AndGV)
        AndGV = new GlobalVariable(*M, Type::getInt64Ty(M->getContext()), false,
                                   GlobalValue::ExternalLinkage, nullptr, "and_mask");

    for (auto &MBB : MF) {
        for (auto MI = MBB.begin(), ME = MBB.end(); MI != ME;) {
            MachineInstr &Instr = *MI++;
            unsigned Opc = Instr.getOpcode();
            if (Opc != X86::JMP64r && Opc != X86::JMP32r)
                continue;

            bool Is64 = (Opc == X86::JMP64r);
            const TargetRegisterClass *RC = Is64 ? &X86::GR64RegClass : &X86::GR32RegClass;

            unsigned OrReg = MRI.createVirtualRegister(RC);
            unsigned OrResultReg = MRI.createVirtualRegister(RC);
            unsigned AndReg = MRI.createVirtualRegister(RC);
            unsigned ResultReg = MRI.createVirtualRegister(RC);
            unsigned TargetReg = Instr.getOperand(0).getReg();

            // Step 1: Load OR mask
            BuildMI(MBB, Instr, Instr.getDebugLoc(), TII->get(Is64 ? X86::MOV64rm : X86::MOV32rm), OrReg)
                .addReg(X86::RIP)
                .addImm(1)
                .addReg(X86::NoRegister)
                .addGlobalAddress(OrGV, 0)
                .addReg(X86::NoRegister);

            // Step 2: OR target with OR mask
            BuildMI(MBB, Instr, Instr.getDebugLoc(),
                    TII->get(Is64 ? X86::OR64rr : X86::OR32rr), OrResultReg)
                .addReg(OrReg)
                .addReg(TargetReg);

            // Step 3: Load AND mask
            BuildMI(MBB, Instr, Instr.getDebugLoc(), TII->get(Is64 ? X86::MOV64rm : X86::MOV32rm), AndReg)
                .addReg(X86::RIP)
                .addImm(1)
                .addReg(X86::NoRegister)
                .addGlobalAddress(AndGV, 0)
                .addReg(X86::NoRegister);

            // Step 4: AND OR result with AND mask
            BuildMI(MBB, Instr, Instr.getDebugLoc(),
                    TII->get(Is64 ? X86::AND64rr : X86::AND32rr), ResultReg)
                .addReg(OrResultReg)
                .addReg(AndReg);

            // Step 5: Jump through final masked register
            BuildMI(MBB, Instr, Instr.getDebugLoc(), TII->get(Opc))
                .addReg(ResultReg);

            Instr.eraseFromParent();
            Modified = true;
        }
    }

    return Modified;
}


INITIALIZE_PASS(X86InstrumentJmpPass, "x86-instrument-jmp",
    "X86 Instrument Indirect JMP (register only, OR [r9+offset] and AND with imm 1337)",
    false, false)