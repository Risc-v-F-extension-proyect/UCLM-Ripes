#include "rv_f_ext.h" // Asegúrate de que el include sea el de F, no el de M

namespace Ripes {
namespace RVISA {
namespace ExtF {

void enableExt(
    const ISAInfoBase *isa,
    InstrVec & instructions,
    PseudoInstrVec &)
{
  using namespace ExtF;

  enableInstructions<
      Flw_s, Fsw_s,
      Fadd_s, Fsub_s, Fmul_s, Fdiv_s, Fsqrt_s, Fmin_s, Fmax_s,
      Fsgnj_s, Fsgnjn_s, Fsgnjx_s
      >(instructions);
  if(isa->bits() == 64){
  }
}

} //namespace ExtF

namespace ExtD {

void enableExt(
    const ISAInfoBase *isa,
    InstrVec & instructions,
    PseudoInstrVec &)
{
  if (isa->bits() != 64) {
    return;
  }

  enableInstructions<
      Fld_d, Fsd_d,
      Fadd_d, Fsub_d, Fmul_d, Fdiv_d, Fsqrt_d, Fmin_d, Fmax_d,
      Fsgnj_d, Fsgnjn_d, Fsgnjx_d, Fcvt_s_d, Fcvt_d_s
      >(instructions);
}

} //namespace ExtD
} //namespace RVISA
} //namespace Ripes
