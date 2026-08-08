#pragma once

#include "VSRTL/core/vsrtl_component.h"
#include "VSRTL/core/vsrtl_register.h"

#include "processors/RISC-V/riscv.h"

#include "../rv5s_no_fw_hz/rv5s_no_fw_hz_exmem.h"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <optional>

namespace vsrtl {
namespace core {
using namespace Ripes;

enum class EXMEMField {
  PC,
  PC4,
  ALUResult,
  R2,
  FR2,
  FALUResult,
  RegWrSrc,
  WrRegIdx,
  RegDoWrite,
  FPRegDoWrite,
  MemDoWrite,
  MemDoRead,
  MemOp,
  DataMemWrSrc,
  Valid,
  EmissionCycle,
  DoBranch,
  ControlFlow
};

template <unsigned XLEN>
class RV5S_EXMEM;

template <unsigned XLEN>
class EXMEMInputSelector : public Component {
public:
  EXMEMInputSelector(const std::string &name, SimComponent *parent,
                     RV5S_EXMEM<XLEN> *exmem);

  OUTPUTPORT(pc, XLEN);
  OUTPUTPORT(pc4, XLEN);
  OUTPUTPORT(aluResult, XLEN);
  OUTPUTPORT(r2, XLEN);
  OUTPUTPORT(fr2, XLEN);
  OUTPUTPORT(faluResult, XLEN);
  OUTPUTPORT(regWrSrc, enumBitWidth<RegWrSrc>());
  OUTPUTPORT(wrRegIdx, c_RVRegsBits);
  OUTPUTPORT(regDoWrite, 1);
  OUTPUTPORT(fpRegDoWrite, 1);
  OUTPUTPORT(memDoWrite, 1);
  OUTPUTPORT(memDoRead, 1);
  OUTPUTPORT(memOp, enumBitWidth<MemOp>());
  OUTPUTPORT(dataMemWrSrc, enumBitWidth<DataMemWrSrc>());
  OUTPUTPORT(valid, 1);
  OUTPUTPORT(emissionCycle, 64);
  OUTPUTPORT(doBranch, 1);
  OUTPUTPORT(clear, 1);
  OUTPUTPORT(enable, 1);
};

template <unsigned XLEN>
class EXMEMPersistenceState : public ClockedComponent {
public:
  struct Instruction {
    VSRTL_VT_U pc = 0;
    VSRTL_VT_U pc4 = 0;
    VSRTL_VT_U aluResult = 0;
    VSRTL_VT_U r2 = 0;
    VSRTL_VT_U fr2 = 0;
    VSRTL_VT_U faluResult = 0;
    VSRTL_VT_U regWrSrc = 0;
    VSRTL_VT_U wrRegIdx = 0;
    VSRTL_VT_U regDoWrite = 0;
    VSRTL_VT_U fpRegDoWrite = 0;
    VSRTL_VT_U memDoWrite = 0;
    VSRTL_VT_U memDoRead = 0;
    VSRTL_VT_U memOp = 0;
    VSRTL_VT_U dataMemWrSrc = 0;
    VSRTL_VT_U valid = 0;
    uint64_t emissionCycle = 0;
    VSRTL_VT_U doBranch = 0;
    VSRTL_VT_U controlFlow = 0;
  };

  struct Snapshot {
    uint64_t currentCycle = 0;
    std::deque<Instruction> pendingInstructions;
  };

  EXMEMPersistenceState(const std::string &name, SimComponent *parent,
                        RV5S_EXMEM<XLEN> *exmem)
      : ClockedComponent(name, parent), exmem(exmem) {}

  VSRTL_VT_U resolvedInput(EXMEMField field, VSRTL_VT_U original) const;

  bool hasPendingInstructions() const {
    return !pendingInstructions.empty();
  }

  void save() override;
  void reset() override;
  void reverse() override;
  void forceValue(VSRTL_VT_U, VSRTL_VT_U) override {}
  void reverseStackSizeChanged() override;

private:
  bool hasValidIncoming() const {
    return exmem->enable.uValue() && !exmem->clear.uValue() &&
           exmem->valid_in.uValue() &&
           exmem->emissionCycle_in.uValue() != 0;
  }

  uint64_t normalizedEmissionCycle(uint64_t emissionCycle) const {
    const uint64_t nextCycle = currentCycle + 1;
    return std::max(emissionCycle, nextCycle);
  }

  Instruction captureIncoming() const;
  std::optional<Instruction> instructionForNextCycle() const;

  RV5S_EXMEM<XLEN> *exmem;
  uint64_t currentCycle = 0;
  std::deque<Instruction> pendingInstructions;
  std::deque<Snapshot> reverseStack;
};

template <unsigned XLEN>
class RV5S_EXMEM : public EXMEM<XLEN> {
public:
  RV5S_EXMEM(const std::string &name, SimComponent *parent)
      : EXMEM<XLEN>(name, parent, false) {
    selector->pc >> this->pc_reg->in;
    selector->pc4 >> this->pc4_reg->in;
    selector->aluResult >> this->alures_reg->in;
    selector->r2 >> this->r2_reg->in;
    selector->regWrSrc >> this->reg_wr_src_ctrl_reg->in;
    selector->wrRegIdx >> this->wr_reg_idx_reg->in;
    selector->regDoWrite >> this->reg_do_write_reg->in;
    selector->memDoWrite >> this->mem_do_write_reg->in;
    selector->memDoRead >> this->mem_do_read_reg->in;
    selector->memOp >> this->mem_op_reg->in;
    selector->valid >> this->valid_reg->in;

    selector->fr2 >> f_r2_reg->in;
    selector->faluResult >> falures_reg->in;
    selector->fpRegDoWrite >> fp_reg_do_write_reg->in;
    selector->dataMemWrSrc >> data_mem_wr_src_ctrl_reg->in;
    selector->emissionCycle >> emissionCycle_reg->in;
    selector->doBranch >> do_branch_reg->in;

    selector->clear >>
        std::vector<Port<1> *>{
            &this->pc_reg->clear, &this->pc4_reg->clear,
            &this->alures_reg->clear, &this->r2_reg->clear,
            &this->reg_wr_src_ctrl_reg->clear,
            &this->wr_reg_idx_reg->clear,
            &this->reg_do_write_reg->clear,
            &this->mem_do_write_reg->clear,
            &this->mem_do_read_reg->clear, &this->mem_op_reg->clear,
            &this->valid_reg->clear, &f_r2_reg->clear,
            &falures_reg->clear, &fp_reg_do_write_reg->clear,
            &data_mem_wr_src_ctrl_reg->clear,
            &emissionCycle_reg->clear, &do_branch_reg->clear};
    selector->enable >>
        std::vector<Port<1> *>{
            &this->pc_reg->enable, &this->pc4_reg->enable,
            &this->alures_reg->enable, &this->r2_reg->enable,
            &this->reg_wr_src_ctrl_reg->enable,
            &this->wr_reg_idx_reg->enable,
            &this->reg_do_write_reg->enable,
            &this->mem_do_write_reg->enable,
            &this->mem_do_read_reg->enable, &this->mem_op_reg->enable,
            &this->valid_reg->enable, &f_r2_reg->enable,
            &falures_reg->enable, &fp_reg_do_write_reg->enable,
            &data_mem_wr_src_ctrl_reg->enable,
            &emissionCycle_reg->enable, &do_branch_reg->enable};

    f_r2_reg->out >> f_r2_out;
    falures_reg->out >> falures_out;
    fp_reg_do_write_reg->out >> fp_reg_do_write_out;
    data_mem_wr_src_ctrl_reg->out >> data_mem_wr_src_ctrl_out;
    emissionCycle_reg->out >> emissionCycle_out;
    do_branch_reg->out >> do_branch_out;

    // We want stalling info to persist through clearing of the register, so
    // stalled register is always enabled and never cleared.
    CONNECT_REGISTERED_CLEN_INPUT(stalled, 0, 1);
  }

  REGISTERED_CLEN_INPUT(f_r2, XLEN);
  REGISTERED_CLEN_INPUT(falures, XLEN);
  REGISTERED_CLEN_INPUT(fp_reg_do_write, 1);
  REGISTERED_CLEN_INPUT(data_mem_wr_src_ctrl, enumBitWidth<DataMemWrSrc>());
  REGISTERED_CLEN_INPUT(emissionCycle, 64);
  REGISTERED_CLEN_INPUT(do_branch, 1);
  INPUTPORT(control_flow_in, 1);

  REGISTERED_CLEN_INPUT(stalled, 1);

  void setFPExtensionEnabled(bool enabled) { fpEnabled = enabled; }
  void setPrioritizeControlFlow(bool enabled) {
    prioritizeControlFlow = enabled;
  }

  bool hasPendingInstructions() const {
    return fpEnabled && persistenceState->hasPendingInstructions();
  }

  VSRTL_VT_U resolvedInput(EXMEMField field, VSRTL_VT_U original) const {
    if (!fpEnabled) {
      return original;
    }
    return persistenceState->resolvedInput(field, original);
  }

  bool resolvedClear() const {
    return fpEnabled ? false : this->clear.uValue();
  }

  bool resolvedEnable() const {
    return fpEnabled ? true : this->enable.uValue();
  }

private:
  friend class EXMEMPersistenceState<XLEN>;
  bool fpEnabled = false;
  bool prioritizeControlFlow = false;
  SUBCOMPONENT(persistenceState, TYPE(EXMEMPersistenceState<XLEN>), this);
  SUBCOMPONENT(selector, TYPE(EXMEMInputSelector<XLEN>), this);
};

template <unsigned XLEN>
EXMEMInputSelector<XLEN>::EXMEMInputSelector(
    const std::string &name, SimComponent *parent, RV5S_EXMEM<XLEN> *exmem)
    : Component(name, parent) {
  setSensitiveTo(exmem->pc_in);
  setSensitiveTo(exmem->pc4_in);
  setSensitiveTo(exmem->alures_in);
  setSensitiveTo(exmem->r2_in);
  setSensitiveTo(exmem->f_r2_in);
  setSensitiveTo(exmem->falures_in);
  setSensitiveTo(exmem->reg_wr_src_ctrl_in);
  setSensitiveTo(exmem->wr_reg_idx_in);
  setSensitiveTo(exmem->reg_do_write_in);
  setSensitiveTo(exmem->fp_reg_do_write_in);
  setSensitiveTo(exmem->mem_do_write_in);
  setSensitiveTo(exmem->mem_do_read_in);
  setSensitiveTo(exmem->mem_op_in);
  setSensitiveTo(exmem->data_mem_wr_src_ctrl_in);
  setSensitiveTo(exmem->valid_in);
  setSensitiveTo(exmem->emissionCycle_in);
  setSensitiveTo(exmem->do_branch_in);
  setSensitiveTo(exmem->control_flow_in);
  setSensitiveTo(exmem->clear);
  setSensitiveTo(exmem->enable);

  pc << [exmem] {
    return exmem->resolvedInput(EXMEMField::PC, exmem->pc_in.uValue());
  };
  pc4 << [exmem] {
    return exmem->resolvedInput(EXMEMField::PC4, exmem->pc4_in.uValue());
  };
  aluResult << [exmem] {
    return exmem->resolvedInput(EXMEMField::ALUResult,
                                exmem->alures_in.uValue());
  };
  r2 << [exmem] {
    return exmem->resolvedInput(EXMEMField::R2, exmem->r2_in.uValue());
  };
  fr2 << [exmem] {
    return exmem->resolvedInput(EXMEMField::FR2, exmem->f_r2_in.uValue());
  };
  faluResult << [exmem] {
    return exmem->resolvedInput(EXMEMField::FALUResult,
                                exmem->falures_in.uValue());
  };
  regWrSrc << [exmem] {
    return exmem->resolvedInput(EXMEMField::RegWrSrc,
                                exmem->reg_wr_src_ctrl_in.uValue());
  };
  wrRegIdx << [exmem] {
    return exmem->resolvedInput(EXMEMField::WrRegIdx,
                                exmem->wr_reg_idx_in.uValue());
  };
  regDoWrite << [exmem] {
    return exmem->resolvedInput(EXMEMField::RegDoWrite,
                                exmem->reg_do_write_in.uValue());
  };
  fpRegDoWrite << [exmem] {
    return exmem->resolvedInput(EXMEMField::FPRegDoWrite,
                                exmem->fp_reg_do_write_in.uValue());
  };
  memDoWrite << [exmem] {
    return exmem->resolvedInput(EXMEMField::MemDoWrite,
                                exmem->mem_do_write_in.uValue());
  };
  memDoRead << [exmem] {
    return exmem->resolvedInput(EXMEMField::MemDoRead,
                                exmem->mem_do_read_in.uValue());
  };
  memOp << [exmem] {
    return exmem->resolvedInput(EXMEMField::MemOp,
                                exmem->mem_op_in.uValue());
  };
  dataMemWrSrc << [exmem] {
    return exmem->resolvedInput(EXMEMField::DataMemWrSrc,
                                exmem->data_mem_wr_src_ctrl_in.uValue());
  };
  valid << [exmem] {
    return exmem->resolvedInput(EXMEMField::Valid,
                                exmem->valid_in.uValue());
  };
  emissionCycle << [exmem] {
    return exmem->resolvedInput(EXMEMField::EmissionCycle,
                                exmem->emissionCycle_in.uValue());
  };
  doBranch << [exmem] {
    return exmem->resolvedInput(EXMEMField::DoBranch,
                                exmem->do_branch_in.uValue());
  };
  clear << [exmem] { return exmem->resolvedClear(); };
  enable << [exmem] { return exmem->resolvedEnable(); };
}

template <unsigned XLEN>
typename EXMEMPersistenceState<XLEN>::Instruction
EXMEMPersistenceState<XLEN>::captureIncoming() const {
  Instruction instruction;
  instruction.pc = exmem->pc_in.uValue();
  instruction.pc4 = exmem->pc4_in.uValue();
  instruction.aluResult = exmem->alures_in.uValue();
  instruction.r2 = exmem->r2_in.uValue();
  instruction.fr2 = exmem->f_r2_in.uValue();
  instruction.faluResult = exmem->falures_in.uValue();
  instruction.regWrSrc = exmem->reg_wr_src_ctrl_in.uValue();
  instruction.wrRegIdx = exmem->wr_reg_idx_in.uValue();
  instruction.regDoWrite = exmem->reg_do_write_in.uValue();
  instruction.fpRegDoWrite = exmem->fp_reg_do_write_in.uValue();
  instruction.memDoWrite = exmem->mem_do_write_in.uValue();
  instruction.memDoRead = exmem->mem_do_read_in.uValue();
  instruction.memOp = exmem->mem_op_in.uValue();
  instruction.dataMemWrSrc = exmem->data_mem_wr_src_ctrl_in.uValue();
  instruction.valid = exmem->valid_in.uValue();
  instruction.emissionCycle =
      normalizedEmissionCycle(exmem->emissionCycle_in.uValue());
  instruction.doBranch = exmem->do_branch_in.uValue();
  instruction.controlFlow = exmem->control_flow_in.uValue();
  return instruction;
}

template <unsigned XLEN>
std::optional<typename EXMEMPersistenceState<XLEN>::Instruction>
EXMEMPersistenceState<XLEN>::instructionForNextCycle() const {
  const uint64_t nextCycle = currentCycle + 1;
  if (hasValidIncoming()) {
    const Instruction incoming = captureIncoming();
    if (exmem->prioritizeControlFlow && incoming.controlFlow &&
        incoming.emissionCycle <= nextCycle)
      return incoming;
  }
  //buscamos una instrucción almacenada que debe salir el próximo ciclo
  const auto pending = std::find_if
  (
    pendingInstructions.begin(),
    pendingInstructions.end(),
    [nextCycle](const Instruction &instruction)
    {
      return instruction.emissionCycle == nextCycle;
    }
  );
  //si encontramos una la devolvemos
  if (pending != pendingInstructions.end()) {
    return *pending;
  }
  //si no hemos encontrado entonces intent
  if (hasValidIncoming()) {
    const Instruction incoming = captureIncoming();
    if (incoming.emissionCycle == nextCycle) {
      return incoming;
    }
  }
  return std::nullopt;
}

template <unsigned XLEN>
VSRTL_VT_U EXMEMPersistenceState<XLEN>::resolvedInput(
    EXMEMField field, VSRTL_VT_U original) const {
  if (!exmem->fpEnabled) {
    return original;
  }
  const auto instruction = instructionForNextCycle();
  if (!instruction.has_value()) {
    return 0;
  }

  switch (field) {
  case EXMEMField::PC:
    return instruction->pc;
  case EXMEMField::PC4:
    return instruction->pc4;
  case EXMEMField::ALUResult:
    return instruction->aluResult;
  case EXMEMField::R2:
    return instruction->r2;
  case EXMEMField::FR2:
    return instruction->fr2;
  case EXMEMField::FALUResult:
    return instruction->faluResult;
  case EXMEMField::RegWrSrc:
    return instruction->regWrSrc;
  case EXMEMField::WrRegIdx:
    return instruction->wrRegIdx;
  case EXMEMField::RegDoWrite:
    return instruction->regDoWrite;
  case EXMEMField::FPRegDoWrite:
    return instruction->fpRegDoWrite;
  case EXMEMField::MemDoWrite:
    return instruction->memDoWrite;
  case EXMEMField::MemDoRead:
    return instruction->memDoRead;
  case EXMEMField::MemOp:
    return instruction->memOp;
  case EXMEMField::DataMemWrSrc:
    return instruction->dataMemWrSrc;
  case EXMEMField::Valid:
    return instruction->valid;
  case EXMEMField::EmissionCycle:
    return instruction->emissionCycle;
  case EXMEMField::DoBranch:
    return instruction->doBranch;
  case EXMEMField::ControlFlow:
    return instruction->controlFlow;
  }
  return 0;
}

template <unsigned XLEN>
void EXMEMPersistenceState<XLEN>::save() {
  if (canReverse()) {
    reverseStack.push_front({currentCycle, pendingInstructions});
    if (reverseStack.size() > reverseStackSize()) {
      reverseStack.pop_back();
    }
  }

  const uint64_t nextCycle = currentCycle + 1;
  if (exmem->fpEnabled) {
    std::optional<Instruction> incoming;
    if (hasValidIncoming()) {
      incoming = captureIncoming();

      // Si una instruccion de control obtiene prioridad para este ciclo, las
      // reservas que colisionaban con ella aun no han sido emitidas. Deben
      // desplazarse antes de retirar la entrada seleccionada para nextCycle.
      if (exmem->prioritizeControlFlow && incoming->controlFlow) {
        for (auto &pending : pendingInstructions) {
          if (pending.emissionCycle >= incoming->emissionCycle)
            ++pending.emissionCycle;
        }
      }
    }

    pendingInstructions.erase(
        std::remove_if(
            pendingInstructions.begin(), pendingInstructions.end(),
            [nextCycle](const Instruction &instruction) {
              return instruction.emissionCycle == nextCycle;
            }),
        pendingInstructions.end());

    if (incoming.has_value() && incoming->emissionCycle > nextCycle) {
      pendingInstructions.push_back(std::move(*incoming));
    }
  }
  currentCycle = nextCycle;
}

template <unsigned XLEN>
void EXMEMPersistenceState<XLEN>::reset() {
  currentCycle = 0;
  pendingInstructions.clear();
  reverseStack.clear();
}

template <unsigned XLEN>
void EXMEMPersistenceState<XLEN>::reverse() {
  if (!reverseStack.empty()) {
    currentCycle = reverseStack.front().currentCycle;
    pendingInstructions =
        std::move(reverseStack.front().pendingInstructions);
    reverseStack.pop_front();
  }
}

template <unsigned XLEN>
void EXMEMPersistenceState<XLEN>::reverseStackSizeChanged() {
  if (reverseStackSize() < reverseStack.size()) {
    reverseStack.resize(reverseStackSize());
  }
}

} // namespace core
} // namespace vsrtl
