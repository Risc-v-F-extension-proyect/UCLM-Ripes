#pragma once

#include "VSRTL/core/vsrtl_adder.h"
#include "VSRTL/core/vsrtl_constant.h"
#include "VSRTL/core/vsrtl_design.h"
#include "VSRTL/core/vsrtl_logicgate.h"
#include "VSRTL/core/vsrtl_multiplexer.h"

#include "../../ripesvsrtlprocessor.h"

// Functional units
#include "processors/RISC-V/riscv.h"
#include "processors/RISC-V/rv_alu.h"
#include "processors/RISC-V/rv_branch.h"
#include "processors/RISC-V/rv_control.h"
#include "processors/RISC-V/rv_decode.h"
#include "processors/RISC-V/rv_ecallchecker.h"
#include "processors/RISC-V/rv_falu.h"
#include "processors/RISC-V/rv_fregisterfile.h"
#include "processors/RISC-V/rv_immediate.h"
#include "processors/RISC-V/rv_memory.h"
#include "processors/RISC-V/rv_registerfile.h"
#include "processors/RISC-V/rv_uncompress.h"
#include "processors/RISC-V/rv_unified_reg_wr_src_adapter.h"
#include "ripessettings.h"

// Stage separating registers
#include "../rv5s_no_fw_hz/rv5s_no_fw_hz_ifid.h"
#include "../rv5s/rv5s_exmem.h"
#include "../rv5s/rv5s_idex.h"
#include "../rv5s/rv5s_memwb.h"

// Forwarding & Hazard detection unit
#include "rv5s_1s_forwardingunit.h"
#include "rv5s_1s_hazardunit.h"

#include <deque>

namespace vsrtl {
namespace core {
using namespace Ripes;

template <unsigned XLEN>
class RV5S1SAddressAdapter : public Component {
public:
  RV5S1SAddressAdapter(const std::string &name, SimComponent *parent)
      : Component(name, parent) {
    out << [this] { return in.uValue(); };
  }

  INPUTPORT(in, XLEN);
  OUTPUTPORT(out, 64);
};

template <typename XLEN_T>
class RV5S_1S : public RipesVSRTLProcessor {
  static_assert(std::is_same<uint32_t, XLEN_T>::value ||
                    std::is_same<uint64_t, XLEN_T>::value,
                "Only supports 32- and 64-bit variants");
  static constexpr unsigned XLEN = sizeof(XLEN_T) * CHAR_BIT;

public:
  enum Stage { IF = 0, ID = 1, EX = 2, MEM = 3, WB = 4, STAGECOUNT };
  RV5S_1S(const QStringList &extensions)
      : RipesVSRTLProcessor("5-Stage RISC-V Processor (1-slot predict-not-taken)") {
    m_enabledISA = ISAInfoRegistry::getISA<XLenToRVISA<XLEN>()>(extensions);
    decode->setISA(m_enabledISA);
    uncompress->setISA(m_enabledISA);
    0 >> exmem_reg->do_branch_in;
    0 >> exmem_reg->control_flow_in;
    const bool fpEnabled = extensions.contains("F");
    idex_reg->setFPExtensionEnabled(fpEnabled);
    exmem_reg->setFPExtensionEnabled(fpEnabled);
    hzunit->setFPExtensionEnabled(fpEnabled);

    hzunit->setConfiguration(
        RipesSettings::value(
            RIPES_SETTING_RV5S_FALU_ADDSUB_LATENCY).toUInt(),
        RipesSettings::value(
            RIPES_SETTING_RV5S_FALU_MUL_LATENCY).toUInt(),
        RipesSettings::value(
            RIPES_SETTING_RV5S_FALU_DIV_LATENCY).toUInt(),

        RipesSettings::value(
            RIPES_SETTING_RV5S_FALU_ADDSUB_COUNT).toUInt(),
        RipesSettings::value(
            RIPES_SETTING_RV5S_FALU_MUL_COUNT).toUInt(),
        RipesSettings::value(
            RIPES_SETTING_RV5S_FALU_DIV_COUNT).toUInt(),

        RipesSettings::value(
            RIPES_SETTING_RV5S_FALU_ADDSUB_PIPELINED).toBool(),
        RipesSettings::value(
            RIPES_SETTING_RV5S_FALU_MUL_PIPELINED).toBool(),
        RipesSettings::value(
            RIPES_SETTING_RV5S_FALU_DIV_PIPELINED).toBool()
    );

    // -----------------------------------------------------------------------
    // Program counter
    pc_reg->out >> pc_4->op1;
    pc_inc->out >> pc_4->op2;
    pc_src->out >> pc_reg->in;
    0 >> pc_reg->clear;
    hzunit->hazardFEEnable >> pc_reg->enable;

    2 >> pc_inc->get(PcInc::INC2);
    4 >> pc_inc->get(PcInc::INC4);
    uncompress->Pc_Inc >> pc_inc->select;

    // Note: pc_src works uses the PcSrc enum, but is selected by the boolean
    // signal from the controlflow OR gate. PcSrc enum values must adhere to the
    // boolean 0/1 values.
    controlflow_or->out >> pc_src->select;

    controlflow_or->out >> *efsc_or->in[0];
    ecallChecker->syscallExit >> *efsc_or->in[1];

    // MODIFIED: disconnect efsc_or from efschz_or
    // efsc_or->out >> *efschz_or->in[0];
    ecallChecker->syscallExit >> *efschz_or->in[0];
    hzunit->setSquashIDCondition(
        [this] { return ecallChecker->isSysCallExiting(); });
    hzunit->hazardIDEXClear >> *efschz_or->in[1];

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
    registerFile->r1_out >> branch_op1_src->get(ForwardingSrc_1S::IdStage);
    exmem_reg->alures_out >> branch_op1_src->get(ForwardingSrc_1S::MemStage);
    reg_wr_src->out >> branch_op1_src->get(ForwardingSrc_1S::WbStage);
    funit->branch_op1_fwctrl >> branch_op1_src->select;

    registerFile->r2_out >> branch_op2_src->get(ForwardingSrc_1S::IdStage);
    exmem_reg->alures_out >> branch_op2_src->get(ForwardingSrc_1S::MemStage);
    reg_wr_src->out >> branch_op2_src->get(ForwardingSrc_1S::WbStage);
    funit->branch_op2_fwctrl >> branch_op2_src->select;

    ifid_reg->pc_out >> jump_addr_src->get(AluSrc1::PC);
    branch_op1_src->out >> jump_addr_src->get(AluSrc1::REG1);
    control->alu_op1_ctrl >> jump_addr_src->select;

    control->comp_ctrl >> branch->comp_op;
    branch_op1_src->out >> branch->op1;
    branch_op2_src->out >> branch->op2;

    branch->res >> *br_and->in[0];
    control->do_branch >> *br_and->in[1];
    br_and->out >> *controlflow_or->in[0];
    control->do_jump >> *controlflow_or->in[1];

    pc_4->out >> pc_src->get(PcSrc::PC4);
    jump_addr_src->out >> branch_adder->op1;
    immediate->imm >> branch_adder->op2;
    branch_adder->out >> pc_src->get(PcSrc::ALU);

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

    idex_reg->f_r1_out >> freg1_fw_src->get(FPForwardingSrc::IdStage);
    reg_wr_src->out >> freg1_fw_src->get(FPForwardingSrc::WbStage);
    funit->falu_reg1_forwarding_ctrl >> freg1_fw_src->select;

    idex_reg->f_r2_out >> freg2_fw_src->get(FPForwardingSrc::IdStage);
    reg_wr_src->out >> freg2_fw_src->get(FPForwardingSrc::WbStage);
    funit->falu_reg2_forwarding_ctrl >> freg2_fw_src->select;

    freg1_fw_src->out >> falu->op1;
    freg2_fw_src->out >> falu->op2;
    0 >> falu->op3;
    idex_reg->falu_ctrl_out >> falu->ctrl;

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
    hzunit->hazardFEEnable >> ifid_reg->enable;
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
    control->fp_reg_do_write_ctrl >> idex_reg->fp_reg_do_write_in;
    control->data_mem_wr_src_ctrl >> idex_reg->data_mem_wr_src_ctrl_in;
    control->falu_ctrl >> idex_reg->falu_ctrl_in;
    hzunit->emissionCycle >> idex_reg->emissionCycle_in;

    ifid_reg->valid_out >> idex_reg->valid_in;

    // -----------------------------------------------------------------------
    // EX/MEM
    1 >> exmem_reg->enable;
    hzunit->hazardEXMEMClear >> exmem_reg->clear;
    hzunit->hazardEXMEMClear >> *mem_stalled_or->in[0];
    idex_reg->stalled_out >> *mem_stalled_or->in[1];
    mem_stalled_or->out >> exmem_reg->stalled_in;

    // Data
    idex_reg->pc_out >> exmem_reg->pc_in;
    idex_reg->pc4_out >> exmem_reg->pc4_in;
    reg2_fw_src->out >> exmem_reg->r2_in;
    alu->res >> exmem_reg->alures_in;
    freg2_fw_src->out >> exmem_reg->f_r2_in;
    falu->res >> exmem_reg->falures_in;
    falu->res >> idex_reg->falu_result_in;
    0 >> idex_reg->badPrediction;

    // Control
    idex_reg->reg_wr_src_ctrl_out >> exmem_reg->reg_wr_src_ctrl_in;
    idex_reg->wr_reg_idx_out >> exmem_reg->wr_reg_idx_in;
    idex_reg->reg_do_write_out >> exmem_reg->reg_do_write_in;
    idex_reg->mem_do_write_out >> exmem_reg->mem_do_write_in;
    idex_reg->mem_do_read_out >> exmem_reg->mem_do_read_in;
    idex_reg->mem_op_out >> exmem_reg->mem_op_in;
    idex_reg->fp_reg_do_write_out >> exmem_reg->fp_reg_do_write_in;
    idex_reg->data_mem_wr_src_ctrl_out >>
        exmem_reg->data_mem_wr_src_ctrl_in;
    idex_reg->emissionCycle_out >> exmem_reg->emissionCycle_in;

    idex_reg->valid_out >> exmem_reg->valid_in;

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
    exmem_reg->mem_op_out >> memwb_reg->mem_op_in;

    exmem_reg->valid_out >> memwb_reg->valid_in;

    // -----------------------------------------------------------------------
    // Forwarding unit
    decode->r1_reg_idx >> funit->if_reg1_idx;
    decode->r2_reg_idx >> funit->if_reg2_idx;
    idex_reg->rd_reg1_idx_out >> funit->id_reg1_idx;
    idex_reg->rd_reg2_idx_out >> funit->id_reg2_idx;

    idex_reg->wr_reg_idx_out >> funit->ex_reg_wr_idx;
    idex_reg->reg_do_write_out >> funit->ex_reg_wr_en;

    exmem_reg->wr_reg_idx_out >> funit->mem_reg_wr_idx;
    exmem_reg->reg_do_write_out >> funit->mem_reg_wr_en;

    memwb_reg->wr_reg_idx_out >> funit->wb_reg_wr_idx;
    memwb_reg->reg_do_write_out >> funit->wb_reg_wr_en;
    memwb_reg->mem_op_out >> funit->wb_mem_op;

    // -----------------------------------------------------------------------
    // Hazard detection unit
    decode->r1_reg_idx >> hzunit->id_reg1_idx;
    decode->r2_reg_idx >> hzunit->id_reg2_idx;
    decode->wr_reg_idx >> hzunit->id_reg_wr_idx;
    ifid_reg->pc_out >> hazard_pc_adapter->in;
    hazard_pc_adapter->out >> hzunit->id_pc;
    decode->opcode >> hzunit->id_opcode;
    ifid_reg->valid_out >> hzunit->id_valid;

    idex_reg->mem_do_read_out >> hzunit->ex_do_mem_read_en;
    idex_reg->wr_reg_idx_out >> hzunit->ex_reg_wr_idx;
    idex_reg->reg_do_write_out >> hzunit->ex_do_reg_write;
    idex_reg->reg_do_write_out >> hzunit->ex_do_reg_write_en;
    idex_reg->fp_reg_do_write_out >> hzunit->ex_do_fp_write_en;

    exmem_reg->reg_do_write_out >> hzunit->mem_do_reg_write;
    exmem_reg->wr_reg_idx_out >> hzunit->mem_reg_wr_idx;
    exmem_reg->mem_do_read_out >> hzunit->mem_do_mem_read_en;
    exmem_reg->fp_reg_do_write_out >> hzunit->mem_do_fp_write;

    memwb_reg->reg_do_write_out >> hzunit->wb_do_reg_write;
    memwb_reg->fp_reg_do_write_out >> hzunit->wb_do_fp_write;

    idex_reg->opcode_out >> hzunit->opcode;
    0 >> hzunit->branchTakenFromMEM;
    control->do_branch >> hzunit->id_do_branch;
    control->do_jump >> hzunit->id_do_jump;
  }

  // Design subcomponents
  SUBCOMPONENT(registerFile, TYPE(RegisterFile<XLEN, true>));
  SUBCOMPONENT(fRegisterFile, TYPE(FRegisterFile<XLEN, true>));
  SUBCOMPONENT(alu, TYPE(ALU<XLEN>));
  SUBCOMPONENT(falu, TYPE(FALU<XLEN>));
  SUBCOMPONENT(control, Control);
  SUBCOMPONENT(immediate, TYPE(Immediate<XLEN>));
  SUBCOMPONENT(decode, TYPE(Decode<XLEN>));
  SUBCOMPONENT(branch, TYPE(Branch<XLEN>));
  SUBCOMPONENT(branch_adder, TYPE(Adder<XLEN>));
  SUBCOMPONENT(pc_4, Adder<XLEN>);
  SUBCOMPONENT(uncompress, TYPE(Uncompress<XLEN>));

  // Registers
  SUBCOMPONENT(pc_reg, RegisterClEn<XLEN>);

  // Stage seperating registers
  SUBCOMPONENT(ifid_reg, TYPE(IFID<XLEN>));
  SUBCOMPONENT(idex_reg, TYPE(RV5S_IDEX<XLEN>));
  SUBCOMPONENT(exmem_reg, TYPE(RV5S_EXMEM<XLEN>));
  SUBCOMPONENT(memwb_reg, TYPE(RV5S_MEMWB<XLEN>));

  // Multiplexers
  SUBCOMPONENT(reg_wr_src, TYPE(EnumMultiplexer<UnifiedRegWrSrc, XLEN>));
  SUBCOMPONENT(reg_wr_src_adapter, UnifiedRegWrSrcAdapter);
  SUBCOMPONENT(pc_src, TYPE(EnumMultiplexer<PcSrc, XLEN>));
  SUBCOMPONENT(branch_op1_src,
               TYPE(EnumMultiplexer<ForwardingSrc_1S, XLEN>));
  SUBCOMPONENT(branch_op2_src,
               TYPE(EnumMultiplexer<ForwardingSrc_1S, XLEN>));
  SUBCOMPONENT(jump_addr_src, TYPE(EnumMultiplexer<AluSrc1, XLEN>));
  SUBCOMPONENT(alu_op1_src, TYPE(EnumMultiplexer<AluSrc1, XLEN>));
  SUBCOMPONENT(alu_op2_src, TYPE(EnumMultiplexer<AluSrc2, XLEN>));
  SUBCOMPONENT(reg1_fw_src, TYPE(EnumMultiplexer<ForwardingSrc, XLEN>));
  SUBCOMPONENT(reg2_fw_src, TYPE(EnumMultiplexer<ForwardingSrc, XLEN>));
  SUBCOMPONENT(freg1_fw_src, TYPE(EnumMultiplexer<FPForwardingSrc, XLEN>));
  SUBCOMPONENT(freg2_fw_src, TYPE(EnumMultiplexer<FPForwardingSrc, XLEN>));
  SUBCOMPONENT(data_mem_wr_src, TYPE(EnumMultiplexer<DataMemWrSrc, XLEN>));
  SUBCOMPONENT(pc_inc, TYPE(EnumMultiplexer<PcInc, XLEN>));

  // Memories
  SUBCOMPONENT(instr_mem, TYPE(ROM<XLEN, c_RVInstrWidth>));
  SUBCOMPONENT(data_mem, TYPE(RVMemory<XLEN, XLEN>));

  // Forwarding & hazard detection units
  SUBCOMPONENT(funit, ForwardingUnit_1S);
  SUBCOMPONENT(hzunit, HazardUnit_1S);
  SUBCOMPONENT(hazard_pc_adapter, TYPE(RV5S1SAddressAdapter<XLEN>));

  // Gates
  // True if branch instruction and branch taken
  SUBCOMPONENT(br_and, TYPE(And<1, 2>));
  // True if branch taken or jump instruction
  SUBCOMPONENT(controlflow_or, TYPE(Or<1, 2>));
  // True if controlflow action or performing syscall finishing
  SUBCOMPONENT(efsc_or, TYPE(Or<1, 2>));
  // True if above or stalling due to load-use hazard
  SUBCOMPONENT(efschz_or, TYPE(Or<1, 2>));

  SUBCOMPONENT(mem_stalled_or, TYPE(Or<1, 2>));

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
            case EX: return idex_reg->pc_out.uValue();
            case MEM: return exmem_reg->pc_out.uValue();
            case WB: return memwb_reg->pc_out.uValue();
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
    bool stageValid = true;
    // Has the pipeline stage been filled?
    stageValid &= stage.index() <= m_cycleCount;

    // clang-format off
        // Has the stage been cleared?
        switch(stage.index()){
        case ID: stageValid &= ifid_reg->valid_out.uValue(); break;
        case EX: stageValid &= idex_reg->valid_out.uValue(); break;
        case MEM: stageValid &= exmem_reg->valid_out.uValue(); break;
        case WB: stageValid &= memwb_reg->valid_out.uValue(); break;
        default: case IF: break;
        }

        // Is the stage carrying a valid (executable) PC?
        switch(stage.index()){
        case ID: stageValid &= isExecutableAddress(ifid_reg->pc_out.uValue()); break;
        case EX: stageValid &= isExecutableAddress(idex_reg->pc_out.uValue()); break;
        case MEM: stageValid &= isExecutableAddress(exmem_reg->pc_out.uValue()); break;
        case WB: stageValid &= isExecutableAddress(memwb_reg->pc_out.uValue()); break;
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
      if (idex_reg->stalled_out.uValue() == 1 && namedState.isEmpty()) {
        state = StageInfo::State::Stalled;
      } else if (m_cycleCount > EX && idex_reg->valid_out.uValue() == 0) {
        state = StageInfo::State::Flushed;
      }
      break;
    }
    case MEM: {
      if (!stageValid && exmem_reg->stalled_out.uValue() == 1) {
        state = StageInfo::State::Stalled;
      } else if (m_cycleCount > MEM && exmem_reg->valid_out.uValue() == 0) {
        state = StageInfo::State::Flushed;
      }
      break;
    }
    case WB: {
      if (!stageValid && memwb_reg->stalled_out.uValue() == 1) {
        state = StageInfo::State::Stalled;
      } else if (m_cycleCount > WB && memwb_reg->valid_out.uValue() == 0) {
        state = StageInfo::State::Flushed;
      }
      break;
    }
    }

    return StageInfo({getPcForStage(stage), stageValid, state, namedState});
  }

  std::vector<FPUnicicleStageInfo> fpUnicicleStageInfos() const override {
    std::vector<FPUnicicleStageInfo> stages;
    const uint64_t currentCycle = hzunit->currentCycle();
    for (const auto &unit : hzunit->functionalUnits()) {
      unsigned unitIndex = 0;
      switch (unit.type) {
      case HazardUnit::FUType::Integer:
        unitIndex = 0;
        break;
      case HazardUnit::FUType::FPAddSub:
        unitIndex = 1;
        break;
      case HazardUnit::FUType::FPMul:
        unitIndex = 2;
        break;
      case HazardUnit::FUType::FPDiv:
        unitIndex = 3;
        break;
      }

      for (std::size_t stageIndex = 0; stageIndex < unit.stages.size();
           ++stageIndex) {
        const auto &instruction = unit.stages[stageIndex];
        if (!instruction.valid) {
          continue;
        }

        unsigned visibleStage = static_cast<unsigned>(stageIndex);
        if (!unit.pipelined) {
          const uint64_t elapsed =
              currentCycle > instruction.inputCycle
                  ? currentCycle - instruction.inputCycle
                  : 0;
          visibleStage = static_cast<unsigned>(
              std::min<uint64_t>(elapsed, unit.latency - 1));
        }
        stages.push_back(
            {true, instruction.pc, unitIndex, visibleStage, unit.instance});
      }
    }
    return stages;
<<<<<<< Updated upstream
  }

  AdvancedExecutionStatistics advancedExecutionStatistics() const override {
    auto statistics = m_advancedStatistics;
    statistics.available = true;
    statistics.integerUnitCount = 0;
    statistics.fpAddSubUnitCount = 0;
    statistics.fpMultiplyUnitCount = 0;
    statistics.fpDivideUnitCount = 0;
    for (const auto &unit : hzunit->functionalUnits()) {
      switch (unit.type) {
      case HazardUnit::FUType::Integer:
        ++statistics.integerUnitCount;
        break;
      case HazardUnit::FUType::FPAddSub:
        ++statistics.fpAddSubUnitCount;
        statistics.fpAddSubLatency = unit.latency;
        break;
      case HazardUnit::FUType::FPMul:
        ++statistics.fpMultiplyUnitCount;
        statistics.fpMultiplyLatency = unit.latency;
        break;
      case HazardUnit::FUType::FPDiv:
        ++statistics.fpDivideUnitCount;
        statistics.fpDivideLatency = unit.latency;
        break;
      }
    }
    return statistics;
=======
>>>>>>> Stashed changes
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
    if (!allStagesInvalid)
      return false;

    return !hzunit->hasPendingInstructions() &&
           !exmem_reg->hasPendingInstructions();
  }

  void setRegister(const std::string_view &regFile, unsigned i, VInt v) override {
    if (regFile == RVISA::FPR) {
      setSynchronousValue(fRegisterFile->_wr_mem, i, v);
      return;
    }
    setSynchronousValue(registerFile->_wr_mem, i, v);
  }

  void clockProcessor() override {
    saveAdvancedStatistics();

    // An instruction has been retired if the instruction in the WB stage is
    // valid and the PC is within the executable range of the program
    if (memwb_reg->valid_out.uValue() != 0 &&
        isExecutableAddress(memwb_reg->pc_out.uValue())) {
      m_instructionsRetired++;
    }

    Design::clock();
  }

  void reverse() override {
    if (m_syscallExitCycle != -1 && m_cycleCount == m_syscallExitCycle) {
      // We are about to undo an exit syscall instruction. In this case, the
      // syscall exiting sequence should be terminate
      ecallChecker->setSysCallExiting(false);
      m_syscallExitCycle = -1;
    }
    Design::reverse();
    if (!m_advancedStatisticsHistory.empty()) {
      m_advancedStatistics = m_advancedStatisticsHistory.front();
      m_advancedStatisticsHistory.pop_front();
    }
    if (memwb_reg->valid_out.uValue() != 0 &&
        isExecutableAddress(memwb_reg->pc_out.uValue())) {
      m_instructionsRetired--;
    }
  }

  void reset() override {
    ecallChecker->setSysCallExiting(false);
    Design::reset();
    m_syscallExitCycle = -1;
    m_advancedStatistics = {};
    m_advancedStatisticsHistory.clear();
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
<<<<<<< Updated upstream
  void saveAdvancedStatistics() {
    m_advancedStatisticsHistory.push_front(m_advancedStatistics);

    if (stageInfo({0, IF}).stage_valid && hzunit->hazardFEEnable.uValue())
      ++m_advancedStatistics.instructionMemoryCycles;

    if (exmem_reg->valid_out.uValue() &&
        (exmem_reg->mem_do_read_out.uValue() ||
         exmem_reg->mem_do_write_out.uValue())) {
      ++m_advancedStatistics.dataMemoryCycles;
    }

    bool anyUnitBusy = false;
    for (const auto &unit : hzunit->functionalUnits()) {
      const bool busy = std::any_of(
          unit.stages.begin(), unit.stages.end(),
          [](const HazardUnit::InstructionInfo &instruction) {
            return instruction.valid;
          });
      if (!busy)
        continue;

      anyUnitBusy = true;
      switch (unit.type) {
      case HazardUnit::FUType::Integer:
        ++m_advancedStatistics.integerUnitCycles;
        break;
      case HazardUnit::FUType::FPAddSub:
        ++m_advancedStatistics.fpAddSubUnitCycles;
        if (unit.instance <
            m_advancedStatistics.fpAddSubUnitCyclesByInstance.size()) {
          ++m_advancedStatistics
                .fpAddSubUnitCyclesByInstance[unit.instance];
        }
        break;
      case HazardUnit::FUType::FPMul:
        ++m_advancedStatistics.fpMultiplyUnitCycles;
        if (unit.instance <
            m_advancedStatistics.fpMultiplyUnitCyclesByInstance.size()) {
          ++m_advancedStatistics
                .fpMultiplyUnitCyclesByInstance[unit.instance];
        }
        break;
      case HazardUnit::FUType::FPDiv:
        ++m_advancedStatistics.fpDivideUnitCycles;
        if (unit.instance <
            m_advancedStatistics.fpDivideUnitCyclesByInstance.size()) {
          ++m_advancedStatistics
                .fpDivideUnitCyclesByInstance[unit.instance];
        }
        break;
      }
    }
    if (anyUnitBusy)
      ++m_advancedStatistics.aluCycles;

    const bool dataHazard = hzunit->dataHazardActive();
    const bool structuralHazard =
        !dataHazard && hzunit->structuralHazardActive();
    if (dataHazard)
      ++m_advancedStatistics.dataHazardStallCycles;
    if (structuralHazard)
      ++m_advancedStatistics.structuralHazardStallCycles;
    if (dataHazard || structuralHazard)
      ++m_advancedStatistics.stallCycles;

    if (br_and->out.uValue()) {
      if (isExecutableAddress(pc_reg->out.uValue()))
        ++m_advancedStatistics.controlHazardDiscardedInstructions;
      if (ifid_reg->valid_out.uValue() &&
          isExecutableAddress(ifid_reg->pc_out.uValue())) {
        ++m_advancedStatistics.controlHazardDiscardedInstructions;
      }
    }

  }

=======
>>>>>>> Stashed changes
  QString fpEXStageName() const {
    switch (HazardUnitState::unitTypeFor(
        idex_reg->opcode_out.eValue<RVInstr>())) {
    case HazardUnitState::FUType::FPAddSub:
      return "A1";
    case HazardUnitState::FUType::FPMul:
      return "M1";
    case HazardUnitState::FUType::FPDiv:
      return "D1";
    case HazardUnitState::FUType::Integer:
      return "";
    }
    return "";
  }

  /**
   * @brief m_syscallExitCycle
   * The variable will contain the cycle of which an exit system call was
   * executed. From this, we may determine when we roll back an exit system call
   * during rewinding.
   */
  long long m_syscallExitCycle = -1;
  AdvancedExecutionStatistics m_advancedStatistics;
  std::deque<AdvancedExecutionStatistics> m_advancedStatisticsHistory;
  std::shared_ptr<ISAInfoBase> m_enabledISA;
  ProcessorStructure m_structure = {{0, 5}};
};

} // namespace core
} // namespace vsrtl
