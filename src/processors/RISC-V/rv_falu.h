#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>

#include "riscv.h"

#include "VSRTL/core/vsrtl_component.h"

namespace vsrtl {
namespace core {
using namespace Ripes;

template <unsigned XLEN>
class FALU : public Component {
public:
  SetGraphicsType(ALU);
  FALU(const std::string &name, SimComponent *parent) : Component(name, parent) {

    //FALU output
    res << [this] {
      const uint32_t op1Val = lowerWord(op1.uValue());
      const uint32_t op2Val = lowerWord(op2.uValue());

      switch (ctrl.eValue<FALUOp>()) {
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
      default:
        throw std::runtime_error("Invalid FALU opcode");
      }
    };
  }

  INPUTPORT_ENUM(ctrl, FALUOp);
  INPUTPORT(op1, XLEN);
  INPUTPORT(op2, XLEN);
  INPUTPORT(op3, XLEN);

  OUTPUTPORT(res, XLEN);

private:
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

};

} // namespace core
} // namespace vsrtl
