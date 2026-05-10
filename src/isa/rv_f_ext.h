#ifndef RV_F_EXT_H
#define RV_F_EXT_H

#endif // RV_F_EXT_H
#pragma once

#include "pseudoinstruction.h"
#include "rv_i_ext.h"
#include "rvisainfo_common.h"

namespace Ripes {
namespace RVISA {
namespace ExtF {

enum class Funct5 : unsigned {
  FADD = 0b00000,
  FSUB = 0b00001,
  FMUL = 0b00010,
  FDIV = 0b00011,
  FSQRT = 0b01011,
  FMINMAX = 0b00101,
  FCVT_W_S = 0b11000,
  FCVT_WU_S = 0b11000,
  FCVT_S_W = 0b11010,
  FCVT_S_WU = 0b11010,
  FCLASS = 0b11100,
  FMV_X_W = 0b11100,
  FMV_W_X = 0b11110,
  FSGNJ = 0b00100
};

enum class Fmt : unsigned { S = 0b00, D = 0b01, Q = 0b11 };

enum class Rm : unsigned {
  RNE = 0b000,
  RTZ = 0b001,
  RDN = 0b010,
  RUP = 0b011,
  RMM = 0b100,
  DYN = 0b111
};

enum struct Rs2 : unsigned { ZERO = 0, ONE = 1 };

template <unsigned V>
using OpPartRs2 = OpPart<V, BitRange<20, 24>>;

template <typename InstrImpl, Funct5 funct5, Rm rm = Rm::RNE>
struct F_Instr : public RV_Instruction<InstrImpl> {
  struct Opcode
      : public OpcodeSet<OpPartOpcode<OpcodeID::OP_FP>,
                         OpPartFunct5<static_cast<unsigned>(funct5)>,
                         OpPartFmt<static_cast<unsigned>(Fmt::S)>,
                         OpPartRm<static_cast<unsigned>(rm)>> {};
  struct Fields : public FieldSet<FRegRd, FRegRs1, FRegRs2> {};
};

template <typename InstrImpl, Rm rm = Rm::RNE>
struct F_Sqrt : public RV_Instruction<InstrImpl> {
  struct Opcode
      : public OpcodeSet<OpPartOpcode<OpcodeID::OP_FP>,
                         OpPartFunct5<static_cast<unsigned>(Funct5::FSQRT)>,
                         OpPartFmt<static_cast<unsigned>(Fmt::S)>,
                         OpPartRs2<0>,
                         OpPartRm<static_cast<unsigned>(rm)>> {};
  struct Fields : public FieldSet<FRegRd, FRegRs1> {};
};

template <typename InstrImpl, OpcodeID opcodeID, Rm width>
struct F_Lw : public RV_Instruction<InstrImpl> {
  struct Opcode : public OpcodeSet<OpPartOpcode<opcodeID>,
                                   OpPartRm<static_cast<unsigned>(width)>> {};
  struct Fields : public FieldSet<FRegRd, ExtI::ImmCommon12, RegRs1> {};
};

template <typename InstrImpl, OpcodeID opcodeID, Rm width>
struct F_Sw : public RV_Instruction<InstrImpl> {
  struct Opcode : public OpcodeSet<OpPartOpcode<opcodeID>,
                                   OpPartRm<static_cast<unsigned>(width)>> {};
  struct Fields : public FieldSet<FRegRs2, ExtI::TypeS::ImmS, RegRs1> {};
};

struct Flw_s : public F_Lw<Flw_s, OpcodeID::LOAD_FP, Rm::RDN> {
  constexpr static std::string_view NAME = "flw";
};

struct Fsw_s : public F_Sw<Fsw_s, OpcodeID::STORE_FP, Rm::RDN> {
  constexpr static std::string_view NAME = "fsw";
};

struct Fadd_s : public F_Instr<Fadd_s, Funct5::FADD> {
  constexpr static std::string_view NAME = "fadd.s";
};

struct Fsub_s : public F_Instr<Fsub_s, Funct5::FSUB> {
  constexpr static std::string_view NAME = "fsub.s";
};

struct Fmul_s : public F_Instr<Fmul_s, Funct5::FMUL> {
  constexpr static std::string_view NAME = "fmul.s";
};

struct Fdiv_s : public F_Instr<Fdiv_s, Funct5::FDIV> {
  constexpr static std::string_view NAME = "fdiv.s";
};

struct Fsqrt_s : public F_Sqrt<Fsqrt_s> {
  constexpr static std::string_view NAME = "fsqrt.s";
};

struct Fmin_s : public F_Instr<Fmin_s, Funct5::FMINMAX, Rm::RNE> {
  constexpr static std::string_view NAME = "fmin.s";
};

struct Fmax_s : public F_Instr<Fmax_s, Funct5::FMINMAX, Rm::RTZ> {
  constexpr static std::string_view NAME = "fmax.s";
};

} // namespace ExtF
} // namespace RVISA
} // namespace Ripes
