#pragma once

#include "VSRTL/core/vsrtl_register.h"

#include <array>
#include <deque>

namespace vsrtl {
namespace core {

/**
 * Almacena destinos de saltos tomados cuando pc_reg no esta habilitado.
 * La etapa de procedencia y la seleccion del PC se gestionan externamente.
 */
template <unsigned XLEN>
class RV5S3SDBPCPersistence : public ClockedComponent {
public:
  RV5S3SDBPCPersistence(const std::string &name, SimComponent *parent)
      : ClockedComponent(name, parent) {
    setSensitiveTo(branchTaken);
    setSensitiveTo(branchTarget);
    setSensitiveTo(flowEnable);

    pendingValid << [this] {
      // La reserva debe estar visible mientras el frontend permanece parado.
      // De este modo, cuando flowEnable se active, pc_reg ya tendra
      // seleccionado pendingPC antes del flanco que reanuda el flujo.
      return static_cast<VSRTL_VT_U>(enabled && pendingCount != 0);
    };
    pendingPC << [this] { return pendingPCs[0]; };
  }

  void setEnabled(bool value) {
    enabled = value;
    if (!enabled) {
      pendingPCs = {};
      pendingCount = 0;
    }
  }

  void save() override {
    if (canReverse()) {
      reverseStack.push_front({pendingPCs, pendingCount});
      if (reverseStack.size() > reverseStackSize())
        reverseStack.pop_back();
    }

    if (!enabled)
      return;

    const bool capture = branchTaken.uValue() &&
                         (!flowEnable.uValue() || pendingCount != 0);
    const bool release = pendingCount != 0 && flowEnable.uValue();

    // Extraer primero permite reemplazar la cabeza e insertar un nuevo PC en
    // el mismo flanco sin alterar el orden FIFO.
    if (release) {
      if (pendingCount == 2)
        pendingPCs[0] = pendingPCs[1];
      --pendingCount;
    }

    if (capture && pendingCount < pendingPCs.size()) {
      pendingPCs[pendingCount] = branchTarget.uValue();
      ++pendingCount;
    }
  }

  void reset() override {
    pendingPCs = {};
    pendingCount = 0;
    reverseStack.clear();
  }

  void reverse() override {
    if (reverseStack.empty())
      return;
    pendingPCs = reverseStack.front().pcs;
    pendingCount = reverseStack.front().count;
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

  OUTPUTPORT(pendingValid, 1);
  OUTPUTPORT(pendingPC, XLEN);

private:
  struct Snapshot {
    std::array<VSRTL_VT_U, 2> pcs{};
    std::size_t count = 0;
  };

  bool enabled = false;
  std::array<VSRTL_VT_U, 2> pendingPCs{};
  std::size_t pendingCount = 0;
  std::deque<Snapshot> reverseStack;
};

} // namespace core
} // namespace vsrtl
