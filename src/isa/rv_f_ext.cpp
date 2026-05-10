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
      Fadd_s, Fsub_s, Fmul_s, Fdiv_s, Fsqrt_s, Fmin_s, Fmax_s
      >(instructions);
  if(isa->bits() == 64){
  }
}

} //namespace ExtF
} //namespace RVISA
} //namespace Ripes
