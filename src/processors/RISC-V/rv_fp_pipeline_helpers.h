#pragma once

#include <string>

#include "VSRTL/core/vsrtl_component.h"
#include "processors/RISC-V/rv_fp_instr_helpers.h"

namespace vsrtl {
namespace core {
using namespace Ripes;

class FPSegmentedRegWriteGate : public Component {
public:
  FPSegmentedRegWriteGate(const std::string &name, SimComponent *parent)
      : Component(name, parent) {
    out << [this] {
      return fp_reg_do_write.uValue() &&
             !isSegmentedFPInstr(opcode.eValue<RVInstr>());
    };
  }

  INPUTPORT(fp_reg_do_write, 1);
  INPUTPORT_ENUM(opcode, RVInstr);
  OUTPUTPORT(out, 1);
};

class FPRegularIssueGate : public Component {
public:
  FPRegularIssueGate(const std::string &name, SimComponent *parent)
      : Component(name, parent) {
    out << [this] {
      return valid.uValue() && !blocked.uValue() &&
             !isSegmentedFPInstr(opcode.eValue<RVInstr>());
    };
  }

  INPUTPORT(valid, 1);
  INPUTPORT(blocked, 1);
  INPUTPORT_ENUM(opcode, RVInstr);
  OUTPUTPORT(out, 1);
};

template <unsigned XLEN>
class FPExMemMux : public Component {
public:
  FPExMemMux(const std::string &name, SimComponent *parent)
      : Component(name, parent) {
    pc_out << [this] { return use_fp.uValue() ? fp_pc.uValue() : pc.uValue(); };
    pc4_out << [this] { return use_fp.uValue() ? fp_pc4.uValue() : pc4.uValue(); };
    r2_out << [this] { return use_fp.uValue() ? VSRTL_VT_U(0) : r2.uValue(); };
    alures_out << [this] { return use_fp.uValue() ? VSRTL_VT_U(0) : alures.uValue(); };
    f_r2_out << [this] { return use_fp.uValue() ? VSRTL_VT_U(0) : f_r2.uValue(); };
    falures_out << [this] { return use_fp.uValue() ? fp_value.uValue() : falures.uValue(); };

    reg_wr_src_ctrl_out << [this] {
      return use_fp.uValue() ? VSRTL_VT_U(static_cast<unsigned>(RegWrSrc::ALURES))
                             : reg_wr_src_ctrl.uValue();
    };
    wr_reg_idx_out << [this] {
      return use_fp.uValue() ? fp_rd.uValue() : wr_reg_idx.uValue();
    };
    reg_do_write_out << [this] {
      return use_fp.uValue() ? VSRTL_VT_U(0) : reg_do_write.uValue();
    };
    mem_do_write_out << [this] {
      return use_fp.uValue() ? VSRTL_VT_U(0) : mem_do_write.uValue();
    };
    mem_do_read_out << [this] {
      return use_fp.uValue() ? VSRTL_VT_U(0) : mem_do_read.uValue();
    };
    mem_op_out << [this] {
      return use_fp.uValue() ? VSRTL_VT_U(static_cast<unsigned>(MemOp::NOP))
                             : mem_op.uValue();
    };
    fp_reg_do_write_out << [this] {
      return use_fp.uValue() ? VSRTL_VT_U(1) : fp_reg_do_write.uValue();
    };
    data_mem_wr_src_ctrl_out << [this] {
      return use_fp.uValue() ? VSRTL_VT_U(static_cast<unsigned>(DataMemWrSrc::FREG2))
                             : data_mem_wr_src_ctrl.uValue();
    };
    valid_out << [this] {
      return use_fp.uValue() ? fp_valid.uValue() : valid.uValue();
    };
  }

  INPUTPORT(use_fp, 1);

  INPUTPORT(pc, XLEN);
  INPUTPORT(pc4, XLEN);
  INPUTPORT(r2, XLEN);
  INPUTPORT(alures, XLEN);
  INPUTPORT(f_r2, XLEN);
  INPUTPORT(falures, XLEN);
  INPUTPORT(reg_wr_src_ctrl, enumBitWidth<RegWrSrc>());
  INPUTPORT(wr_reg_idx, c_RVRegsBits);
  INPUTPORT(reg_do_write, 1);
  INPUTPORT(mem_do_write, 1);
  INPUTPORT(mem_do_read, 1);
  INPUTPORT(mem_op, enumBitWidth<MemOp>());
  INPUTPORT(fp_reg_do_write, 1);
  INPUTPORT(data_mem_wr_src_ctrl, enumBitWidth<DataMemWrSrc>());
  INPUTPORT(valid, 1);

  INPUTPORT(fp_valid, 1);
  INPUTPORT(fp_pc, XLEN);
  INPUTPORT(fp_pc4, XLEN);
  INPUTPORT(fp_rd, c_RVRegsBits);
  INPUTPORT(fp_value, XLEN);

  OUTPUTPORT(pc_out, XLEN);
  OUTPUTPORT(pc4_out, XLEN);
  OUTPUTPORT(r2_out, XLEN);
  OUTPUTPORT(alures_out, XLEN);
  OUTPUTPORT(f_r2_out, XLEN);
  OUTPUTPORT(falures_out, XLEN);
  OUTPUTPORT(reg_wr_src_ctrl_out, enumBitWidth<RegWrSrc>());
  OUTPUTPORT(wr_reg_idx_out, c_RVRegsBits);
  OUTPUTPORT(reg_do_write_out, 1);
  OUTPUTPORT(mem_do_write_out, 1);
  OUTPUTPORT(mem_do_read_out, 1);
  OUTPUTPORT(mem_op_out, enumBitWidth<MemOp>());
  OUTPUTPORT(fp_reg_do_write_out, 1);
  OUTPUTPORT(data_mem_wr_src_ctrl_out, enumBitWidth<DataMemWrSrc>());
  OUTPUTPORT(valid_out, 1);
};

} // namespace core
} // namespace vsrtl
