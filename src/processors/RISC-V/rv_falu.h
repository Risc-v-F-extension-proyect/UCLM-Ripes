#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <optional>
#include <vector>

#include "riscv.h"

#include "VSRTL/core/vsrtl_register.h"

namespace vsrtl {
namespace core {
using namespace Ripes;

template <unsigned XLEN>
class FALU : public ClockedComponent {
public:
  SetGraphicsType(FPUnits);
  FALU(const std::string &name, SimComponent *parent)
      : ClockedComponent(name, parent) {

    //inicializamos las entradas de cada unidad funcional al igual que el
    //contenido del array de registros destinos
    initializeUnits();

    //emisión del valor calculado dentro de la falu a partir de un tipo de operación(op)
    //y el contenido de 2 registros fuente(rs1 y rs2)
    res << [this] {
      //bypass_in==1 siempre en uniciclo
      if (bypass_in.uValue()) {
        return computeResult(opFromValue(ctrl.uValue()), op1.uValue(), op2.uValue());
      }
      //buscamos al entry mas adecuada a emitir en caso de haberla si estamos en la
      //configuración de 5 stages con pipelines
      const std::optional<EntryRef> selected = chosenEntryRef();
      const Entry *entry = selected ? getEntry(*selected) : nullptr;
      //si la hay emitimos el resultado o bien 0
      return entry ? entry->result : VT_U(0);
    };

    instr_tag_out << [this] {
      //bypass_in==1 siempre en uniciclo
      if (bypass_in.uValue()) {
        return instr_tag_in.uValue();
      }
      //buscamos al entry mas adecuada a mitir en caso de haberla
      const std::optional<EntryRef> selected = chosenEntryRef();
      const Entry *entry = selected ? getEntry(*selected) : nullptr;
      //si la hay emitimos suidentificador tag, si no, emitimos 0
      return entry ? entry->instr_tag : VT_U(0);
    };

    //eliminar posiblemente
    valid_out << [this] {
      if (bypass_in.uValue()) {
        return enable_in.uValue();
      }
      return chosenEntryRef() ? VT_U(1) : VT_U(0);
    };
  }

  //función que se ejecuta al finalizar un ciclo y antes de comenzar el siguiente
  void save() override {
    //guardamos estado anterior en el historial
    saveToStack();

    //si es uniciclo entonces limpiamos todo
    if (bypass_in.uValue()) {
      clearFPAlu();
      return;
    }

    //seleccionamos una entry para emitir
    const std::optional<EntryRef> selected = chosenEntryRef();
    //si tenemos una entry lista y la ventana permite emitirla, la liberamos
    //directamente mediante su referencia interna
    //mientras la ventana no notifique aceptada las señales de la entry que referencia
    //el EntryRef m_outputEntry insistirá mandando el mismo result e insr_tag hasta que
    //la ventana las acepte
    if (accepted_in.uValue() && selected) {
      m_outputEntry.reset();
      releaseEntry(*selected);
    }

    //actualizamos el estado de las demas entradas activas
    updateFPUnits();
    //capturamos entradas y comprobamos si podemos guardar en la FLU las salidas de ID/EX
    captureInput();
    //si m_outputEntry se limpio entonces buscamos una nueva Entry para emitir el proximo ciclo
    if (!m_outputEntry) {
      m_outputEntry = nextToGo();
    }
  }

  //funcion que limpia la FALU
  void reset() override {
    clearFPAlu();
    m_reverseStack.clear();
  }

  //retrocedemos el estado de la FALU al contendido de hace un ciclo
  void reverse() override {
    if (!m_reverseStack.empty()) {
      m_units = m_reverseStack.front().units;
      m_lastWriters = m_reverseStack.front().lastWriters;
      m_outputEntry = m_reverseStack.front().outputEntry;
      m_lastInputTag = m_reverseStack.front().lastInputTag;
      m_reverseStack.pop_front();
    }
  }

  //no usada de momento
  void forceValue(VSRTL_VT_U, VSRTL_VT_U) override {}

  //comprobador de si el tamaño del historial se superó,
  //si es el caso se ajusta el contenido con lo más reciente
  //descartando contenido más viejo que no cabe
  void reverseStackSizeChanged() override {
    if (reverseStackSize() < m_reverseStack.size()) {
      m_reverseStack.resize(reverseStackSize());
    }
  }

  //conficucación inicial del contenido de la FALU
  void setLatencies(unsigned addSub, unsigned mul, unsigned div) {
    m_addSubLatency = std::max(1u, addSub);
    m_mulLatency = std::max(1u, mul);
    m_divLatency = std::max(1u, div);
    initializeUnits();
  }

  //función explusiva para la animación de la FALU en la UI
  std::vector<unsigned> getGraphicsPipelineStages() const override {
    if (bypass_in.uValue()) {
      return {};
    }
    return {m_addSubLatency, m_mulLatency, m_divLatency};
  }

  //función explusiva para la animación de la FALU en la UI
  std::vector<std::vector<bool>> getGraphicsPipelineActivity() const override {
    if (bypass_in.uValue()) {
      return {};
    }

    std::vector<std::vector<bool>> activity = {
        std::vector<bool>(m_addSubLatency, false),
        std::vector<bool>(m_mulLatency, false),
        std::vector<bool>(m_divLatency, false)};

    for (unsigned unitIdx = 0; unitIdx < m_units.size(); ++unitIdx) {
      const std::vector<int> &assignedStage = m_units.at(unitIdx).assignedStage;
      for (unsigned stage = 0; stage < assignedStage.size(); ++stage) {
        activity.at(unitIdx).at(stage) = assignedStage.at(stage) >= 0;
      }
    }
    const FALUOp incomingOp = opFromValue(ctrl.uValue());
    const VSRTL_VT_U incomingTag = instr_tag_in.uValue();
    //if (enable_in.uValue() && incomingTag != 0 && isStoredOp(incomingOp) &&
    if (enable_in.uValue() && incomingTag != 0 && incomingOp != FALUOp::NOP &&
        incomingTag != m_lastInputTag) {
      const unsigned incomingUnit = unitIndex(incomingOp);
      if (availableUnitEntry(incomingUnit)) {
        activity.at(incomingUnit).at(0) = true;
      }
    }
    return activity;
  }

  struct PipelineDiagramEntry {
    bool valid = false;
    VSRTL_VT_U instrTag = 0;
    unsigned unit = 0;
    unsigned stage = 0;
  };

  unsigned pipelineDiagramSlotCount() const {
    unsigned count = 1;
    for (const FunctionalUnit &unit : m_units) {
      count += static_cast<unsigned>(unit.entries.size());
    }
    return count;
  }

  std::vector<PipelineDiagramEntry> pipelineDiagramEntries() const {
    std::vector<PipelineDiagramEntry> result;
    result.reserve(pipelineDiagramSlotCount());
    for (unsigned unitIdx = 0; unitIdx < m_units.size(); ++unitIdx) {
      const FunctionalUnit &unit = m_units.at(unitIdx);
      const std::vector<PipelineDiagramEntry>::size_type offset = result.size();
      result.resize(offset + unit.entries.size());
      for (unsigned stage = 0; stage < unit.assignedStage.size(); ++stage) {
        const int entryIdx = unit.assignedStage.at(stage);
        if (entryIdx < 0) {
          continue;
        }
        const Entry &entry = unit.entries.at(static_cast<unsigned>(entryIdx));
        result.at(offset + static_cast<unsigned>(entryIdx)) = {
            entry.valid, entry.instr_tag, unitIdx, stage};
      }
    }

    const FALUOp incomingOp = opFromValue(ctrl.uValue());
    const VSRTL_VT_U incomingTag = instr_tag_in.uValue();
    PipelineDiagramEntry preview;
    //if (enable_in.uValue() && incomingTag != 0 && isStoredOp(incomingOp) &&
    if (enable_in.uValue() && incomingTag != 0 && incomingOp != FALUOp::NOP &&
        incomingTag != m_lastInputTag) {
      const unsigned incomingUnit = unitIndex(incomingOp);
      preview = {true, incomingTag, incomingUnit, 0};
    }
    result.push_back(preview);
    return result;
  }

  //puertos de entrada en la FALU
  INPUTPORT_ENUM(ctrl, FALUOp);
  INPUTPORT(op1, XLEN);
  INPUTPORT(op2, XLEN);
  INPUTPORT(op3, XLEN);
  INPUTPORT(rd_idx_in, c_RVRegsBits);
  INPUTPORT(instr_tag_in, XLEN);
  INPUTPORT(enable_in, 1);
  INPUTPORT(bypass_in, 1);
  INPUTPORT(accepted_in, 1);

  //puertos de salida en la FALU
  OUTPUTPORT(res, XLEN);
  OUTPUTPORT(instr_tag_out, XLEN);
  OUTPUTPORT(valid_out, 1);

private:
  struct EntryRef {
    //indicador del indice de la unidad funcional a la cual pertenece la instancia actual de EntryRef
    uint8_t unit = 0;
    //indicador de la Entry dentro de una unidad funcional que es referenciada por la instancia actual de EntryRef
    uint8_t entryIndex = 0;

    //metodo que sobreescribe el aperando "==" para que permita a 2 instancias de EntryRef
    //compararse por la igualdad de sus atributos internos o miembros
    bool operator==(const EntryRef &other) const {
      //si resulta que other se encuentra en la misma unidad funcional y ocupa el mismo
      //indice que la Entry referenciada en esta instancia entonces this y other son la misma instrucción
      return unit == other.unit && entryIndex == other.entryIndex;
    }
  };

  struct Entry {
    //nos dice si la Entry esta disponible para ocuparla
    bool valid = false;
    //indica si ya completo todos los ciclos en su unidad funcional correspondiente
    bool completed = false;
    //indica si esta instrucción genera WaW si se emite nada mas completarse
    bool premature = false;
    //indicador del tipo de operación a realizar
    FALUOp op = FALUOp::NOP;
    VSRTL_VT_U op1 = 0;
    VSRTL_VT_U op2 = 0;
    //índice del registro destino dentro del banco de registros FP
    VSRTL_VT_U rd = 0;
    //valor calculado final
    VSRTL_VT_U result = 0;
    //identificador que señala result a cual instrucción almacenada en PIW pertenece
    VSRTL_VT_U instr_tag = 0;
    //booleano que nos indica si hay una instrucción mas vojen con mismo rd
    bool hasNextWriter = false;
    //
    EntryRef nextWriter = {};
  };

  struct FunctionalUnit {
    std::vector<Entry> entries;
    std::vector<int> assignedStage;
  };

  //alias de un array de 3 unidades funcionales, FADD/FSUB, FMUL y FDIV
  using Units = std::array<FunctionalUnit, 3>;
  //alias de un array de tamaño 32 donde cada indice i representa la referencia de una Entry
  //que posee como rd el registro del indice i dentro del banco de instrucciones FP
  using LastWriters = std::array<std::optional<EntryRef>, c_RVRegs>;

  //estructura global que mantendra el estado de las unidades funcionales y el vector de registros
  //en punto flotante usados como destino
  struct State {
    Units units;
    LastWriters lastWriters;
    std::optional<EntryRef> outputEntry;
    VSRTL_VT_U lastInputTag = 0;
  };

  static FALUOp opFromValue(VSRTL_VT_U value) {
    switch (value) {
    case static_cast<VSRTL_VT_U>(FALUOp::ADD):
      return FALUOp::ADD;
    case static_cast<VSRTL_VT_U>(FALUOp::SUB):
      return FALUOp::SUB;
    case static_cast<VSRTL_VT_U>(FALUOp::MUL):
      return FALUOp::MUL;
    case static_cast<VSRTL_VT_U>(FALUOp::DIV):
      return FALUOp::DIV;
    case static_cast<VSRTL_VT_U>(FALUOp::SQRT):
      return FALUOp::SQRT;
    case static_cast<VSRTL_VT_U>(FALUOp::MIN):
      return FALUOp::MIN;
    case static_cast<VSRTL_VT_U>(FALUOp::MAX):
      return FALUOp::MAX;
    case static_cast<VSRTL_VT_U>(FALUOp::SGNJ):
      return FALUOp::SGNJ;
    case static_cast<VSRTL_VT_U>(FALUOp::SGNJN):
      return FALUOp::SGNJN;
    case static_cast<VSRTL_VT_U>(FALUOp::SGNJX):
      return FALUOp::SGNJX;
    case static_cast<VSRTL_VT_U>(FALUOp::NOP):
    default:
      return FALUOp::NOP;
    }
  }

  //indicador del indice de la unidad funcional a la cual pertenece el tipo
  //de operación según op
  static unsigned unitIndex(FALUOp op) {
    switch (op) {
    case FALUOp::ADD:
    case FALUOp::SUB:
      return 0;
    case FALUOp::MUL:
      return 1;
    case FALUOp::DIV:
    case FALUOp::SQRT:
      return 2;
    default:
      return 0;
    }
  }

  //indicador de la latencia de la unidad funcional que se encuentra en el
  //indice index dentro del array de unidades funcionales
  unsigned unitLatency(unsigned index) const {
    switch (index) {
    case 0:
      return m_addSubLatency;
    case 1:
      return m_mulLatency;
    case 2:
      return m_divLatency;
    default:
      return 1;
    }
  }

  //funcion que en caso de poderse almacena el estado de FALU en el ciclo anterior
  //antes de manipularse en el proximo ciclo
  void saveToStack() {
    if (canReverse()) {
      m_reverseStack.push_front(
          {m_units, m_lastWriters, m_outputEntry, m_lastInputTag});
    }
  }

  //inicializador que inicializa cada unidad funcional de latencia x con x entradas vacias
  //aparte de instanciar el vector de registros destino vacio al no existir
  //dentro de la falu
  void initializeUnits() {
    m_units.at(0).entries.assign(m_addSubLatency, Entry{});
    m_units.at(0).assignedStage.assign(m_addSubLatency, -1);
    m_units.at(1).entries.assign(m_mulLatency, Entry{});
    m_units.at(1).assignedStage.assign(m_mulLatency, -1);
    m_units.at(2).entries.assign(m_divLatency, Entry{});
    m_units.at(2).assignedStage.assign(m_divLatency, -1);
    m_lastWriters.fill(std::nullopt);
    m_outputEntry.reset();
    m_lastInputTag = 0;
  }

  //vacia cada unidad funcional aparte de la lista de registros destino
  void clearFPAlu() {
    for (FunctionalUnit &unit : m_units) {
      std::fill(unit.entries.begin(), unit.entries.end(), Entry{});
      std::fill(unit.assignedStage.begin(), unit.assignedStage.end(), -1);
    }
    m_lastWriters.fill(std::nullopt);
    m_outputEntry.reset();
    m_lastInputTag = 0;
  }

  void updateFPUnits() {
    for (FunctionalUnit &unit : m_units) {
      int stages = static_cast<int>(unit.assignedStage.size());
      for (int stage = stages - 2; stage >= 0; --stage) {
        int &owner = unit.assignedStage.at(static_cast<unsigned>(stage));
        int &nextOwner = unit.assignedStage.at(static_cast<unsigned>(stage + 1));
        if (owner < 0 || nextOwner >= 0) {
          continue;
        }
        nextOwner = owner;
        owner = -1;
      }

      for (unsigned stage = 0; stage < unit.assignedStage.size(); ++stage) {
        const int owner = unit.assignedStage.at(stage);
        if (owner < 0) {
          continue;
        }
        Entry &entry = unit.entries.at(static_cast<unsigned>(owner));
        entry.completed = stage + 1 == unit.assignedStage.size();
      }
    }
  }

  //funcional que captura las señales entrantes desde ID/EX y las asigna a una Entry no usada
  void captureInput() {
    const FALUOp op = opFromValue(ctrl.uValue());
    const VSRTL_VT_U tag = instr_tag_in.uValue();
    if (!enable_in.uValue() || op == FALUOp::NOP || tag == 0 || tag == m_lastInputTag) {
      return;
    }
    const unsigned targetUnit = unitIndex(op);
    if (!availableUnitEntry(targetUnit)) {
      return;
    }
    //consultamos si la primera etapa de la unidad está disponible
    const std::optional<unsigned> freeIndex = firstFreeEntry(targetUnit);
    if (!freeIndex) {
      return;
    }
    m_lastInputTag = tag;
    //si poseemos entrada disponible en la unidad entonces copiamos el indice
    //de la unidad usada al igual que el indice de la Entry libre para usarla
    //más en adelante nos servirá para vincular la información de esta instrucción
    //con el vector de registros destinos para indicar qué usaremos rd como destino
    const EntryRef currentRef{
      static_cast<uint8_t>(targetUnit),
      static_cast<uint8_t>(*freeIndex)
    };
    FunctionalUnit &unit = m_units.at(targetUnit);
    Entry &entry = unit.entries.at(*freeIndex);
    const unsigned latency = unitLatency(targetUnit);

    const unsigned initialStage = stageAssignation(targetUnit);
    //asignamos las señales de la instrucción y otros valores necesarios en la entrada libre
    entry.valid = true;
    //la logica de completed es sencilla, al entrar en la falu en realidad necesitamos estar asignados
    //al stage 2 en nuestra unidad funcional debido al retraso del almacenamiento y actualización
    //de las señales entrantes en la FALU. initialStage poseevalores desde 0 hasta latency-1 para indicar
    //el stage que tenemos asignado, por lo tango si initialStage es 0 en realidad indica stage 1, si vale 1
    //en realidad indica stage 2. Por este motivo necesitamos sumar una unidad a initialStage, si resulta que
    //initialStage + 1 == latency, eso quiere decir que en el momento en que se consultará la
    //instrucción esta ya estará completa
    entry.completed = initialStage + 1 == latency;
    unit.assignedStage.at(initialStage) = static_cast<int>(*freeIndex);
    entry.op = op;
    entry.op1 = op1.uValue();
    entry.op2 = op2.uValue();
    entry.rd = rd_idx_in.uValue();
    //computo del resultado
    entry.result = computeResult(op, entry.op1, entry.op2);
    entry.instr_tag = tag;
    //referencia del indice rd dentro del vector de egistros destinos ocupados
    std::optional<EntryRef> &lastWriter = m_lastWriters.at(entry.rd);
    //si el contenido del indice rd no está vacio entonces significa que hay riesgo WaW
    if (lastWriter) {
      if (Entry *older = getEntry(*lastWriter)) {
        //marcamos la instrucción entrante como prematura
        entry.premature = true;
        //indicamos a la instruccion mas vieja con mismo rd que ahora hay otra con mismo rd tambien
        older->hasNextWriter = true;
        //vinculamos la nueva instrucción entrante con la instrucción con mismo rd, menor tag pero mayor
        //al de la instrucción entrante para que notifique a la actual entrante que ya no hay riesgo WaW
        //con ella cuando salga de FPAlu
        older->nextWriter = currentRef;
      } else {
        //si no hay ya ninguna instrucción con mismo rd vaciamos el contenido del indice rd del array
        //de registros destino por seguridad
        lastWriter.reset();
      }
    }
    //asignamos la información de la nueva instrucción que posee como destino el registro del indice rd
    lastWriter = currentRef;
  }

  //libera directamente la entrada indicada, sin volver a buscarla por tag
  void releaseEntry(const EntryRef &ref) {
    FunctionalUnit &unit = m_units.at(ref.unit);//apuntamos a la unidad funcional usada
    Entry &entry = unit.entries.at(ref.entryIndex);//apuntamos a la entarda usada por la instrucción
    if (!entry.valid) {
      return;
    }
    //eliberamos el ultimo stage ocupado por la instrucción
    unit.assignedStage.back() = -1;

    if (entry.hasNextWriter) {
      Entry *nextWriter = getEntry(entry.nextWriter);
      if (nextWriter) {
        nextWriter->premature = false;
      }
    } else {
      std::optional<EntryRef> &lastWriter = m_lastWriters.at(entry.rd);
      if (lastWriter && *lastWriter == ref) {
        lastWriter.reset();
      }
    }
    entry = Entry{};
  }

  //function que busca una instruccion lista para eliberarla hacia EX/MEM,
  //elibera la instrucción con menor tag en caso de existir varias a la vez listas
  std::optional<EntryRef> chosenEntryRef() const {
    //si ya poseemos una referencia a una entrada supuestamente lista entonces la comprobamos
    if (m_outputEntry) {
      //si resulta que getEntry(...) encuentra una Entry valida con los datos de m_outputEntry
      //entonces sabemos que esa es la que debemos retornar
      if (getEntry(*m_outputEntry)) {
        return m_outputEntry;
      }
    }
    //si m_outputEntry no apunta a ninguna Entry entonces debemos consultar si hay alguna lista
    //almacenada en alguna unidad funcional
    return nextToGo();
  }

  std::optional<EntryRef> nextToGo() const {
    std::optional<EntryRef> best;
    for (unsigned unitIdx = 0; unitIdx < m_units.size(); ++unitIdx) {
      const FunctionalUnit &unit = m_units.at(unitIdx);
      const int entryIdx = unit.assignedStage.back();

      if(entryIdx < 0){
        continue;
      }

      const Entry &entry = unit.entries.at(static_cast<unsigned>(entryIdx));

      if (!entry.valid || !entry.completed || entry.premature) {
        continue;
      }
      if(!best || entry.instr_tag < getEntry(*best)->instr_tag){
        best = EntryRef {
          static_cast<uint8_t>(unitIdx),
          static_cast<uint8_t>(entryIdx)
        };
      }
    }
    return best;
  }



  //función que verifica si una unidad funcional puede aceptar una nueva entrada
  bool availableUnitEntry(unsigned unitIdx) const {
    const std::vector<Entry> &entries = m_units.at(unitIdx).entries;

    //si la unidad funcional es la de división, como no está segmentada solo puede
    //aceptar una nueva instrucción si estan todas sus entradas vacias, con haber al menos
    //una ocupada ya se rechaza la acptura de señales en la FALU
    if (unitIdx == 2) {
      return std::none_of(
        entries.begin(),
        entries.end(),
        [](const Entry &entry){
          return entry.valid;
        });
    }
    //si existe una instrucción activa a la cual le quedan firstStageRemaining restantes
    //eso quiere decir que la primera etapa estáocupada
    return m_units.at(unitIdx).assignedStage.front() < 0;
  }

  unsigned stageAssignation(unsigned unitIdx) const {
    //verificamos la latenciade la unidad funcional en el indice unitIdx
    const unsigned latency = unitLatency(unitIdx);
    //si la latencia es <= 1 eso quiere decir que hay una unica etapa o stage
    if (latency <= 1) {
      return 0;
    }
    if (unitIdx == 2 ||
      m_units.at(unitIdx).assignedStage.at(1) < 0) {
      return 1;
    }
    return 0;
  }

  //retornamos una entrada a partir de la unidad funcional
  //e indice interno indicado dentro de ref
  Entry *getEntry(const EntryRef &ref) {
    Entry &entry = m_units.at(ref.unit).entries.at(ref.entryIndex);
    return entry.valid ? &entry : nullptr;
  }
  const Entry *getEntry(const EntryRef &ref) const {
    const Entry &entry = m_units.at(ref.unit).entries.at(ref.entryIndex);
    return entry.valid ? &entry : nullptr;
  }

  //conociendo la unidad funcional necesaria buscamos dentro de ella la primera
  //entrada disponible para almacenar las señales entrantes, si no se encuentra nada
  //retornamos un puntero vacio
  std::optional<unsigned> firstFreeEntry(unsigned unitIdx) const {
    const std::vector<Entry> &entries = m_units.at(unitIdx).entries;
    for (unsigned index = 0; index < entries.size(); ++index) {
      if (!entries.at(index).valid) {
        return index;
      }
    }
    return std::nullopt;
  }

  //funcion que calcula el resultado a emitir a partir del tipo de operación
  //y 2 registros fuente
  static VSRTL_VT_U computeResult(FALUOp op, VSRTL_VT_U rawOp1, VSRTL_VT_U rawOp2) {
    const uint32_t op1Val = lowerWord(rawOp1);
    const uint32_t op2Val = lowerWord(rawOp2);

    switch (op) {
    case FALUOp::ADD:
      return packSingle(unpackSingle(op1Val) + unpackSingle(op2Val));
    case FALUOp::SUB:
      return packSingle(unpackSingle(op1Val) - unpackSingle(op2Val));
    case FALUOp::MUL:
      return packSingle(unpackSingle(op1Val) * unpackSingle(op2Val));
    case FALUOp::DIV:
      return packSingle(unpackSingle(op1Val) / unpackSingle(op2Val));
    case FALUOp::SQRT:
      return packSingle(std::sqrt(unpackSingle(op1Val)));
    case FALUOp::MIN:
      return minSingle(op1Val, op2Val);
    case FALUOp::MAX:
      return maxSingle(op1Val, op2Val);
    case FALUOp::SGNJ: {
      const uint32_t signMask = 0x80000000u;
      const uint32_t op2Sign = op2Val & signMask;
      const uint32_t op1Magnitude = op1Val & ~signMask;
      return VT_U(op2Sign | op1Magnitude);
    }
    case FALUOp::SGNJN: {
      const uint32_t signMask = 0x80000000u;
      const uint32_t invertedSign = (~op2Val) & signMask;
      const uint32_t op1Magnitude = op1Val & ~signMask;
      return VT_U(invertedSign | op1Magnitude);
    }
    case FALUOp::SGNJX: {
      const uint32_t signMask = 0x80000000u;
      const uint32_t op1Magnitude = op1Val & ~signMask;
      const uint32_t xorSign = (op1Val ^ op2Val) & signMask;
      return VT_U(xorSign | op1Magnitude);
    }
    case FALUOp::NOP:
      return VT_U(0);
    }
    return VT_U(0);
  }

  static uint32_t lowerWord(VSRTL_VT_U value) {
    return static_cast<uint32_t>(value & 0xffffffffu);
  }

  static float unpackSingle(uint32_t value) {
    float result;
    std::memcpy(&result, &value, sizeof(result));
    return result;
  }

  static VSRTL_VT_U packSingle(float value) {
    uint32_t result;
    std::memcpy(&result, &value, sizeof(result));
    return VT_U(result);
  }

  static VSRTL_VT_U minSingle(uint32_t lhsBits, uint32_t rhsBits) {
    const float lhs = unpackSingle(lhsBits);
    const float rhs = unpackSingle(rhsBits);
    if (std::isnan(lhs) && std::isnan(rhs)) {
      return VT_U(0x7fc00000u);
    }
    if (std::isnan(lhs)) {
      return VT_U(rhsBits);
    }
    if (std::isnan(rhs)) {
      return VT_U(lhsBits);
    }
    return packSingle(std::fmin(lhs, rhs));
  }

  static VSRTL_VT_U maxSingle(uint32_t lhsBits, uint32_t rhsBits) {
    const float lhs = unpackSingle(lhsBits);
    const float rhs = unpackSingle(rhsBits);
    if (std::isnan(lhs) && std::isnan(rhs)) {
      return VT_U(0x7fc00000u);
    }
    if (std::isnan(lhs)) {
      return VT_U(rhsBits);
    }
    if (std::isnan(rhs)) {
      return VT_U(lhsBits);
    }
    return packSingle(std::fmax(lhs, rhs));
  }

  //Lista de unidades funcionales
  Units m_units = {};
  //Lista de registros destinos usados vinculados a las Entrys que los usan como
  //registro destino
  LastWriters m_lastWriters = {};
  VSRTL_VT_U m_lastInputTag = 0;
  //Candidata de salida retenida hasta que la ventana active release.
  std::optional<EntryRef> m_outputEntry;
  //Cola historial de la FALU
  std::deque<State> m_reverseStack;
  //latencias por defecto de cada unidad funcional
  unsigned m_addSubLatency = 4;
  unsigned m_mulLatency = 7;
  unsigned m_divLatency = 25;
};

} // namespace core
} // namespace vsrtl
