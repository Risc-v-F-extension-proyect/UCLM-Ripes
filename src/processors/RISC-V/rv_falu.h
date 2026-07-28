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
      const uint32_t op1Val = readSingleFromFReg(op1.uValue());
      const uint32_t op2Val = readSingleFromFReg(op2.uValue());
      const uint64_t op1DoubleVal = lowerDouble(op1.uValue());
      const uint64_t op2DoubleVal = lowerDouble(op2.uValue());

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
        return packSingleBits(op2Sign | op1Magnitude);
      }
      case FALUOp::SGNJN: {
        const uint32_t signMask = 0x80000000u;
        const uint32_t invertedSign = (~op2Val) & signMask;
        const uint32_t op1Magnitude = op1Val & ~signMask;
        return packSingleBits(invertedSign | op1Magnitude);
      }
      case FALUOp::SGNJX: {
        const uint32_t signMask = 0x80000000u;
        const uint32_t op1Magnitude = op1Val & ~signMask;
        const uint32_t xorSign = (op1Val ^ op2Val) & signMask;
        return packSingleBits(xorSign | op1Magnitude);
      }
      case FALUOp::ADD_D:
        return packDouble(unpackDouble(op1DoubleVal) +
                          unpackDouble(op2DoubleVal));
      case FALUOp::SUB_D:
        return packDouble(unpackDouble(op1DoubleVal) -
                          unpackDouble(op2DoubleVal));
      case FALUOp::MUL_D:
        return packDouble(unpackDouble(op1DoubleVal) *
                          unpackDouble(op2DoubleVal));
      case FALUOp::DIV_D:
        return packDouble(unpackDouble(op1DoubleVal) /
                          unpackDouble(op2DoubleVal));
      case FALUOp::SQRT_D:
        return packDouble(std::sqrt(unpackDouble(op1DoubleVal)));
      case FALUOp::MIN_D:
        return minDouble(op1DoubleVal, op2DoubleVal);
      case FALUOp::MAX_D:
        return maxDouble(op1DoubleVal, op2DoubleVal);
      case FALUOp::SGNJ_D: {
        const uint64_t signMask = 0x8000000000000000ull;
        return VT_U((op2DoubleVal & signMask) |
                    (op1DoubleVal & ~signMask));
      }
      case FALUOp::SGNJN_D: {
        const uint64_t signMask = 0x8000000000000000ull;
        return VT_U(((~op2DoubleVal) & signMask) |
                    (op1DoubleVal & ~signMask));
      }
      case FALUOp::SGNJX_D: {
        const uint64_t signMask = 0x8000000000000000ull;
        return VT_U(((op1DoubleVal ^ op2DoubleVal) & signMask) |
                    (op1DoubleVal & ~signMask));
      }
      case FALUOp::CVT_S_D:
        return packSingle(static_cast<float>(unpackDouble(op1DoubleVal)));
      case FALUOp::CVT_D_S:
        return packDouble(static_cast<double>(unpackSingle(op1Val)));
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

  static uint32_t readSingleFromFReg(VSRTL_VT_U value) {
    if constexpr (XLEN > 32) {
      if ((value >> 32) != 0xffffffffu) {
        return 0x7fc00000u;
      }
    }
    return lowerWord(value);
  }

  static uint64_t lowerDouble(VSRTL_VT_U value) {
    return static_cast<uint64_t>(value);
  }

  static float unpackSingle(uint32_t value) {
    float result;
    std::memcpy(&result, &value, sizeof(result));
    return result;
  }

  static VSRTL_VT_U packSingle(float value) {
    if (std::isnan(value)) {
      return packSingleBits(0x7fc00000u);
    }
    uint32_t result;
    std::memcpy(&result, &value, sizeof(result));
    return packSingleBits(result);
  }

  static VSRTL_VT_U packSingleBits(uint32_t value) {
    if constexpr (XLEN > 32) {
      return (VT_U(0xffffffffu) << 32) | VT_U(value);
    }
    return VT_U(value);
  }

  static double unpackDouble(uint64_t value) {
    double result;
    std::memcpy(&result, &value, sizeof(result));
    return result;
  }

  static VSRTL_VT_U packDouble(double value) {
    if (std::isnan(value)) {
      return VT_U(0x7ff8000000000000ull);
    }
    uint64_t result;
    std::memcpy(&result, &value, sizeof(result));
    return VT_U(result);
  }

  static VSRTL_VT_U minSingle(uint32_t lhsBits, uint32_t rhsBits) {
    const float lhs = unpackSingle(lhsBits);
    const float rhs = unpackSingle(rhsBits);
    if (std::isnan(lhs) && std::isnan(rhs)) {
      return packSingleBits(0x7fc00000u);
    }
    if (std::isnan(lhs)) {
      return packSingleBits(rhsBits);
    }
    if (std::isnan(rhs)) {
      return packSingleBits(lhsBits);
    }
    return packSingle(std::fmin(lhs, rhs));
  }

  static VSRTL_VT_U maxSingle(uint32_t lhsBits, uint32_t rhsBits) {
    const float lhs = unpackSingle(lhsBits);
    const float rhs = unpackSingle(rhsBits);
    if (std::isnan(lhs) && std::isnan(rhs)) {
      return packSingleBits(0x7fc00000u);
    }
    if (std::isnan(lhs)) {
      return packSingleBits(rhsBits);
    }
    if (std::isnan(rhs)) {
      return packSingleBits(lhsBits);
    }
    return packSingle(std::fmax(lhs, rhs));
  }

  static VSRTL_VT_U minDouble(uint64_t lhsBits, uint64_t rhsBits) {
    const double lhs = unpackDouble(lhsBits);
    const double rhs = unpackDouble(rhsBits);
    if (std::isnan(lhs) && std::isnan(rhs)) {
      return VT_U(0x7ff8000000000000ull);
    }
    if (std::isnan(lhs)) {
      return VT_U(rhsBits);
    }
    if (std::isnan(rhs)) {
      return VT_U(lhsBits);
    }
    return packDouble(std::fmin(lhs, rhs));
  }

  static VSRTL_VT_U maxDouble(uint64_t lhsBits, uint64_t rhsBits) {
    const double lhs = unpackDouble(lhsBits);
    const double rhs = unpackDouble(rhsBits);
    if (std::isnan(lhs) && std::isnan(rhs)) {
      return VT_U(0x7ff8000000000000ull);
    }
    if (std::isnan(lhs)) {
      return VT_U(rhsBits);
    }
    if (std::isnan(rhs)) {
      return VT_U(lhsBits);
    }
    return packDouble(std::fmax(lhs, rhs));
  }

};

} // namespace core
} // namespace vsrtl
