#pragma once

#include "processors/RISC-V/riscv.h"

#include "VSRTL/core/vsrtl_component.h"

namespace Ripes {
enum class ForwardingSrc { IdStage, MemStage, WbStage };
enum class FPForwardingSrc { IdStage, WbStage };
}

namespace vsrtl {
namespace core {
using namespace Ripes;

class ForwardingUnit : public Component {
public:
  ForwardingUnit(const std::string &name, SimComponent *parent)
      : Component(name, parent) {
    alu_reg1_forwarding_ctrl << [this] {
      const auto idx = id_reg1_idx.uValue();
      if (idx == 0) {
        return ForwardingSrc::IdStage;
      } else if (idx == mem_reg_wr_idx.uValue() && mem_reg_wr_en.uValue()) {
        return ForwardingSrc::MemStage;
      } else if (idx == wb_reg_wr_idx.uValue() && wb_reg_wr_en.uValue()) {
        return ForwardingSrc::WbStage;
      } else {
        return ForwardingSrc::IdStage;
      }
    };

    alu_reg2_forwarding_ctrl << [this] {
      const auto idx = id_reg2_idx.uValue();
      if (idx == 0) {
        return ForwardingSrc::IdStage;
      } else if (idx == mem_reg_wr_idx.uValue() && mem_reg_wr_en.uValue()) {
        return ForwardingSrc::MemStage;
      } else if (idx == wb_reg_wr_idx.uValue() && wb_reg_wr_en.uValue()) {
        return ForwardingSrc::WbStage;
      } else {
        return ForwardingSrc::IdStage;
      }
    };

    falu_reg1_forwarding_ctrl << [this] {
      const auto idx = id_reg1_idx.uValue();
      const auto memOp = wb_mem_op.eValue<MemOp>();
      if (idx == wb_reg_wr_idx.uValue() &&
          (memOp == MemOp::FLW || memOp == MemOp::FLD)) {
        return FPForwardingSrc::WbStage;
      } else {
        return FPForwardingSrc::IdStage;
      }
    };

    falu_reg2_forwarding_ctrl << [this] {
      const auto idx = id_reg2_idx.uValue();
      const auto memOp = wb_mem_op.eValue<MemOp>();
      if (idx == wb_reg_wr_idx.uValue() &&
          (memOp == MemOp::FLW || memOp == MemOp::FLD)) {
        return FPForwardingSrc::WbStage;
      } else {
        return FPForwardingSrc::IdStage;
      }
    };
  }

  INPUTPORT(id_reg1_idx, c_RVRegsBits);
  INPUTPORT(id_reg2_idx, c_RVRegsBits);

  INPUTPORT(mem_reg_wr_idx, c_RVRegsBits);
  INPUTPORT(mem_reg_wr_en, 1);

  INPUTPORT(wb_reg_wr_idx, c_RVRegsBits);
  INPUTPORT(wb_reg_wr_en, 1);
  INPUTPORT(wb_mem_op, enumBitWidth<MemOp>());

  OUTPUTPORT_ENUM(alu_reg1_forwarding_ctrl, ForwardingSrc);
  OUTPUTPORT_ENUM(alu_reg2_forwarding_ctrl, ForwardingSrc);
  OUTPUTPORT_ENUM(falu_reg1_forwarding_ctrl, FPForwardingSrc);
  OUTPUTPORT_ENUM(falu_reg2_forwarding_ctrl, FPForwardingSrc);
};
} // namespace core
} // namespace vsrtl
