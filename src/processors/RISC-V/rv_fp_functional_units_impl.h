#pragma once

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ostream>

namespace vsrtl {
namespace core {

template <unsigned XLEN>
FPFunctionalUnits<XLEN>::FPFunctionalUnits(const std::string &name,
                                           SimComponent *parent)
    : Component(name, parent) {
  stall << [this] { return shouldStallID(); };
  busy << [this] { return hasActiveFpInstruction(); };
  id_issue_ready << [this] {
    if (!id_valid.uValue()) {
      return false;
    }
    const auto opc = id_opcode.template eValue<RVInstr>();
    return isSegmentedFPInstr(opc) && !shouldStallID();
  };
  issue_accepted << [this] { return m_clearIdexThisCycle; };
  fe_issue_accepted << [this] { return m_advanceFeThisCycle; };
  normal_waw_stall << [this] { return normalStageHasWAW(); };

  exmem_valid << [this] { return m_exmemEntry.has_value(); };
  
  exmem_pc << [this] { return m_exmemEntry ? m_exmemEntry->pc : VSRTL_VT_U(0);};
  exmem_pc4 << [this] { return exmem_pc.uValue() + 4; };

  exmem_rd << [this] { return m_exmemEntry ? VSRTL_VT_U(m_exmemEntry->rd) : VSRTL_VT_U(0); };
  exmem_value << [this] { return m_exmemEntry ? m_exmemEntry->value : VSRTL_VT_U(0); };
}

template <unsigned XLEN>
void FPFunctionalUnits<XLEN>::setLatencies(unsigned addSub, unsigned mul,
                                           unsigned div) {
  m_latencies = {std::max(1u, addSub), std::max(1u, mul),
                 std::max(1u, div)};
  resetUnits();
}

template <unsigned XLEN> void FPFunctionalUnits<XLEN>::beginCycle() {
  m_cycleSaved = false;
  save();
  if (normal_wb_fp_write.uValue()) {
    const unsigned rd = normal_wb_rd.uValue();
    const auto pc = normal_wb_pc.uValue();
    auto writer = m_pendingWriters.end();
    for (auto it = m_pendingWriters.begin(); it != m_pendingWriters.end();
         ++it) {
      if (it->rd != rd || it->pc != pc) {
        continue;
      }
      if (writer == m_pendingWriters.end() ||
          it->sequence < writer->sequence) {
        writer = it;
      }
    }
    if (writer != m_pendingWriters.end()) {
      m_pendingWriters.erase(writer);
    }
    m_pendingRegs.at(rd) = hasPendingDestination(rd);
  }
  m_issuedThisCycle = false;
  m_clearIdexThisCycle = false;
  m_advanceFeThisCycle = false;
}

template <unsigned XLEN>
void FPFunctionalUnits<XLEN>::insert(RVInstr opc, unsigned rd, unsigned rs1,
                                     unsigned rs2, VSRTL_VT_U op1Value,
                                     VSRTL_VT_U op2Value, VSRTL_VT_U pc,
                                     uint64_t sequence, bool clearIdex) {
  if (!canIssueNow(opc, rs1, rs2)) {
    return;
  }

  auto *unit = unitForOpcode(opc);
  if (unit == nullptr || unit->stages.empty()) {
    return;
  }

  save();
  Entry entry;
  entry.rd = rd;
  entry.rs1 = rs1;
  entry.rs2 = rs2;
  entry.op1Value = op1Value;
  entry.op2Value = op2Value;
  entry.value = computeResult(opc, op1Value, op2Value);
  entry.pc = pc;
  entry.opcode = opc;
  entry.sequence = sequence;

  unit->stages.front() = entry;
  m_pendingWriters.push_back(entry);
  m_pendingRegs.at(rd) = true;
  m_issuedThisCycle = true;
  m_clearIdexThisCycle = clearIdex;
  m_advanceFeThisCycle = true;
}

template <unsigned XLEN> void FPFunctionalUnits<XLEN>::advance() {
  save();
  for (auto &unit : m_units) {
    advanceUnit(unit);
  }
}

template <unsigned XLEN> void FPFunctionalUnits<XLEN>::consumeExMem() {
  const auto unitIndex = readyUnitForExMem();
  if (!unitIndex) {
    return;
  }

  auto &unit = m_units.at(*unitIndex);
  auto &tail = unit.stages.back();
  if (!tail) {
    return;
  }

  save();
  m_exmemEntry = *tail;
  tail.reset();
}

template <unsigned XLEN> void FPFunctionalUnits<XLEN>::clearExMemLatch() {
  if (!m_exmemEntry) {
    return;
  }
  save();
  m_exmemEntry.reset();
}

template <unsigned XLEN> void FPFunctionalUnits<XLEN>::reset() {
  resetUnits();
  m_pendingRegs.fill(false);
  m_pendingWriters.clear();
  m_exmemEntry.reset();
  m_history.clear();
  m_nextSequence = 0;
  m_issuedThisCycle = false;
  m_clearIdexThisCycle = false;
  m_advanceFeThisCycle = false;
  m_cycleSaved = false;
}

template <unsigned XLEN> void FPFunctionalUnits<XLEN>::reverse() {
  if (m_history.empty()) {
    return;
  }

  const auto snapshot = m_history.back();
  m_units = snapshot.units;
  m_pendingRegs = snapshot.pendingRegs;
  m_pendingWriters = snapshot.pendingWriters;
  m_exmemEntry = snapshot.exmemEntry;
  m_nextSequence = snapshot.nextSequence;
  m_issuedThisCycle = snapshot.issuedThisCycle;
  m_clearIdexThisCycle = snapshot.clearIdexThisCycle;
  m_advanceFeThisCycle = snapshot.advanceFeThisCycle;
  m_history.pop_back();
  m_cycleSaved = false;
}

template <unsigned XLEN>
bool FPFunctionalUnits<XLEN>::hasActiveFpInstruction() const {
  if (m_exmemEntry) {
    return true;
  }
  return std::any_of(m_units.begin(), m_units.end(), [](const auto &unit) {
    return std::any_of(unit.stages.begin(), unit.stages.end(),
                       [](const auto &entry) { return entry.has_value(); });
  });
}

template <unsigned XLEN>
bool FPFunctionalUnits<XLEN>::hasReadyExMemEntry() const {
  return readyUnitForExMem().has_value();
}

template <unsigned XLEN>
std::optional<uint64_t> FPFunctionalUnits<XLEN>::readyExMemSequence() const {
  const auto unitIndex = readyUnitForExMem();
  if (!unitIndex) {
    return std::nullopt;
  }

  const auto &unit = m_units.at(*unitIndex);
  if (unit.stages.empty() || !unit.stages.back()) {
    return std::nullopt;
  }
  return unit.stages.back()->sequence;
}

template <unsigned XLEN>
std::optional<uint64_t> FPFunctionalUnits<XLEN>::latchedExMemSequence() const {
  if (!m_exmemEntry) {
    return std::nullopt;
  }
  return m_exmemEntry->sequence;
}

template <unsigned XLEN>
bool FPFunctionalUnits<XLEN>::containsPc(VSRTL_VT_U pc) const {
  if (m_exmemEntry && m_exmemEntry->pc == pc) {
    return true;
  }
  bool found = false;
  inspectEntries(
      [&found, pc](const Entry &entry) { found = found || entry.pc == pc; });
  return found;
}

template <unsigned XLEN>
void FPFunctionalUnits<XLEN>::debugDump(std::ostream &os) const {
  const auto dumpEntry = [&os](const char *label,
                              const std::optional<Entry> &entry) {
    os << ' ' << label << '=';
    if (!entry) {
      os << '.';
      return;
    }
    os << "pc=0x" << std::hex << entry->pc << std::dec << "/f" << entry->rd
       << "/seq" << entry->sequence;
  };

  const char *unitNames[UNIT_COUNT] = {"A", "M", "D"};
  for (unsigned unitIndex = 0; unitIndex < UNIT_COUNT; ++unitIndex) {
    const auto &unit = m_units.at(unitIndex);
    os << "    " << unitNames[unitIndex] << ':';
    for (unsigned idx = 0; idx < unit.stages.size(); ++idx) {
      const auto label = unitNames[unitIndex] + std::to_string(idx + 1);
      dumpEntry(label.c_str(), unit.stages.at(idx));
    }
    os << '\n';
  }
  os << "    fp_exmem:";
  dumpEntry("entry", m_exmemEntry);
  os << '\n';
}

template <unsigned XLEN>
typename FPFunctionalUnits<XLEN>::StageView
FPFunctionalUnits<XLEN>::fpUnitView(
    unsigned unitIndex, std::optional<unsigned> slotIndex) const {
  const auto active =
      slotIndex ? slotEntry(unitIndex, *slotIndex) : oldestInUnit(unitIndex);
  if (!active) {
    return {};
  }

  const auto prefix = stagePrefixForOpcode(active->entry.opcode);
  return {true, active->entry.pc,
          prefix.empty() ? "" : prefix + std::to_string(active->cycle)};
}

template <unsigned XLEN>
bool FPFunctionalUnits<XLEN>::canIssueNow(RVInstr opc, unsigned rs1, unsigned rs2) const {
  if (!isSegmentedFPInstr(opc) || !canAccept(opc) || m_issuedThisCycle) {
    return false;
  }
  return isFpSourceReady(rs1) && isFpSourceReady(rs2);
}

template <unsigned XLEN>
unsigned FPFunctionalUnits<XLEN>::unitIndexForOpcode(RVInstr opc) {
  switch (opc) {
  case RVInstr::FADD:
  case RVInstr::FSUB:
    return ADD_SUB_UNIT;
  case RVInstr::FMUL:
    return MUL_UNIT;
  case RVInstr::FDIV:
    return DIV_UNIT;
  default:
    return UNIT_COUNT;
  }
}

template <unsigned XLEN>
std::string FPFunctionalUnits<XLEN>::stagePrefixForOpcode(RVInstr opc) {
  switch(opc){
    case RVInstr::FADD:
    case RVInstr::FSUB:
      return "A";
    case RVInstr::FMUL:
      return "M";
    case RVInstr::FDIV:
      return "D";
    default:
      return "";

  }
}

template <unsigned XLEN>
uint32_t FPFunctionalUnits<XLEN>::lowerWord(VSRTL_VT_U value) {
  return static_cast<uint32_t>(value & 0xffffffffu);
}

template <unsigned XLEN>
float FPFunctionalUnits<XLEN>::unpackSingle(uint32_t value) {
  float result;
  std::memcpy(&result, &value, sizeof(result));
  return result;
}

template <unsigned XLEN>
VSRTL_VT_U FPFunctionalUnits<XLEN>::packSingle(float value) {
  uint32_t result;
  std::memcpy(&result, &value, sizeof(result));
  return VT_U(result);
}

template <unsigned XLEN>
VSRTL_VT_U FPFunctionalUnits<XLEN>::computeResult(RVInstr opc, VSRTL_VT_U op1,
                                                  VSRTL_VT_U op2) {
  const uint32_t op1Val = lowerWord(op1);
  const uint32_t op2Val = lowerWord(op2);
  switch (opc) {
  case RVInstr::FADD:
    return packSingle(unpackSingle(op1Val) + unpackSingle(op2Val));
  case RVInstr::FSUB:
    return packSingle(unpackSingle(op1Val) - unpackSingle(op2Val));
  case RVInstr::FMUL:
    return packSingle(unpackSingle(op1Val) * unpackSingle(op2Val));
  case RVInstr::FDIV:
    return packSingle(unpackSingle(op1Val) / unpackSingle(op2Val));
  default:
    return VT_U(0);
  }
}

template <unsigned XLEN>
typename FPFunctionalUnits<XLEN>::Unit *
FPFunctionalUnits<XLEN>::unitForOpcode(RVInstr opc) {
  const auto index = unitIndexForOpcode(opc);
  return index < UNIT_COUNT ? &m_units.at(index) : nullptr;
}

template <unsigned XLEN>
const typename FPFunctionalUnits<XLEN>::Unit *
FPFunctionalUnits<XLEN>::unitForOpcode(RVInstr opc) const {
  const auto index = unitIndexForOpcode(opc);
  return index < UNIT_COUNT ? &m_units.at(index) : nullptr;
}

template <unsigned XLEN>
bool FPFunctionalUnits<XLEN>::canAccept(RVInstr opc) const {
  const auto *unit = unitForOpcode(opc);
  if (unit == nullptr || unit->stages.empty()) {
    return false;
  }
  if (unit->segmented) {
    return !unit->stages.front().has_value();
  }
  return std::all_of(unit->stages.begin(), unit->stages.end(),
                     [](const auto &entry) { return !entry.has_value(); });
}

template <unsigned XLEN> bool FPFunctionalUnits<XLEN>::shouldStallID() const {
  if (!id_valid.uValue()) {
    return false;
  }

  const auto opc = id_opcode.template eValue<RVInstr>();
  const unsigned rs1 = id_rs1.uValue();
  const unsigned rs2 = id_rs2.uValue();
  const unsigned rd = id_rd.uValue();

  if (opc == RVInstr::FLW) {
    return exStageWritesFpReg(rd);
  }
  if (opc == RVInstr::FSW) {
    return !isFpSourceReady(rs2);
  }
  if (!isSegmentedFPInstr(opc)) {
    return false;
  }
  return !canIssueNow(opc, rs1, rs2);
}

template <unsigned XLEN>
bool FPFunctionalUnits<XLEN>::isFpRegPending(unsigned reg) const {
  return m_pendingRegs.at(reg);
}

template <unsigned XLEN>
bool FPFunctionalUnits<XLEN>::isFpSourceReady(unsigned reg) const {
  if (exStageWritesFpReg(reg)) {
    return false;
  }

  const bool memLoadWritesReg = mem_forward_valid.uValue() &&
                                mem_forward_fp_write.uValue() &&
                                mem_forward_is_load.uValue() &&
                                mem_forward_rd.uValue() == reg;
  if (memLoadWritesReg) {
    return false;
  }

  return !isFpRegPending(reg) || forwardingWritesReg(reg);
}

template <unsigned XLEN>
bool FPFunctionalUnits<XLEN>::forwardingWritesReg(unsigned reg) const {
  if (m_exmemEntry && m_exmemEntry->rd == reg) {
    return true;
  }
  const bool memWritesReg = mem_forward_valid.uValue() &&
                            mem_forward_fp_write.uValue() &&
                            !mem_forward_is_load.uValue() &&
                            mem_forward_rd.uValue() == reg;
  const bool wbWritesReg = wb_forward_valid.uValue() &&
                           wb_forward_fp_write.uValue() &&
                           wb_forward_rd.uValue() == reg;
  return memWritesReg || wbWritesReg;
}

template <unsigned XLEN>
bool FPFunctionalUnits<XLEN>::exStageWritesFpReg(unsigned reg) const {
  if (!ex_valid.uValue() || ex_rd.uValue() != reg) {
    return false;
  }
  const auto opc = ex_opcode.template eValue<RVInstr>();
  return opc == RVInstr::FLW || isSegmentedFPInstr(opc);
}

template <unsigned XLEN>
bool FPFunctionalUnits<XLEN>::normalStageHasWAW() const {
  if (!ex_valid.uValue()) {
    return false;
  }
  const auto opc = ex_opcode.template eValue<RVInstr>();
  if (isSegmentedFPInstr(opc) || opc != RVInstr::FLW) {
    return false;
  }
  const unsigned rd = ex_rd.uValue();
  return std::any_of(m_pendingWriters.begin(), m_pendingWriters.end(),
                     [this, rd](const Entry &entry) {
                       return entry.rd == rd && !pendingWriterIsInMem(entry);
                     });
}

template <unsigned XLEN>
bool FPFunctionalUnits<XLEN>::hasPendingDestination(unsigned rd) const {
  return std::any_of(m_pendingWriters.begin(), m_pendingWriters.end(),
                     [rd](const Entry &entry) { return entry.rd == rd; });
}

template <unsigned XLEN>
bool FPFunctionalUnits<XLEN>::hasOlderSameDestination(
    const typename FPFunctionalUnits<XLEN>::Entry &candidate) const {
  return std::any_of(m_pendingWriters.begin(), m_pendingWriters.end(),
                     [this, &candidate](const Entry &entry) {
                       return entry.rd == candidate.rd &&
                              entry.sequence < candidate.sequence &&
                              !pendingWriterIsInMem(entry);
                     });
}

template <unsigned XLEN>
bool FPFunctionalUnits<XLEN>::pendingWriterIsInMem(
    const typename FPFunctionalUnits<XLEN>::Entry &entry) const {
  return mem_forward_valid.uValue() && mem_forward_fp_write.uValue() &&
         mem_forward_pc.uValue() == entry.pc &&
         mem_forward_rd.uValue() == entry.rd;
}

template <unsigned XLEN> void FPFunctionalUnits<XLEN>::resetUnits() {
  resizeUnit(m_units.at(ADD_SUB_UNIT), m_latencies.at(ADD_SUB_UNIT), true);
  resizeUnit(m_units.at(MUL_UNIT), m_latencies.at(MUL_UNIT), true);
  resizeUnit(m_units.at(DIV_UNIT), m_latencies.at(DIV_UNIT), false);
}

template <unsigned XLEN>
void FPFunctionalUnits<XLEN>::resizeUnit(
    typename FPFunctionalUnits<XLEN>::Unit &unit, unsigned latency, bool segmented) {
  unit.stages.assign(latency, std::nullopt);
  unit.segmented = segmented;
}

template <unsigned XLEN>
void FPFunctionalUnits<XLEN>::advanceUnit(
    typename FPFunctionalUnits<XLEN>::Unit &unit) {
  if (unit.stages.empty()) {
    return;
  }

  for (auto idx = unit.stages.size() - 1; idx > 0; --idx) {
    if (!unit.stages.at(idx) && unit.stages.at(idx - 1)) {
      unit.stages.at(idx) = unit.stages.at(idx - 1);
      unit.stages.at(idx - 1).reset();
    }
  }
}

template <unsigned XLEN> void FPFunctionalUnits<XLEN>::save() {
  if (m_cycleSaved) {
    return;
  }

  m_history.push_back({m_units, m_pendingRegs, m_pendingWriters, m_exmemEntry,
                       m_nextSequence, m_issuedThisCycle,
                       m_clearIdexThisCycle, m_advanceFeThisCycle});
  m_cycleSaved = true;
}

template <unsigned XLEN>
std::optional<typename FPFunctionalUnits<XLEN>::ActiveEntry>
FPFunctionalUnits<XLEN>::oldestInUnit(unsigned unitIndex) const {
  if (unitIndex >= UNIT_COUNT) {
    return std::nullopt;
  }

  std::optional<ActiveEntry> oldest;
  const auto &unit = m_units.at(unitIndex);
  for (unsigned idx = 0; idx < unit.stages.size(); ++idx) {
    const auto &entry = unit.stages.at(idx);
    if (!entry) {
      continue;
    }
    ActiveEntry candidate{*entry, idx + 1};
    if (!oldest || candidate.entry.sequence < oldest->entry.sequence) {
      oldest = candidate;
    }
  }
  return oldest;
}

template <unsigned XLEN>
std::optional<typename FPFunctionalUnits<XLEN>::ActiveEntry>
FPFunctionalUnits<XLEN>::slotEntry(unsigned unitIndex,
                                   unsigned slotIndex) const {
  if (unitIndex >= UNIT_COUNT) {
    return std::nullopt;
  }
  const auto &unit = m_units.at(unitIndex);
  if (slotIndex >= unit.stages.size() || !unit.stages.at(slotIndex)) {
    return std::nullopt;
  }
  return ActiveEntry{*unit.stages.at(slotIndex), slotIndex + 1};
}

template <unsigned XLEN>
std::optional<unsigned> FPFunctionalUnits<XLEN>::readyUnitForExMem() const {
  if (m_exmemEntry) {
    return std::nullopt;
  }

  std::optional<unsigned> selected;
  std::optional<uint64_t> selectedSequence;
  for (unsigned unitIndex = 0; unitIndex < UNIT_COUNT; ++unitIndex) {
    const auto &unit = m_units.at(unitIndex);
    if (unit.stages.empty() || !unit.stages.back()) {
      continue;
    }

    const auto &candidate = *unit.stages.back();
    if (hasOlderSameDestination(candidate)) {
      continue;
    }
    if (!selectedSequence || candidate.sequence < *selectedSequence) {
      selected = unitIndex;
      selectedSequence = candidate.sequence;
    }
  }
  return selected;
}

template <unsigned XLEN>
template <typename Fn>
void FPFunctionalUnits<XLEN>::inspectEntries(Fn fn) const {
  for (const auto &unit : m_units) {
    for (const auto &entry : unit.stages) {
      if (entry) {
        fn(*entry);
      }
    }
  }
}

} // namespace core
} // namespace vsrtl
