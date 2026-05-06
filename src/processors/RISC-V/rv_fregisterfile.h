#pragma once

#include "VSRTL/core/vsrtl_memory.h"

#include "riscv.h"

namespace vsrtl {
namespace core {
using namespace Ripes;


template <unsigned XLEN, bool readBypass>
class FRegisterFile : public Component {
public:
  SetGraphicsType(ClockedComponent);
  FRegisterFile(const std::string &name, SimComponent *parent)
      : Component(name, parent) {
    // Writes
    wr_addr >> _wr_mem->addr;
    wr_en >> _wr_mem->wr_en;
    data_in >> _wr_mem->data_in;
    (XLEN / CHAR_BIT) >> _wr_mem->wr_width;

    // Reads
    r1_addr >> _rd1_mem->addr;
    r2_addr >> _rd2_mem->addr;
    r3_addr >> _rd3_mem->addr;

    if constexpr (readBypass) {
      r1_out << [this] {
        const unsigned wr_idx = wr_addr.uValue();
        if (wr_en.uValue() && wr_idx == r1_addr.uValue()) {
          return data_in.uValue();
        } else {
          return _rd1_mem->data_out.uValue();
        }
      };

      r2_out << [this] {
        const unsigned wr_idx = wr_addr.uValue();
        if (wr_en.uValue() && wr_idx == r2_addr.uValue()) {
          return data_in.uValue();
        } else {
          return _rd2_mem->data_out.uValue();
        }
      };

      r3_out << [this] {
        const unsigned wr_idx = wr_addr.uValue();
        if (wr_en.uValue() && wr_idx == r3_addr.uValue()) {
          return data_in.uValue();
        } else {
          return _rd3_mem->data_out.uValue();
        }
      };
    } else {
      _rd1_mem->data_out >> r1_out;
      _rd2_mem->data_out >> r2_out;
      _rd3_mem->data_out >> r3_out;
    }
  }

  SUBCOMPONENT(_wr_mem, TYPE(WrMemory<c_RVRegsBits, XLEN, false>));
  SUBCOMPONENT(_rd1_mem, TYPE(RdMemory<c_RVRegsBits, XLEN, false>));
  SUBCOMPONENT(_rd2_mem, TYPE(RdMemory<c_RVRegsBits, XLEN, false>));
  SUBCOMPONENT(_rd3_mem, TYPE(RdMemory<c_RVRegsBits, XLEN, false>));

  INPUTPORT(r1_addr, c_RVRegsBits);
  INPUTPORT(r2_addr, c_RVRegsBits);
  INPUTPORT(r3_addr, c_RVRegsBits);
  INPUTPORT(wr_addr, c_RVRegsBits);

  INPUTPORT(data_in, XLEN);
  INPUTPORT(wr_en, 1);

  OUTPUTPORT(r1_out, XLEN);
  OUTPUTPORT(r2_out, XLEN);
  OUTPUTPORT(r3_out, XLEN);

  VSRTL_VT_U getRegister(unsigned i) const {
    return m_memory->readMemConst(i << ceillog2(XLEN / CHAR_BIT),
                                  XLEN / CHAR_BIT);
  }

  std::vector<VSRTL_VT_U> getRegisters() {
    std::vector<VSRTL_VT_U> regs;
    for (int i = 0; i < c_RVRegs; ++i)
      regs.push_back(getRegister(i));
    return regs;
  }

  void setMemory(AddressSpace *mem) {
    m_memory = mem;
    _wr_mem->setMemory(m_memory);
    _rd1_mem->setMemory(m_memory);
    _rd2_mem->setMemory(m_memory);
    _rd3_mem->setMemory(m_memory);
  }

private:
  AddressSpace *m_memory = nullptr;
};

} // namespace core
} // namespace vsrtl

