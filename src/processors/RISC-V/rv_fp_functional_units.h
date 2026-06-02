#pragma once

#include <array>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

#include "VSRTL/core/vsrtl_component.h"
#include "processors/RISC-V/rv_fp_instr_helpers.h"

namespace vsrtl {
namespace core {
using namespace Ripes;

template <unsigned XLEN>
class FPFunctionalUnits : public Component {
public:
  FPFunctionalUnits(const std::string &name, SimComponent *parent);

  void setLatencies(unsigned addSub, unsigned mul, unsigned div);
  void beginCycle();
  void insert(RVInstr opc, unsigned rd, unsigned rs1, unsigned rs2,
              VSRTL_VT_U op1Value, VSRTL_VT_U op2Value, VSRTL_VT_U pc,
              uint64_t sequence, bool clearIdex = true);
  void advance();
  void consumeExMem();
  void clearExMemLatch();
  void reset();
  void reverse();

  struct StageView {
    bool valid = false;
    VSRTL_VT_U pc = 0;
    std::string label;
  };

  bool hasActiveFpInstruction() const;
  bool hasReadyExMemEntry() const;
  std::optional<uint64_t> readyExMemSequence() const;
  std::optional<uint64_t> latchedExMemSequence() const;
  bool containsPc(VSRTL_VT_U pc) const;
  void debugDump(std::ostream &os) const;
  StageView fpUnitView(unsigned unitIndex,
                       std::optional<unsigned> slotIndex = std::nullopt) const;
  bool canIssueNow(RVInstr opc, unsigned rs1, unsigned rs2) const;

  static unsigned unitIndexForOpcode(RVInstr opc);

private:
  static constexpr unsigned ADD_SUB_UNIT = 0;
  static constexpr unsigned MUL_UNIT = 1;
  static constexpr unsigned DIV_UNIT = 2;
  static constexpr unsigned UNIT_COUNT = 3;

  struct Entry {
    unsigned rd = 0;
    unsigned rs1 = 0;
    unsigned rs2 = 0;
    VSRTL_VT_U value = 0;
    VSRTL_VT_U op1Value = 0;
    VSRTL_VT_U op2Value = 0;
    VSRTL_VT_U pc = 0;
    RVInstr opcode = RVInstr::NOP;
    uint64_t sequence = 0;
  };

  struct Unit {
    std::vector<std::optional<Entry>> stages;
    bool segmented = true;
  };

  struct ActiveEntry {
    Entry entry;
    unsigned cycle = 0;
  };

  struct Snapshot {
    std::array<Unit, UNIT_COUNT> units;
    std::array<bool, c_RVRegs> pendingRegs;
    std::vector<Entry> pendingWriters;
    std::optional<Entry> exmemEntry;
    uint64_t nextSequence = 0;
    bool issuedThisCycle = false;
    bool clearIdexThisCycle = false;
    bool advanceFeThisCycle = false;
  };

  static std::string stagePrefixForOpcode(RVInstr opc);
  static uint32_t lowerWord(VSRTL_VT_U value);
  static float unpackSingle(uint32_t value);
  static VSRTL_VT_U packSingle(float value);
  static VSRTL_VT_U computeResult(RVInstr opc, VSRTL_VT_U op1,
                                  VSRTL_VT_U op2);

  Unit *unitForOpcode(RVInstr opc);
  const Unit *unitForOpcode(RVInstr opc) const;
  bool canAccept(RVInstr opc) const;
  bool shouldStallID() const;
  bool isFpRegPending(unsigned reg) const;
  bool isFpSourceReady(unsigned reg) const;
  bool forwardingWritesReg(unsigned reg) const;
  bool exStageWritesFpReg(unsigned reg) const;
  bool normalStageHasWAW() const;
  bool hasPendingDestination(unsigned rd) const;
  bool hasOlderSameDestination(const Entry &candidate) const;
  bool pendingWriterIsInMem(const Entry &entry) const;

  void resetUnits();
  void resizeUnit(Unit &unit, unsigned latency, bool segmented);
  void advanceUnit(Unit &unit);
  void save();

  std::optional<ActiveEntry> oldestInUnit(unsigned unitIndex) const;
  std::optional<ActiveEntry> slotEntry(unsigned unitIndex,
                                       unsigned slotIndex) const;
  std::optional<unsigned> readyUnitForExMem() const;

  template <typename Fn> void inspectEntries(Fn fn) const;

public:
  INPUTPORT_ENUM(id_opcode, RVInstr);
  INPUTPORT(id_rs1, c_RVRegsBits);
  INPUTPORT(id_rs2, c_RVRegsBits);
  INPUTPORT(id_rd, c_RVRegsBits);
  INPUTPORT(id_valid, 1);

  INPUTPORT_ENUM(ex_opcode, RVInstr);
  INPUTPORT(ex_rd, c_RVRegsBits);
  INPUTPORT(ex_valid, 1);

  INPUTPORT(normal_wb_fp_write, 1);
  INPUTPORT(normal_wb_pc, XLEN);
  INPUTPORT(normal_wb_rd, c_RVRegsBits);
  INPUTPORT(mem_forward_valid, 1);
  INPUTPORT(mem_forward_fp_write, 1);
  INPUTPORT(mem_forward_is_load, 1);
  INPUTPORT(mem_forward_pc, XLEN);
  INPUTPORT(mem_forward_rd, c_RVRegsBits);
  INPUTPORT(wb_forward_valid, 1);
  INPUTPORT(wb_forward_fp_write, 1);
  INPUTPORT(wb_forward_rd, c_RVRegsBits);

  OUTPUTPORT(stall, 1);
  OUTPUTPORT(busy, 1);
  OUTPUTPORT(id_issue_ready, 1);
  OUTPUTPORT(issue_accepted, 1);
  OUTPUTPORT(fe_issue_accepted, 1);
  OUTPUTPORT(normal_waw_stall, 1);
  OUTPUTPORT(exmem_valid, 1);
  OUTPUTPORT(exmem_pc, XLEN);
  OUTPUTPORT(exmem_pc4, XLEN);
  OUTPUTPORT(exmem_rd, c_RVRegsBits);
  OUTPUTPORT(exmem_value, XLEN);

private:
  std::array<unsigned, UNIT_COUNT> m_latencies = {4, 7, 25};
  std::array<Unit, UNIT_COUNT> m_units;
  std::array<bool, c_RVRegs> m_pendingRegs = {};
  std::vector<Entry> m_pendingWriters;
  std::optional<Entry> m_exmemEntry;
  std::vector<Snapshot> m_history;
  uint64_t m_nextSequence = 0;
  bool m_issuedThisCycle = false;
  bool m_clearIdexThisCycle = false;
  bool m_advanceFeThisCycle = false;
  bool m_cycleSaved = false;
};

} // namespace core
} // namespace vsrtl

#include "processors/RISC-V/rv_fp_functional_units_impl.h"
