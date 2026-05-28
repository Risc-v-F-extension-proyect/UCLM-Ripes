#pragma once

#include "VSRTL/core/vsrtl_component.h"
#include "riscv.h"

namespace vsrtl {
namespace core {
using namespace Ripes;

class UnifiedRegWrSrcAdapter : public Component {
public:
  UnifiedRegWrSrcAdapter(const std::string &name, SimComponent *parent)
      : Component(name, parent) {
    out << [this] {
      const auto src = reg_wr_src.eValue<RegWrSrc>();
      if (fp_reg_do_write.uValue() && src == RegWrSrc::ALURES) {
        return UnifiedRegWrSrc::FALURES;
      }

      switch (src) {
      case RegWrSrc::MEMREAD:
        return UnifiedRegWrSrc::MEMREAD;
      case RegWrSrc::PC4:
        return UnifiedRegWrSrc::PC4;
      case RegWrSrc::ALURES:
      default:
        return UnifiedRegWrSrc::ALURES;
      }
    };
  }

  INPUTPORT_ENUM(reg_wr_src, RegWrSrc);
  INPUTPORT(fp_reg_do_write, 1);
  OUTPUTPORT_ENUM(out, UnifiedRegWrSrc);
};

} // namespace core
} // namespace vsrtl
