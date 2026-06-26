#pragma once
#include "processors/RISC-V/rv_control.h"
#include "VSRTL/core/vsrtl_register.h"
#include "processors/RISC-V/riscv.h"

#include <array>
#include <deque>
#include <limits>

namespace vsrtl {
namespace core {
using namespace Ripes;

//unsigned XLEN indica el ancho de bits del contenido de nuestros registros que puede ser 16, 32 o 64 y posibkemente 128 en el futuro.
template <unsigned XLEN, unsigned WINDOW_SIZE = 8>
//hereda de ClockedComponent puesto que nos interesa detectar el clock de cada nuevo ciclo
class PendingInstructionWindowState : public ClockedComponent {
public:
  struct Entry {

    bool valid = false;
    bool stalled = false;
    bool completed = false;
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

    bool branch_res = false;
    bool do_br = false;
    bool do_jmp = false;
    bool branch_taken = false;
    bool controlflow = false;
    bool stallEcallHandling = false;

  };
  struct FunctionalUnitInfo{
    unsigned latency;
    bool pipelined;
    unsigned freeSlots;
  };
  struct StateSnapshot {
    std::array<Entry, WINDOW_SIZE> entries;
    std::array<FunctionalUnitInfo, 4> functionalUnits;
  };

  PendingInstructionWindowState(const std::string &name, SimComponent *parent)
      : ClockedComponent(name, parent) {
    setDescription("Pending instruction window state");
  }

  const std::array<Entry, WINDOW_SIZE> &entries() const { return m_entries; }

  void save() override {
    //almacenamiento del estado actual de la ventana del PIWS antes de modificarlo con nuevas señales
    saveToStack();

    //si la señal input de limpiar esta activada vaciamos el contenido
    //de cada entry de private std::array<Entry, WINDOW_SIZE> m_entries; dentro de PIWS
    if (clear_in.uValue()) {
      clearEntries();
      resetFunctionalUnits();
      return;
    }

    //miramos tag o identificador unico de una instruccion si tag != 0
    const auto emittedTag = emitted_tag_in.uValue();
    if (emittedTag != 0) {
      for (auto &entry : m_entries) {
        if (entry.valid && entry.instr_tag == emittedTag) {
          //si recibimos el tag de una instruccion almacenada eso es que ya poseemos el resultado
          //calculado y ya podemos emitir la instruccion al exterior y dejar la entrada ocupada libre
          //entry = Entry{};
          entry.valid = false;
          updateFUSlots(entry.opcode, true);
          break;
        }
      }
    }

    for (auto &entry : m_entries) {
      //si no es una entrada ocupada o incompleta la saltamos
      if (!entry.valid || entry.completed) {
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
    if (store_input_in.uValue() && instr_tag_in.uValue() != 0) {
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
  bool structuralRiscTrigger(RVInstr incomingOpcode, RVInstr emittedOpcode) const {
    if (incomingOpcode == RVInstr::NOP) {
      return false;
    }
    const unsigned incomingIndex = functionalUnitIndex(incomingOpcode);
    const unsigned emittedIndex = functionalUnitIndex(emittedOpcode);
    const auto &unit = functionalUnits[incomingIndex];

    int availableSlots = static_cast<int>(unit.freeSlots);
    if (incomingIndex == emittedIndex) {
      ++availableSlots;
    }

    if (unit.pipelined) {
      return availableSlots <= 0;
    }

    return availableSlots < static_cast<int>(unit.latency);
  }

  static RVInstr safeOpcode(VSRTL_VT_U opcToStore) {
    const auto maxOpcode = static_cast<VSRTL_VT_U>(RVInstr::REMUW);
    return opcToStore <= maxOpcode ? static_cast<RVInstr>(opcToStore) : RVInstr::NOP;
  }
  //señales inputports y outputports del componente PIWS
  //señal que avisa de limpiar entradas de la ventana al igual que su historial
  INPUTPORT(clear_in, 1);
  //indica si se deben almacenar las entradas dentro de la ventana state
  INPUTPORT(store_input_in, 1);
  //identificador de la instrucción a almacenar en caso de haber una
  INPUTPORT(emitted_tag_in, XLEN);
  //la latencia de dicha instrucción
  INPUTPORT(latency_in, 4);
  //temporal signal
  INPUTPORT(stalled_in, 1);

  INPUTPORT(pc_in, XLEN);
  INPUTPORT(pc4_in, XLEN);
  INPUTPORT(r2_in, XLEN);
  INPUTPORT(f_r2_in, XLEN);
  INPUTPORT(alures_in, XLEN);
  INPUTPORT(falures_in, XLEN);

  INPUTPORT(reg_wr_src_ctrl_in, enumBitWidth<RegWrSrc>());
  INPUTPORT(wr_reg_idx_in, c_RVRegsBits);
  INPUTPORT(reg_do_write_in, 1);
  INPUTPORT(mem_do_write_in, 1);
  INPUTPORT(mem_do_read_in, 1);
  INPUTPORT(mem_op_in, enumBitWidth<MemOp>());
  INPUTPORT(fp_reg_do_write_in, 1);
  INPUTPORT(data_mem_wr_src_ctrl_in, enumBitWidth<DataMemWrSrc>());

  INPUTPORT(branch_res_in, 1);
  INPUTPORT(do_br_in, 1);
  INPUTPORT(do_jmp_in, 1);
  INPUTPORT(branch_taken_in, 1);
  INPUTPORT(controlflow_in, 1);

  INPUTPORT(stallEcallHandling_in, 1);
  //INPUTPORT_ENUM(opcode_in, RVInstr);
  INPUTPORT(opcode_in, enumBitWidth<RVInstr>());
  INPUTPORT(instr_tag_in, XLEN);

private:
  unsigned fuAlu_latency = 1;
  unsigned fpAdd_latency = 2;
  unsigned fpMul_latency = 4;
  unsigned fpDiv_latency = 6;
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
        {fpDiv_latency, fpDiv_pipelined, fpDiv_latency}
    }};
  }


  //almacenamiento del estado acrtual de la ventana
  //del ciclo anterior antes de modificarla en el nuevo ciclo
  void saveToStack() {
    if (canReverse()) {
      m_reverseStack.push_front({m_entries, functionalUnits});
    }
  }
  unsigned functionalUnitIndex(RVInstr opcode) const {
    switch (opcode) {
    case RVInstr::FADD:
    case RVInstr::FSUB:
      return 1;
      //return fpAdd_latency;
    case RVInstr::FMUL:
      return 2;
      //return fpMul_latency;
    case RVInstr::FDIV:
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

    //miramos que latencia posee la instruccion entrante, default: FADD=4, FMUL=7, FDIV=25
    const auto latency = static_cast<unsigned>(latency_in.uValue());
    const auto remaining = latency <= 2 ? 0 : latency - 2;

    //la entrada se marca como valida == ocupada cuando se asigna
    entry.valid = true;
    entry.stalled = stalled_in.uValue() == 1;
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

    entry.branch_res = branch_res_in.uValue() != 0;
    entry.do_br = do_br_in.uValue() != 0;
    entry.do_jmp = do_jmp_in.uValue() != 0;
    entry.branch_taken = branch_taken_in.uValue() != 0;
    entry.controlflow = controlflow_in.uValue() != 0;

    entry.stallEcallHandling = stallEcallHandling_in.uValue() != 0;
    entry.opcode = safeOpcode(opcode_in.uValue());
    entry.instr_tag = instr_tag_in.uValue();

  }

  //es el tamaño de PIWS que por defecto poseera 8 entradas
  std::array<Entry, WINDOW_SIZE> m_entries = {};
  //es una cola de ventanas para el tema del historial de ciclos
  std::deque<StateSnapshot> m_reverseStack;

  //estructura que usaremos para informarnos sobre disponibilidad de cada unidad funcionl
  //ante una instrucción pretendienta desde ID
  std::array<FunctionalUnitInfo, 4> functionalUnits = {{
      {1, false, 1},
      {fpAdd_latency, fpAdd_pipelined, fpAdd_latency},
      {fpMul_latency, fpMul_pipelined, fpMul_latency},
      {fpDiv_latency, fpDiv_pipelined, fpDiv_latency}
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
    //las salidas de los ouputports privados del PIW conectadas a las entrasnas del state PIWS
    clear_in >> state->clear_in;
    store_input >> state->store_input_in;
    emitted_tag >> state->emitted_tag_in;
    input_latency >> state->latency_in;
    /////
    /////
    /////
    /////

    //las señales entrandes desde ID/EX
    stalled_in >> state->stalled_in;
    pc_in >> state->pc_in;
    pc4_in >> state->pc4_in;
    r2_in >> state->r2_in;
    f_r2_in >> state->f_r2_in;
    alures_in >> state->alures_in;
    falures_in >> state->falures_in;

    reg_wr_src_ctrl_in >> state->reg_wr_src_ctrl_in;
    wr_reg_idx_in >> state->wr_reg_idx_in;
    reg_do_write_in >> state->reg_do_write_in;
    mem_do_write_in >> state->mem_do_write_in;
    mem_do_read_in >> state->mem_do_read_in;
    mem_op_in >> state->mem_op_in;
    fp_reg_do_write_in >> state->fp_reg_do_write_in;
    data_mem_wr_src_ctrl_in >> state->data_mem_wr_src_ctrl_in;

    branch_res_in >> state->branch_res_in;
    do_br_in >> state->do_br_in;
    do_jmp_in >> state->do_jmp_in;
    branch_taken_in >> state->branch_taken_in;
    controlflow_in >> state->controlflow_in;

    stallEcallHandling_in >> state->stallEcallHandling_in;
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

    enable_out << [this] { return enable_in.uValue(); };
    clear_out << [this] { return clear_in.uValue(); };
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

    //señales de la instruccion
    pc_out << [this] { return outputFromInput() ? pc_in.uValue() : selected()->pc; };
    pc4_out << [this] { return outputFromInput() ? pc4_in.uValue() : selected()->pc4; };
    r2_out << [this] { return outputFromInput() ? r2_in.uValue() : selected()->r2; };
    f_r2_out << [this] { return outputFromInput() ? f_r2_in.uValue() : selected()->f_r2; };
    alures_out << [this] { return outputFromInput() ? alures_in.uValue() : selected()->alures; };
    falures_out << [this] { return outputFromInput() ? falures_in.uValue() : selected()->falures; };

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

    branch_res_out << [this] {
      return outputFromInput() ? branch_res_in.uValue()
                               : static_cast<VSRTL_VT_U>(selected()->branch_res);
    };
    do_br_out << [this] { return outputFromInput() ? do_br_in.uValue() : static_cast<VSRTL_VT_U>(selected()->do_br); };
    do_jmp_out << [this] { return outputFromInput() ? do_jmp_in.uValue() : static_cast<VSRTL_VT_U>(selected()->do_jmp); };
    branch_taken_out << [this] {
      return outputFromInput() ? branch_taken_in.uValue()
                               : static_cast<VSRTL_VT_U>(selected()->branch_taken);
    };
    controlflow_out << [this] {
      return outputFromInput() ? controlflow_in.uValue()
                               : static_cast<VSRTL_VT_U>(selected()->controlflow);
    };

    stallEcallHandling_out << [this] {
      return outputFromInput() ? stallEcallHandling_in.uValue()
                               : static_cast<VSRTL_VT_U>(selected()->stallEcallHandling);
    };
    opcode_out << [this] {
      if (!outputValid()) {
        return static_cast<VSRTL_VT_U>(RVInstr::NOP);
      }
      return outputFromInput() ? safeOpcodeEmission(opcode_in.uValue())
                               : static_cast<VSRTL_VT_U>(selected()->opcode);
    };

    instr_tag_out << [this] {
      return outputFromInput() ? instr_tag_in.uValue() : selected()->instr_tag;
    };
    
    stallNeeded_out << [this]() -> VSRTL_VT_U {
      return 1;
    };
    
    
  }

  // Register control
  INPUTPORT(enable_in, 1);
  INPUTPORT(clear_in, 1);
  INPUTPORT(stalled_in, 1);
  INPUTPORT(valid_in, 1);

  OUTPUTPORT(enable_out, 1);
  OUTPUTPORT(clear_out, 1);
  OUTPUTPORT(stalled_out, 1);
  OUTPUTPORT(valid_out, 1);

  // Data
  INPUTPORT(pc_in, XLEN);
  INPUTPORT(pc4_in, XLEN);
  INPUTPORT(r2_in, XLEN);
  INPUTPORT(f_r2_in, XLEN);
  INPUTPORT(alures_in, XLEN);
  INPUTPORT(falures_in, XLEN);

  OUTPUTPORT(pc_out, XLEN);
  OUTPUTPORT(pc4_out, XLEN);
  OUTPUTPORT(r2_out, XLEN);
  OUTPUTPORT(f_r2_out, XLEN);
  OUTPUTPORT(alures_out, XLEN);
  OUTPUTPORT(falures_out, XLEN);

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

  // EX-local control-flow results that do not enter EX/MEM.
  INPUTPORT(branch_res_in, 1);
  INPUTPORT(do_br_in, 1);
  INPUTPORT(do_jmp_in, 1);
  INPUTPORT(branch_taken_in, 1);
  INPUTPORT(controlflow_in, 1);

  OUTPUTPORT(branch_res_out, 1);
  OUTPUTPORT(do_br_out, 1);
  OUTPUTPORT(do_jmp_out, 1);
  OUTPUTPORT(branch_taken_out, 1);
  OUTPUTPORT(controlflow_out, 1);

  // EX-local syscallExit and opcode result input/output.
  INPUTPORT(stallEcallHandling_in, 1);
  INPUTPORT(opcode_in, enumBitWidth<RVInstr>());

  OUTPUTPORT(stallEcallHandling_out, 1);
  OUTPUTPORT(opcode_out, enumBitWidth<RVInstr>());


  // Instruction tag identifier.
  INPUTPORT(instr_tag_in, XLEN);
  OUTPUTPORT(instr_tag_out, XLEN);

  //inputs especiales procedentes desde la unidad hazard para detectar
  //RaW respecto instrucciones pendientes en la ventana o bien estructurales por
  //no existir una entrada valida en la unidad funcional que necesita la proxima instrucción de ID
  INPUTPORT(id_reg1_idx_in, c_RVRegsBits);
  INPUTPORT(id_reg2_idx_in, c_RVRegsBits);
  INPUTPORT(id_opcode_in, enumBitWidth<RVInstr>());
  OUTPUTPORT(stallNeeded_out, 1);


private:
  //subcomponente de PIW que almacenará las instrucciones pendientes
  SUBCOMPONENT(state, State);
  //estos outports privados son exclusivos para informar al subcomponente
  //de información adicional de las señales entrantes
  OUTPUTPORT(store_input, 1);
  OUTPUTPORT(emitted_tag, XLEN);
  OUTPUTPORT(input_latency, 4);

  //funcion que controla la cada
  static VSRTL_VT_U safeOpcodeEmission(VSRTL_VT_U opcToEmit) {
    const auto maxOpcode = static_cast<VSRTL_VT_U>(RVInstr::REMUW);
    return opcToEmit <= maxOpcode ? opcToEmit : static_cast<VSRTL_VT_U>(RVInstr::NOP);
  }

  static bool sameDestination(const Entry &entry, VSRTL_VT_U regWrite, VSRTL_VT_U fpWrite, VSRTL_VT_U rd) {
    //WaW si coincide mismo banco destino al igual que registro dentro de dicho banco
    return (regWrite && entry.reg_do_write && rd != 0 && entry.wr_reg_idx == rd) ||
           (fpWrite && entry.fp_reg_do_write && entry.wr_reg_idx == rd);
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
    for(const auto &entry: state->entries()){
      //si se escribe en el banco de instrucciones FP aparte del mismo registro
      //que nosotros necesitamos entonces avisamos
      if(entry.valid && entry.fp_reg_do_write && entry.wr_reg_idx == regIdx){
        return true;
      }
    }
    //si no encontramos escritores en el banco FP en el mismo registro
    //que necesimos consultar entonces avisamos que no hay RaW
    return false;
  }

  bool inputValid() const {
    //es valido si se activa la señal de valida, un identificador
    //valido distindo de 0 y la señal de limpiar esta desactivada
    return valid_in.uValue() != 0 && instr_tag_in.uValue() != 0 &&
           clear_in.uValue() == 0;
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
      return 6;
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
  bool olderEntryReadyThisCycle(VSRTL_VT_U inputTag) const {
    for (const auto &entry : state->entries()) {
      if (!entry.valid) {
        continue;
      }
      if (entry.instr_tag >= inputTag) {
        continue;
      }
      if (entryHasOlderWaw(entry)) {
        continue;
      }
      if (entry.completed) {
        return true;
      }
    }
    return false;
  }


  //decidimos si emitimos una instruccion almacenada o no
  const Entry *selected() const {
    //inicialmente la entrada valida es una estructura vacia
    const Entry *best = &m_emptyEntry;
    //recorremos cada entrada de la ventana de instrucciones
    for (const auto &entry : state->entries()) {
      //continuaos si la entrada no es usada, completa o bien es prematura por riesgo WaW
      if (!entry.valid || !entry.completed || entryHasOlderWaw(entry)) {
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
    return inputValid() && latencyForInput() == 0 && !inputHasOlderWaw();
  }

  bool outputFromInput() const {
    //si no se cumple lo mencionado en el metodo anterior no podemos aplicar bypass
    if (!inputReadyForBypass()) {
      return false;
    }

    if (olderEntryReadyThisCycle(static_cast<VSRTL_VT_U>(instr_tag_in.uValue()))) {
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

  //estamos seguros que podemos lanzar al exterior si podemos
  //realizar bypass o bien disponer de una instrucción almacenada lista
  bool outputValid() const {
    return outputFromInput() || selected()->valid;
  }

  //debemos almacenar los inputs si son salidos y no se puede aplicar bypass
  bool shouldStoreInput() const {
    return inputValid() && !outputFromInput();
  }

  //extracción del tag de la instrucción emitida
  VSRTL_VT_U emittedTag() const {
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
    RVInstr emittedOpcode = windowEmission ? selected()->opcode : RVInstr::NOP;
    return state->structuralRiscTrigger(idOpcode, emittedOpcode);
  }

  //estructura vacia por defecto que se usa si no se
  //encuentra una instrucción lista para emitir
  Entry m_emptyEntry = {};
};

} // namespace core
} // namespace vsrtl
