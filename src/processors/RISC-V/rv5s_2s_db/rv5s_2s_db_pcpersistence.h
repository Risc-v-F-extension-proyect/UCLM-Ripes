#pragma once

#include "VSRTL/core/vsrtl_register.h"

#include <array>
#include <deque>
#include <functional>

namespace vsrtl {
namespace core {

/**
 * Conserva los destinos de saltos tomados del procesador de dos delay slots
 * cuando el frontend esta detenido por una operacion de punto flotante.
 *
 * Esta implementacion es deliberadamente independiente de la variante de
 * tres slots: el numero de instrucciones posteriores al salto forma parte de
 * la arquitectura de este procesador y no debe heredarse de otro modelo.
 */
template <unsigned XLEN>
class RV5S2SDBPCPersistence : public ClockedComponent {
public:
  RV5S2SDBPCPersistence(const std::string &name, SimComponent *parent)
      : ClockedComponent(name, parent) {
    setSensitiveTo(branchTaken);
    setSensitiveTo(branchTarget);
    setSensitiveTo(flowEnable);
    setSensitiveTo(instructionAccepted);

    pendingValid << [this] {
      return static_cast<VSRTL_VT_U>(enabled && pendingCount != 0 &&
                                     slotsRemaining[0] == 0);
    };
    pendingPC << [this] { return pendingPCs[0]; };
  }

  void setEnabled(bool value) {
    enabled = value;
    if (!enabled) {
      pendingPCs = {};
      slotsRemaining = {};
      pendingCount = 0;
      acceptedPCs.clear();
    }
  }

  void setDelayedSlotSources(std::function<VSRTL_VT_U()> acceptedPC,
                             std::function<VSRTL_VT_U()> resolvedBranchPC) {
    this->acceptedPC = std::move(acceptedPC);
    this->resolvedBranchPC = std::move(resolvedBranchPC);
  }

  void save() override {
    if (canReverse()) {
      reverseStack.push_front(
          {pendingPCs, slotsRemaining, pendingCount, acceptedPCs});
      if (reverseStack.size() > reverseStackSize())
        reverseStack.pop_back();
    }

    if (!enabled)
      return;

    const bool accepted = instructionAccepted.uValue();
    const bool release = pendingCount != 0 && flowEnable.uValue() &&
                         slotsRemaining[0] == 0;

    if (accepted) {
      const auto pc = acceptedPC();
      acceptedPCs.push_back(pc);
      if (acceptedPCs.size() > acceptedHistoryLimit)
        acceptedPCs.pop_front();

      for (std::size_t i = 0; i < pendingCount; ++i) {
        if (slotsRemaining[i] != 0)
          --slotsRemaining[i];
      }
    }

    const bool capture = branchTaken.uValue() &&
                         (!flowEnable.uValue() || pendingCount != 0);

    if (release) {
      if (pendingCount == 2) {
        pendingPCs[0] = pendingPCs[1];
        slotsRemaining[0] = slotsRemaining[1];
      }
      --pendingCount;
    }

    if (capture && pendingCount < pendingPCs.size()) {
      pendingPCs[pendingCount] = branchTarget.uValue();
      slotsRemaining[pendingCount] = missingDelayedSlots();
      ++pendingCount;
    }
  }

  void reset() override {
    pendingPCs = {};
    slotsRemaining = {};
    pendingCount = 0;
    acceptedPCs.clear();
    reverseStack.clear();
  }

  void reverse() override {
    if (reverseStack.empty())
      return;
    pendingPCs = reverseStack.front().pcs;
    slotsRemaining = reverseStack.front().remaining;
    pendingCount = reverseStack.front().count;
    acceptedPCs = reverseStack.front().acceptedPCs;
    reverseStack.pop_front();
  }

  void forceValue(VSRTL_VT_U, VSRTL_VT_U) override {}

  void reverseStackSizeChanged() override {
    if (reverseStack.size() > reverseStackSize())
      reverseStack.resize(reverseStackSize());
  }

  INPUTPORT(branchTaken, 1);
  INPUTPORT(branchTarget, XLEN);
  INPUTPORT(flowEnable, 1);
  INPUTPORT(instructionAccepted, 1);

  OUTPUTPORT(pendingValid, 1);
  OUTPUTPORT(pendingPC, XLEN);

private:
  struct Snapshot {
    std::array<VSRTL_VT_U, 2> pcs{};
    std::array<unsigned, 2> remaining{};
    std::size_t count = 0;
    std::deque<VSRTL_VT_U> acceptedPCs;
  };

  unsigned missingDelayedSlots() const {
    const auto branchPC = resolvedBranchPC();
    unsigned acceptedAfterBranch = 0;
    bool found = false;

    for (auto it = acceptedPCs.rbegin(); it != acceptedPCs.rend(); ++it) {
      if (*it == branchPC) {
        found = true;
        break;
      }
      ++acceptedAfterBranch;
    }

    if (!found)
      return delayedSlotCount - 1;
    if (acceptedAfterBranch >= delayedSlotCount)
      return 0;

    const unsigned missing = delayedSlotCount - acceptedAfterBranch;
    return missing > 0 ? missing - 1 : 0;
  }

  bool enabled = false;
  std::array<VSRTL_VT_U, 2> pendingPCs{};
  std::array<unsigned, 2> slotsRemaining{};
  std::size_t pendingCount = 0;
  std::deque<VSRTL_VT_U> acceptedPCs;
  std::deque<Snapshot> reverseStack;
  std::function<VSRTL_VT_U()> acceptedPC = [] { return VSRTL_VT_U{0}; };
  std::function<VSRTL_VT_U()> resolvedBranchPC = [] {
    return VSRTL_VT_U{0};
  };
  static constexpr unsigned delayedSlotCount = 2;
  static constexpr std::size_t acceptedHistoryLimit = 16;
};

} // namespace core
} // namespace vsrtl
