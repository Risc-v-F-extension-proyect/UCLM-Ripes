#pragma once

#include "processors/RISC-V/riscv.h"

namespace vsrtl {
namespace core {
using namespace Ripes;

inline bool isSegmentedFPInstr(RVInstr opc) {
  return opc == RVInstr::FADD || opc == RVInstr::FSUB ||
         opc == RVInstr::FMUL || opc == RVInstr::FDIV;
}

inline bool isFPAddSubInstr(RVInstr opc) {
  return opc == RVInstr::FADD || opc == RVInstr::FSUB;
}

} // namespace core
} // namespace vsrtl
