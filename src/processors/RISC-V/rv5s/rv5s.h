#pragma once

#include "VSRTL/core/vsrtl_adder.h"
#include "VSRTL/core/vsrtl_constant.h"
#include "VSRTL/core/vsrtl_design.h"
#include "VSRTL/core/vsrtl_logicgate.h"
#include "VSRTL/core/vsrtl_multiplexer.h"

#include <iostream>
#include <optional>
#include <unordered_map>
#include <vector>

#include "../../ripesvsrtlprocessor.h"

// Functional units
#include "processors/RISC-V/riscv.h"
#include "processors/RISC-V/rv_alu.h"
#include "processors/RISC-V/rv_branch.h"
#include "processors/RISC-V/rv_control.h"
#include "processors/RISC-V/rv_decode.h"
#include "processors/RISC-V/rv_ecallchecker.h"
#include "processors/RISC-V/rv_falu.h"
#include "processors/RISC-V/rv_falu_latency.h"

#include "processors/RISC-V/rv_fp_functional_units.h"
#include "processors/RISC-V/rv_fp_pipeline_helpers.h"
#include "processors/RISC-V/rv_fregisterfile.h"
#include "processors/RISC-V/rv_immediate.h"
#include "processors/RISC-V/rv_memory.h"
#include "processors/RISC-V/rv_registerfile.h"
#include "processors/RISC-V/rv_uncompress.h"
#include "processors/RISC-V/rv_unified_reg_wr_src_adapter.h"
#include "ripessettings.h"

// Stage separating registers
#include "../rv5s_no_fw_hz/rv5s_no_fw_hz_ifid.h"
#include "rv5s_exmem.h"
#include "rv5s_idex.h"
#include "rv5s_memwb.h"

// Forwarding & Hazard detection unit
#include "rv5s_forwardingunit.h"
#include "rv5s_hazardunit.h"

namespace vsrtl {
namespace core {
using namespace Ripes;

template <typename XLEN_T>
class RV5S : public RipesVSRTLProcessor {
  static_assert(std::is_same<uint32_t, XLEN_T>::value ||
                    std::is_same<uint64_t, XLEN_T>::value,
                "Only supports 32- and 64-bit variants");
  static constexpr unsigned XLEN = sizeof(XLEN_T) * CHAR_BIT;

public:
  enum Stage { IF = 0, ID = 1, EX = 2, MEM = 3, WB = 4, STAGECOUNT };
  RV5S(const QStringList &extensions)
      : RipesVSRTLProcessor("5-Stage RISC-V Processor") {
    m_enabledISA = ISAInfoRegistry::getISA<XLenToRVISA<XLEN>()>(extensions);
    decode->setISA(m_enabledISA);
    uncompress->setISA(m_enabledISA);
    falu_latency->setLatencies(
        RipesSettings::value(RIPES_SETTING_RV5S_FALU_ADDSUB_LATENCY).toUInt(),
        RipesSettings::value(RIPES_SETTING_RV5S_FALU_MUL_LATENCY).toUInt(),
        RipesSettings::value(RIPES_SETTING_RV5S_FALU_DIV_LATENCY).toUInt());
    fp_units->setLatencies(
        RipesSettings::value(RIPES_SETTING_RV5S_FALU_ADDSUB_LATENCY).toUInt(),
        RipesSettings::value(RIPES_SETTING_RV5S_FALU_MUL_LATENCY).toUInt(),
        RipesSettings::value(RIPES_SETTING_RV5S_FALU_DIV_LATENCY).toUInt());
    rebuildStructure();

    // -----------------------------------------------------------------------
    // Program counter
    pc_reg->out >> pc_4->op1;
    pc_inc->out >> pc_4->op2;
    pc_src->out >> pc_reg->in;
    0 >> pc_reg->clear;
    hzunit->hazardFEEnable >> *fe_enable_or->in[0];
    fp_units->fe_issue_accepted >> *fe_enable_or->in[1];
    fe_enable_or->out >> pc_reg->enable;

    2 >> pc_inc->get(PcInc::INC2);
    4 >> pc_inc->get(PcInc::INC4);
    uncompress->Pc_Inc >> pc_inc->select;

    // Note: pc_src works uses the PcSrc enum, but is selected by the boolean
    // signal from the controlflow OR gate. PcSrc enum values must adhere to the
    // boolean 0/1 values.
    controlflow_or->out >> pc_src->select;

    controlflow_or->out >> *efsc_or->in[0];
    ecallChecker->syscallExit >> *efsc_or->in[1];

    efsc_or->out >> *efschz_or->in[0];
    hzunit->hazardIDEXClear >> *efschz_or->in[1];
    fp_units->issue_accepted >> *efschz_or->in[2];
    fp_units->fe_issue_accepted >> *fp_id_issue_clear_and->in[0];
    hzunit->hazardIDEXEnable >> *fp_id_issue_clear_and->in[1];
    fp_id_issue_clear_and->out >> *efschz_or->in[3];

    // -----------------------------------------------------------------------
    // Instruction memory
    pc_reg->out >> instr_mem->addr;
    instr_mem->setMemory(m_memory);

    // -----------------------------------------------------------------------
    // Decode
    ifid_reg->instr_out >> decode->instr;

    // -----------------------------------------------------------------------
    // Control signals
    decode->opcode >> control->opcode;

    // -----------------------------------------------------------------------
    // Immediate
    decode->opcode >> immediate->opcode;
    ifid_reg->instr_out >> immediate->instr;

    // -----------------------------------------------------------------------
    // Registers
    decode->r1_reg_idx >> registerFile->r1_addr;
    decode->r2_reg_idx >> registerFile->r2_addr;
    reg_wr_src->out >> registerFile->data_in;

    memwb_reg->wr_reg_idx_out >> registerFile->wr_addr;
    memwb_reg->reg_do_write_out >> registerFile->wr_en;
    memwb_reg->mem_read_out >> reg_wr_src->get(UnifiedRegWrSrc::MEMREAD);
    memwb_reg->alures_out >> reg_wr_src->get(UnifiedRegWrSrc::ALURES);
    memwb_reg->pc4_out >> reg_wr_src->get(UnifiedRegWrSrc::PC4);
    memwb_reg->falures_out >> reg_wr_src->get(UnifiedRegWrSrc::FALURES);
    memwb_reg->reg_wr_src_ctrl_out >> reg_wr_src_adapter->reg_wr_src;
    memwb_reg->fp_reg_do_write_out >> reg_wr_src_adapter->fp_reg_do_write;
    reg_wr_src_adapter->out >> reg_wr_src->select;

    registerFile->setMemory(m_regMem);

    decode->r1_reg_idx >> fRegisterFile->r1_addr;
    decode->r2_reg_idx >> fRegisterFile->r2_addr;
    0 >> fRegisterFile->r3_addr;
    reg_wr_src->out >> fRegisterFile->data_in;
    memwb_reg->wr_reg_idx_out >> fRegisterFile->wr_addr;
    memwb_reg->fp_reg_do_write_out >> fRegisterFile->wr_en;
    fRegisterFile->setMemory(m_fRegMem);
    fRegisterFile->setDisplayName("FP Registers");

    // -----------------------------------------------------------------------
    // Branch
    idex_reg->br_op_out >> branch->comp_op;
    reg1_fw_src->out >> branch->op1;
    reg2_fw_src->out >> branch->op2;

    branch->res >> *br_and->in[0];
    idex_reg->do_br_out >> *br_and->in[1];
    br_and->out >> *controlflow_or->in[0];
    idex_reg->do_jmp_out >> *controlflow_or->in[1];

    pc_4->out >> pc_src->get(PcSrc::PC4);
    alu->res >> pc_src->get(PcSrc::ALU);

    // -----------------------------------------------------------------------
    // ALU

    // Forwarding multiplexers
    idex_reg->r1_out >> reg1_fw_src->get(ForwardingSrc::IdStage);
    exmem_reg->alures_out >>
        reg1_fw_src->get(
            ForwardingSrc::MemStage); // Todo: Mem stage needs a mux to allow
                                      // for AUIPC forwarding
    reg_wr_src->out >> reg1_fw_src->get(ForwardingSrc::WbStage);
    funit->alu_reg1_forwarding_ctrl >> reg1_fw_src->select;

    idex_reg->r2_out >> reg2_fw_src->get(ForwardingSrc::IdStage);
    exmem_reg->alures_out >> reg2_fw_src->get(ForwardingSrc::MemStage);
    reg_wr_src->out >> reg2_fw_src->get(ForwardingSrc::WbStage);
    funit->alu_reg2_forwarding_ctrl >> reg2_fw_src->select;

    // ALU operand multiplexers
    reg1_fw_src->out >> alu_op1_src->get(AluSrc1::REG1);
    idex_reg->pc_out >> alu_op1_src->get(AluSrc1::PC);
    idex_reg->alu_op1_ctrl_out >> alu_op1_src->select;

    reg2_fw_src->out >> alu_op2_src->get(AluSrc2::REG2);
    idex_reg->imm_out >> alu_op2_src->get(AluSrc2::IMM);
    idex_reg->alu_op2_ctrl_out >> alu_op2_src->select;

    alu_op1_src->out >> alu->op1;
    alu_op2_src->out >> alu->op2;

    idex_reg->alu_ctrl_out >> alu->ctrl;

    // -----------------------------------------------------------------------
    // FALU

    idex_reg->f_r1_out >> freg1_fw_src->get(ForwardingSrc::IdStage);
    exmem_reg->falures_out >> freg1_fw_src->get(ForwardingSrc::MemStage);
    reg_wr_src->out >> freg1_fw_src->get(ForwardingSrc::WbStage);
    funit->falu_reg1_forwarding_ctrl >> freg1_fw_src->select;

    idex_reg->f_r2_out >> freg2_fw_src->get(ForwardingSrc::IdStage);
    exmem_reg->falures_out >> freg2_fw_src->get(ForwardingSrc::MemStage);
    reg_wr_src->out >> freg2_fw_src->get(ForwardingSrc::WbStage);
    funit->falu_reg2_forwarding_ctrl >> freg2_fw_src->select;

    freg1_fw_src->out >> falu->op1;
    freg2_fw_src->out >> falu->op2;
    0 >> falu->op3;
    idex_reg->falu_ctrl_out >> falu->ctrl;
    idex_reg->opcode_out >> falu_latency->opcode;
    idex_reg->valid_out >> falu_latency->valid;
    falu_latency_remaining_reg->out >> falu_latency->remaining;
    falu_latency->next_remaining >> falu_latency_remaining_reg->in;
    1 >> falu_latency_remaining_reg->enable;
    efschz_or->out >> falu_latency_remaining_reg->clear;

    // -----------------------------------------------------------------------
    // Data memory
    exmem_reg->alures_out >> data_mem->addr;
    exmem_reg->mem_do_write_out >> data_mem->wr_en;
    exmem_reg->r2_out >> data_mem_wr_src->get(DataMemWrSrc::REG2);
    exmem_reg->f_r2_out >> data_mem_wr_src->get(DataMemWrSrc::FREG2);
    exmem_reg->data_mem_wr_src_ctrl_out >> data_mem_wr_src->select;
    data_mem_wr_src->out >> data_mem->data_in;
    exmem_reg->mem_op_out >> data_mem->op;
    data_mem->mem->setMemory(m_memory);

    // -----------------------------------------------------------------------
    // Ecall checker

    idex_reg->opcode_out >> ecallChecker->opcode;
    ecallChecker->setSyscallCallback(&trapHandler);
    hzunit->stallEcallHandling >> ecallChecker->stallEcallHandling;

    // -----------------------------------------------------------------------
    // IF/ID
    pc_4->out >> ifid_reg->pc4_in;
    pc_reg->out >> ifid_reg->pc_in;
    uncompress->exp_instr >> ifid_reg->instr_in;
    fe_enable_or->out >> ifid_reg->enable;
    efsc_or->out >> ifid_reg->clear;
    1 >> ifid_reg->valid_in; // Always valid unless register is cleared

    // -----------------------------------------------------------------------
    // Increment
    instr_mem->data_out >> uncompress->instr;

    // -----------------------------------------------------------------------
    // ID/EX
    hzunit->hazardIDEXEnable >> idex_reg->enable;
    hzunit->hazardIDEXClear >> idex_reg->stalled_in;
    efschz_or->out >> idex_reg->clear;

    // Data
    ifid_reg->pc4_out >> idex_reg->pc4_in;
    ifid_reg->pc_out >> idex_reg->pc_in;
    registerFile->r1_out >> idex_reg->r1_in;
    registerFile->r2_out >> idex_reg->r2_in;
    fRegisterFile->r1_out >> idex_reg->f_r1_in;
    fRegisterFile->r2_out >> idex_reg->f_r2_in;
    immediate->imm >> idex_reg->imm_in;

    // Control
    decode->wr_reg_idx >> idex_reg->wr_reg_idx_in;
    control->reg_wr_src_ctrl >> idex_reg->reg_wr_src_ctrl_in;
    control->reg_do_write_ctrl >> idex_reg->reg_do_write_in;
    control->alu_op1_ctrl >> idex_reg->alu_op1_ctrl_in;
    control->alu_op2_ctrl >> idex_reg->alu_op2_ctrl_in;
    control->mem_do_write_ctrl >> idex_reg->mem_do_write_in;
    control->alu_ctrl >> idex_reg->alu_ctrl_in;
    control->mem_ctrl >> idex_reg->mem_op_in;
    control->comp_ctrl >> idex_reg->br_op_in;
    control->do_branch >> idex_reg->do_br_in;
    control->do_jump >> idex_reg->do_jmp_in;
    decode->r1_reg_idx >> idex_reg->rd_reg1_idx_in;
    decode->r2_reg_idx >> idex_reg->rd_reg2_idx_in;
    decode->opcode >> idex_reg->opcode_in;
    control->mem_do_read_ctrl >> idex_reg->mem_do_read_in;
    control->fp_reg_do_write_ctrl >> fp_reg_write_gate->fp_reg_do_write;
    decode->opcode >> fp_reg_write_gate->opcode;
    fp_reg_write_gate->out >> idex_reg->fp_reg_do_write_in;
    control->data_mem_wr_src_ctrl >> idex_reg->data_mem_wr_src_ctrl_in;
    control->falu_ctrl >> idex_reg->falu_ctrl_in;

    ifid_reg->valid_out >> idex_reg->valid_in;

    // -----------------------------------------------------------------------
    // EX/MEM
    1 >> exmem_reg->enable;
    hzunit->hazardEXMEMClear >> *exmem_clear_or->in[0];
    0 >> *exmem_clear_or->in[1];
    exmem_clear_or->out >> exmem_reg->clear;
    fp_units->exmem_valid >> exmem_mux->use_fp;
    fp_units->exmem_valid >> *fp_exmem_valid_not->in[0];
    fp_exmem_valid_not->out >> *normal_exmem_stalled_and->in[0];
    idex_reg->stalled_out >> *normal_exmem_stalled_and->in[1];
    hzunit->hazardEXMEMClear >> *mem_stalled_or->in[0];
    0 >> *mem_stalled_or->in[1];
    mem_stalled_or->out >> exmem_reg->stalled_in;

    // Data
    idex_reg->pc_out >> exmem_mux->pc;
    idex_reg->pc4_out >> exmem_mux->pc4;
    reg2_fw_src->out >> exmem_mux->r2;
    alu->res >> exmem_mux->alures;
    freg2_fw_src->out >> exmem_mux->f_r2;
    falu->res >> exmem_mux->falures;
    fp_units->exmem_pc >> exmem_mux->fp_pc;
    fp_units->exmem_pc4 >> exmem_mux->fp_pc4;
    fp_units->exmem_rd >> exmem_mux->fp_rd;
    fp_units->exmem_value >> exmem_mux->fp_value;

    exmem_mux->pc_out >> exmem_reg->pc_in;
    exmem_mux->pc4_out >> exmem_reg->pc4_in;
    exmem_mux->r2_out >> exmem_reg->r2_in;
    exmem_mux->alures_out >> exmem_reg->alures_in;
    exmem_mux->f_r2_out >> exmem_reg->f_r2_in;
    exmem_mux->falures_out >> exmem_reg->falures_in;

    // Control
    idex_reg->reg_wr_src_ctrl_out >> exmem_mux->reg_wr_src_ctrl;
    idex_reg->wr_reg_idx_out >> exmem_mux->wr_reg_idx;
    idex_reg->reg_do_write_out >> exmem_mux->reg_do_write;
    idex_reg->mem_do_write_out >> exmem_mux->mem_do_write;
    idex_reg->mem_do_read_out >> exmem_mux->mem_do_read;
    idex_reg->mem_op_out >> exmem_mux->mem_op;
    idex_reg->fp_reg_do_write_out >> exmem_mux->fp_reg_do_write;
    idex_reg->data_mem_wr_src_ctrl_out >> exmem_mux->data_mem_wr_src_ctrl;
    fp_regular_issue_gate->out >> exmem_mux->valid;
    fp_units->exmem_valid >> exmem_mux->fp_valid;

    exmem_mux->reg_wr_src_ctrl_out >> exmem_reg->reg_wr_src_ctrl_in;
    exmem_mux->wr_reg_idx_out >> exmem_reg->wr_reg_idx_in;
    exmem_mux->reg_do_write_out >> exmem_reg->reg_do_write_in;
    exmem_mux->mem_do_write_out >> exmem_reg->mem_do_write_in;
    exmem_mux->mem_do_read_out >> exmem_reg->mem_do_read_in;
    exmem_mux->mem_op_out >> exmem_reg->mem_op_in;
    exmem_mux->fp_reg_do_write_out >> exmem_reg->fp_reg_do_write_in;
    exmem_mux->data_mem_wr_src_ctrl_out >>
        exmem_reg->data_mem_wr_src_ctrl_in;
    exmem_mux->valid_out >> exmem_reg->valid_in;

    idex_reg->valid_out >> fp_regular_issue_gate->valid;
    fp_units->normal_waw_stall >> fp_regular_issue_gate->blocked;
    idex_reg->opcode_out >> fp_regular_issue_gate->opcode;

    // -----------------------------------------------------------------------
    // MEM/WB

    exmem_reg->stalled_out >> memwb_reg->stalled_in;

    // Data
    exmem_reg->pc_out >> memwb_reg->pc_in;
    exmem_reg->pc4_out >> memwb_reg->pc4_in;
    exmem_reg->alures_out >> memwb_reg->alures_in;
    exmem_reg->falures_out >> memwb_reg->falures_in;
    data_mem->data_out >> memwb_reg->mem_read_in;

    // Control
    exmem_reg->reg_wr_src_ctrl_out >> memwb_reg->reg_wr_src_ctrl_in;
    exmem_reg->wr_reg_idx_out >> memwb_reg->wr_reg_idx_in;
    exmem_reg->reg_do_write_out >> memwb_reg->reg_do_write_in;
    exmem_reg->fp_reg_do_write_out >> memwb_reg->fp_reg_do_write_in;

    exmem_reg->valid_out >> memwb_reg->valid_in;

    // -----------------------------------------------------------------------
    // Forwarding unit
    idex_reg->rd_reg1_idx_out >> funit->id_reg1_idx;
    idex_reg->rd_reg2_idx_out >> funit->id_reg2_idx;

    exmem_reg->wr_reg_idx_out >> funit->mem_reg_wr_idx;
    exmem_reg->reg_do_write_out >> funit->mem_reg_wr_en;

    memwb_reg->wr_reg_idx_out >> funit->wb_reg_wr_idx;
    memwb_reg->reg_do_write_out >> funit->wb_reg_wr_en;

    exmem_reg->fp_reg_do_write_out >> funit->mem_fp_reg_wr_en;

    memwb_reg->fp_reg_do_write_out >> funit->wb_fp_reg_wr_en;

    // -----------------------------------------------------------------------
    // Hazard detection unit
    decode->r1_reg_idx >> hzunit->id_reg1_idx;
    decode->r2_reg_idx >> hzunit->id_reg2_idx;
    decode->opcode >> hzunit->id_opcode;

    idex_reg->mem_do_read_out >> hzunit->ex_do_mem_read_en;
    idex_reg->wr_reg_idx_out >> hzunit->ex_reg_wr_idx;
    idex_reg->reg_do_write_out >> hzunit->ex_do_reg_write_en;
    idex_reg->fp_reg_do_write_out >> hzunit->ex_do_fp_write_en;

    exmem_reg->reg_do_write_out >> hzunit->mem_do_reg_write;

    memwb_reg->reg_do_write_out >> hzunit->wb_do_reg_write;

    idex_reg->opcode_out >> hzunit->opcode;
    0 >> hzunit->falu_stall;
    fp_units->stall >> hzunit->fp_unit_stall;
    fp_units->busy >> hzunit->fp_unit_busy;
    fp_units->normal_waw_stall >> hzunit->fp_normal_waw_stall;
    fp_units->exmem_valid >> *fp_exmem_stall_and->in[0];
    idex_reg->valid_out >> *fp_exmem_stall_and->in[1];
    fp_exmem_stall_and->out >> hzunit->fp_exmem_stall;

    // -----------------------------------------------------------------------
    // Segmented FP functional units
    decode->opcode >> fp_units->id_opcode;
    decode->r1_reg_idx >> fp_units->id_rs1;
    decode->r2_reg_idx >> fp_units->id_rs2;
    decode->wr_reg_idx >> fp_units->id_rd;
    ifid_reg->valid_out >> fp_units->id_valid;
    idex_reg->opcode_out >> fp_units->ex_opcode;
    idex_reg->wr_reg_idx_out >> fp_units->ex_rd;
    idex_reg->valid_out >> fp_units->ex_valid;
    memwb_reg->fp_reg_do_write_out >> fp_units->normal_wb_fp_write;
    memwb_reg->pc_out >> fp_units->normal_wb_pc;
    memwb_reg->wr_reg_idx_out >> fp_units->normal_wb_rd;
    exmem_reg->valid_out >> fp_units->mem_forward_valid;
    exmem_reg->fp_reg_do_write_out >> fp_units->mem_forward_fp_write;
    exmem_reg->mem_do_read_out >> fp_units->mem_forward_is_load;
    exmem_reg->pc_out >> fp_units->mem_forward_pc;
    exmem_reg->wr_reg_idx_out >> fp_units->mem_forward_rd;
    memwb_reg->valid_out >> fp_units->wb_forward_valid;
    memwb_reg->fp_reg_do_write_out >> fp_units->wb_forward_fp_write;
    memwb_reg->wr_reg_idx_out >> fp_units->wb_forward_rd;
  }

  // Design subcomponents
  SUBCOMPONENT(registerFile, TYPE(RegisterFile<XLEN, true>));
  SUBCOMPONENT(fRegisterFile, TYPE(FRegisterFile<XLEN, true>));
  SUBCOMPONENT(alu, TYPE(ALU<XLEN>));
  SUBCOMPONENT(falu, TYPE(FALU<XLEN>));
  SUBCOMPONENT(falu_latency, FALULatency);
  SUBCOMPONENT(fp_units, TYPE(FPFunctionalUnits<XLEN>));
  SUBCOMPONENT(control, Control);
  SUBCOMPONENT(immediate, TYPE(Immediate<XLEN>));
  SUBCOMPONENT(decode, TYPE(Decode<XLEN>));
  SUBCOMPONENT(branch, TYPE(Branch<XLEN>));
  SUBCOMPONENT(pc_4, Adder<XLEN>);
  SUBCOMPONENT(uncompress, TYPE(Uncompress<XLEN>));

  // Registers
  SUBCOMPONENT(pc_reg, RegisterClEn<XLEN>);
  SUBCOMPONENT(falu_latency_remaining_reg, RegisterClEn<8>);

  // Stage seperating registers
  SUBCOMPONENT(ifid_reg, TYPE(IFID<XLEN>));
  SUBCOMPONENT(idex_reg, TYPE(RV5S_IDEX<XLEN>));
  SUBCOMPONENT(exmem_reg, TYPE(RV5S_EXMEM<XLEN>));
  SUBCOMPONENT(memwb_reg, TYPE(RV5S_MEMWB<XLEN>));

  // Multiplexers
  SUBCOMPONENT(reg_wr_src, TYPE(EnumMultiplexer<UnifiedRegWrSrc, XLEN>));
  SUBCOMPONENT(reg_wr_src_adapter, UnifiedRegWrSrcAdapter);
  SUBCOMPONENT(exmem_mux, TYPE(FPExMemMux<XLEN>));
  SUBCOMPONENT(pc_src, TYPE(EnumMultiplexer<PcSrc, XLEN>));
  SUBCOMPONENT(alu_op1_src, TYPE(EnumMultiplexer<AluSrc1, XLEN>));
  SUBCOMPONENT(alu_op2_src, TYPE(EnumMultiplexer<AluSrc2, XLEN>));
  SUBCOMPONENT(reg1_fw_src, TYPE(EnumMultiplexer<ForwardingSrc, XLEN>));
  SUBCOMPONENT(reg2_fw_src, TYPE(EnumMultiplexer<ForwardingSrc, XLEN>));
  SUBCOMPONENT(freg1_fw_src, TYPE(EnumMultiplexer<ForwardingSrc, XLEN>));
  SUBCOMPONENT(freg2_fw_src, TYPE(EnumMultiplexer<ForwardingSrc, XLEN>));
  SUBCOMPONENT(data_mem_wr_src, TYPE(EnumMultiplexer<DataMemWrSrc, XLEN>));
  SUBCOMPONENT(pc_inc, TYPE(EnumMultiplexer<PcInc, XLEN>));

  // Memories
  SUBCOMPONENT(instr_mem, TYPE(ROM<XLEN, c_RVInstrWidth>));
  SUBCOMPONENT(data_mem, TYPE(RVMemory<XLEN, XLEN>));

  // Forwarding & hazard detection units
  SUBCOMPONENT(funit, ForwardingUnit);
  SUBCOMPONENT(hzunit, HazardUnit);
  SUBCOMPONENT(fp_reg_write_gate, FPSegmentedRegWriteGate);
  SUBCOMPONENT(fp_regular_issue_gate, FPRegularIssueGate);

  // Gates
  // True if branch instruction and branch taken
  SUBCOMPONENT(br_and, TYPE(And<1, 2>));
  // True if branch taken or jump instruction
  SUBCOMPONENT(controlflow_or, TYPE(Or<1, 2>));
  // True if controlflow action or performing syscall finishing
  SUBCOMPONENT(efsc_or, TYPE(Or<1, 2>));
  // True if above or stalling due to load-use hazard
  SUBCOMPONENT(efschz_or, TYPE(Or<1, 4>));

  SUBCOMPONENT(fe_enable_or, TYPE(Or<1, 2>));
  SUBCOMPONENT(mem_stalled_or, TYPE(Or<1, 2>));
  SUBCOMPONENT(exmem_clear_or, TYPE(Or<1, 2>));
  SUBCOMPONENT(fp_exmem_stall_and, TYPE(And<1, 2>));
  SUBCOMPONENT(fp_id_issue_clear_and, TYPE(And<1, 2>));
  SUBCOMPONENT(fp_exmem_valid_not, TYPE(Not<1, 1>));
  SUBCOMPONENT(normal_exmem_stalled_and, TYPE(And<1, 2>));

  // Address spaces
  ADDRESSSPACEMM(m_memory);
  ADDRESSSPACE(m_regMem);
  ADDRESSSPACE(m_fRegMem);

  SUBCOMPONENT(ecallChecker, EcallChecker);

  // Ripes interface compliance
  const ProcessorStructure &structure() const override { return m_structure; }
  unsigned int getPcForStage(StageIndex idx) const override {
    // clang-format off
        switch (idx.index()) {
            case IF: return pc_reg->out.uValue();
            case ID: return ifid_reg->pc_out.uValue();
            case EX: {
              if (idx.lane() > 0) {
                const auto slot = fpSlotForLane(idx.lane());
                if (!slot.valid) {
                  return 0;
                }
                const auto view = fp_units->fpUnitView(slot.unit, slot.slot);
                return view.pc;
              }
              return idex_reg->pc_out.uValue();
            }
            case MEM:
              if (idx.lane() > 0) {
                return 0;
              }
              return exmem_reg->pc_out.uValue();
            case WB:
              if (idx.lane() > 0) {
                return 0;
              }
              return memwb_reg->pc_out.uValue();
            default: assert(false && "Processor does not contain stage");
        }
        Q_UNREACHABLE();
    // clang-format on
  }
  AInt nextFetchedAddress() const override { return pc_src->out.uValue(); }
  QString stageName(StageIndex idx) const override {
    // clang-format off
        switch (idx.index()) {
            case IF: return "IF";
            case ID: return "ID";
            case EX: return "EX";
            case MEM: return "MEM";
            case WB: return "WB";
            default: assert(false && "Processor does not contain stage");
        }
        Q_UNREACHABLE();
    // clang-format on
  }
  StageInfo stageInfo(StageIndex stage) const override {
    if (stage.lane() > 0) {
      bool stageValid = stage.index() <= m_cycleCount;
      AInt pc = 0;
      QString namedState;
      switch (stage.index()) {
      case EX: {
        const auto slot = fpSlotForLane(stage.lane());
        if (slot.valid) {
          const auto view = fp_units->fpUnitView(slot.unit, slot.slot);
          if (view.valid) {
            pc = view.pc;
            namedState = QString::fromStdString(view.label);
          } else {
            stageValid = false;
          }
        } else {
          stageValid = false;
        }
        break;
      }
      case MEM:
      case WB:
        stageValid = false;
        break;
      default:
        stageValid = false;
        break;
      }
      stageValid &= isExecutableAddress(pc);
      return StageInfo({pc, stageValid, StageInfo::State::None, namedState});
    }

    bool stageValid = true;
    // Has the pipeline stage been filled?
    stageValid &= stage.index() <= m_cycleCount;

    // clang-format off
        // Has the stage been cleared?
        switch(stage.index()){
        case ID: stageValid &= ifid_reg->valid_out.uValue(); break;
        case EX:
          stageValid &= idex_reg->valid_out.uValue() &&
                        !isSegmentedFPInstr(
                            idex_reg->opcode_out.eValue<RVInstr>());
          break;
        case MEM: stageValid &= exmem_reg->valid_out.uValue(); break;
        case WB:
          stageValid &= memwb_reg->valid_out.uValue();
          break;
        default: case IF: break;
        }

        // Is the stage carrying a valid (executable) PC?
        switch(stage.index()){
        case ID: stageValid &= isExecutableAddress(ifid_reg->pc_out.uValue()); break;
        case EX: stageValid &= isExecutableAddress(idex_reg->pc_out.uValue()); break;
        case MEM: stageValid &= isExecutableAddress(exmem_reg->pc_out.uValue()); break;
        case WB:
          stageValid &= isExecutableAddress(memwb_reg->pc_out.uValue());
          break;
        default: case IF: stageValid &= isExecutableAddress(pc_reg->out.uValue()); break;
        }

        // Are we currently clearing the pipeline due to a syscall exit?
        // if such, all stages before the EX stage are invalid
        if(stage.index() < EX){
            stageValid &= !ecallChecker->isSysCallExiting();
        }
    // clang-format on

    // Gather stage state info
    StageInfo::State state = StageInfo ::State::None;
    QString namedState;
    switch (stage.index()) {
    case IF:
      break;
    case ID:
      if (m_cycleCount > ID && ifid_reg->valid_out.uValue() == 0) {
        state = StageInfo::State::Flushed;
      }
      break;
    case EX: {
      if (stageValid) {
        namedState = fpEXStageName();
      }
      if (idex_reg->stalled_out.uValue() == 1 &&
          !isSegmentedFPInstr(idex_reg->opcode_out.eValue<RVInstr>()) &&
          !fp_units->busy.uValue()) {
        state = StageInfo::State::Stalled;
      } else if (m_cycleCount > EX && idex_reg->valid_out.uValue() == 0) {
        state = StageInfo::State::Flushed;
      }
      break;
    }
    case MEM: {
      if (exmem_reg->stalled_out.uValue() == 1) {
        state = StageInfo::State::Stalled;
      } else if (m_cycleCount > MEM && exmem_reg->valid_out.uValue() == 0) {
        state = StageInfo::State::Flushed;
      }
      break;
    }
    case WB: {
      if (memwb_reg->stalled_out.uValue() == 1) {
        state = StageInfo::State::Stalled;
      } else if (m_cycleCount > WB && memwb_reg->valid_out.uValue() == 0) {
        state = StageInfo::State::Flushed;
      }
      break;
    }
    }

    return StageInfo({getPcForStage(stage), stageValid, state, namedState});
  }

  void setProgramCounter(AInt address) override {
    pc_reg->forceValue(0, address);
    propagateDesign();
  }
  void setPCInitialValue(AInt address) override {
    pc_reg->setInitValue(address);
  }
  AddressSpaceMM &getMemory() override { return *m_memory; }
  VInt getRegister(const std::string_view &regFile, unsigned i) const override {
    if (regFile == RVISA::FPR) {
      return fRegisterFile->getRegister(i);
    }
    return registerFile->getRegister(i);
  }
  void finalize(FinalizeReason fr) override {
    if ((fr & FinalizeReason::exitSyscall) &&
        !ecallChecker->isSysCallExiting()) {
      // An exit system call was executed. Record the cycle of the execution,
      // and enable the ecallChecker's system call exiting signal.
      m_syscallExitCycle = m_cycleCount;
    }
    ecallChecker->setSysCallExiting(ecallChecker->isSysCallExiting() ||
                                    (fr & FinalizeReason::exitSyscall));
  }
  const std::vector<StageIndex> breakpointTriggeringStages() const override {
    return {{0, IF}};
  }

  MemoryAccess dataMemAccess() const override {
    return memToAccessInfo(data_mem);
  }
  MemoryAccess instrMemAccess() const override {
    auto instrAccess = memToAccessInfo(instr_mem);
    instrAccess.type = MemoryAccess::Read;
    return instrAccess;
  }

  bool finished() const override {
    // The processor is finished when there are no more valid instructions in
    // the pipeline
    bool allStagesInvalid = true;
    for (int stage = IF; stage < STAGECOUNT; stage++) {
      allStagesInvalid &= !stageInfo({0, stage}).stage_valid;
      if (!allStagesInvalid)
        break;
    }
    return allStagesInvalid && !fp_units->busy.uValue();
  }

  void setRegister(const std::string_view &regFile, unsigned i, VInt v) override {
    if (regFile == RVISA::FPR) {
      setSynchronousValue(fRegisterFile->_wr_mem, i, v);
      return;
    }
    setSynchronousValue(registerFile->_wr_mem, i, v);
  }

  void clockProcessor() override {
    fp_units->beginCycle();

    // An instruction has been retired if the instruction in the WB stage is
    // valid and the PC is within the executable range of the program
    const bool regularRetired =
        memwb_reg->valid_out.uValue() != 0 &&
        isExecutableAddress(memwb_reg->pc_out.uValue());
    if (regularRetired) {
      m_instructionsRetired++;
    }
    m_regularRetiredHistory.push_back(regularRetired);

    const auto fpReadySequence = fp_units->readyExMemSequence();
    const bool normalExMemCandidate =
        fp_regular_issue_gate->out.uValue() &&
        isExecutableAddress(idex_reg->pc_out.uValue());
    const auto idexSequence = currentIDEXSequence();
    const bool normalWinsExMem =
        normalExMemCandidate && idexSequence && fpReadySequence &&
        *idexSequence < *fpReadySequence;
    const bool fpExMemAccepted =
        fpReadySequence.has_value() && !hzunit->hazardEXMEMClear.uValue() &&
        !normalWinsExMem;

    if (fpExMemAccepted) {
      fp_units->consumeExMem();
    }

    // Make the just-selected FP completion visible to the EX/MEM register
    // inputs before Design::clock() snapshots registered values.
    Design::propagate();

    fp_units->advance();

    // Advancing A/M/D may free A1/M1/D1. Re-propagate before checking whether
    // the current ID instruction can enter a FP unit in this same cycle.
    Design::propagate();

    const bool fpIssueAccepted = !fp_units->exmem_valid.uValue();
    const bool allowFPIDIssue = fp_units->id_issue_ready.uValue();

    const auto fpIssue = makeFPIssue();
    const auto fpIdIssue = allowFPIDIssue ? makeFPIDIssue() : FPIssue{};

    if (fpIdIssue.valid) {
      fp_units->insert(fpIdIssue.opcode, fpIdIssue.rd, fpIdIssue.rs1,
                       fpIdIssue.rs2, fpIdIssue.op1Value, fpIdIssue.op2Value,
                       fpIdIssue.pc, fpIdIssue.sequence, false);
    } else if (fpIssueAccepted && fpIssue.valid) {
      fp_units->insert(fpIssue.opcode, fpIssue.rd, fpIssue.rs1, fpIssue.rs2,
                       fpIssue.op1Value, fpIssue.op2Value, fpIssue.pc,
                       fpIssue.sequence);
    }

    // FP issue changes fe_issue_accepted/issue_accepted, which drive PC,
    // IF/ID and ID/EX enables/clears. Propagate before the clock edge so the
    // instruction cannot remain in the normal pipe and be issued again.
    Design::propagate();

    const bool ifidEnable = fe_enable_or->out.uValue();
    const bool ifidClear = efsc_or->out.uValue();
    const bool idexEnable = hzunit->hazardIDEXEnable.uValue();
    const bool idexClear = efschz_or->out.uValue();
    const bool exmemClear = exmem_clear_or->out.uValue();
    const bool fpLatchedForExMem = fp_units->exmem_valid.uValue();
    const auto fpLatchedSequence = fp_units->latchedExMemSequence();
    const bool normalLatchedForExMem =
        fp_regular_issue_gate->out.uValue() &&
        isExecutableAddress(idex_reg->pc_out.uValue());
    const bool fetchesExecutable =
        isExecutableAddress(pc_reg->out.uValue());
    const bool exmemWasValid = exmem_reg->valid_out.uValue();

    savePipelineSequences();

    Design::clock();

    advancePipelineSequences(ifidEnable, ifidClear, idexEnable, idexClear,
                             exmemClear, fpLatchedForExMem,
                             fpLatchedSequence, normalLatchedForExMem,
                             fetchesExecutable, exmemWasValid);

    debugDumpFpPipeline();

    fp_units->clearExMemLatch();
  }

  void reverse() override {
    if (m_syscallExitCycle != -1 && m_cycleCount == m_syscallExitCycle) {
      // We are about to undo an exit syscall instruction. In this case, the
      // syscall exiting sequence should be terminate
      ecallChecker->setSysCallExiting(false);
      m_syscallExitCycle = -1;
    }
    Design::reverse();
    fp_units->reverse();
    reversePipelineSequences();
    if (!m_regularRetiredHistory.empty()) {
      if (m_regularRetiredHistory.back()) {
        m_instructionsRetired--;
      }
      m_regularRetiredHistory.pop_back();
    }
  }

  void reset() override {
    ecallChecker->setSysCallExiting(false);
    fp_units->reset();
    Design::reset();
    m_syscallExitCycle = -1;
    m_regularRetiredHistory.clear();
    resetPipelineSequences();
  }

  static ProcessorISAInfo supportsISA() { return RVISA::supportsISA<XLEN>(); }
  std::shared_ptr<ISAInfoBase> implementsISA() const override {
    return m_enabledISA;
  }
  std::shared_ptr<const ISAInfoBase> fullISA() const override {
    return RVISA::fullISA<XLEN>();
  }

  const std::set<std::string_view> registerFiles() const override {
    std::set<std::string_view> rfs;
    rfs.insert(RVISA::GPR);

    if (implementsISA()->extensionEnabled("F")) {
      rfs.insert(RVISA::FPR);
    }
    return rfs;
  }

private:
  struct PipelineSequenceSnapshot {
    uint64_t nextInstructionSequence = 0;
    std::optional<uint64_t> ifidSequence;
    std::optional<uint64_t> idexSequence;
    std::optional<uint64_t> exmemSequence;
    std::optional<uint64_t> memwbSequence;
    std::unordered_map<VSRTL_VT_U, uint64_t> latestSequenceForPc;
  };

  uint64_t nextFallbackSequence() const { return m_nextInstructionSequence; }

  std::optional<uint64_t> sequenceForPc(VSRTL_VT_U pc) const {
    const auto it = m_latestSequenceForPc.find(pc);
    if (it == m_latestSequenceForPc.end()) {
      return std::nullopt;
    }
    return it->second;
  }

  std::optional<uint64_t> currentIDEXSequence() const {
    if (m_idexSequence) {
      return m_idexSequence;
    }
    if (!idex_reg->valid_out.uValue()) {
      return std::nullopt;
    }
    return sequenceForPc(idex_reg->pc_out.uValue());
  }

  void savePipelineSequences() {
    m_pipelineSequenceHistory.push_back(
        {m_nextInstructionSequence, m_ifidSequence, m_idexSequence,
         m_exmemSequence, m_memwbSequence, m_latestSequenceForPc});
  }

  void reversePipelineSequences() {
    if (m_pipelineSequenceHistory.empty()) {
      return;
    }
    const auto snapshot = m_pipelineSequenceHistory.back();
    m_nextInstructionSequence = snapshot.nextInstructionSequence;
    m_ifidSequence = snapshot.ifidSequence;
    m_idexSequence = snapshot.idexSequence;
    m_exmemSequence = snapshot.exmemSequence;
    m_memwbSequence = snapshot.memwbSequence;
    m_latestSequenceForPc = snapshot.latestSequenceForPc;
    m_pipelineSequenceHistory.pop_back();
  }

  void resetPipelineSequences() {
    m_nextInstructionSequence = 0;
    m_ifidSequence.reset();
    m_idexSequence.reset();
    m_exmemSequence.reset();
    m_memwbSequence.reset();
    m_latestSequenceForPc.clear();
    m_pipelineSequenceHistory.clear();
  }

  void advancePipelineSequences(
      bool ifidEnable, bool ifidClear, bool idexEnable, bool idexClear,
      bool exmemClear, bool fpLatchedForExMem,
      std::optional<uint64_t> fpLatchedSequence, bool normalLatchedForExMem,
      bool fetchesExecutable, bool exmemWasValid) {
    const auto oldIfidSequence = m_ifidSequence;
    const auto oldIdexSequence = m_idexSequence;
    const auto oldExmemSequence = m_exmemSequence;

    m_memwbSequence = exmemWasValid ? oldExmemSequence : std::nullopt;

    if (exmemClear) {
      m_exmemSequence.reset();
    } else if (fpLatchedForExMem) {
      m_exmemSequence = fpLatchedSequence;
    } else if (normalLatchedForExMem) {
      m_exmemSequence = oldIdexSequence;
    } else {
      m_exmemSequence.reset();
    }

    if (idexClear) {
      m_idexSequence.reset();
    } else if (idexEnable) {
      m_idexSequence = oldIfidSequence;
    }

    if (ifidClear) {
      m_ifidSequence.reset();
    } else if (ifidEnable) {
      if (fetchesExecutable) {
        const auto sequence = m_nextInstructionSequence++;
        m_ifidSequence = sequence;
        m_latestSequenceForPc[pc_reg->out.uValue()] = sequence;
      } else {
        m_ifidSequence.reset();
      }
    }
  }

  void debugDumpFpPipeline() const {
    std::cerr << "[fp-pipe] cycle=" << m_cycleCount
              << " fp_ready=" << fp_units->hasReadyExMemEntry()
              << " fp_exmem_valid=" << fp_units->exmem_valid.uValue()
              << " id_issue_ready=" << fp_units->id_issue_ready.uValue()
              << '\n';

    std::cerr << "    IFID pc=0x" << std::hex << ifid_reg->pc_out.uValue()
              << std::dec << " valid=" << ifid_reg->valid_out.uValue()
              << '\n';
    std::cerr << "    IDEX pc=0x" << std::hex << idex_reg->pc_out.uValue()
              << std::dec << " valid=" << idex_reg->valid_out.uValue()
              << " stalled=" << idex_reg->stalled_out.uValue()
              << " rd=" << idex_reg->wr_reg_idx_out.uValue() << '\n';
    std::cerr << "    EXMEM pc=0x" << std::hex << exmem_reg->pc_out.uValue()
              << std::dec << " valid=" << exmem_reg->valid_out.uValue()
              << " stalled=" << exmem_reg->stalled_out.uValue()
              << " rd=" << exmem_reg->wr_reg_idx_out.uValue()
              << " fp_wr=" << exmem_reg->fp_reg_do_write_out.uValue()
              << '\n';
    std::cerr << "    MEMWB pc=0x" << std::hex << memwb_reg->pc_out.uValue()
              << std::dec << " valid=" << memwb_reg->valid_out.uValue()
              << " stalled=" << memwb_reg->stalled_out.uValue()
              << " rd=" << memwb_reg->wr_reg_idx_out.uValue()
              << " fp_wr=" << memwb_reg->fp_reg_do_write_out.uValue()
              << '\n';
    fp_units->debugDump(std::cerr);
  }

  struct FPSlotLane {
    bool valid = false;
    unsigned unit = 0;
    unsigned slot = 0;
  };

  unsigned fpAddFirstLane() const { return 1; }
  unsigned fpMulFirstLane() const { return fpAddFirstLane() + m_fpAddLanes; }
  unsigned fpDivFirstLane() const { return fpMulFirstLane() + m_fpMulLanes; }
  unsigned fpLaneCount() const {
    return m_fpAddLanes + m_fpMulLanes + m_fpDivLanes;
  }

  FPSlotLane fpSlotForLane(unsigned lane) const {
    if (lane >= fpAddFirstLane() && lane < fpMulFirstLane()) {
      return {true, FPFunctionalUnits<XLEN>::unitIndexForOpcode(RVInstr::FADD),
              lane - fpAddFirstLane()};
    }
    if (lane >= fpMulFirstLane() && lane < fpDivFirstLane()) {
      return {true, FPFunctionalUnits<XLEN>::unitIndexForOpcode(RVInstr::FMUL),
              lane - fpMulFirstLane()};
    }
    if (lane >= fpDivFirstLane() &&
        lane < fpDivFirstLane() + m_fpDivLanes) {
      return {true, FPFunctionalUnits<XLEN>::unitIndexForOpcode(RVInstr::FDIV),
              lane - fpDivFirstLane()};
    }
    return {};
  }

  void rebuildStructure() {
    m_fpAddLanes = std::max(
        1u, RipesSettings::value(RIPES_SETTING_RV5S_FALU_ADDSUB_LATENCY)
                .toUInt());
    m_fpMulLanes = std::max(
        1u, RipesSettings::value(RIPES_SETTING_RV5S_FALU_MUL_LATENCY).toUInt());
    m_fpDivLanes = std::max(
        1u, RipesSettings::value(RIPES_SETTING_RV5S_FALU_DIV_LATENCY).toUInt());

    m_structure.clear();
    m_structure[0] = 5;
    for (unsigned lane = 1; lane <= fpLaneCount(); ++lane) {
      m_structure[lane] = 5;
    }
  }

  struct FPIssue {
    bool valid = false;
    RVInstr opcode = RVInstr::NOP;
    unsigned rd = 0;
    unsigned rs1 = 0;
    unsigned rs2 = 0;
    VSRTL_VT_U op1Value = 0;
    VSRTL_VT_U op2Value = 0;
    VSRTL_VT_U pc = 0;
    uint64_t sequence = 0;
  };

  VSRTL_VT_U fpOperandValue(unsigned reg) const {
    if (fp_units->exmem_valid.uValue() &&
        fp_units->exmem_rd.uValue() == reg) {
      return fp_units->exmem_value.uValue();
    }
    if (exmem_reg->valid_out.uValue() &&
        exmem_reg->fp_reg_do_write_out.uValue() &&
        exmem_reg->wr_reg_idx_out.uValue() == reg) {
      if (exmem_reg->mem_do_read_out.uValue()) {
        return data_mem->data_out.uValue();
      }
      return exmem_reg->falures_out.uValue();
    }
    if (memwb_reg->valid_out.uValue() &&
        memwb_reg->fp_reg_do_write_out.uValue() &&
        memwb_reg->wr_reg_idx_out.uValue() == reg) {
      return reg_wr_src->out.uValue();
    }
    return fRegisterFile->getRegister(reg);
  }

  FPIssue makeFPIssue() const {
    const auto opcode = idex_reg->opcode_out.eValue<RVInstr>();
    if (!idex_reg->valid_out.uValue() || !isSegmentedFPInstr(opcode) ||
        !isExecutableAddress(idex_reg->pc_out.uValue()) ||
        fp_units->containsPc(idex_reg->pc_out.uValue())) {
      return {};
    }

    const auto rs1 = static_cast<unsigned>(idex_reg->rd_reg1_idx_out.uValue());
    const auto rs2 = static_cast<unsigned>(idex_reg->rd_reg2_idx_out.uValue());
    return {true,
            opcode,
            static_cast<unsigned>(idex_reg->wr_reg_idx_out.uValue()),
            rs1,
            rs2,
            fpOperandValue(rs1),
            fpOperandValue(rs2),
            idex_reg->pc_out.uValue(),
            currentIDEXSequence().value_or(nextFallbackSequence())};
  }

  FPIssue makeFPIDIssue() const {
    const auto opcode = decode->opcode.eValue<RVInstr>();
    if (!ifid_reg->valid_out.uValue() || !isSegmentedFPInstr(opcode) ||
        !isExecutableAddress(ifid_reg->pc_out.uValue()) ||
        fp_units->containsPc(ifid_reg->pc_out.uValue())) {
      return {};
    }

    const auto rs1 = static_cast<unsigned>(decode->r1_reg_idx.uValue());
    const auto rs2 = static_cast<unsigned>(decode->r2_reg_idx.uValue());
    if (!fp_units->canIssueNow(opcode, rs1, rs2)) {
      return {};
    }

    return {true,
            opcode,
            static_cast<unsigned>(decode->wr_reg_idx.uValue()),
            rs1,
            rs2,
            fpOperandValue(rs1),
            fpOperandValue(rs2),
            ifid_reg->pc_out.uValue(),
            m_ifidSequence.value_or(
                sequenceForPc(ifid_reg->pc_out.uValue())
                    .value_or(nextFallbackSequence()))};
  }

  QString fpEXStageName() const {
    if (isSegmentedFPInstr(idex_reg->opcode_out.eValue<RVInstr>())) {
      return "";
    }

    const unsigned cycle = falu_latency->current_cycle.uValue();
    if (cycle == 0) {
      return "";
    }

    switch (idex_reg->opcode_out.eValue<RVInstr>()) {
    case RVInstr::FADD:
    case RVInstr::FSUB:
      return "A" + QString::number(cycle);
    case RVInstr::FMUL:
      return "M" + QString::number(cycle);
    case RVInstr::FDIV:
      return "D" + QString::number(cycle);
    default:
      return "";
    }
  }

  /**
   * @brief m_syscallExitCycle
   * The variable will contain the cycle of which an exit system call was
   * executed. From this, we may determine when we roll back an exit system call
   * during rewinding.
   */
  long long m_syscallExitCycle = -1;
  std::vector<bool> m_regularRetiredHistory;
  uint64_t m_nextInstructionSequence = 0;
  std::optional<uint64_t> m_ifidSequence;
  std::optional<uint64_t> m_idexSequence;
  std::optional<uint64_t> m_exmemSequence;
  std::optional<uint64_t> m_memwbSequence;
  std::unordered_map<VSRTL_VT_U, uint64_t> m_latestSequenceForPc;
  std::vector<PipelineSequenceSnapshot> m_pipelineSequenceHistory;
  std::shared_ptr<ISAInfoBase> m_enabledISA;
  unsigned m_fpAddLanes = 4;
  unsigned m_fpMulLanes = 7;
  unsigned m_fpDivLanes = 25;
  ProcessorStructure m_structure;
};

} // namespace core
} // namespace vsrtl
