#pragma once

#include "processors/RISC-V/riscv.h"
#include "../rv5s/rv5s_hazardunit.h"

#include "VSRTL/core/vsrtl_component.h"

namespace vsrtl {
namespace core {
using namespace Ripes;

class HazardUnit_1S : public Component {
public:
  HazardUnit_1S(const std::string &name, SimComponent *parent)
      : Component(name, parent) {
    id_reg1_idx >> fpHazard->id_reg1_idx;
    id_reg2_idx >> fpHazard->id_reg2_idx;
    id_reg_wr_idx >> fpHazard->id_reg_wr_idx;
    id_valid >> fpHazard->id_valid;
    id_pc >> fpHazard->id_pc;
    ex_reg_wr_idx >> fpHazard->ex_reg_wr_idx;
    ex_do_mem_read_en >> fpHazard->ex_do_mem_read_en;
    ex_do_reg_write_en >> fpHazard->ex_do_reg_write_en;
    ex_do_fp_write_en >> fpHazard->ex_do_fp_write_en;
    mem_do_reg_write >> fpHazard->mem_do_reg_write;
    wb_do_reg_write >> fpHazard->wb_do_reg_write;
    mem_do_fp_write >> fpHazard->mem_do_fp_write;
    wb_do_fp_write >> fpHazard->wb_do_fp_write;
    opcode >> fpHazard->opcode;
    id_opcode >> fpHazard->id_opcode;
    branchTakenFromMEM >> fpHazard->branchTakenFromMEM;

    hazardFEEnable << [this] {
      return !hasHazard() &&
             (!fpEnabled || fpHazard->hazardFEEnable.uValue());
    };
    hazardIDEXEnable << [this] {
      return !hasEcallHazard() &&
             (!fpEnabled || fpHazard->hazardIDEXEnable.uValue());
    };
    hazardEXMEMClear << [this] {
      return hasEcallHazard() ||
             (fpEnabled && fpHazard->hazardEXMEMClear.uValue());
    };
    hazardIDEXClear << // MODIFIED
        [this] {
          return hasLoadUseHazard() || branchHasDataHazard() ||
                 (fpEnabled && fpHazard->hazardIDEXClear.uValue());
        };
    stallEcallHandling << [this] {
      return hasEcallHazard() ||
             (fpEnabled && fpHazard->stallEcallHandling.uValue());
    };
    emissionCycle << [this] {
      return fpEnabled ? fpHazard->emissionCycle.uValue() : VSRTL_VT_U(0);
    };
  }

  void setFPExtensionEnabled(bool enabled) {
    fpEnabled = enabled;
    fpHazard->setFPExtensionEnabled(enabled);
  }

  void setConfiguration(unsigned addLatency, unsigned multiplyLatency,
                        unsigned divideLatency, unsigned addCount,
                        unsigned multiplyCount, unsigned divideCount,
                        bool addPipelined, bool multiplyPipelined,
                        bool dividePipelined) {
    fpHazard->setConfiguration(addLatency, multiplyLatency, divideLatency,
                               addCount, multiplyCount, divideCount,
                               addPipelined, multiplyPipelined,
                               dividePipelined);
  }

  const std::vector<HazardUnit::FunctionalUnit> &functionalUnits() const {
    return fpHazard->functionalUnits();
  }
  bool hasPendingInstructions() const {
    return fpEnabled && fpHazard->hasPendingInstructions();
  }
  uint64_t currentCycle() const { return fpHazard->currentCycle(); }
  bool dataHazardActive() const {
    return hasLoadUseHazard() || branchHasDataHazard() ||
           (fpEnabled && fpHazard->dataHazardActive());
  }
  bool structuralHazardActive() const {
    return fpEnabled && fpHazard->structuralHazardActive();
  }

  INPUTPORT(id_reg1_idx, c_RVRegsBits);
  INPUTPORT(id_reg2_idx, c_RVRegsBits);
  INPUTPORT(id_reg_wr_idx, c_RVRegsBits);
  INPUTPORT(id_valid, 1);
  INPUTPORT(id_pc, 64);

  INPUTPORT(ex_reg_wr_idx, c_RVRegsBits);
  INPUTPORT(ex_do_mem_read_en, 1);
  INPUTPORT(ex_do_reg_write_en, 1);
  INPUTPORT(ex_do_fp_write_en, 1);

  INPUTPORT(mem_do_reg_write, 1);
  INPUTPORT(mem_do_fp_write, 1);

  INPUTPORT(wb_do_reg_write, 1);
  INPUTPORT(wb_do_fp_write, 1);

  INPUTPORT_ENUM(opcode, RVInstr);
  INPUTPORT_ENUM(id_opcode, RVInstr);
  INPUTPORT(branchTakenFromMEM, 1);

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
  OUTPUTPORT(emissionCycle, 64);

  // MODIFIED: enable checking ID stage for branch instructions
  INPUTPORT(id_do_branch, 1);
  INPUTPORT(id_do_jump, 1);

  // MODIFIED: probe EX stage for dependencies
  INPUTPORT(ex_do_reg_write, 1);

  // MODIFIED: probe MEM stage for dependencies
  INPUTPORT(mem_reg_wr_idx, c_RVRegsBits);
  INPUTPORT(mem_do_mem_read_en, 1);

private:
  SUBCOMPONENT(fpHazard, HazardUnit);
  bool fpEnabled = false;
  bool hasHazard() { return hasLoadUseHazard() || hasEcallHazard() || branchHasDataHazard(); }

  // MODIFIED for load/use hazard detection of branch operands
  bool hasLoadUseHazard() const {
    const unsigned ex_idx = ex_reg_wr_idx.uValue();
    const unsigned mem_idx = mem_reg_wr_idx.uValue();

    const unsigned idx1 = id_reg1_idx.uValue();
    const unsigned idx2 = id_reg2_idx.uValue();

    const bool ex_do_mem_read = ex_do_mem_read_en.uValue();
    const bool mem_do_mem_read = mem_do_mem_read_en.uValue();

    const bool is_branch = id_do_branch || id_do_jump;

    return ((ex_idx == idx1 || ex_idx == idx2) && ex_do_mem_read)
        || ((mem_idx == idx1 || mem_idx == idx2) && mem_do_mem_read && is_branch);
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
    return isEcall && (mem_do_reg_write.uValue() || wb_do_reg_write.uValue());
  }
};
} // namespace core
} // namespace vsrtl
