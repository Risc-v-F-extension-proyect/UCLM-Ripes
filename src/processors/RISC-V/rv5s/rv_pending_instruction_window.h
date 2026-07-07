#pragma once
#include "processors/RISC-V/rv_control.h"
#include "VSRTL/core/vsrtl_register.h"
#include "processors/RISC-V/riscv.h"

#include <array>
#include <deque>
#include <limits>
#include <optional>

namespace vsrtl {
namespace core {
using namespace Ripes;

//unsigned XLEN indica el ancho de bits del contenido de nuestros registros que puede ser 16, 32 o 64 y posibkemente 128 en el futuro.
template <unsigned XLEN, unsigned WINDOW_SIZE = 80>
//hereda de ClockedComponent puesto que nos interesa detectar el clock de cada nuevo ciclo
class PendingInstructionWindowState : public ClockedComponent {
public:
  struct Entry {

    bool valid = false;
    bool stalled = false;
    bool completed = false;
    bool waiting_falu_result = false;
    bool fu_slot_reserved = false;
    unsigned remaining = 0;

    RVInstr opcode = RVInstr::NOP;
    RegWrSrc reg_wr_src_ctrl = RegWrSrc::ALURES;
    MemOp mem_op = MemOp::NOP;
    DataMemWrSrc data_mem_wr_src_ctrl = DataMemWrSrc::REG2;

    VSRTL_VT_U pc = 0;
    VSRTL_VT_U pc4 = 0;
    VSRTL_VT_U r2 = 0;
    VSRTL_VT_U f_r2 = 0;
    VSRTL_VT_U alures = 0;
    VSRTL_VT_U falures = 0;
    VSRTL_VT_U instr_tag = 0;
    VSRTL_VT_U wr_reg_idx = 0;

    bool reg_do_write = false;
    bool mem_do_write = false;
    bool mem_do_read = false;
    bool fp_reg_do_write = false;

  };
  struct FunctionalUnitInfo{
    unsigned latency;
    bool pipelined;
    unsigned freeSlots;
  };
  struct StateSnapshot {
    std::array<Entry, WINDOW_SIZE> entries;
    std::array<FunctionalUnitInfo, 4> functionalUnits;
    VSRTL_VT_U lastInputTag = 0;
  };

  PendingInstructionWindowState(const std::string &name, SimComponent *parent)
      : ClockedComponent(name, parent) {
    setDescription("Pending instruction window state");
  }

  const std::array<Entry, WINDOW_SIZE> &entries() const { return m_entries; }
  VSRTL_VT_U lastInputTag() const { return m_lastInputTag; }

  void setLatencies(unsigned addSub, unsigned mul, unsigned div) {
    fpAdd_latency = addSub;
    fpMul_latency = mul;
    fpDiv_latency = div;
    resetFunctionalUnits();
  }

  bool containsTag(VSRTL_VT_U tag) const {
    if (tag == 0) {
      return false;
    }
    for (const auto &entry : m_entries) {
      if (entry.valid && entry.instr_tag == tag) {
        return true;
      }
    }
    return false;
  }

  void save() override {
    //almacenamiento del estado actual de la ventana del PIWS antes de modificarlo con nuevas señales
    saveToStack();

    //si la señal input de limpiar esta activada vaciamos el contenido
    //de cada entry de private std::array<Entry, WINDOW_SIZE> m_entries; dentro de PIWS
    const bool inputAlreadyStored = containsTag(instr_tag_in.uValue());
    if (input_valid_in.uValue() && instr_tag_in.uValue() != 0 &&
        instr_tag_in.uValue() != m_lastInputTag) {
      m_lastInputTag = instr_tag_in.uValue();
    }

    //si hemos emitido a EX/MEM señales entonces limpiamos la entry que almacenó
    //las señales mandadas a EX/MEM al igual que eliberar el stage ocupado
    const auto emittedTag = emitted_tag_in.uValue();
    if (emittedTag != 0) {
      for (auto &entry : m_entries) {
        if (entry.valid && entry.instr_tag == emittedTag) {
          //si recibimos el tag de una instruccion almacenada eso es que ya poseemos el resultado
          //calculado y ya podemos emitir la instruccion al exterior y dejar la entrada ocupada libre
          //entry = Entry{};
          if (entry.fu_slot_reserved) {//#repasar
            updateFUSlots(entry.opcode, true);

          }
          entry = Entry{};
          break;
        }
      }
    }

    //si FPAlu emitio datos, actulaizamos las señales de la entrada con mismo tag indicado por
    //falu_result_tag_in.uValue() != 0
    if (falu_result_valid_in.uValue() && falu_result_tag_in.uValue() != 0) {
      for (auto &entry : m_entries) {
        if (entry.valid && entry.instr_tag == falu_result_tag_in.uValue()) {
          entry.falures = falu_result_in.uValue();
          entry.remaining = 0;
          entry.waiting_falu_result = false;
          entry.completed = true;
          if (entry.fu_slot_reserved) {
            updateFUSlots(entry.opcode, true);
            entry.fu_slot_reserved = false;
          }
          break;
        }
      }
    }

    for (auto &entry : m_entries) {
      //si no es una entrada ocupada o incompleta la saltamos
      if (!entry.valid || entry.completed || entry.waiting_falu_result) {
        continue;
      }

      //si le queda algun ciclo mas entonces disminuimos el contador de ciclos pendientes
      if (entry.remaining > 0) {
        --entry.remaining;
      }
      //cuando le queda un ciclo en realidad ya no le queda ninguno puesto que dicho ciclo ya lo esta
      //completando el ciclo en el cual nos encontramos actualmente por lo que no debe esperar más.
      if (entry.remaining == 0) {
        //la instruccion la marcamos como completada al finalizar los ciclos de espera
        entry.completed = true;
      }
    }


    //si la señal de almacenamiento esta activada al igual que la llegada de un
    //identificador unico entonces procedemos a almacenar las señales entrantes
    //en la primera entrada libre de la ventana que podamos
    if (store_input_in.uValue() && instr_tag_in.uValue() != 0 &&
        !inputAlreadyStored) {
      for (auto &entry : m_entries) {
        if (!entry.valid) {
          //dentro de &entry almacenamos los inputs
          capture(entry);
          updateFUSlots(entry.opcode, false);
          break;
        }
      }
    }
  }

  //reset hace una limpieza total de todas las entrada existente dentro de PIWS
  void reset() override {
    //vaciamos la ventana que usamos
    clearEntries();
    resetFunctionalUnits();
    m_lastInputTag = 0;
    //vaciamos el historial de ventanas
    m_reverseStack.clear();
  }

  void reverse() override {
    //si el historial no esta vacio copianos el estado de la ventana en el ciclo anterior
    //tras actualizar la ventana con los datos del ciclo anterior, los datos eliminados del
    //historial para que ahora apunte al estado del circuito hace 2 ciclos
    if (!m_reverseStack.empty()) {
      //copiamos estado de la ventana en el ciclo anterior
      m_entries = m_reverseStack.front().entries;
      functionalUnits = m_reverseStack.front().functionalUnits;
      m_lastInputTag = m_reverseStack.front().lastInputTag;
      //tras copiar el estado de hace un ciclo hacemos que el historial
      //apunte al estado de hace 2 ciclos
      m_reverseStack.pop_front();
    }
  }

  //no usamos momentaneamente el forzado de valor
  void forceValue(VSRTL_VT_U, VSRTL_VT_U) override {}

  //funcion que evita que el historial posea un tamaño ilimitado
  void reverseStackSizeChanged() override {
    //si el historial supera el numero maximo de ciclos a recordar X,
    //nos quedamos con los ultimos X ciclos guardados del historial descartanto
    //los primeros ciclos no considerados
    if (reverseStackSize() < m_reverseStack.size()) {
      m_reverseStack.resize(reverseStackSize());
    }
  }
  bool structuralRiscTrigger(RVInstr incomingOpcode, RVInstr emittedOpcode,
                             bool emittedReleasesUnit, RVInstr pendingOpcode,
                             bool pendingWillStore) const {
    if (incomingOpcode == RVInstr::NOP) {
      return false;
    }
    const unsigned incomingIndex = functionalUnitIndex(incomingOpcode);
    const unsigned pendingIndex = functionalUnitIndex(pendingOpcode);
    const unsigned emittedIndex = functionalUnitIndex(emittedOpcode);
    const auto &unit = functionalUnits[incomingIndex];

    int availableSlots = static_cast<int>(unit.freeSlots);
    if (emittedReleasesUnit && emittedOpcode != RVInstr::NOP &&
        incomingIndex == emittedIndex) {
      ++availableSlots;
    }
    if (pendingWillStore && pendingOpcode != RVInstr::NOP &&
        incomingIndex == pendingIndex) {
      --availableSlots;
    }
    return availableSlots <= 0;
  }

  static RVInstr safeOpcode(VSRTL_VT_U opcToStore) {
    const auto maxOpcode = static_cast<VSRTL_VT_U>(RVInstr::REMUW);
    return opcToStore <= maxOpcode ? static_cast<RVInstr>(opcToStore) : RVInstr::NOP;
  }
  //señales inputports y outputports del componente PIWS
  //indica si se deben almacenar las entradas dentro de la ventana state
  INPUTPORT(store_input_in, 1);
  //identificador de la instrucción a almacenar en caso de haber una
  INPUTPORT(emitted_tag_in, XLEN);
  //la latencia de dicha instrucción
  INPUTPORT(latency_in, 4);
  //temporal signal
  INPUTPORT(stalled_in, 1);
  INPUTPORT(input_valid_in, 1);

  INPUTPORT(pc_in, XLEN);
  INPUTPORT(pc4_in, XLEN);
  INPUTPORT(r2_in, XLEN);
  INPUTPORT(f_r2_in, XLEN);
  INPUTPORT(alures_in, XLEN);
  INPUTPORT(falures_in, XLEN);
  INPUTPORT(falu_result_in, XLEN);
  INPUTPORT(falu_result_tag_in, XLEN);
  INPUTPORT(falu_result_valid_in, 1);
  INPUTPORT(reg_wr_src_ctrl_in, enumBitWidth<RegWrSrc>());
  INPUTPORT(wr_reg_idx_in, c_RVRegsBits);
  INPUTPORT(reg_do_write_in, 1);
  INPUTPORT(mem_do_write_in, 1);
  INPUTPORT(mem_do_read_in, 1);
  INPUTPORT(mem_op_in, enumBitWidth<MemOp>());
  INPUTPORT(fp_reg_do_write_in, 1);
  INPUTPORT(data_mem_wr_src_ctrl_in, enumBitWidth<DataMemWrSrc>());

  //INPUTPORT_ENUM(opcode_in, RVInstr);
  INPUTPORT(opcode_in, enumBitWidth<RVInstr>());
  INPUTPORT(instr_tag_in, XLEN);

private:
  unsigned fuAlu_latency = 1;
  unsigned fpAdd_latency = 4;
  unsigned fpMul_latency = 7;
  unsigned fpDiv_latency = 25;
  bool fpAdd_pipelined = true;
  bool fpMul_pipelined = true;
  bool fpDiv_pipelined = false;

  //funcion que vacia cada entrada de m_entries
  void clearEntries() {
    for (auto &entry : m_entries) {
      //entry = Entry{};
      entry.valid = false;
    }
  }

  void resetFunctionalUnits() {
    functionalUnits = {{
        {fuAlu_latency, false, fuAlu_latency},
        {fpAdd_latency, fpAdd_pipelined, fpAdd_latency},
        {fpMul_latency, fpMul_pipelined, fpMul_latency},
        {fpDiv_latency, fpDiv_pipelined, 1}
    }};
  }


  //almacenamiento del estado acrtual de la ventana
  //del ciclo anterior antes de modificarla en el nuevo ciclo
  void saveToStack() {
    if (canReverse()) {
      m_reverseStack.push_front({m_entries, functionalUnits, m_lastInputTag});
    }
  }
  unsigned functionalUnitIndex(RVInstr opcode) const {
    switch (opcode) {
    case RVInstr::FADD:
    case RVInstr::FSUB:
    case RVInstr::FMIN:
    case RVInstr::FMAX:
    case RVInstr::FSGNJ:
    case RVInstr::FSGNJN:
    case RVInstr::FSGNJX:
      return 1;
      //return fpAdd_latency;
    case RVInstr::FMUL:
      return 2;
      //return fpMul_latency;
    case RVInstr::FDIV:
    case RVInstr::FSQRT:
      return 3;
      //return fpDiv_latency;
    default:
      return 0;
      //return fuAlu_latency;
    }
  }

  void updateFUSlots(RVInstr opcode, bool increaseSpace){
    //incrementSpace indica si eliberamos espacio u ocupamos slots de una unidad funcional particular
    //si es verdadero eso es que eliberamos un slot, en caso contrario ocupamos un slot
    const unsigned index = functionalUnitIndex(opcode);
    if (increaseSpace) {
      const unsigned maxSlots = functionalUnits[index].pipelined
                                    ? functionalUnits[index].latency
                                    : 1;
      if (functionalUnits[index].freeSlots < maxSlots) {
        ++functionalUnits[index].freeSlots;
      }
    } else if (functionalUnits[index].freeSlots > 0) {
      --functionalUnits[index].freeSlots;
    }
  }

  //funcion almacenadora de las señales de entrada tras inticarle la referencia de una
  //entrada libre
  void capture(Entry &entry) {

    //miramos que latencia que posee la instruccion entrante, default: FADD=4, FMUL=7, FDIV=25
    const auto latency = static_cast<unsigned>(latency_in.uValue());
    const auto remaining = latency <= 2 ? 0 : latency - 2;

    //la entrada se marca como valida == ocupada cuando se asigna
    entry.valid = true;
    entry.fu_slot_reserved = true;
    entry.stalled = stalled_in.uValue() == 1;
    entry.waiting_falu_result = false;
    //esta completada unicamente si posee una latencia 0, lo que es lo mismo en este caso que
    //se completa en el mismo ciclo desde que sale de ID/EX y entra la PIW
    entry.completed = remaining == 0;

    //si la latencia es nula ya esta completada, si por ejemplo es de 4 marcamos que
    //quedan 3 ciclos esperar para completarse.¿Por qué 3 si su latencia es de 4?
    //debe esperar 3 ciclos puesto que 1 ciclo de los 4 ya lo esta consumiendo ahora mismo
    entry.remaining = remaining;

    //las demas señales son los inputs que se almacenan en la &entry libre
    entry.pc = pc_in.uValue();
    entry.pc4 = pc4_in.uValue();
    entry.r2 = r2_in.uValue();
    entry.f_r2 = f_r2_in.uValue();
    entry.alures = alures_in.uValue();
    entry.falures = falures_in.uValue();

    entry.reg_wr_src_ctrl = static_cast<RegWrSrc>(reg_wr_src_ctrl_in.uValue());
    entry.wr_reg_idx = wr_reg_idx_in.uValue();
    entry.reg_do_write = reg_do_write_in.uValue() != 0;
    entry.mem_do_write = mem_do_write_in.uValue() != 0;
    entry.mem_do_read = mem_do_read_in.uValue() != 0;
    entry.mem_op = static_cast<MemOp>(mem_op_in.uValue());
    entry.fp_reg_do_write = fp_reg_do_write_in.uValue() != 0;
    entry.data_mem_wr_src_ctrl =
        static_cast<DataMemWrSrc>(data_mem_wr_src_ctrl_in.uValue());

    entry.opcode = safeOpcode(opcode_in.uValue());
    entry.instr_tag = instr_tag_in.uValue();

    if (Control::isFALUInstr(entry.opcode)) {
      entry.completed = false;
      entry.remaining = 0;
      entry.waiting_falu_result = true;
    }
  }

  //es el tamaño de PIWS que por defecto poseera 8 entradas
  std::array<Entry, WINDOW_SIZE> m_entries = {};
  //es una cola de ventanas para el tema del historial de ciclos
  std::deque<StateSnapshot> m_reverseStack;
  VSRTL_VT_U m_lastInputTag = 0;

  //estructura que usaremos para informarnos sobre disponibilidad de cada unidad funcionl
  //ante una instrucción pretendienta desde ID
  std::array<FunctionalUnitInfo, 4> functionalUnits = {{
      {1, false, 1},
      {fpAdd_latency, fpAdd_pipelined, fpAdd_latency},
      {fpMul_latency, fpMul_pipelined, fpMul_latency},
      {fpDiv_latency, fpDiv_pipelined, 1}
  }};
};

//XLEN indica el ancho de bits del contenido de los registros usados
template <unsigned XLEN>
class PendingInstructionWindow : public Component {
  using State = PendingInstructionWindowState<XLEN>;
  using Entry = typename State::Entry;

public:
  PendingInstructionWindow(const std::string &name, SimComponent *parent)
      : Component(name, parent) {
    setDescription("Pending instruction window");

    //asignamos cada señal de entrada del componente PIW principal a las entradas del PIWS

    /////
    /////
    /////
    /////
    //las salidas de los ouputports privados del PIW conectadas a las entradas del state PIWS
    store_input >> state->store_input_in;
    emitted_tag >> state->emitted_tag_in;
    input_latency >> state->latency_in;
    /////
    /////
    /////
    /////

    //las señales entrandes desde ID/EX
    valid_in >> state->input_valid_in;
    stalled_in >> state->stalled_in;
    pc_in >> state->pc_in;
    pc4_in >> state->pc4_in;
    r2_in >> state->r2_in;
    f_r2_in >> state->f_r2_in;
    alures_in >> state->alures_in;
    falures_in >> state->falures_in;
    falu_result_in >> state->falu_result_in;
    falu_result_tag_in >> state->falu_result_tag_in;
    falu_result_valid_in >> state->falu_result_valid_in;
    reg_wr_src_ctrl_in >> state->reg_wr_src_ctrl_in;
    wr_reg_idx_in >> state->wr_reg_idx_in;
    reg_do_write_in >> state->reg_do_write_in;
    mem_do_write_in >> state->mem_do_write_in;
    mem_do_read_in >> state->mem_do_read_in;
    mem_op_in >> state->mem_op_in;
    fp_reg_do_write_in >> state->fp_reg_do_write_in;
    data_mem_wr_src_ctrl_in >> state->data_mem_wr_src_ctrl_in;

    opcode_in >> state->opcode_in;
    instr_tag_in >> state->instr_tag_in;



    //señales procesadas internamente
    //indica la latencia de la instrucción que se almacenará
    input_latency << [this] { return latencyForInput(); };
    //retorno de si la instrucción entrante se almacena o fue mandada por bypass
    store_input << [this] { return shouldStoreInput() ? 1 : 0; };
    //si emittedtag() manda 0 eso quiere decir que no hay instrucción qué almacenar,
    //en caso contrario mandará el tag de la instrucción a almacenar
    emitted_tag << [this] { return emittedTag(); };

    stalled_out << [this] {
      if (outputFromInput()) {
        return stalled_in.uValue();
      }

      if (selected()->valid) {
        return static_cast<vsrtl::VSRTL_VT_U>(0);
      }

      return stalled_in.uValue();
    };
    //esta claro que si no sale ninguna instrucción hacia EX/MEM la sañal valid saliente no puede ser 1
    valid_out << [this] { return outputValid() ? 1 : 0; };
    falu_release_out << [this] {
      return faluEmission() ? VT_U(1) : VT_U(0);
    };

    //señales de la instruccion
    pc_out << [this] { return outputFromInput() ? pc_in.uValue() : selected()->pc; };
    pc4_out << [this] { return outputFromInput() ? pc4_in.uValue() : selected()->pc4; };
    r2_out << [this] { return outputFromInput() ? r2_in.uValue() : selected()->r2; };
    f_r2_out << [this] { return outputFromInput() ? f_r2_in.uValue() : selected()->f_r2; };
    alures_out << [this] { return outputFromInput() ? alures_in.uValue() : selected()->alures; };
    falures_out << [this] {
      if (outputFromInput()) {
        return falures_in.uValue();
      }
      return selectedUsesFaluResult() ? falu_result_in.uValue()
                                      : selected()->falures;
    };

    reg_wr_src_ctrl_out << [this] {
      return outputFromInput() ? reg_wr_src_ctrl_in.uValue()
                               : static_cast<VSRTL_VT_U>(selected()->reg_wr_src_ctrl);};
    wr_reg_idx_out << [this] {
      return outputFromInput() ? wr_reg_idx_in.uValue() : selected()->wr_reg_idx;
    };
    reg_do_write_out << [this] {
      return outputFromInput() ? reg_do_write_in.uValue()
                               : static_cast<VSRTL_VT_U>(selected()->reg_do_write);
    };
    mem_do_write_out << [this] {
      return outputFromInput() ? mem_do_write_in.uValue()
                               : static_cast<VSRTL_VT_U>(selected()->mem_do_write);
    };
    mem_do_read_out << [this] {
      return outputFromInput() ? mem_do_read_in.uValue()
                               : static_cast<VSRTL_VT_U>(selected()->mem_do_read);
    };
    mem_op_out << [this] {
      return outputFromInput() ? mem_op_in.uValue()
                               : static_cast<VSRTL_VT_U>(selected()->mem_op);
    };
    fp_reg_do_write_out << [this] {
      return outputFromInput() ? fp_reg_do_write_in.uValue()
                               : static_cast<VSRTL_VT_U>(selected()->fp_reg_do_write);
    };
    data_mem_wr_src_ctrl_out << [this] {
      return outputFromInput() ? data_mem_wr_src_ctrl_in.uValue()
                               : static_cast<VSRTL_VT_U>(selected()->data_mem_wr_src_ctrl);
    };

    instr_tag_out << [this] {
      return outputFromInput() ? instr_tag_in.uValue() : selected()->instr_tag;
    };
    windowReady << [this]() -> VSRTL_VT_U {
      if (ecallInEx()) {
        return VT_U(1);
      }
      const auto opcode = State::safeOpcode(id_opcode_in.uValue());
      const bool mustStall =
          rawQuery(id_reg1_idx_in.uValue(), id_reg2_idx_in.uValue(), opcode) ||
          structuralQuery(opcode) ||
          ecallWindowHazard(opcode);
      return mustStall ? VT_U(0) : VT_U(1);
    };
    
    
  }

  std::optional<VSRTL_VT_U> pcForTag(VSRTL_VT_U tag) const {
    for (const auto &entry : state->entries()) {
      if (entry.valid && entry.instr_tag == tag) {
        return entry.pc;
      }
    }
    return std::nullopt;
  }

  void setLatencies(unsigned addSub, unsigned mul, unsigned div) {
    state->setLatencies(addSub, mul, div);
  }

  // Register control
  INPUTPORT(stalled_in, 1);
  INPUTPORT(valid_in, 1);

  OUTPUTPORT(stalled_out, 1);
  OUTPUTPORT(valid_out, 1);

  // Data
  INPUTPORT(pc_in, XLEN);
  INPUTPORT(pc4_in, XLEN);
  INPUTPORT(r2_in, XLEN);
  INPUTPORT(f_r2_in, XLEN);
  INPUTPORT(alures_in, XLEN);
  INPUTPORT(falures_in, XLEN);
  INPUTPORT(falu_result_in, XLEN);
  INPUTPORT(falu_result_tag_in, XLEN);
  INPUTPORT(falu_result_valid_in, 1);

  OUTPUTPORT(pc_out, XLEN);
  OUTPUTPORT(pc4_out, XLEN);
  OUTPUTPORT(r2_out, XLEN);
  OUTPUTPORT(f_r2_out, XLEN);
  OUTPUTPORT(alures_out, XLEN);
  OUTPUTPORT(falures_out, XLEN);
  OUTPUTPORT(falu_release_out, 1);

  // Control
  INPUTPORT(reg_wr_src_ctrl_in, enumBitWidth<RegWrSrc>());
  INPUTPORT(wr_reg_idx_in, c_RVRegsBits);
  INPUTPORT(reg_do_write_in, 1);
  INPUTPORT(mem_do_write_in, 1);
  INPUTPORT(mem_do_read_in, 1);
  INPUTPORT(mem_op_in, enumBitWidth<MemOp>());
  INPUTPORT(fp_reg_do_write_in, 1);
  INPUTPORT(data_mem_wr_src_ctrl_in, enumBitWidth<DataMemWrSrc>());
  INPUTPORT_ENUM(falu_ctrl_in, FALUOp);

  OUTPUTPORT(reg_wr_src_ctrl_out, enumBitWidth<RegWrSrc>());
  OUTPUTPORT(wr_reg_idx_out, c_RVRegsBits);
  OUTPUTPORT(reg_do_write_out, 1);
  OUTPUTPORT(mem_do_write_out, 1);
  OUTPUTPORT(mem_do_read_out, 1);
  OUTPUTPORT(mem_op_out, enumBitWidth<MemOp>());
  OUTPUTPORT(fp_reg_do_write_out, 1);
  OUTPUTPORT(data_mem_wr_src_ctrl_out, enumBitWidth<DataMemWrSrc>());

  // Opcode identifies EX-local instructions which bypass window storage.
  INPUTPORT(opcode_in, enumBitWidth<RVInstr>());


  // Instruction tag identifier.
  INPUTPORT(instr_tag_in, XLEN);
  OUTPUTPORT(instr_tag_out, XLEN);

  //inputs especiales procedentes desde la unidad hazard para detectar
  //RaW respecto instrucciones pendientes en la ventana o bien estructurales por
  //no existir una entrada valida en la unidad funcional que necesita la proxima instrucción de ID
  INPUTPORT(id_reg1_idx_in, c_RVRegsBits);
  INPUTPORT(id_reg2_idx_in, c_RVRegsBits);
  INPUTPORT(id_opcode_in, enumBitWidth<RVInstr>());
  OUTPUTPORT(windowReady, 1);


private:
  //subcomponente de PIW que almacenará las instrucciones pendientes
  SUBCOMPONENT(state, State);
  //estos outports privados son exclusivos para informar al subcomponente
  //de información adicional de las señales entrantes
  OUTPUTPORT(store_input, 1);
  OUTPUTPORT(emitted_tag, XLEN);
  OUTPUTPORT(input_latency, 4);

  static bool sameDestination(const Entry &storedEntry, VSRTL_VT_U reg_do_Write, VSRTL_VT_U fp_reg_do_Write, VSRTL_VT_U rd) {
    const bool integerWriter = reg_do_Write && storedEntry.reg_do_write && rd != 0 && storedEntry.wr_reg_idx == rd;
    const bool fpWriter = fp_reg_do_Write && storedEntry.fp_reg_do_write && storedEntry.wr_reg_idx == rd;
    //WaW si coincide mismo banco destino al igual que registro dentro de dicho banco
    return integerWriter || fpWriter;
  }
  bool ecallWindowHazard(RVInstr opc) const {
    //detección de riesgo cuando en ID encontramos una instrucción ECALL y en la ventana de instrucción no estará vacia el próximo ciclo,
    //en ese caso debemos bloquear el avance de la instrucción ECALL
    if (opc == RVInstr::ECALL) {
        const auto leavingTag = emittedTag();

        for (const auto &entry : state->entries()) {
            if (entry.valid && entry.instr_tag != leavingTag) {
                return true;
            }
        }
        if (shouldStoreInput()) {
            return true;
        }
    }
    return false;
  }

  //detecta si hay riesgo RaW
  bool rawQuery(VSRTL_VT_U rs1, VSRTL_VT_U rs2, RVInstr opc) const {
    if (opc == RVInstr::NOP) {
      return false;
    }
    //comprobamos que el tipo deopracion segun el opc emplea o no el primer registro fuente
    bool useFPReg1 = Control::isFALUInstr(opc);
    //hacemos lo mismo que antes pero en este caso si es un almacenamiento fp si debemos indicar que
    //el segundo serigstro se usa aunque no sea una aritmetico lógica
    bool useFPReg2 = (opc == RVInstr::FSW) ? true : Control::isFALUInstr(opc);

    //si usa como primer registro un punto flotante y existe una instrucción incomplera
    //que emplea e mismo registro como destino avisamos de riesgo WaW
    if(useFPReg1 && prematureRead(rs1)){
      return true;
    }
    //si usa como segundo registro un punto flotante y existe una instrucción incomplera
    //que emplea e mismo registro como destino avisamos de riesgo WaW
    if(useFPReg2 && prematureRead(rs2)){
      return true;
    }
    //si no hay instrucción pendiente que escribe en el mismo banco
    //de registros aparte del mismo registro entonces avisamos que no hay riesgo WaW.
    return false;
  }

  //funcion detectora de RAW dentro de la ventana de instrucciones
  bool prematureRead(const VSRTL_VT_U regIdx) const {
    const auto leavingTag = emittedTag();
    for(const auto &entry: state->entries()){
      // Una escritura que cruza a EX/MEM este ciclo ya no pertenece al estado
      // efectivo que encontrara la instruccion actualmente situada en ID.
      if(entry.valid && entry.instr_tag != leavingTag &&
         entry.fp_reg_do_write && entry.wr_reg_idx == regIdx){
        return true;
      }
    }


    // save() todavia no ha incorporado la instruccion de ID/EX. La contamos
    // anticipadamente solo cuando permanecera en la ventana el proximo ciclo.
    if (shouldStoreInput() && fp_reg_do_write_in.uValue() != 0 &&
        wr_reg_idx_in.uValue() == regIdx) {
      return true;
    }

    return false;
  }

  bool inputValid() const {
    const auto opcode = State::safeOpcode(opcode_in.uValue());
    const bool conditionalBranch = Control::do_branch_ctrl(opcode) != 0;
    const bool jumpWithoutLink =
        Control::do_jump_ctrl(opcode) != 0 &&
        (reg_do_write_in.uValue() == 0 || wr_reg_idx_in.uValue() == 0);

    //es valido si se activa la señal de valida, un identificador
    //valido distindo de 0
    return valid_in.uValue() != 0 && instr_tag_in.uValue() != 0 &&
           opcode != RVInstr::ECALL && !conditionalBranch &&
           !jumpWithoutLink &&
           instr_tag_in.uValue() != state->lastInputTag() &&
           !inputAlreadyStored();
  }

  bool ecallInEx() const {
    return valid_in.uValue() != 0 &&
           State::safeOpcode(opcode_in.uValue()) == RVInstr::ECALL;
  }

  bool inputAlreadyStored() const {
    return state->containsTag(instr_tag_in.uValue());
  }

  //retorno de latencia segun el tipo de instrucción entrante
  VSRTL_VT_U latencyForInput() const {
    switch (static_cast<FALUOp>(falu_ctrl_in.uValue())) {
    case FALUOp::ADD:
    case FALUOp::SUB:
      return 2;
    case FALUOp::MUL:
      return 4;
    case FALUOp::DIV:
    case FALUOp::SQRT:
      return 6;
    case FALUOp::MIN:
    case FALUOp::MAX:
    case FALUOp::SGNJ:
    case FALUOp::SGNJN:
    case FALUOp::SGNJX:
      return 2;
    default:
      return 0;
    }
  }

  //comprueba si las señales input pueden generar WaW
  bool inputHasOlderWaw() const {
    if (!inputValid()) {
      return false;
    }

    //identificador de la instruccion entrante
    const auto tag = instr_tag_in.uValue();
    //señal que indica si su registro destino pertenece al banco entero
    const auto regWrite = reg_do_write_in.uValue();
    //señal que indica si su registro destino pertenece al banco en punto flotante
    const auto fpWrite = fp_reg_do_write_in.uValue();
    //señal que indica el numero del registro destino
    const auto rd = wr_reg_idx_in.uValue();


    for (const auto &entry : state->entries()) {
      //si identificamos una instruccion que posee un identificador menor y que posee
      //como destino el mismo registro del mismo banco entonces avisamos de riesgo WaW
      if (entry.valid && entry.instr_tag < tag &&
          sameDestination(entry, regWrite, fpWrite, rd)) {
        return true;
      }
    }
    return false;
  }

  //lo mismo que el metodo anterior pero en vez de
  //comparar señales entrantes se comparan entradas usadas
  bool entryHasOlderWaw(const Entry &candidate) const {
    for (const auto &entry : state->entries()) {
      if (entry.valid && entry.instr_tag < candidate.instr_tag &&
          sameDestination(entry, candidate.reg_do_write,
                          candidate.fp_reg_do_write, candidate.wr_reg_idx)) {
        return true;
      }
    }
    return false;
  }
  bool receivesFaluResult(const Entry &entry) const {
    return falu_result_valid_in.uValue() != 0 &&
           falu_result_tag_in.uValue() != 0 &&
           entry.instr_tag == falu_result_tag_in.uValue();
  }

  bool readyThisCycle(const Entry &entry) const {
    return entry.completed || receivesFaluResult(entry);
  }


  //decidimos si emitimos una instruccion almacenada o no
  const Entry *selected() const {
    //inicialmente la entrada valida es una estructura vacia
    const Entry *best = &m_emptyEntry;
    //recorremos cada entrada de la ventana de instrucciones
    for (const auto &entry : state->entries()) {
      //continuaos si la entrada no es usada, completa o bien es prematura por riesgo WaW
      if (!entry.valid || !readyThisCycle(entry) || entryHasOlderWaw(entry)) {
        continue;
      }
      //si es valida, y su tag es menor que una instrucción ya seleccionada para sacar
      //entonces consideramos la actual como mejor para sacar
      if (!best->valid || entry.instr_tag < best->instr_tag) {
        best = &entry;
      }
    }
    return best;
  }

  //relanzamos las señales entrantes por bypass sin almacenar nada si se cumple que
  //las entradas son validas, la latencia de la instrucción es 1 aparte de no generar
  //ningún riesgo WaW al no coincidir su escritura con el mismo banco y registro de alguno
  //instrucción alamcenada en la ventana de instrucciones pendientes
  bool inputReadyForBypass() const {
    const auto opcode = State::safeOpcode(opcode_in.uValue());
    return inputValid() && !Control::isFALUInstr(opcode) &&
           !inputHasOlderWaw();
    //no podemos aplicar bypass si resulta que hay resultados salientes desde la FALU
    //de una instrucción con un tag menor al de la instrucción que pretende
    //pasar directamente desde ID/EX hasta EX/MEM.
  }

  bool outputFromInput() const {
    //si no se cumple lo mencionado en el metodo anterior no podemos aplicar bypass
    if (!inputReadyForBypass()) {
      return false;
    }

    //si podemos aplicar bypass debemos comprobar si
    //existe una instrucción almacenada lista para salir
    const auto *stored = selected();
    //solo aplicamos bypass si resulta que el metodo selected efectivamente nos retorno una
    //estructura Entry con señales validas, no una estructura vacia. Para comprobar que el retorno de
    //selected() es valido se comprueba si la señal valid != 0 o lo mismo que valid == 1, si es el caso
    //se compara el tag o identificador unico de cada instrucción. El tag es un valor numerico que indica el ciclo
    //en el cual aparecio la instruccion en el circuito en la etapa IF. Si una instrucción X posee menor tag que otra Y
    //significa que X aparecio antes y por lo tanto es mas vieja y por consiguiente es mas prioritaria para sacar de EX
    return !stored->valid || instr_tag_in.uValue() < stored->instr_tag;
  }

  bool selectedUsesFaluResult() const {
    if (outputFromInput()) {
      return false;
    }
    const auto *stored = selected();
    return stored->valid && receivesFaluResult(*stored);
  }

  // La FALU puede liberar su entrada tanto si el resultado cruza directamente
  // a EX/MEM como si la ventana debe conservarlo para un ciclo posterior.
  bool faluEmission() const {
    if (falu_result_valid_in.uValue() == 0 ||
        falu_result_tag_in.uValue() == 0) {
      return false;
    }
    for (const auto &entry : state->entries()) {
      if (entry.valid && entry.waiting_falu_result &&
          entry.instr_tag == falu_result_tag_in.uValue()) {
        return true;
      }
    }
    return false;
  }

  //estamos seguros que podemos lanzar al exterior si podemos
  //realizar bypass o bien disponer de una instrucción almacenada lista
  bool outputValid() const {
    if (ecallInEx()) {
      return false;
    }
    return outputFromInput() || selected()->valid;
  }

  //debemos almacenar los inputs si son salidos y no se puede aplicar bypass
  bool shouldStoreInput() const {
    return inputValid() && !outputFromInput();
  }

  //extracción del tag de la instrucción emitida
  VSRTL_VT_U emittedTag() const {
    if (ecallInEx()) {
      return 0;
    }
    //si resulta que aplicamos bypass retornamos 0 para indicar a la ventana
    //que las señales entrantes no se almacenarán al pasar por bypass
    if (outputFromInput()) {
      return 0;
    }

    //si no aplicamos bypass miramos que instrucción almacenada podemos exmitir
    const auto *stored = selected();
    //si hay instrucción almacenada lista para emitir mandamos su tag
    //en caso contrario mandamos 0 al no existir instrucción disponible a emitir
    return stored->valid ? stored->instr_tag : 0;
  }

  bool structuralQuery(RVInstr idOpcode) const {
    const bool windowEmission = !outputFromInput() && selected()->valid;
    const bool pendingWillStore = shouldStoreInput();
    const auto pendingOpcode =
        pendingWillStore ? State::safeOpcode(opcode_in.uValue()) : RVInstr::NOP;

    RVInstr releasedOpcode = RVInstr::NOP;
    bool releasesUnit = false;
    if (faluEmission()) {
      for (const auto &entry : state->entries()) {
        if (entry.valid && entry.fu_slot_reserved &&
            entry.instr_tag == falu_result_tag_in.uValue()) {
          releasedOpcode = entry.opcode;
          releasesUnit = true;
          break;
        }
      }
    } else if (windowEmission && selected()->fu_slot_reserved) {
      releasedOpcode = selected()->opcode;
      releasesUnit = true;
    }

    return state->structuralRiscTrigger(
        idOpcode, releasedOpcode, releasesUnit, pendingOpcode, pendingWillStore);
  }

  //estructura vacia por defecto que se usa si no se
  //encuentra una instrucción lista para emitir
  Entry m_emptyEntry = {};
};

} // namespace core
} // namespace vsrtl
