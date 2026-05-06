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

enum class FALUOp {
  NOP,
  FADD,
  FSUB,
  FMUL,
  FDIV,
  FMIN,
  FMAX,
  FSGNJ,
  FSGNJN,
  FSGNJX,
  FMADD,
  FMSUB,
  FNMSUB,
  FNMADD
};

template <unsigned XLEN>
class FALU : public Component {
public:
  SetGraphicsType(ALU);
  FALU(const std::string &name, SimComponent *parent) : Component(name, parent) {
    res << [this] {
      const uint32_t lhsBits = lowerWord(op1.uValue());
      const uint32_t rhsBits = lowerWord(op2.uValue());
      const uint32_t addendBits = lowerWord(op3.uValue());

      switch (ctrl.eValue<FALUOp>()) {
      case FALUOp::FADD:
        return packSingle(unpackSingle(lhsBits) + unpackSingle(rhsBits));
      case FALUOp::FSUB:
        return packSingle(unpackSingle(lhsBits) - unpackSingle(rhsBits));
      case FALUOp::FMUL:
        return packSingle(unpackSingle(lhsBits) * unpackSingle(rhsBits));
      case FALUOp::FDIV:
        return packSingle(unpackSingle(lhsBits) / unpackSingle(rhsBits));
      case FALUOp::FMIN:
        return minSingle(lhsBits, rhsBits);
      case FALUOp::FMAX:
        return maxSingle(lhsBits, rhsBits);
      case FALUOp::FSGNJ:
        return signInject(lhsBits, rhsBits);
      case FALUOp::FSGNJN:
        return signInjectNeg(lhsBits, rhsBits);
      case FALUOp::FSGNJX:
        return signInjectXor(lhsBits, rhsBits);
      case FALUOp::FMADD:
        return packSingle(unpackSingle(lhsBits) * unpackSingle(rhsBits) +
                          unpackSingle(addendBits));
      case FALUOp::FMSUB:
        return packSingle(unpackSingle(lhsBits) * unpackSingle(rhsBits) -
                          unpackSingle(addendBits));
      case FALUOp::FNMSUB:
        return packSingle(-unpackSingle(lhsBits) * unpackSingle(rhsBits) +
                          unpackSingle(addendBits));
      case FALUOp::FNMADD:
        return packSingle(-unpackSingle(lhsBits) * unpackSingle(rhsBits) -
                          unpackSingle(addendBits));
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

  static VSRTL_VT_U signInject(uint32_t lhsBits, uint32_t rhsBits) {
    return VT_U((lhsBits & 0x7fffffffu) | (rhsBits & 0x80000000u));
  }

  static VSRTL_VT_U signInjectNeg(uint32_t lhsBits, uint32_t rhsBits) {
    return VT_U((lhsBits & 0x7fffffffu) | (~rhsBits & 0x80000000u));
  }

  static VSRTL_VT_U signInjectXor(uint32_t lhsBits, uint32_t rhsBits) {
    return VT_U(lhsBits ^ (rhsBits & 0x80000000u));
  }
};

} // namespace core
} // namespace vsrtl
