#pragma once

// Version 3 independiente de rv5s_hazardunit.h.
// Implementa un scoreboard temporal cuando F esta habilitada. Todas las
// instrucciones validas reciben un emissionCycle y compiten por la unica
// salida EX/MEM -> MEM. Requiere cablear las entradas nuevas desde rv5s.h.

#include "processors/RISC-V/rv_control.h"
#include "processors/RISC-V/riscv.h"
#include "ripessettings.h"
#include "VSRTL/core/vsrtl_component.h"
#include "VSRTL/core/vsrtl_register.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace vsrtl {
namespace core {
using namespace Ripes;

class HazardUnit;

class HazardUnitState : public ClockedComponent {
public:
  //tipos de unidades funcionales
  enum class FUType : uint8_t { Integer, FPAddSub, FPMul, FPDiv };

  //información de una instrucción en vuelo, para seguimientod de la Hazard Unit
  struct InstructionInfo {
    enum class ValueSource : uint8_t {
      None,
      TemporaryFP,
      Writeback
    };

    bool valid = false;
    uint64_t id = 0;
    AInt pc = 0;
    RVInstr opcode = RVInstr::NOP;
    bool writer = false;
    bool isFP = false;
    bool isLoad = false;
    bool isStore = false;
    bool forwardable = false;
    bool speculative = false;
    uint8_t rd = 0;

    uint64_t inputCycle = 0;
    uint64_t completionCycle = 0;
    uint64_t readyCycle = 0;
    uint64_t emissionCycle = 0;
    ValueSource valueSource = ValueSource::None;
  };

  //información del estado de una unidad funcional temporalmente en la Hazard Unit
  struct FunctionalUnit {
    FUType type = FUType::Integer;
    unsigned instance = 0;
    bool pipelined = false;
    unsigned latency = 1;
    std::vector<InstructionInfo> stages;
  };

  //datos de una instrucción entrante para analizar posibles conflictos
  struct IncomingInstruction {
    AInt pc = 0;
    RVInstr opcode = RVInstr::NOP;
    FUType requiredUnit = FUType::Integer;
    unsigned latency = 1;

    bool usesRs1 = false;
    bool rs1IsFP = false;
    uint8_t rs1 = 0;
    bool usesRs2 = false;
    bool rs2IsFP = false;
    uint8_t rs2 = 0;

    bool writer = false;
    bool destinationIsFP = false;
    bool isLoad = false;
    bool isStore = false;
    uint8_t rd = 0;
  };

  struct RegisterStatus {
    bool pending = false;
    uint64_t producerId = 0;
    uint64_t readyCycle = 0;
    uint64_t emissionCycle = 0;
    InstructionInfo::ValueSource valueSource =
        InstructionInfo::ValueSource::None;
  };

  //resultado del analisis de conflictos para una instrucción entrante
  struct Decision {

    bool rawHazard = false;
    bool structuralHazard = false;
    bool wawHazard = false;
    bool canBypass = false;
    bool mustStore = false;
    bool canAdvance = false;
    uint64_t completionCycle = 0;
    uint64_t readyCycle = 0;
    uint64_t emissionCycle = 0;
    std::optional<std::size_t> functionalUnitIndex;

    bool mustStall() const { return rawHazard || structuralHazard; }
  };

  //snapshot del estado de la Hazard Unit para permitir reversión
  struct StateSnapshot {
    uint64_t cycle = 0;
    uint64_t nextInstructionId = 1;
    std::vector<FunctionalUnit> functionalUnits;
    std::array<RegisterStatus, 32> integerRegisters{};
    std::array<RegisterStatus, 32> fpRegisters{};
    uint64_t lastMemoryEmissionCycle = 0;
    bool speculationActive = false;
    bool branchAwaitingResolution = false;
  };

  HazardUnitState(const std::string &name, SimComponent *parent,
                  HazardUnit *owner)
      : ClockedComponent(name, parent), owner(owner) {
    initializeFunctionalUnits();
  }

  void setEnabled(bool enabled) {
    trackingEnabled = enabled;
    // Si no estan hbilitadas instrcciones FP, nose usa persistencia temporal de instrucciones
    if (!trackingEnabled)
      clearInstructions();
  }

  void setThreeSlotControlFlow(bool enabled, bool squashSpeculation) {
    prioritizeControlFlow = enabled;
    speculativeControlFlow = enabled && squashSpeculation;
    if (!speculativeControlFlow) {
      speculationActive = false;
      branchAwaitingResolution = false;
    }
  }

  // configuración de unidades funcionales para conocer el estado de cada una en paralelo y facilitar la deteccion de conflictos
  void setConfiguration(unsigned addLatency, unsigned multiplyLatency,
                        unsigned divideLatency, unsigned addCount,
                        unsigned multiplyCount, unsigned divideCount,
                        bool addPipelined, bool multiplyPipelined,
                        bool dividePipelined) {
    addSubLatency = std::clamp(addLatency, 1u, 4u);
    mulLatency = std::clamp(multiplyLatency, 1u, 7u);
    divLatency = std::clamp(divideLatency, 1u, 25u);
    addSubCount = addPipelined ? 1u : std::clamp(addCount, 1u, 3u);
    mulCount = multiplyPipelined ? 1u : std::clamp(multiplyCount, 1u, 3u);
    divCount = dividePipelined ? 1u : std::clamp(divideCount, 1u, 3u);
    addSubSegmented = addPipelined;
    mulSegmented = multiplyPipelined;
    divSegmented = dividePipelined;
    initializeFunctionalUnits();
    integerRegisters = {};
    fpRegisters = {};
    lastMemoryEmissionCycle = 0;
    reverseStack.clear();
  }

  // nos incicará en que ciclo nos encontramos actualmente
  uint64_t cycle() const { return currentCycle; }

  const std::vector<FunctionalUnit> &functionalUnits() const {
    return functionalUnitList;
  }

  bool hasPendingInstructions() const {
    if (!trackingEnabled)
      return false;
    return std::any_of(
        functionalUnitList.begin(), functionalUnitList.end(),
        [](const FunctionalUnit &unit) {
          return std::any_of(
              unit.stages.begin(), unit.stages.end(),
              [](const InstructionInfo &instruction) {
                return instruction.valid;
              });
        });
  }

  bool hasPendingInstructionsAfterNextClock() const {
    if (!trackingEnabled)
      return false;

    const uint64_t nextCycle = currentCycle + 1;
    for (const auto &unit : functionalUnitList) {
      if (unit.stages.empty())
        continue;

      if (!unit.pipelined || unit.stages.size() == 1) {
        const auto &instruction = unit.stages.front();
        if (instruction.valid && instruction.emissionCycle > nextCycle)
          return true;
        continue;
      }

      for (std::size_t stage = 0; stage + 1 < unit.stages.size(); ++stage) {
        if (unit.stages[stage].valid)
          return true;
      }

      const auto &lastInstruction = unit.stages.back();
      if (lastInstruction.valid &&
          lastInstruction.emissionCycle > nextCycle)
        return true;
    }
    return false;
  }

  // una instrucción está pendiente si su ciclo de saldia es superior al ciclo actual
  bool isPending(const InstructionInfo &instruction) const {
    return instruction.valid && instruction.emissionCycle > currentCycle;
  }
  // incoming es la instrucción en ID esperando en las entradas de ID/EX.
  // La instrucción que sale de ID/EX ya la comprueba la lógica clásica.
  Decision analyze(const IncomingInstruction &incoming) const {
    Decision result;
    //si no esta habilitada el seguimiento de instrucciones FP o bien la instrucción entrante es NOP, no hay conflictos
    if (!trackingEnabled || incoming.opcode == RVInstr::NOP)
      return result;

    //ciclo ideal en que la instrucción estará completa si no encuentra
    //atascos dentro de la unidad funcional
    result.completionCycle = currentCycle + incoming.latency;
    result.emissionCycle = result.completionCycle + 1;
    std::vector<uint64_t> reservedOutputCycles;
    inspectRAW(incoming, result);

    // Una sola pasada obtiene RAW, WAW, ciclos reservados y unidad destino.
    for (std::size_t index = 0; index < functionalUnitList.size(); ++index) {
      const auto &unit = functionalUnitList[index];

      //si la unidad funcional es del tipo requerido, aun no se asigno una unidad y la unidad funcional dispone de espacio
      //para aceptar la instrucción entrante, se asigna la unidad funcional a la instrucción entrante
      if (unit.type == incoming.requiredUnit &&
          !result.functionalUnitIndex.has_value() &&
          inputWillBeAvailable(unit)) {
        result.functionalUnitIndex = index;
      }

      for (const auto &older : unit.stages) {
        if (!isPending(older))
          continue;
        reservedOutputCycles.push_back(older.emissionCycle);
      }
    }

    //si resulta que no tenermos unidad funcional asignada hasta ahora eso es porque hay un riesgo estructural
    result.structuralHazard = !result.functionalUnitIndex.has_value();

    if (!result.rawHazard && !result.structuralHazard) {
      const auto &selectedUnit =
          functionalUnitList.at(*result.functionalUnitIndex);
      const uint64_t neighborEmissionCycle =
          nearestInputNeighborEmissionCycle(selectedUnit);
      result.completionCycle =
          std::max(currentCycle + incoming.latency,
                   neighborEmissionCycle);
      result.emissionCycle = result.completionCycle + 1;
    }

    inspectWAW(incoming, result);
    if (incoming.isLoad || incoming.isStore) {
      result.emissionCycle =
          std::max(result.emissionCycle, lastMemoryEmissionCycle + 1);
    }

    //si el ciclo de salida de la instrucción entrante coincide con un ciclo reservado por otra instrucción,
    //se incrementa el ciclo de salida hasta que no haya conflictos
    while (
      std::find(reservedOutputCycles.begin(), reservedOutputCycles.end(),
                result.emissionCycle) != reservedOutputCycles.end()
    ) {
      ++result.emissionCycle;
    }

    // En 3 slots un salto no debe quedar esperando detras de reservas de la
    // salida comun. El desplazamiento coordinado de las reservas antiguas se
    // realiza cuando el salto termina EX.
    if (prioritizeControlFlow && isControlFlow(incoming.opcode))
      result.emissionCycle = result.completionCycle + 1;

    //aplicamos bypass si no hay riego RAW ni estructural, si la latencia es 1 y no hay conflictos de salida
    result.canAdvance = !result.mustStall();
    result.canBypass =
        result.canAdvance &&
        result.emissionCycle == result.completionCycle + 1;
    //si no hay stall pero tampoco puede pasar por bypass, entonces debe almacenarse en la unidad funcional
    result.mustStore =
        result.canAdvance &&
        result.emissionCycle > result.completionCycle + 1;
    if (incoming.writer) {
      if (incoming.isLoad) {
        result.readyCycle = result.emissionCycle + 1;
      } else if (incoming.destinationIsFP &&
                 isFPArithmeticInstruction(incoming.opcode)) {
        result.readyCycle = result.completionCycle;
      } else {
        // La logica clasica de forwarding integer permite consumir en EX el
        // resultado de una instruccion que se encuentra en MEM. Las cargas
        // quedan excluidas por la rama anterior porque su dato solo estara
        // disponible desde WB.
        result.readyCycle = result.emissionCycle;
      }
    }
    return result;
  }

  //almacenamiento de los datos de la instrucción entrante en la unidad funcional asignada, si corresponde
  void store(const IncomingInstruction &incoming, const Decision &decision) {
    if (!trackingEnabled || !decision.canAdvance ||
        !decision.functionalUnitIndex.has_value())
      return;

    auto &unit = functionalUnitList.at(*decision.functionalUnitIndex);
    if (unit.stages.empty() || !inputWillBeAvailable(unit))
      return;

    InstructionInfo instruction;
    instruction.valid = true;
    instruction.id = nextInstructionId++;
    instruction.pc = incoming.pc;
    instruction.opcode = incoming.opcode;
    instruction.writer = incoming.writer;
    instruction.isFP = incoming.destinationIsFP;
    instruction.isLoad = incoming.isLoad;
    instruction.isStore = incoming.isStore;
    instruction.forwardable =
        incoming.destinationIsFP && !incoming.isLoad &&
        isFPArithmeticInstruction(incoming.opcode);
    instruction.speculative = speculativeControlFlow && speculationActive;
    instruction.rd = incoming.rd;
    instruction.inputCycle = currentCycle + 1;
    instruction.completionCycle = decision.completionCycle;
    instruction.readyCycle = decision.readyCycle;
    instruction.emissionCycle = decision.emissionCycle;
    instruction.valueSource =
        instruction.forwardable
            ? InstructionInfo::ValueSource::TemporaryFP
            : (instruction.writer
                   ? InstructionInfo::ValueSource::Writeback
                   : InstructionInfo::ValueSource::None);

    unit.stages.front() = instruction;
    if (instruction.writer &&
        validRegister(instruction.isFP, instruction.rd)) {
      auto &status = registerStatus(instruction.isFP, instruction.rd);
      status.pending = true;
      status.producerId = instruction.id;
      status.readyCycle = instruction.readyCycle;
      status.emissionCycle = instruction.emissionCycle;
      status.valueSource = instruction.valueSource;
    }
    if (instruction.isLoad || instruction.isStore)
      lastMemoryEmissionCycle = instruction.emissionCycle;
    if (speculativeControlFlow && isControlFlow(instruction.opcode))
      speculationActive = true;
  }

  void save() override;

  void reset() override {
    currentCycle = 0;
    nextInstructionId = 1;
    integerRegisters = {};
    fpRegisters = {};
    lastMemoryEmissionCycle = 0;
    speculationActive = false;
    branchAwaitingResolution = false;
    clearInstructions();
    reverseStack.clear();
  }

  void reverse() override {
    if (reverseStack.empty())
      return;
    currentCycle = reverseStack.front().cycle;
    nextInstructionId = reverseStack.front().nextInstructionId;
    functionalUnitList =
        std::move(reverseStack.front().functionalUnits);
    integerRegisters = reverseStack.front().integerRegisters;
    fpRegisters = reverseStack.front().fpRegisters;
    lastMemoryEmissionCycle =
        reverseStack.front().lastMemoryEmissionCycle;
    speculationActive = reverseStack.front().speculationActive;
    branchAwaitingResolution =
        reverseStack.front().branchAwaitingResolution;
    reverseStack.pop_front();
  }

  void forceValue(VSRTL_VT_U, VSRTL_VT_U) override {}

  void reverseStackSizeChanged() override {
    if (reverseStackSize() < reverseStack.size())
      reverseStack.resize(reverseStackSize());
  }

  static FUType unitTypeFor(RVInstr opcode) {
    switch (opcode) {
    case RVInstr::FADD:
    case RVInstr::FSUB:
    case RVInstr::FADDD:
    case RVInstr::FSUBD:
    case RVInstr::FMIN:
    case RVInstr::FMAX:
    case RVInstr::FSGNJ:
    case RVInstr::FSGNJN:
    case RVInstr::FSGNJX:
    case RVInstr::FMIND:
    case RVInstr::FMAXD:
    case RVInstr::FSGNJD:
    case RVInstr::FSGNJND:
    case RVInstr::FSGNJXD:
    case RVInstr::FCVTSD:
    case RVInstr::FCVTDS:
      return FUType::FPAddSub;
    case RVInstr::FMUL:
    case RVInstr::FMULD:
      return FUType::FPMul;
    case RVInstr::FDIV:
    case RVInstr::FDIVD:
    case RVInstr::FSQRT:
    case RVInstr::FSQRTD:
      return FUType::FPDiv;
    case RVInstr::FLW:
    case RVInstr::FSW:
    case RVInstr::FLD:
    case RVInstr::FSD:
      return FUType::Integer;
    default:
      return FUType::Integer;
    }
  }

  static bool isFPArithmeticInstruction(RVInstr opcode) {
    switch (opcode) {
    case RVInstr::FADD:
    case RVInstr::FSUB:
    case RVInstr::FMUL:
    case RVInstr::FDIV:
    case RVInstr::FSQRT:
    case RVInstr::FMIN:
    case RVInstr::FMAX:
    case RVInstr::FSGNJ:
    case RVInstr::FSGNJN:
    case RVInstr::FSGNJX:
    case RVInstr::FADDD:
    case RVInstr::FSUBD:
    case RVInstr::FMULD:
    case RVInstr::FDIVD:
    case RVInstr::FSQRTD:
    case RVInstr::FMIND:
    case RVInstr::FMAXD:
    case RVInstr::FSGNJD:
    case RVInstr::FSGNJND:
    case RVInstr::FSGNJXD:
    case RVInstr::FCVTSD:
    case RVInstr::FCVTDS:
      return true;
    default:
      return false;
    }
  }

  unsigned latencyFor(RVInstr opcode) const {
    switch (opcode) {
    case RVInstr::FADD:
    case RVInstr::FSUB:
    case RVInstr::FADDD:
    case RVInstr::FSUBD:
    case RVInstr::FMIN:
    case RVInstr::FMAX:
    case RVInstr::FSGNJ:
    case RVInstr::FSGNJN:
    case RVInstr::FSGNJX:
    case RVInstr::FMIND:
    case RVInstr::FMAXD:
    case RVInstr::FSGNJD:
    case RVInstr::FSGNJND:
    case RVInstr::FSGNJXD:
    case RVInstr::FCVTSD:
    case RVInstr::FCVTDS:
      return addSubLatency;
    case RVInstr::FMUL:
    case RVInstr::FMULD:
      return mulLatency;
    case RVInstr::FDIV:
    case RVInstr::FDIVD:
    case RVInstr::FSQRT:
    case RVInstr::FSQRTD:
      return divLatency;
    case RVInstr::FLW:
    case RVInstr::FSW:
    case RVInstr::FLD:
    case RVInstr::FSD:
      return 1;
    default:
      return 1;
    }
  }

private:
  HazardUnit *owner = nullptr;
  bool trackingEnabled = false;
  uint64_t currentCycle = 0;
  uint64_t nextInstructionId = 1;
  unsigned addSubLatency = 1, mulLatency = 1, divLatency = 1;
  unsigned addSubCount = 1, mulCount = 1, divCount = 1;
  bool addSubSegmented = false, mulSegmented = false, divSegmented = false;
  std::vector<FunctionalUnit> functionalUnitList;
  std::array<RegisterStatus, 32> integerRegisters{};
  std::array<RegisterStatus, 32> fpRegisters{};
  uint64_t lastMemoryEmissionCycle = 0;
  std::deque<StateSnapshot> reverseStack;

  static bool validRegister(bool isFP, uint8_t reg) {
    return isFP || reg != 0;
  }

  RegisterStatus &registerStatus(bool fp, uint8_t reg) {
    return fp ? fpRegisters.at(reg) : integerRegisters.at(reg);
  }

  const RegisterStatus &registerStatus(bool fp, uint8_t reg) const {
    return fp ? fpRegisters.at(reg) : integerRegisters.at(reg);
  }

  bool sourceHasUnresolvedRAW(bool used, bool fp, uint8_t reg,
                              uint64_t consumerUseCycle) const {
    if (!used || !validRegister(fp, reg))
      return false;
    const auto &producer = registerStatus(fp, reg);
    if (!producer.pending)
      return false;
    if (producer.valueSource == InstructionInfo::ValueSource::None)
      return true;

    // Un resultado FP anticipado solo puede consumirlo otra instruccion a
    // partir del ciclo posterior a completar la ultima etapa A/M/D.
    if (producer.valueSource == InstructionInfo::ValueSource::TemporaryFP)
      return producer.readyCycle >= consumerUseCycle;

    // Los valores procedentes de WB, incluidas las cargas, pueden consumirse
    // en el mismo ciclo mediante forwarding.
    return producer.readyCycle > consumerUseCycle;
  }

  void inspectRAW(const IncomingInstruction &incoming,
                  Decision &result) const {
    const uint64_t consumerUseCycle = currentCycle + 1;
    result.rawHazard =
        sourceHasUnresolvedRAW(incoming.usesRs1, incoming.rs1IsFP,
                               incoming.rs1, consumerUseCycle) ||
        sourceHasUnresolvedRAW(incoming.usesRs2, incoming.rs2IsFP,
                               incoming.rs2, consumerUseCycle);
  }

  void inspectWAW(const IncomingInstruction &incoming,
                  Decision &result) const {
    if (!incoming.writer ||
        !validRegister(incoming.destinationIsFP, incoming.rd))
      return;
    const auto &older =
        registerStatus(incoming.destinationIsFP, incoming.rd);
    if (!older.pending)
      return;
    result.wawHazard = true;
    result.emissionCycle =
        std::max(result.emissionCycle, older.emissionCycle + 1);
  }

  uint64_t
  nearestInputNeighborEmissionCycle(const FunctionalUnit &unit) const {
    for (const auto &instruction : unit.stages) {
      if (isPending(instruction))
        return instruction.emissionCycle;
    }
    return 0;
  }

  //funcion comprobadora de si la unidad funcional unit posee la primera etapa libre para aceptar una instrucción entrante
  bool inputWillBeAvailable(const FunctionalUnit &unit) const {
    if (unit.stages.empty())
      return false;
    //comprobación que la instrucción en el primer ciclo no es una activa
    if (!isPending(unit.stages.front()))
      return true;
    //si la unidad funcional es no segmentada o bien posee una unica etapa entonces no puede aceptar una instrucción entrante
    if (!unit.pipelined || unit.stages.size() == 1)
      return unit.stages.front().emissionCycle <= currentCycle + 1;

    // La última etapa quedará libre si su instrucción sale en el
    // próximo flanco o si actualmente no contiene una instrucción activa.
    bool nextStageFree =
        !isPending(unit.stages.back()) ||
        unit.stages.back().emissionCycle <= currentCycle + 1;

    // Propaga la disponibilidad desde la última etapa hasta la primera.
    for (std::size_t stage = unit.stages.size() - 1; stage > 0; --stage) {
      const auto &currentStage = unit.stages[stage - 1];

      // Esta etapa quedará libre si ya lo está o si su instrucción
      // puede desplazarse hacia la siguiente.
      nextStageFree = !isPending(currentStage) || nextStageFree;
    }


    return nextStageFree;
  }

  //actualización del estadod e las unidades funcionales en el flancod de reloj durante clockedUpdate
  void updateFunctionalUnits(uint64_t nextCycle) {
    for (auto &unit : functionalUnitList) {
      if (unit.stages.empty())
        continue;
      if (!unit.pipelined) {
        auto &instruction = unit.stages.front();
        if (instruction.valid &&
            instruction.emissionCycle <= nextCycle)
          instruction = {};
        continue;
      }

      auto &last = unit.stages.back();
      if (last.valid && last.emissionCycle <= nextCycle)
        last = {};

      for (std::size_t stage = unit.stages.size() - 1; stage > 0; --stage) {
        auto &destination = unit.stages[stage];
        auto &source = unit.stages[stage - 1];
        if (!destination.valid && source.valid) {
          destination = source;
          source = {};
        }
      }
    }
  }

  //metodo para agregar una unidad funcional a la lista de unidades funcionales de la Hazard Unit
  void appendUnits(FUType type, unsigned latency, unsigned count, bool pipelined) {
    for (unsigned instance = 0; instance < count; ++instance) {
      FunctionalUnit unit;
      unit.type = type;
      unit.instance = instance;
      unit.pipelined = pipelined;
      unit.latency = latency;
      unit.stages.resize(pipelined ? latency : 1u);
      functionalUnitList.push_back(std::move(unit));
    }
  }

  void initializeFunctionalUnits() {
    functionalUnitList.clear();
    appendUnits(FUType::Integer, 1, 1, true);
    appendUnits(FUType::FPAddSub, addSubLatency,
      addSubCount,addSubSegmented);
    appendUnits(FUType::FPMul, mulLatency, mulCount, mulSegmented);
    appendUnits(FUType::FPDiv, divLatency, divCount, divSegmented);
  }

  void clearInstructions() {
    for (auto &unit : functionalUnitList)
      for (auto &instruction : unit.stages)
        instruction = {};
    integerRegisters = {};
    fpRegisters = {};
    lastMemoryEmissionCycle = 0;
  }

  void updateRegisterStatuses(uint64_t nextCycle) {
    auto clearCommitted = [nextCycle](auto &registers) {
      for (auto &status : registers) {
        // Todos los escritores atraviesan WB un ciclo despues de MEM.
        if (status.pending &&
            status.emissionCycle + 1 <= nextCycle)
          status = {};
      }
    };
    clearCommitted(integerRegisters);
    clearCommitted(fpRegisters);
  }

  void saveToStack() {
    if (canReverse()) {
      reverseStack.push_front(
          {currentCycle, nextInstructionId, functionalUnitList,
           integerRegisters, fpRegisters, lastMemoryEmissionCycle,
           speculationActive, branchAwaitingResolution});
      if (reverseStack.size() > reverseStackSize())
        reverseStack.pop_back();
    }
  }

  static bool isControlFlow(RVInstr opcode) {
    return Control::do_branch_ctrl(opcode) || Control::do_jump_ctrl(opcode);
  }

  bool controlFlowEmitsAt(uint64_t cycle) const {
    for (const auto &unit : functionalUnitList)
      for (const auto &instruction : unit.stages)
        if (instruction.valid && isControlFlow(instruction.opcode) &&
            instruction.emissionCycle <= cycle)
          return true;
    return false;
  }

  void prioritizeControlFlowAt(uint64_t cycle) {
    bool keptPriorityInstruction = false;
    for (auto &unit : functionalUnitList) {
      for (auto &instruction : unit.stages) {
        if (!instruction.valid)
          continue;
        if (!keptPriorityInstruction && isControlFlow(instruction.opcode) &&
            instruction.emissionCycle <= cycle) {
          keptPriorityInstruction = true;
          continue;
        }
        if (instruction.emissionCycle >= cycle) {
          ++instruction.emissionCycle;
          if (instruction.isLoad)
            instruction.readyCycle = instruction.emissionCycle + 1;
          else if (!instruction.forwardable && instruction.writer)
            instruction.readyCycle = instruction.emissionCycle;
        }
      }
    }
    rebuildTrackedResources();
  }

  void rebuildTrackedResources() {
    integerRegisters = {};
    fpRegisters = {};
    lastMemoryEmissionCycle = 0;
    for (const auto &unit : functionalUnitList) {
      for (const auto &instruction : unit.stages) {
        if (!instruction.valid)
          continue;
        if (instruction.writer && validRegister(instruction.isFP,
                                                instruction.rd)) {
          auto &status = registerStatus(instruction.isFP, instruction.rd);
          if (!status.pending || instruction.id >= status.producerId) {
            status.pending = true;
            status.producerId = instruction.id;
            status.readyCycle = instruction.readyCycle;
            status.emissionCycle = instruction.emissionCycle;
            status.valueSource = instruction.valueSource;
          }
        }
        if (instruction.isLoad || instruction.isStore)
          lastMemoryEmissionCycle =
              std::max(lastMemoryEmissionCycle, instruction.emissionCycle);
      }
    }
  }

  void resolveSpeculation(bool taken) {
    if (taken) {
      for (auto &unit : functionalUnitList)
        for (auto &instruction : unit.stages)
          if (instruction.speculative)
            instruction = {};
    } else {
      for (auto &unit : functionalUnitList)
        for (auto &instruction : unit.stages)
          instruction.speculative = false;
    }
    speculationActive = false;
    for (const auto &unit : functionalUnitList)
      for (const auto &instruction : unit.stages)
        speculationActive |= instruction.valid &&
                             isControlFlow(instruction.opcode);
    rebuildTrackedResources();
  }

  bool prioritizeControlFlow = false;
  bool speculativeControlFlow = false;
  bool speculationActive = false;
  bool branchAwaitingResolution = false;
};

class HazardUnit : public Component {
public:
  using FUType = HazardUnitState::FUType;
  using IncomingInstruction = HazardUnitState::IncomingInstruction;
  using InstructionInfo = HazardUnitState::InstructionInfo;
  using FunctionalUnit = HazardUnitState::FunctionalUnit;
  using Decision = HazardUnitState::Decision;

  HazardUnit(const std::string &name, SimComponent *parent)
      : Component(name, parent) {
    // verificamos si la extension F esta activada en la configuración de Ripes para habilitar el seguimiento de ejecución fuera de orden
    const QStringList enabledExtensions =
        RipesSettings::value(RIPES_SETTING_PROCESSOR_EXTENSIONS)
            .value<QStringList>();
    // ajustamos el enable del HazardUnitState según si la extensión F está habilitada o no
    setFPExtensionEnabled(enabledExtensions.contains("F"));

    hazardFEEnable << [this] { return !hasHazard(); };
    hazardIDEXClear << [this] {
      const bool classicLoadUse =
          hasLoadUseHazard() || hasFPLoadUseHazard();
      if (!fpEnabled)
        return classicLoadUse;

      const auto decision = extendedDecision();
      return classicLoadUse || decision.rawHazard ||
             decision.structuralHazard || hasIncomingEcallHazard();
    };
    hazardIDEXEnable << [this] {
      return !hasEcallHazard();
    };
    hazardEXMEMClear << [this] {
      return hasEcallHazard();
    };
    stallEcallHandling << [this] { return hasEcallHazard(); };
    emissionCycle << [this] {
      if (!fpEnabled || !id_valid.uValue())
        return VSRTL_VT_U(0);
      const auto decision = extendedDecision();
      return decision.canAdvance
                 ? VSRTL_VT_U(decision.emissionCycle)
                 : VSRTL_VT_U(0);
    };
  }

  //activamos o desactivamos la persistencia temporal de datos en Hazard segun si la extension F esta habilitada o no
  void setFPExtensionEnabled(bool enabled) {
    fpEnabled = enabled;
    state->setEnabled(enabled);
  }

  // En 3 slots se conserva la planificacion temporal de 2 slots, pero los
  // saltos se resuelven al llegar a MEM. La variante delayed no invalida las
  // instrucciones posteriores y por eso pasa squashSpeculation=false.
  void setThreeSlotControlFlow(bool enabled, bool squashSpeculation) {
    threeSlotSpeculation = enabled && squashSpeculation;
    state->setThreeSlotControlFlow(enabled, squashSpeculation);
  }

  const std::vector<FunctionalUnit> &functionalUnits() const {
    return state->functionalUnits();
  }

  bool hasPendingInstructions() const {
    return state->hasPendingInstructions();
  }

  uint64_t currentCycle() const { return state->cycle(); }

  bool dataHazardActive() const {
    const bool classic = hasLoadUseHazard() || hasFPLoadUseHazard();
    return classic || (fpEnabled && extendedDecision().rawHazard);
  }

  bool structuralHazardActive() const {
    return fpEnabled && extendedDecision().structuralHazard;
  }

  //notificación de la configuración de las unidades funcionales en EX para el seguimiento de ejecución fuera de orden
  void setConfiguration(unsigned addLatency, unsigned multiplyLatency,
                        unsigned divideLatency, unsigned addCount,
                        unsigned multiplyCount, unsigned divideCount,
                        bool addPipelined, bool multiplyPipelined,
                        bool dividePipelined) {
    state->setConfiguration(addLatency, multiplyLatency, divideLatency,
                            addCount, multiplyCount, divideCount, addPipelined,
                            multiplyPipelined, dividePipelined);
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
  INPUTPORT(wb_do_reg_write, 1);
  INPUTPORT(mem_do_fp_write, 1);
  INPUTPORT(wb_do_fp_write, 1);
  INPUTPORT_ENUM(opcode, RVInstr);
  INPUTPORT_ENUM(id_opcode, RVInstr);
  INPUTPORT(branchTakenFromMEM, 1);

  OUTPUTPORT(hazardFEEnable, 1);
  OUTPUTPORT(hazardIDEXEnable, 1);
  OUTPUTPORT(hazardEXMEMClear, 1);
  OUTPUTPORT(hazardIDEXClear, 1);
  OUTPUTPORT(stallEcallHandling, 1);
  OUTPUTPORT(emissionCycle, 64);

private:
  friend class HazardUnitState;
  //estado interno de la Hazard Unit que posibilita la persistencia temporal de datos para consultar posibles conflictos cuando la extension F esta habilitada
  SUBCOMPONENT(state, HazardUnitState, this);
  //bool que concientiza si la extension F está habilitada o no.
  bool fpEnabled = false;
  bool threeSlotSpeculation = false;

  struct SourceUsage {
    bool usesRs1 = false;
    bool rs1IsFP = false;
    bool usesRs2 = false;
    bool rs2IsFP = false;
  };

  static SourceUsage sourceUsageFor(RVInstr instruction) {
    switch (instruction) {
    case RVInstr::NOP:
    case RVInstr::ECALL:
    case RVInstr::LUI:
    case RVInstr::AUIPC:
    case RVInstr::JAL:
      return {};

    case RVInstr::JALR:
    case RVInstr::LB:
    case RVInstr::LH:
    case RVInstr::LW:
    case RVInstr::LBU:
    case RVInstr::LHU:
    case RVInstr::LWU:
    case RVInstr::LD:
    case RVInstr::ADDI:
    case RVInstr::SLTI:
    case RVInstr::SLTIU:
    case RVInstr::XORI:
    case RVInstr::ORI:
    case RVInstr::ANDI:
    case RVInstr::SLLI:
    case RVInstr::SRLI:
    case RVInstr::SRAI:
    case RVInstr::ADDIW:
    case RVInstr::SLLIW:
    case RVInstr::SRLIW:
    case RVInstr::SRAIW:
      return {true, false, false, false};

    // Carga FP: direccion base integer; no hay segunda fuente FP.
    case RVInstr::FLW:
    case RVInstr::FLD:
      return {true, false, false, false};

    // Store FP: direccion base integer y dato fuente FP.
    case RVInstr::FSW:
    case RVInstr::FSD:
      return {true, false, true, true};

    // Operaciones FP unarias.
    case RVInstr::FSQRT:
    case RVInstr::FSQRTD:
    case RVInstr::FCVTSD:
    case RVInstr::FCVTDS:
      return {true, true, false, false};

    // Operaciones FP binarias.
    case RVInstr::FADD:
    case RVInstr::FSUB:
    case RVInstr::FMUL:
    case RVInstr::FDIV:
    case RVInstr::FMIN:
    case RVInstr::FMAX:
    case RVInstr::FSGNJ:
    case RVInstr::FSGNJN:
    case RVInstr::FSGNJX:
    case RVInstr::FADDD:
    case RVInstr::FSUBD:
    case RVInstr::FMULD:
    case RVInstr::FDIVD:
    case RVInstr::FMIND:
    case RVInstr::FMAXD:
    case RVInstr::FSGNJD:
    case RVInstr::FSGNJND:
    case RVInstr::FSGNJXD:
      return {true, true, true, true};

    // Para cualquier instruccion integer se comparan directamente ambos
    // campos fuente con destinos integer en vuelo, como solicita esta variante.
    default:
      return {true, false, true, false};
    }
  }

  bool hasHazard() const {
    // Lógica original de la HazardUnit.
    const bool classicHazard =
        hasLoadUseHazard() || hasEcallHazard() ||
        hasFPLoadUseHazard();

    if (!fpEnabled)
      return classicHazard;

    // Con F habilitada, la tabla multiciclo solo añade riesgos que la
    // lógica clásica no puede observar.
    const auto decision = extendedDecision();
    return classicHazard || decision.rawHazard ||
           decision.structuralHazard || hasIncomingEcallHazard();
  }

  Decision extendedDecision() const {
    if (!fpEnabled || !id_valid.uValue())
      return {};
    //consultamos las señales en ID
    const IncomingInstruction incoming = incomingFromID();
    return state->analyze(incoming);
  }

  IncomingInstruction incomingFromID() const {
    IncomingInstruction incoming;
    incoming.pc = id_pc.uValue();
    incoming.opcode = id_opcode.eValue<RVInstr>();
    incoming.requiredUnit = HazardUnitState::unitTypeFor(incoming.opcode);
    incoming.latency = state->latencyFor(incoming.opcode);
    incoming.rs1 = static_cast<uint8_t>(id_reg1_idx.uValue());
    incoming.rs2 = static_cast<uint8_t>(id_reg2_idx.uValue());
    incoming.rd = static_cast<uint8_t>(id_reg_wr_idx.uValue());
    incoming.destinationIsFP = writesFPRegister(incoming.opcode);
    incoming.writer =
        incoming.destinationIsFP || writesIntegerRegister(incoming.opcode);
    incoming.isLoad = isLoadInstruction(incoming.opcode);
    incoming.isStore = isStoreInstruction(incoming.opcode);

    //En este diseño nuevo la tabla se usa para las fuentes FP.
    const SourceUsage usage = sourceUsageFor(incoming.opcode);
    incoming.usesRs1 = usage.usesRs1;
    incoming.rs1IsFP = usage.rs1IsFP;
    incoming.usesRs2 = usage.usesRs2;
    incoming.rs2IsFP = usage.rs2IsFP;
    return incoming;
  }

  bool extendedInstructionAccepted() const {
    if (!fpEnabled || !id_valid.uValue() || hasEcallHazard() ||
        (threeSlotSpeculation && branchTakenFromMEM.uValue()) ||
        hasIncomingEcallHazard() ||
        hasLoadUseHazard() ||
        hasFPLoadUseHazard())
      return false;
    return extendedDecision().canAdvance;
  }

  static bool writesFPRegister(RVInstr instruction) {
    switch (instruction) {
    case RVInstr::FLW:
    case RVInstr::FLD:
    case RVInstr::FADD:
    case RVInstr::FSUB:
    case RVInstr::FMUL:
    case RVInstr::FDIV:
    case RVInstr::FSQRT:
    case RVInstr::FMIN:
    case RVInstr::FMAX:
    case RVInstr::FSGNJ:
    case RVInstr::FSGNJN:
    case RVInstr::FSGNJX:
    case RVInstr::FADDD:
    case RVInstr::FSUBD:
    case RVInstr::FMULD:
    case RVInstr::FDIVD:
    case RVInstr::FSQRTD:
    case RVInstr::FMIND:
    case RVInstr::FMAXD:
    case RVInstr::FSGNJD:
    case RVInstr::FSGNJND:
    case RVInstr::FSGNJXD:
    case RVInstr::FCVTSD:
    case RVInstr::FCVTDS:
      return true;
    default:
      return false;
    }
  }

  static bool writesIntegerRegister(RVInstr instruction) {
    switch (instruction) {
    case RVInstr::LUI:
    case RVInstr::AUIPC:
    case RVInstr::JAL:
    case RVInstr::JALR:
    case RVInstr::LB:
    case RVInstr::LH:
    case RVInstr::LW:
    case RVInstr::LBU:
    case RVInstr::LHU:
    case RVInstr::ADDI:
    case RVInstr::SLTI:
    case RVInstr::SLTIU:
    case RVInstr::XORI:
    case RVInstr::ORI:
    case RVInstr::ANDI:
    case RVInstr::SLLI:
    case RVInstr::SRLI:
    case RVInstr::SRAI:
    case RVInstr::ADD:
    case RVInstr::SUB:
    case RVInstr::SLL:
    case RVInstr::SLT:
    case RVInstr::SLTU:
    case RVInstr::XOR:
    case RVInstr::SRL:
    case RVInstr::SRA:
    case RVInstr::OR:
    case RVInstr::AND:
    case RVInstr::MUL:
    case RVInstr::MULH:
    case RVInstr::MULHSU:
    case RVInstr::MULHU:
    case RVInstr::DIV:
    case RVInstr::DIVU:
    case RVInstr::REM:
    case RVInstr::REMU:
    case RVInstr::ADDIW:
    case RVInstr::SLLIW:
    case RVInstr::SRLIW:
    case RVInstr::SRAIW:
    case RVInstr::ADDW:
    case RVInstr::SUBW:
    case RVInstr::SLLW:
    case RVInstr::SRLW:
    case RVInstr::SRAW:
    case RVInstr::LWU:
    case RVInstr::LD:
    case RVInstr::MULW:
    case RVInstr::DIVW:
    case RVInstr::DIVUW:
    case RVInstr::REMW:
    case RVInstr::REMUW:
      return true;
    default:
      return false;
    }
  }

  static bool isLoadInstruction(RVInstr instruction) {
    switch (instruction) {
    case RVInstr::LB:
    case RVInstr::LH:
    case RVInstr::LW:
    case RVInstr::LBU:
    case RVInstr::LHU:
    case RVInstr::LWU:
    case RVInstr::LD:
    case RVInstr::FLW:
    case RVInstr::FLD:
      return true;
    default:
      return false;
    }
  }

  static bool isStoreInstruction(RVInstr instruction) {
    switch (instruction) {
    case RVInstr::SB:
    case RVInstr::SH:
    case RVInstr::SW:
    case RVInstr::SD:
    case RVInstr::FSW:
    case RVInstr::FSD:
      return true;
    default:
      return false;
    }
  }

  bool hasLoadUseHazard() const {
    const unsigned exidx = ex_reg_wr_idx.uValue();
    const unsigned idx1 = id_reg1_idx.uValue();
    const unsigned idx2 = id_reg2_idx.uValue();
    const bool mrd =
        ex_do_mem_read_en.uValue() && ex_do_reg_write_en.uValue();

    return (exidx == idx1 || exidx == idx2) && mrd;
  }

  static bool usesFPReg1(RVInstr opcode) {
    return Control::isFALUInstr(opcode);
  }

  static bool usesFPReg2(RVInstr opcode) {
    switch (opcode) {
    case RVInstr::FSQRT:
    case RVInstr::FSQRTD:
    case RVInstr::FCVTSD:
    case RVInstr::FCVTDS:
      return false;
    case RVInstr::FSW:
    case RVInstr::FSD:
      return true;
    default:
      return Control::isFALUInstr(opcode);
    }
  }

  bool hasFPLoadUseHazard() const {
    const auto opcode = id_opcode.eValue<RVInstr>();
    const unsigned exidx = ex_reg_wr_idx.uValue();
    const bool mrd =
        ex_do_mem_read_en.uValue() && ex_do_fp_write_en.uValue();

    const bool reg1Hazard =
        usesFPReg1(opcode) && exidx == id_reg1_idx.uValue();
    const bool reg2Hazard =
        usesFPReg2(opcode) && exidx == id_reg2_idx.uValue();
    return mrd && (reg1Hazard || reg2Hazard);
  }

  bool hasEcallHazard() const {
    const bool ecall = opcode.eValue<RVInstr>() == RVInstr::ECALL;
    if (!ecall)
      return false;

    const bool classicHazard =
        mem_do_reg_write.uValue() || wb_do_reg_write.uValue();
    if (!fpEnabled)
      return classicHazard;

    return classicHazard || mem_do_fp_write.uValue() ||
           wb_do_fp_write.uValue();
  }

  bool hasIncomingEcallHazard() const {
    if (!fpEnabled || !id_valid.uValue() ||
        id_opcode.eValue<RVInstr>() != RVInstr::ECALL)
      return false;

    return state->hasPendingInstructionsAfterNextClock();
  }
};

inline void HazardUnitState::save() {
  const uint64_t nextCycle =
      static_cast<uint64_t>(getDesign()->getCycleCount() + 1);
  if (!trackingEnabled) {
    currentCycle = nextCycle;
    return;
  }

  saveToStack();

  // La Hazard Unit conoce que el salto emitido en el flanco anterior ocupa
  // ahora MEM, por lo que un cero es una resolucion valida (no tomado), no la
  // ausencia de una notificacion.
  if (speculativeControlFlow && branchAwaitingResolution) {
    resolveSpeculation(owner->branchTakenFromMEM.uValue());
    branchAwaitingResolution = false;
  }

  // La decision se calcula sobre el estado anterior al flanco. Primero se
  // desplazan/liberan las FU y despues se confirma la instruccion que ID
  // realmente ha aceptado en ese mismo flanco.
  const IncomingInstruction incoming = owner->incomingFromID();
  const Decision decision = analyze(incoming);
  const bool accepted = owner->extendedInstructionAccepted();

  const bool emittedControlFlow =
      prioritizeControlFlow && controlFlowEmitsAt(nextCycle);

  if (emittedControlFlow)
    prioritizeControlFlowAt(nextCycle);

  updateFunctionalUnits(nextCycle);
  updateRegisterStatuses(nextCycle);
  if (accepted)
    store(incoming, decision);

  if (speculativeControlFlow && emittedControlFlow)
    branchAwaitingResolution = true;

  currentCycle = nextCycle;
}

} // namespace core
} // namespace vsrtl
