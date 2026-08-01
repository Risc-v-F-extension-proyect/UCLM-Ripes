#pragma once

#include "VSRTL/core/vsrtl_component.h"
#include "VSRTL/core/vsrtl_constant.h"
#include "VSRTL/core/vsrtl_register.h"

#include "processors/RISC-V/riscv.h"
#include "processors/RISC-V/rv_falu.h"

#include "../rv5s_no_fw_hz/rv5s_no_fw_hz_idex.h"

#include <algorithm>
#include <array>
#include <deque>

namespace vsrtl {
namespace core {
using namespace Ripes;

template <unsigned XLEN>
class RV5S_IDEX;

template <unsigned XLEN>
class TemporaryFPRegisterState : public ClockedComponent {
public:
  struct Register {
    bool anticipated = false;
    VSRTL_VT_U value = 0;
    uint64_t blockedUntil = 0;
  };

  TemporaryFPRegisterState(const std::string &name, SimComponent *parent, RV5S_IDEX<XLEN> *idex)
  : ClockedComponent(name, parent), idex(idex) {}

  VSRTL_VT_U resolvedValue(unsigned index, VSRTL_VT_U physicalValue) const;

  const std::array<Register, 32> &registers() const {
    return temporaryRegisters;
  }

  void save() override;
  void reset() override;
  void reverse() override;
  void forceValue(VSRTL_VT_U, VSRTL_VT_U) override {}
  void reverseStackSizeChanged() override;

private:
  static bool usesSecondOperand(FALUOp operation) {
    switch (operation) {
    case FALUOp::SQRT:
    case FALUOp::SQRT_D:
    case FALUOp::CVT_S_D:
    case FALUOp::CVT_D_S:
    case FALUOp::NOP:
      return false;
    default:
      return true;
    }
  }

  RV5S_IDEX<XLEN> *idex;
  std::array<Register, 32> temporaryRegisters{};
  std::deque<std::array<Register, 32>> reverseStack;
};

template <unsigned XLEN>
class TemporaryFPRegisterResolver : public Component {
public:
  TemporaryFPRegisterResolver(const std::string &name, SimComponent *parent,
                              TemporaryFPRegisterState<XLEN> *state)
      : Component(name, parent), state(state) {
    setSensitiveTo(physicalValue);
    setSensitiveTo(registerIndex);
    resolvedValue << [this] {
      return this->state->resolvedValue(registerIndex.uValue(),
                                        physicalValue.uValue());
    };
  }

  INPUTPORT(physicalValue, XLEN);
  INPUTPORT(registerIndex, c_RVRegsBits);
  OUTPUTPORT(resolvedValue, XLEN);

private:
  TemporaryFPRegisterState<XLEN> *state;
};

/**
 * @brief The RV5S_IDEX class
 * A specialization of the default IDEX stage separating register utilized by
 * the rv5s_no_fw_hz processor. Storage of register read indices is added, which
 * are required by the forwarding unit.
 */
template <unsigned XLEN>
class RV5S_IDEX : public IDEX<XLEN> {
public:
  RV5S_IDEX(const std::string &name, SimComponent *parent)
      : IDEX<XLEN>(name, parent) {
    CONNECT_REGISTERED_CLEN_INPUT(rd_reg1_idx, this->clear, this->enable);
    CONNECT_REGISTERED_CLEN_INPUT(rd_reg2_idx, this->clear, this->enable);
    CONNECT_REGISTERED_CLEN_INPUT(opcode, this->clear, this->enable);

    f_r1_in >> f_r1_resolver->physicalValue;
    rd_reg1_idx_in >> f_r1_resolver->registerIndex;
    f_r1_resolver->resolvedValue >> f_r1_reg->in;
    this->clear >> f_r1_reg->clear;
    this->enable >> f_r1_reg->enable;
    f_r1_reg->out >> f_r1_out;

    f_r2_in >> f_r2_resolver->physicalValue;
    rd_reg2_idx_in >> f_r2_resolver->registerIndex;
    f_r2_resolver->resolvedValue >> f_r2_reg->in;
    this->clear >> f_r2_reg->clear;
    this->enable >> f_r2_reg->enable;
    f_r2_reg->out >> f_r2_out;
    CONNECT_REGISTERED_CLEN_INPUT(fp_reg_do_write, this->clear, this->enable);
    CONNECT_REGISTERED_CLEN_INPUT(data_mem_wr_src_ctrl, this->clear,this->enable);
    CONNECT_REGISTERED_CLEN_INPUT(falu_ctrl, this->clear, this->enable);
    //nueva señal que indicará al ROB interno de EX/MEM cuando las señales de esta instrucción deberán avanzar hacia MEM.
    //Esta señal la genera la Hazard Unit y sirve para emivar conflictos por la salida de EX/MEM.
    CONNECT_REGISTERED_CLEN_INPUT(emissionCycle, this->clear, this->enable);

    // We want stalling info to persist through clearing of the register, so
    // stalled register is always enabled and never cleared.
    CONNECT_REGISTERED_CLEN_INPUT(stalled, 0, 1);
  }

  REGISTERED_CLEN_INPUT(f_r1, XLEN);
  REGISTERED_CLEN_INPUT(f_r2, XLEN);

  REGISTERED_CLEN_INPUT(rd_reg1_idx, c_RVRegsBits);
  REGISTERED_CLEN_INPUT(rd_reg2_idx, c_RVRegsBits);
  REGISTERED_CLEN_INPUT(opcode, enumBitWidth<RVInstr>());
  REGISTERED_CLEN_INPUT(fp_reg_do_write, 1);
  REGISTERED_CLEN_INPUT(data_mem_wr_src_ctrl, enumBitWidth<DataMemWrSrc>());
  REGISTERED_CLEN_INPUT(falu_ctrl, enumBitWidth<FALUOp>());
  REGISTERED_CLEN_INPUT(emissionCycle, 64);

  REGISTERED_CLEN_INPUT(stalled, 1);

  void setFPExtensionEnabled(bool enabled) { fpEnabled = enabled; }

private:
  friend class TemporaryFPRegisterState<XLEN>;

  bool fpEnabled = false;
  SUBCOMPONENT(temporaryFPRegisters, TYPE(TemporaryFPRegisterState<XLEN>),
               this);
  SUBCOMPONENT(f_r1_resolver, TYPE(TemporaryFPRegisterResolver<XLEN>),
               temporaryFPRegisters);
  SUBCOMPONENT(f_r2_resolver, TYPE(TemporaryFPRegisterResolver<XLEN>),
               temporaryFPRegisters);
};

template <unsigned XLEN>
VSRTL_VT_U TemporaryFPRegisterState<XLEN>::resolvedValue(unsigned index, VSRTL_VT_U physicalValue) const {
  const auto &temporaryRegister = temporaryRegisters.at(index);
  return temporaryRegister.anticipated ? temporaryRegister.value : physicalValue;
}

template <unsigned XLEN>
void TemporaryFPRegisterState<XLEN>::save() {
  if (canReverse()) {
    reverseStack.push_front(temporaryRegisters);
  }

  if (!idex->fpEnabled || !idex->enable.uValue() ||
      idex->clear.uValue() || !idex->valid_in.uValue() ||
      !idex->fp_reg_do_write_in.uValue()) {
    return;
  }

  const unsigned destination = idex->wr_reg_idx_in.uValue();
  const FALUOp operation = idex->falu_ctrl_in.eValue<FALUOp>();
  auto &temporaryRegister = temporaryRegisters.at(destination);
  const uint64_t nextCycle =
      static_cast<uint64_t>(getDesign()->getCycleCount() + 1);

  const MemOp memoryOperation = idex->mem_op_in.eValue<MemOp>();
  //esta claro que cuando nos encontramos ante una carga FP nunca podremos tener
  //el valor anticipado para el registro destido por lo que las futuras instrucciones
  //tendran el valor correcto a partir de la lectura del banco fisico o bien por
  //adelantamiento desde WB hasta que se almacene en el banco fisico FP
  if (memoryOperation == MemOp::FLW || memoryOperation == MemOp::FLD) {
    temporaryRegister = {};
    temporaryRegister.blockedUntil = nextCycle + 2;
    return;
  }

  if (operation == FALUOp::NOP) {
    return;
  }

  const unsigned source1 = idex->rd_reg1_idx_in.uValue();
  const unsigned source2 = idex->rd_reg2_idx_in.uValue();
  const auto &source1Register = temporaryRegisters.at(source1);
  const auto &source2Register = temporaryRegisters.at(source2);
  const uint64_t source1BlockedUntil =
      source1Register.blockedUntil;
  const uint64_t source2BlockedUntil =
      usesSecondOperand(operation)
          ? source2Register.blockedUntil
          : 0;
  const uint64_t inheritedBlock =
      std::max(source1BlockedUntil, source2BlockedUntil);

  if (inheritedBlock >= nextCycle) {
    temporaryRegister.anticipated = false;
    temporaryRegister.blockedUntil = inheritedBlock;
    return;
  }

  const VSRTL_VT_U operand1 =
      resolvedValue(source1, idex->f_r1_in.uValue());
  const VSRTL_VT_U operand2 =
      resolvedValue(source2, idex->f_r2_in.uValue());

  temporaryRegister.value =
      FALU<XLEN>::execute(operation, operand1, operand2);
  temporaryRegister.anticipated = false;
  temporaryRegister.blockedUntil = 0;
}

template <unsigned XLEN>
void TemporaryFPRegisterState<XLEN>::reset() {
  temporaryRegisters = {};
  reverseStack.clear();
}

template <unsigned XLEN>
void TemporaryFPRegisterState<XLEN>::reverse() {
  if (!reverseStack.empty()) {
    temporaryRegisters = reverseStack.front();
    reverseStack.pop_front();
  }
}

template <unsigned XLEN>
void TemporaryFPRegisterState<XLEN>::reverseStackSizeChanged() {
  if (reverseStackSize() < reverseStack.size()) {
    reverseStack.resize(reverseStackSize());
  }
}

} // namespace core
} // namespace vsrtl
