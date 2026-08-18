#pragma once

#include "VSRTL/core/vsrtl_component.h"
#include "VSRTL/core/vsrtl_register.h"

#include "../riscv.h"

namespace vsrtl {
namespace core {
using namespace Ripes;

template <unsigned XLEN>
class EXMEM : public Component {
public:
  EXMEM(const std::string &name, SimComponent *parent,
        bool connectInputs = true)
      : Component(name, parent) {
    setDescription("Execute/memory stage separating register");
    if (connectInputs) {
      CONNECT_REGISTERED_CLEN_INPUT(pc, clear, enable);
      CONNECT_REGISTERED_CLEN_INPUT(pc4, clear, enable);
      CONNECT_REGISTERED_CLEN_INPUT(alures, clear, enable);
      CONNECT_REGISTERED_CLEN_INPUT(r2, clear, enable);
      CONNECT_REGISTERED_CLEN_INPUT(reg_wr_src_ctrl, clear, enable);
      CONNECT_REGISTERED_CLEN_INPUT(wr_reg_idx, clear, enable);
      CONNECT_REGISTERED_CLEN_INPUT(reg_do_write, clear, enable);
      CONNECT_REGISTERED_CLEN_INPUT(mem_do_write, clear, enable);
      CONNECT_REGISTERED_CLEN_INPUT(mem_do_read, clear, enable);
      CONNECT_REGISTERED_CLEN_INPUT(mem_op, clear, enable);
      CONNECT_REGISTERED_CLEN_INPUT(valid, clear, enable);
    } else {
      pc_reg->out >> pc_out;
      pc4_reg->out >> pc4_out;
      alures_reg->out >> alures_out;
      r2_reg->out >> r2_out;
      reg_wr_src_ctrl_reg->out >> reg_wr_src_ctrl_out;
      wr_reg_idx_reg->out >> wr_reg_idx_out;
      reg_do_write_reg->out >> reg_do_write_out;
      mem_do_write_reg->out >> mem_do_write_out;
      mem_do_read_reg->out >> mem_do_read_out;
      mem_op_reg->out >> mem_op_out;
      valid_reg->out >> valid_out;
    }
  }

  // Data
  REGISTERED_CLEN_INPUT(pc, XLEN);
  REGISTERED_CLEN_INPUT(pc4, XLEN);
  REGISTERED_CLEN_INPUT(alures, XLEN);
  REGISTERED_CLEN_INPUT(r2, XLEN);

  // Control
  REGISTERED_CLEN_INPUT(reg_wr_src_ctrl, enumBitWidth<RegWrSrc>());
  REGISTERED_CLEN_INPUT(wr_reg_idx, c_RVRegsBits);
  REGISTERED_CLEN_INPUT(reg_do_write, 1);
  REGISTERED_CLEN_INPUT(mem_do_write, 1);
  REGISTERED_CLEN_INPUT(mem_do_read, 1);
  REGISTERED_CLEN_INPUT(mem_op, enumBitWidth<MemOp>());

  // Register bank controls
  INPUTPORT(enable, 1);
  INPUTPORT(clear, 1);

  // Valid signal. False when the register bank has been cleared. May be used by
  // UI to determine whether the NOP in the stage is a user-inserted nop or the
  // result of some pipeline action.
  REGISTERED_CLEN_INPUT(valid, 1);
};

} // namespace core
} // namespace vsrtl
