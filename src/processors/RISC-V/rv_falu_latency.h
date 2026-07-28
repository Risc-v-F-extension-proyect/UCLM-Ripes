#pragma once

#include <algorithm>

#include "VSRTL/core/vsrtl_component.h"
#include "riscv.h"
#include "rv_control.h"

namespace vsrtl {
namespace core {
using namespace Ripes;

class FALULatency : public Component {
public:
  FALULatency(const std::string &name, SimComponent *parent)
      : Component(name, parent) {
    stall << [this] { return shouldStall(); };
    next_remaining << [this] { return nextRemaining(); };
    current_cycle << [this] { return currentCycle(); };
    total_cycles << [this] { return totalCycles(); };

    //Addition
    addsub_current_cycle << [this] {return currentCycleFor(RVInstr::FADD, RVInstr::FSUB);};
    addsub_total_cycles << [this] { return m_addSubLatency; };

    //Multiplication
    mul_current_cycle << [this] { return currentCycleFor(RVInstr::FMUL); };
    mul_total_cycles << [this] { return m_mulLatency; };

    //Division
    div_current_cycle << [this] { return currentCycleFor(RVInstr::FDIV); };
    div_total_cycles << [this] { return m_divLatency; };
  }

  void setLatencies(unsigned addSub, unsigned mul, unsigned div) {
    m_addSubLatency = std::max(1u, addSub);
    m_mulLatency = std::max(1u, mul);
    m_divLatency = std::max(1u, div);
  }

  INPUTPORT_ENUM(opcode, RVInstr);
  INPUTPORT(valid, 1);
  INPUTPORT(remaining, 8);

  OUTPUTPORT(stall, 1);
  OUTPUTPORT(next_remaining, 8);
  OUTPUTPORT(current_cycle, 8);
  OUTPUTPORT(total_cycles, 8);
  OUTPUTPORT(addsub_current_cycle, 8);
  OUTPUTPORT(addsub_total_cycles, 8);
  OUTPUTPORT(mul_current_cycle, 8);
  OUTPUTPORT(mul_total_cycles, 8);
  OUTPUTPORT(div_current_cycle, 8);
  OUTPUTPORT(div_total_cycles, 8);

private:
  unsigned latencyFor(RVInstr opc) const {
    switch (opc) {
    case RVInstr::FADD:
    case RVInstr::FSUB:
    case RVInstr::FADDD:
    case RVInstr::FSUBD:
      return m_addSubLatency;
    case RVInstr::FMUL:
    case RVInstr::FMULD:
      return m_mulLatency;
    case RVInstr::FDIV:
    case RVInstr::FDIVD:
      return m_divLatency;
    default:
      return 1;
    }
  }

  bool activeFALUInstruction() const {
    return valid.uValue() && Control::isFALUInstr(opcode.eValue<RVInstr>());
  }

  bool shouldStall() const {
    if (!activeFALUInstruction()) {
      return false;
    }
    const unsigned latency = latencyFor(opcode.eValue<RVInstr>());
    return latency > 1 && remaining.uValue() != 1;
  }

  VSRTL_VT_U nextRemaining() const {
    if (!activeFALUInstruction()) {
      return 0;
    }

    const unsigned latency = latencyFor(opcode.eValue<RVInstr>());
    if (latency <= 1) {
      return 0;
    }

    const unsigned rem = remaining.uValue();
    if (rem == 0) {
      return latency - 1;
    }
    if (rem > 1) {
      return rem - 1;
    }
    return 0;
  }

  VSRTL_VT_U currentCycle() const {
    if (!activeFALUInstruction()) {
      return 0;
    }

    const unsigned latency = latencyFor(opcode.eValue<RVInstr>());
    if (latency <= 1) {
      return 1;
    }

    const unsigned rem = remaining.uValue();
    if (rem == 0) {
      return 1;
    }
    return latency - rem + 1;
    //return latency - (rem - 1);//same as above, but more intuitive
  }

  VSRTL_VT_U totalCycles() const {
    if (!activeFALUInstruction()) {
      return 0;
    }
    return latencyFor(opcode.eValue<RVInstr>());
  }

  VSRTL_VT_U currentCycleFor(RVInstr opc) const {
    if (opcode.eValue<RVInstr>() != opc) {
      return 0;
    }
    return currentCycle();
  }

  VSRTL_VT_U currentCycleFor(RVInstr opc1, RVInstr opc2) const {
    const auto activeOpc = opcode.eValue<RVInstr>();
    if (activeOpc != opc1 && activeOpc != opc2) {
      return 0;
    }
    return currentCycle();
  }

  unsigned m_addSubLatency = 4;
  unsigned m_mulLatency = 7;
  unsigned m_divLatency = 25;
};

} // namespace core
} // namespace vsrtl
