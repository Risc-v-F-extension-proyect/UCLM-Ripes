#pragma once

#include "VSRTL/core/vsrtl_adder.h"
#include "VSRTL/core/vsrtl_component.h"
#include "VSRTL/core/vsrtl_register.h"

#include "processors/RISC-V/riscv.h"

namespace vsrtl {
namespace core {
using namespace Ripes;

template <unsigned XLEN>
class InstructionTagGenerator : public Component {
public:
  InstructionTagGenerator(const std::string &name, SimComponent *parent)
      : Component(name, parent) {
    setDescription("Instruction Tag Generator");

    tag_reg->setInitValue(1);
    tag_reg->out >> instr_tag;

    tag_reg->out >> tag_inc->op1;
    1 >> tag_inc->op2;
    tag_inc->out >> tag_reg->in;

    enable_in >> tag_reg->enable;
    0 >> tag_reg->clear;
  }

  INPUTPORT(enable_in, 1);
  OUTPUTPORT(instr_tag, XLEN);

private:
  SUBCOMPONENT(tag_reg, RegisterClEn<XLEN>);
  SUBCOMPONENT(tag_inc, Adder<XLEN>);
};

} // namespace core
} // namespace vsrtl