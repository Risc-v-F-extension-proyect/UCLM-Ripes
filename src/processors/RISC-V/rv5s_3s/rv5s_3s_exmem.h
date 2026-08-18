#pragma once

#include "VSRTL/core/vsrtl_component.h"
#include "VSRTL/core/vsrtl_register.h"

#include "processors/RISC-V/riscv.h"

#include "../rv5s/rv5s_exmem.h"

namespace vsrtl {
namespace core {
using namespace Ripes;

template <unsigned XLEN>
class RV5S_3S_EXMEM : public RV5S_EXMEM<XLEN> {
public:
  RV5S_3S_EXMEM(const std::string &name, SimComponent *parent)
      : RV5S_EXMEM<XLEN>(name, parent) {}
};

} // namespace core
} // namespace vsrtl
