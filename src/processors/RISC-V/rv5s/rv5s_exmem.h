#pragma once

#include "VSRTL/core/vsrtl_component.h"
#include "VSRTL/core/vsrtl_register.h"

#include "processors/RISC-V/riscv.h"

#include "../rv5s_no_fw_hz/rv5s_no_fw_hz_exmem.h"

namespace vsrtl {
namespace core {
using namespace Ripes;

template <unsigned XLEN>
class RV5S_EXMEM : public EXMEM<XLEN> {
public:
  RV5S_EXMEM(const std::string &name, SimComponent *parent)
      : EXMEM<XLEN>(name, parent) {
    CONNECT_REGISTERED_CLEN_INPUT(f_r2, this->clear, this->enable);
    CONNECT_REGISTERED_CLEN_INPUT(falures, this->clear, this->enable);
    CONNECT_REGISTERED_CLEN_INPUT(fp_reg_do_write, this->clear, this->enable);
    CONNECT_REGISTERED_CLEN_INPUT(data_mem_wr_src_ctrl, this->clear,
                                  this->enable);

    // We want stalling info to persist through clearing of the register, so
    // stalled register is always enabled and never cleared.
    CONNECT_REGISTERED_CLEN_INPUT(stalled, 0, 1);
  }

  REGISTERED_CLEN_INPUT(f_r2, XLEN);
  REGISTERED_CLEN_INPUT(falures, XLEN);
  REGISTERED_CLEN_INPUT(fp_reg_do_write, 1);
  REGISTERED_CLEN_INPUT(data_mem_wr_src_ctrl, enumBitWidth<DataMemWrSrc>());

  REGISTERED_CLEN_INPUT(stalled, 1);
};

} // namespace core
} // namespace vsrtl
