#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

#include "VSRTL/core/vsrtl_component.h"

#include "processors/RISC-V/riscv.h"
#include "processors/RISC-V/rv_control.h"

namespace vsrtl {
namespace core {
using namespace Ripes;

template <unsigned XLEN>
class FALU_DUAL : public Component {
public:
  SetGraphicsType(ALU);
  FALU_DUAL(const std::string &name, SimComponent *parent)
      : Component(name, parent) {
    res << [this] {
      const RVInstr instr = opcode.eValue<RVInstr>();
      if (!Control::isFALUInstr(instr)) {
        return VT_U(0);
      }
      return computeResult(Control::do_falu_ctrl(instr), op1.uValue(),
                           op2.uValue());
    };
  }

  INPUTPORT(op1, XLEN);
  INPUTPORT(op2, XLEN);
  INPUTPORT_ENUM(opcode, RVInstr);

  OUTPUTPORT(res, XLEN);

private:
  static VSRTL_VT_U computeResult(FALUOp op, VSRTL_VT_U rawOp1,
                                  VSRTL_VT_U rawOp2) {
    const uint32_t op1Val = readSingleFromFReg(rawOp1);
    const uint32_t op2Val = readSingleFromFReg(rawOp2);
    const uint64_t op1DoubleVal = lowerDouble(rawOp1);
    const uint64_t op2DoubleVal = lowerDouble(rawOp2);

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
    case FALUOp::SGNJ:
      return sgnjSingle(op1Val, op2Val, false, false);
    case FALUOp::SGNJN:
      return sgnjSingle(op1Val, op2Val, true, false);
    case FALUOp::SGNJX:
      return sgnjSingle(op1Val, op2Val, false, true);
    case FALUOp::ADD_D:
      return packDouble(unpackDouble(op1DoubleVal) + unpackDouble(op2DoubleVal));
    case FALUOp::SUB_D:
      return packDouble(unpackDouble(op1DoubleVal) - unpackDouble(op2DoubleVal));
    case FALUOp::MUL_D:
      return packDouble(unpackDouble(op1DoubleVal) * unpackDouble(op2DoubleVal));
    case FALUOp::DIV_D:
      return packDouble(unpackDouble(op1DoubleVal) / unpackDouble(op2DoubleVal));
    case FALUOp::SQRT_D:
      return packDouble(std::sqrt(unpackDouble(op1DoubleVal)));
    case FALUOp::MIN_D:
      return minDouble(op1DoubleVal, op2DoubleVal);
    case FALUOp::MAX_D:
      return maxDouble(op1DoubleVal, op2DoubleVal);
    case FALUOp::SGNJ_D:
      return sgnjDouble(op1DoubleVal, op2DoubleVal, false, false);
    case FALUOp::SGNJN_D:
      return sgnjDouble(op1DoubleVal, op2DoubleVal, true, false);
    case FALUOp::SGNJX_D:
      return sgnjDouble(op1DoubleVal, op2DoubleVal, false, true);
    case FALUOp::CVT_S_D:
      return packSingle(static_cast<float>(unpackDouble(op1DoubleVal)));
    case FALUOp::CVT_D_S:
      return packDouble(static_cast<double>(unpackSingle(op1Val)));
    case FALUOp::NOP:
      return VT_U(0);
    }
    return VT_U(0);
  }

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
    } else {
      return VT_U(value);
    }
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

  static VSRTL_VT_U sgnjSingle(uint32_t op1Val, uint32_t op2Val,
                               bool invert, bool xorSign) {
    const uint32_t signMask = 0x80000000u;
    const uint32_t op1Magnitude = op1Val & ~signMask;
    uint32_t sign = xorSign ? ((op1Val ^ op2Val) & signMask)
                            : (op2Val & signMask);
    if (invert) {
      sign = (~op2Val) & signMask;
    }
    return packSingleBits(sign | op1Magnitude);
  }

  static VSRTL_VT_U sgnjDouble(uint64_t op1Val, uint64_t op2Val, bool invert,
                               bool xorSign) {
    const uint64_t signMask = 0x8000000000000000ull;
    const uint64_t op1Magnitude = op1Val & ~signMask;
    uint64_t sign = xorSign ? ((op1Val ^ op2Val) & signMask)
                            : (op2Val & signMask);
    if (invert) {
      sign = (~op2Val) & signMask;
    }
    return VT_U(sign | op1Magnitude);
  }

};

} // namespace core
} // namespace vsrtl
