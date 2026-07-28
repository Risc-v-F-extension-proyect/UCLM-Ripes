#pragma once

#include "VSRTL/core/vsrtl_component.h"
#include "riscv.h"

namespace vsrtl {
namespace core {
using namespace Ripes;

class Control : public Component {
public:
  /* clang-format off */
    static bool isFALUInstr(RVInstr opc) {
        switch(opc) {
            case RVInstr::FADD:
            case RVInstr::FSUB:
            case RVInstr::FMUL:
            case RVInstr::FDIV:
            case RVInstr::FSQRT:
            case RVInstr::FMIN:
            case RVInstr::FMAX:
            case RVInstr::FSGNJ:
            case RVInstr::FSGNJN:
            case RVInstr::FSGNJX:
            case RVInstr::FADDD:
            case RVInstr::FSUBD:
            case RVInstr::FMULD:
            case RVInstr::FDIVD:
            case RVInstr::FSQRTD:
            case RVInstr::FMIND:
            case RVInstr::FMAXD:
            case RVInstr::FSGNJD:
            case RVInstr::FSGNJND:
            case RVInstr::FSGNJXD:
            case RVInstr::FCVTSD:
            case RVInstr::FCVTDS:
                return true;
            default:
                return false;
        }
    }

    static CompOp do_comp_ctrl(RVInstr opc) {
        switch(opc){
            case RVInstr::BEQ: return CompOp::EQ;
            case RVInstr::BNE: return CompOp::NE;
            case RVInstr::BLT: return CompOp::LT;
            case RVInstr::BGE: return CompOp::GE;
            case RVInstr::BLTU: return CompOp::LTU;
            case RVInstr::BGEU: return CompOp::GEU;
            default: return CompOp::NOP;
        }
    }

    static VSRTL_VT_U do_branch_ctrl(RVInstr opc) {
        switch(opc){
            case RVInstr::BEQ: case RVInstr::BNE: case RVInstr::BLT:
            case RVInstr::BGE: case RVInstr::BLTU: case RVInstr::BGEU:
                return 1;
            default:
                return 0;
        }
    }

    static VSRTL_VT_U do_jump_ctrl(RVInstr opc) {
        switch(opc){
            case RVInstr::JAL: case RVInstr::JALR:
                return 1;
            default:
                return 0;
        }
    }

    static MemOp do_mem_ctrl(RVInstr opc) {
        switch(opc){
            case RVInstr::SB: return MemOp::SB;
            case RVInstr::SH: return MemOp::SH;
            case RVInstr::SW: return MemOp::SW;
            case RVInstr::SD: return MemOp::SD;
            case RVInstr::LB: return MemOp::LB;
            case RVInstr::LH: return MemOp::LH;
            case RVInstr::LW: return MemOp::LW;
            case RVInstr::LD: return MemOp::LD;
            case RVInstr::LBU: return MemOp::LBU;
            case RVInstr::LHU: return MemOp::LHU;
            case RVInstr::LWU: return MemOp::LWU;
            //F extesion cases
            case RVInstr::FLW: return MemOp::FLW;
            case RVInstr::FSW: return MemOp::FSW;
            case RVInstr::FLD: return MemOp::FLD;
            case RVInstr::FSD: return MemOp::FSD;
            default:
                return MemOp::NOP;
        }
    }

    static VSRTL_VT_U do_fp_reg_do_write_ctrl(RVInstr opc) {
        switch(opc) {
            case RVInstr::FLW:
            case RVInstr::FLD:
                return 1;
            default:
                return isFALUInstr(opc);
        }
    }

    static VSRTL_VT_U do_reg_do_write_ctrl(RVInstr opc) {
        switch(opc) {
            case RVInstr::LUI:
            case RVInstr::AUIPC:

            // Arithmetic-immediate instructions
            case RVInstr::ADDI: case RVInstr::SLTI: case RVInstr::SLTIU: case RVInstr::XORI:
            case RVInstr::ORI: case RVInstr::ANDI: case RVInstr::SLLI: case RVInstr::SRLI:
            case RVInstr::SRAI: case RVInstr::ADDIW: case RVInstr::SLLIW: case RVInstr::SRLIW:
            case RVInstr::SRAIW:

            // Arithmetic instructions
            case RVInstr::MUL: case RVInstr::MULH: case RVInstr:: MULHSU: case RVInstr::MULHU:
            case RVInstr::DIV: case RVInstr::DIVU: case RVInstr::REM: case RVInstr::REMU:
            case RVInstr::ADD: case RVInstr::SUB: case RVInstr::SLL: case RVInstr::SLT:
            case RVInstr::SLTU: case RVInstr::XOR: case RVInstr::SRL: case RVInstr::SRA:
            case RVInstr::OR: case RVInstr::AND: case RVInstr::ADDW: case RVInstr::SUBW:
            case RVInstr::SLLW: case RVInstr::SRLW: case RVInstr::SRAW: case RVInstr::MULW:
            case RVInstr::DIVW: case RVInstr::DIVUW: case RVInstr::REMW: case RVInstr::REMUW:

            // Load instructions
            case RVInstr::LB: case RVInstr::LH: case RVInstr::LW: case RVInstr::LBU: case RVInstr::LHU:
            case RVInstr::LWU: case RVInstr::LD:

            // Jump instructions
            case RVInstr::JALR:
            case RVInstr::JAL:
                return 1;
            default: return 0;
        }
    }

    static RegWrSrc do_reg_wr_src_ctrl(RVInstr opc) {
        switch(opc){
            // Load instructions
            case RVInstr::LB: case RVInstr::LH: case RVInstr::LW: case RVInstr::LBU:
            case RVInstr::LHU: case RVInstr::LWU: case RVInstr::LD:
            case RVInstr::FLW:
            case RVInstr::FLD:
                return RegWrSrc::MEMREAD;

            // Jump instructions
            case RVInstr::JALR:
            case RVInstr::JAL:
                return RegWrSrc::PC4;

            default:
                return RegWrSrc::ALURES;
        }
    }

    static DataMemWrSrc do_data_mem_wr_src_ctrl(RVInstr opc) {
        switch(opc) { 
            case RVInstr::FSW:
            case RVInstr::FSD:
                return DataMemWrSrc::FREG2;
            default:
                return DataMemWrSrc::REG2;
        }
    }

    static AluSrc1 do_alu_op1_ctrl(RVInstr opc) {
        switch(opc) {
            case RVInstr::AUIPC: case RVInstr::JAL:
            case RVInstr::BEQ: case RVInstr::BNE: case RVInstr::BLT:
            case RVInstr::BGE: case RVInstr::BLTU: case RVInstr::BGEU:
                return AluSrc1::PC;
            default:
                return AluSrc1::REG1;
        }
    }

    static AluSrc2 do_alu_op2_ctrl(RVInstr opc) {
        switch(opc) {
        case RVInstr::LUI:
        case RVInstr::AUIPC:
            return AluSrc2::IMM;

        // Arithmetic-immediate instructions
        case RVInstr::ADDI: case RVInstr::SLTI: case RVInstr::SLTIU: case RVInstr::XORI:
        case RVInstr::ORI: case RVInstr::ANDI: case RVInstr::SLLI: case RVInstr::SRLI:
        case RVInstr::SRAI: case RVInstr::ADDIW: case RVInstr::SLLIW: case RVInstr::SRLIW:
        case RVInstr::SRAIW:
            return AluSrc2::IMM;

        // Arithmetic instructions
        case RVInstr::MUL: case RVInstr::MULH: case RVInstr:: MULHSU: case RVInstr::MULHU:
        case RVInstr::DIV: case RVInstr::DIVU: case RVInstr::REM: case RVInstr::REMU:
        case RVInstr::ADD: case RVInstr::SUB: case RVInstr::SLL: case RVInstr::SLT:
        case RVInstr::SLTU: case RVInstr::XOR: case RVInstr::SRL: case RVInstr::SRA:
        case RVInstr::OR: case RVInstr::AND: case RVInstr::ADDW: case RVInstr::SUBW:
        case RVInstr::SLLW: case RVInstr::SRLW: case RVInstr::SRAW: case RVInstr::MULW:
        case RVInstr::DIVW: case RVInstr::DIVUW: case RVInstr::REMW: case RVInstr::REMUW:
            return AluSrc2::REG2;

        // Load/Store instructions
        case RVInstr::LB: case RVInstr::LH: case RVInstr::LW: case RVInstr::LBU: case RVInstr::LHU:
        case RVInstr::SB: case RVInstr::SH: case RVInstr::SW: case RVInstr::LWU: case RVInstr::LD:
        case RVInstr::SD: case RVInstr::FLW: case RVInstr::FSW:
        case RVInstr::FLD: case RVInstr::FSD:
            return AluSrc2::IMM;

        // Branch instructions
        case RVInstr::BEQ: case RVInstr::BNE: case RVInstr::BLT:
        case RVInstr::BGE: case RVInstr::BLTU: case RVInstr::BGEU:
            return AluSrc2::IMM;

        // Jump instructions
        case RVInstr::JALR:
        case RVInstr::JAL:
            return AluSrc2::IMM;

        default:
            return AluSrc2::REG2;
        }
    }

    static FALUOp do_falu_ctrl(RVInstr opc) {
        switch(opc){
            case RVInstr::FADD : return FALUOp::ADD;
            case RVInstr::FSUB : return FALUOp::SUB;
            case RVInstr::FMUL : return FALUOp::MUL;
            case RVInstr::FDIV : return FALUOp::DIV;
            case RVInstr::FSQRT: return FALUOp::SQRT;
            case RVInstr::FMIN : return FALUOp::MIN;
            case RVInstr::FMAX : return FALUOp::MAX;
            case RVInstr::FSGNJ : return FALUOp::SGNJ;
            case RVInstr::FSGNJN: return FALUOp::SGNJN;
            case RVInstr::FSGNJX: return FALUOp::SGNJX;
            case RVInstr::FADDD : return FALUOp::ADD_D;
            case RVInstr::FSUBD : return FALUOp::SUB_D;
            case RVInstr::FMULD : return FALUOp::MUL_D;
            case RVInstr::FDIVD : return FALUOp::DIV_D;
            case RVInstr::FSQRTD: return FALUOp::SQRT_D;
            case RVInstr::FMIND : return FALUOp::MIN_D;
            case RVInstr::FMAXD : return FALUOp::MAX_D;
            case RVInstr::FSGNJD : return FALUOp::SGNJ_D;
            case RVInstr::FSGNJND: return FALUOp::SGNJN_D;
            case RVInstr::FSGNJXD: return FALUOp::SGNJX_D;
            case RVInstr::FCVTSD: return FALUOp::CVT_S_D;
            case RVInstr::FCVTDS: return FALUOp::CVT_D_S;

            default: return FALUOp::NOP;

        }
    }
    static ALUOp do_alu_ctrl(RVInstr opc) {
        switch(opc) {
            case RVInstr::LB: case RVInstr::LH: case RVInstr::LW: case RVInstr::LBU: case RVInstr::LHU:
            case RVInstr::SB: case RVInstr::SH: case RVInstr::SW: case RVInstr::LWU: case RVInstr::LD:
            case RVInstr::SD:
                return ALUOp::ADD;
            case RVInstr::LUI:
                return ALUOp::LUI;
            case RVInstr::JAL: case RVInstr::JALR: case RVInstr::AUIPC:
            case RVInstr::ADD: case RVInstr::ADDI:
            case RVInstr::BEQ: case RVInstr::BNE: case RVInstr::BLT:
            case RVInstr::BGE: case RVInstr::BLTU: case RVInstr::BGEU:
                return ALUOp::ADD;
            case RVInstr::SUB: return ALUOp::SUB;
            case RVInstr::SLT: case RVInstr::SLTI:
                return ALUOp::LT;
            case RVInstr::SLTU: case RVInstr::SLTIU:
                return ALUOp::LTU;
            case RVInstr::XOR: case RVInstr::XORI:
                return ALUOp::XOR;
            case RVInstr::OR: case RVInstr::ORI:
                return ALUOp::OR;
            case RVInstr::AND: case RVInstr::ANDI:
                return ALUOp::AND;
            case RVInstr::SLL: case RVInstr::SLLI:
                return ALUOp::SL;
            case RVInstr::SRL: case RVInstr::SRLI:
                return ALUOp::SRL;
            case RVInstr::SRA: case RVInstr::SRAI:
                return ALUOp::SRA;
            case RVInstr::MUL   : return ALUOp::MUL;
            case RVInstr::MULH  : return ALUOp::MULH;
            case RVInstr::MULHU : return ALUOp::MULHU;
            case RVInstr::MULHSU: return ALUOp::MULHSU;
            case RVInstr::DIV   : return ALUOp::DIV;
            case RVInstr::DIVU  : return ALUOp::DIVU;
            case RVInstr::REM   : return ALUOp::REM;
            case RVInstr::REMU  : return ALUOp::REMU;
            case RVInstr::ADDIW : return ALUOp::ADDW;
            case RVInstr::SLLIW : return ALUOp::SLW;
            case RVInstr::SRLIW : return ALUOp::SRLW;
            case RVInstr::SRAIW : return ALUOp::SRAW;
            case RVInstr::ADDW  : return ALUOp::ADDW ;
            case RVInstr::SUBW  : return ALUOp::SUBW ;
            case RVInstr::SLLW  : return ALUOp::SLW ;
            case RVInstr::SRLW  : return ALUOp::SRLW ;
            case RVInstr::SRAW  : return ALUOp::SRAW ;
            case RVInstr::MULW  : return ALUOp::MULW ;
            case RVInstr::DIVW  : return ALUOp::DIVW ;
            case RVInstr::DIVUW : return ALUOp::DIVUW;
            case RVInstr::REMW  : return ALUOp::REMW ;
            case RVInstr::REMUW : return ALUOp::REMUW;

            // F extension cases for flw and fsw
            case RVInstr::FLW: case RVInstr::FSW:
            case RVInstr::FLD: case RVInstr::FSD:
                return ALUOp::ADD;

            default: return ALUOp::NOP;
        }
    }

    static VSRTL_VT_U do_do_mem_write_ctrl(RVInstr opc) {
        switch(opc) {
            case RVInstr::SB: case RVInstr::SH: case RVInstr::SW: case RVInstr::SD:
            case RVInstr::FSW:
            case RVInstr::FSD:
                return 1;
            default: return 0;
        }
    }

    static VSRTL_VT_U do_do_read_ctrl(RVInstr opc) {
        switch(opc) {
            case RVInstr::LB: case RVInstr::LH: case RVInstr::LW: case RVInstr::LBU:
            case RVInstr::LHU: case RVInstr::LWU: case RVInstr::LD:
                return 1;
            case RVInstr::FLW:
            case RVInstr::FLD:
                return 1;
            default: return 0;
        }
    }
  /* clang-format on */

public:
  Control(const std::string &name, SimComponent *parent)
      : Component(name, parent) {
    comp_ctrl << [this] { return do_comp_ctrl(opcode.eValue<RVInstr>()); };
    do_branch << [this] { return do_branch_ctrl(opcode.eValue<RVInstr>()); };
    do_jump << [this] { return do_jump_ctrl(opcode.eValue<RVInstr>()); };
    mem_ctrl << [this] { return do_mem_ctrl(opcode.eValue<RVInstr>()); };
    reg_do_write_ctrl <<
        [this] { return do_reg_do_write_ctrl(opcode.eValue<RVInstr>()); };

    fp_reg_do_write_ctrl <<
        //[this] { return 0; };
        [this] { return do_fp_reg_do_write_ctrl(opcode.eValue<RVInstr>()); };
    reg_wr_src_ctrl <<
        [this] { return do_reg_wr_src_ctrl(opcode.eValue<RVInstr>()); };
    data_mem_wr_src_ctrl <<
        [this] { return do_data_mem_wr_src_ctrl(opcode.eValue<RVInstr>()); };
    alu_op1_ctrl <<
        [this] { return do_alu_op1_ctrl(opcode.eValue<RVInstr>()); };
    alu_op2_ctrl <<
        [this] { return do_alu_op2_ctrl(opcode.eValue<RVInstr>()); };
    alu_ctrl << [this] { return do_alu_ctrl(opcode.eValue<RVInstr>()); };
    falu_ctrl << [this] { return do_falu_ctrl(opcode.eValue<RVInstr>()); };
    mem_do_write_ctrl <<
        [this] { return do_do_mem_write_ctrl(opcode.eValue<RVInstr>()); };
    mem_do_read_ctrl <<
        [this] { return do_do_read_ctrl(opcode.eValue<RVInstr>()); };
  }

  INPUTPORT_ENUM(opcode, RVInstr);
  OUTPUTPORT(fp_reg_do_write_ctrl, 1);
  OUTPUTPORT(reg_do_write_ctrl, 1);
  OUTPUTPORT(mem_do_write_ctrl, 1);
  OUTPUTPORT(mem_do_read_ctrl, 1);
  OUTPUTPORT(do_branch, 1);
  OUTPUTPORT(do_jump, 1);

  OUTPUTPORT_ENUM(comp_ctrl, CompOp);
  OUTPUTPORT_ENUM(reg_wr_src_ctrl, RegWrSrc);
  OUTPUTPORT_ENUM(data_mem_wr_src_ctrl, DataMemWrSrc);
  OUTPUTPORT_ENUM(mem_ctrl, MemOp);
  OUTPUTPORT_ENUM(alu_op1_ctrl, AluSrc1);
  OUTPUTPORT_ENUM(alu_op2_ctrl, AluSrc2);
  OUTPUTPORT_ENUM(alu_ctrl, ALUOp);
  
  //special outputports for F extension
  //OUTPUTPORT_ENUM(falu_op3_ctrl, AluSrc1);
  OUTPUTPORT_ENUM(falu_ctrl, FALUOp);
};

} // namespace core
} // namespace vsrtl
