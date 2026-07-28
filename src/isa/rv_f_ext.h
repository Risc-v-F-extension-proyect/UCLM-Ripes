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
  FCVT_FMT = 0b01000,
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

enum struct Rs2 : unsigned { ZERO = 0, ONE = 1, S = 0, D = 1 };

template <unsigned V>
using OpPartRs2 = OpPart<V, BitRange<20, 24>>;

template <typename InstrImpl, Funct5 funct5, Fmt fmt = Fmt::S, Rm rm = Rm::RNE>
struct F_Instr : public RV_Instruction<InstrImpl> {
  struct Opcode
      : public OpcodeSet<OpPartOpcode<OpcodeID::OP_FP>,
                         OpPartFunct5<static_cast<unsigned>(funct5)>,
                         OpPartFmt<static_cast<unsigned>(fmt)>,
                         OpPartRm<static_cast<unsigned>(rm)>> {};
  struct Fields : public FieldSet<FRegRd, FRegRs1, FRegRs2> {};
};

template <typename InstrImpl, Fmt fmt = Fmt::S, Rm rm = Rm::RNE>
struct F_Sqrt : public RV_Instruction<InstrImpl> {
  struct Opcode
      : public OpcodeSet<OpPartOpcode<OpcodeID::OP_FP>,
                         OpPartFunct5<static_cast<unsigned>(Funct5::FSQRT)>,
                         OpPartFmt<static_cast<unsigned>(fmt)>,
                         OpPartRs2<0>,
                         OpPartRm<static_cast<unsigned>(rm)>> {};
  struct Fields : public FieldSet<FRegRd, FRegRs1> {};
};

template <typename InstrImpl, Fmt dstFmt, Rs2 srcFmt, Rm rm = Rm::RNE>
struct F_Cvt_Fmt : public RV_Instruction<InstrImpl> {
  struct Opcode
      : public OpcodeSet<OpPartOpcode<OpcodeID::OP_FP>,
                         OpPartFunct5<static_cast<unsigned>(Funct5::FCVT_FMT)>,
                         OpPartFmt<static_cast<unsigned>(dstFmt)>,
                         OpPartRs2<static_cast<unsigned>(srcFmt)>,
                         OpPartRm<static_cast<unsigned>(rm)>> {};
  struct Fields : public FieldSet<FRegRd, FRegRs1> {};
};

template <typename InstrImpl, OpcodeID opcodeID, unsigned width>
struct F_Lw : public RV_Instruction<InstrImpl> {
  struct Opcode : public OpcodeSet<OpPartOpcode<opcodeID>, OpPartRm<width>> {};
  struct Fields : public FieldSet<FRegRd, ExtI::ImmCommon12, RegRs1> {};
};

template <typename InstrImpl, OpcodeID opcodeID, unsigned width>
struct F_Sw : public RV_Instruction<InstrImpl> {
  struct Opcode : public OpcodeSet<OpPartOpcode<opcodeID>, OpPartRm<width>> {};
  struct Fields : public FieldSet<FRegRs2, ExtI::TypeS::ImmS, RegRs1> {};
};

struct Flw_s : public F_Lw<Flw_s, OpcodeID::LOAD_FP, 0b010> {
  constexpr static std::string_view NAME = "flw";
};

struct Fsw_s : public F_Sw<Fsw_s, OpcodeID::STORE_FP, 0b010> {
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

struct Fmin_s : public F_Instr<Fmin_s, Funct5::FMINMAX, Fmt::S, Rm::RNE> {
  constexpr static std::string_view NAME = "fmin.s";
};

struct Fmax_s : public F_Instr<Fmax_s, Funct5::FMINMAX, Fmt::S, Rm::RTZ> {
  constexpr static std::string_view NAME = "fmax.s";
};
/*
  RNE = 0b000,
  RTZ = 0b001,
  RDN = 0b010,
*/
/// fsgnj, fsgnjn, fsgnjx
struct Fsgnj_s : public F_Instr<Fsgnj_s, Funct5::FSGNJ, Fmt::S, Rm::RNE> {
  constexpr static std::string_view NAME = "fsgnj.s";
};
struct Fsgnjn_s : public F_Instr<Fsgnjn_s, Funct5::FSGNJ, Fmt::S, Rm::RTZ> {
  constexpr static std::string_view NAME = "fsgnjn.s";
};
struct Fsgnjx_s : public F_Instr<Fsgnjx_s, Funct5::FSGNJ, Fmt::S, Rm::RDN> {
  constexpr static std::string_view NAME = "fsgnjx.s";
};

} // namespace ExtF

namespace ExtD {

struct Fld_d : public ExtF::F_Lw<Fld_d, OpcodeID::LOAD_FP, 0b011> {
  constexpr static std::string_view NAME = "fld";
};

struct Fsd_d : public ExtF::F_Sw<Fsd_d, OpcodeID::STORE_FP, 0b011> {
  constexpr static std::string_view NAME = "fsd";
};

struct Fadd_d : public ExtF::F_Instr<Fadd_d, ExtF::Funct5::FADD, ExtF::Fmt::D> {
  constexpr static std::string_view NAME = "fadd.d";
};

struct Fsub_d : public ExtF::F_Instr<Fsub_d, ExtF::Funct5::FSUB, ExtF::Fmt::D> {
  constexpr static std::string_view NAME = "fsub.d";
};

struct Fmul_d : public ExtF::F_Instr<Fmul_d, ExtF::Funct5::FMUL, ExtF::Fmt::D> {
  constexpr static std::string_view NAME = "fmul.d";
};

struct Fdiv_d : public ExtF::F_Instr<Fdiv_d, ExtF::Funct5::FDIV, ExtF::Fmt::D> {
  constexpr static std::string_view NAME = "fdiv.d";
};

struct Fsqrt_d : public ExtF::F_Sqrt<Fsqrt_d, ExtF::Fmt::D> {
  constexpr static std::string_view NAME = "fsqrt.d";
};

struct Fmin_d : public ExtF::F_Instr<Fmin_d, ExtF::Funct5::FMINMAX, ExtF::Fmt::D, ExtF::Rm::RNE> {
  constexpr static std::string_view NAME = "fmin.d";
};

struct Fmax_d : public ExtF::F_Instr<Fmax_d, ExtF::Funct5::FMINMAX, ExtF::Fmt::D, ExtF::Rm::RTZ> {
  constexpr static std::string_view NAME = "fmax.d";
};

struct Fsgnj_d : public ExtF::F_Instr<Fsgnj_d, ExtF::Funct5::FSGNJ, ExtF::Fmt::D, ExtF::Rm::RNE> {
  constexpr static std::string_view NAME = "fsgnj.d";
};

struct Fsgnjn_d : public ExtF::F_Instr<Fsgnjn_d, ExtF::Funct5::FSGNJ, ExtF::Fmt::D, ExtF::Rm::RTZ> {
  constexpr static std::string_view NAME = "fsgnjn.d";
};

struct Fsgnjx_d : public ExtF::F_Instr<Fsgnjx_d, ExtF::Funct5::FSGNJ, ExtF::Fmt::D, ExtF::Rm::RDN> {
  constexpr static std::string_view NAME = "fsgnjx.d";
};

struct Fcvt_s_d : public ExtF::F_Cvt_Fmt<Fcvt_s_d, ExtF::Fmt::S, ExtF::Rs2::D> {
  constexpr static std::string_view NAME = "fcvt.s.d";
};

struct Fcvt_d_s : public ExtF::F_Cvt_Fmt<Fcvt_d_s, ExtF::Fmt::D, ExtF::Rs2::S> {
  constexpr static std::string_view NAME = "fcvt.d.s";
};

void enableExt(const ISAInfoBase *isa, InstrVec &instructions,
               PseudoInstrVec &pseudoInstructions);

} // namespace ExtD
} // namespace RVISA
} // namespace Ripes
