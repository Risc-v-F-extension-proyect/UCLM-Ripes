#pragma once

#include "processors/RISC-V/rv_control.h"
#include "processors/RISC-V/riscv.h"

#include "VSRTL/core/vsrtl_component.h"

namespace vsrtl {
namespace core {
using namespace Ripes;

class HazardUnit_1S : public Component {
public:
  HazardUnit_1S(const std::string &name, SimComponent *parent)
      : Component(name, parent) {
    // tried doing this leveraging inheritance but couldn't assign these
    // outputs to the child class' functions. pls help no hablo c++
    hazardFEEnable << [this] { return !hasHazard(); };
    hazardIDEXEnable << [this] { return !hasEcallHazard(); };
    hazardEXMEMClear << [this] { return hasEcallHazard(); };
    hazardIDEXClear << [this] {
      return hasLoadUseHazard() || hasFPLoadUseHazard() ||
             branchHasDataHazard();
    };
    stallEcallHandling << [this] { return hasEcallHazard(); };
  }

  INPUTPORT(id_reg1_idx, c_RVRegsBits);
  INPUTPORT(id_reg2_idx, c_RVRegsBits);

  INPUTPORT(ex_reg_wr_idx, c_RVRegsBits);
  INPUTPORT(ex_do_mem_read_en, 1);
  INPUTPORT(ex_do_reg_write_en, 1);
  INPUTPORT(ex_do_fp_write_en, 1);

  INPUTPORT(mem_do_reg_write, 1);

  INPUTPORT(wb_do_reg_write, 1);

  INPUTPORT(fp_mem_do_reg_write, 1);
  INPUTPORT(fp_wb_do_reg_write, 1);

  INPUTPORT_ENUM(opcode, RVInstr);
  INPUTPORT_ENUM(id_opcode, RVInstr);

  // Hazard Front End enable: Low when stalling the front end (shall be
  // connected to a register 'enable' input port). The
  OUTPUTPORT(hazardFEEnable, 1);

  // Hazard IDEX enable: Low when stalling due to an ECALL hazard
  OUTPUTPORT(hazardIDEXEnable, 1);

  // EXMEM clear: High when an ECALL hazard is detected
  OUTPUTPORT(hazardEXMEMClear, 1);
  // IDEX clear: High when a load-use hazard is detected
  OUTPUTPORT(hazardIDEXClear, 1);

  // Stall Ecall Handling: High whenever we are about to handle an ecall, but
  // have outstanding writes in the pipeline which must be comitted to the
  // register file before handling the ecall.
  OUTPUTPORT(stallEcallHandling, 1);

  // MODIFIED: enable checking ID stage for branch instructions
  INPUTPORT(id_do_branch, 1);
  INPUTPORT(id_do_jump, 1);

  // MODIFIED: probe EX stage for dependencies
  INPUTPORT(ex_do_reg_write, 1);

  // MODIFIED: probe MEM stage for dependencies
  INPUTPORT(mem_reg_wr_idx, c_RVRegsBits);
  INPUTPORT(mem_do_mem_read_en, 1);

private:
  bool hasHazard() {
    return hasLoadUseHazard() || hasFPLoadUseHazard() || hasEcallHazard() ||
           branchHasDataHazard();
  }

  // MODIFIED for load/use hazard detection of branch operands
  bool hasLoadUseHazard() const {
    const unsigned ex_idx = ex_reg_wr_idx.uValue();
    const unsigned mem_idx = mem_reg_wr_idx.uValue();

    const unsigned idx1 = id_reg1_idx.uValue();
    const unsigned idx2 = id_reg2_idx.uValue();

    const bool ex_do_mem_read =
        ex_do_mem_read_en.uValue() && ex_do_reg_write_en.uValue();
    const bool mem_do_mem_read = mem_do_mem_read_en.uValue();

    const bool is_branch = id_do_branch || id_do_jump;

    return ((ex_idx == idx1 || ex_idx == idx2) && ex_do_mem_read)
        || ((mem_idx == idx1 || mem_idx == idx2) && mem_do_mem_read && is_branch);
  }

  static bool usesFPReg1(RVInstr opc) { return Control::isFALUInstr(opc); }

  static bool usesFPReg2(RVInstr opc) {
    switch (opc) {
    case RVInstr::FSQRT:
    case RVInstr::FSQRTD:
    case RVInstr::FCVTSD:
    case RVInstr::FCVTDS:
      return false;
    case RVInstr::FSW:
    case RVInstr::FSD:
      return true;
    default:
      return Control::isFALUInstr(opc);
    }
  }

  bool hasFPLoadUseHazard() const {
    const auto opc = id_opcode.eValue<RVInstr>();
    const unsigned exidx = ex_reg_wr_idx.uValue();
    const bool mrd =
        ex_do_mem_read_en.uValue() && ex_do_fp_write_en.uValue();

    const bool reg1Hazard = usesFPReg1(opc) && exidx == id_reg1_idx.uValue();
    const bool reg2Hazard = usesFPReg2(opc) && exidx == id_reg2_idx.uValue();
    return mrd && (reg1Hazard || reg2Hazard);
  }

  // MODIFIED: detect regular data hazards for branch operands
  bool branchHasDataHazard() const {
    const unsigned ex_idx = ex_reg_wr_idx.uValue();
    const unsigned idx1 = id_reg1_idx.uValue();
    const unsigned idx2 = id_reg2_idx.uValue();
    
    const bool ex_writes = ex_do_reg_write.uValue() && ex_idx != 0;
    const bool is_branch = id_do_branch || id_do_jump;
    
    return (ex_idx == idx1 || ex_idx == idx2) && ex_writes && is_branch;
  }

  bool hasEcallHazard() const {
    // Check for ECALL hazard. We are implictly dependent on all registers when
    // performing an ECALL operation. As such, all outstanding writes to the
    // register file must be performed before handling the ecall. Hence, the
    // front-end of the pipeline shall be stalled until the remainder of the
    // pipeline has been cleared and there are no more outstanding writes.
    const bool isEcall = opcode.eValue<RVInstr>() == RVInstr::ECALL;
    const bool integerOutstandingWrites =
        mem_do_reg_write.uValue() || wb_do_reg_write.uValue();
    const bool fpOutstandingWrites =
        fp_mem_do_reg_write.uValue() || fp_wb_do_reg_write.uValue();
    return isEcall && (integerOutstandingWrites || fpOutstandingWrites);
  }
};
} // namespace core
} // namespace vsrtl
