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
      Flw_s, Fsw_s                                // Carga y almacenamiento
      >(instructions);
  if(isa->bits() == 64){
  }
}

} //namespace ExtF
} //namespace RVISA
} //namespace Ripes
