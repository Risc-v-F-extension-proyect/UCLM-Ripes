create_ripes_lib(isa LINK_TO_RIPES_LIB)

target_sources(isa_lib
  PRIVATE
    mipsisainfo_common.h mipsisainfo_common.cpp
    mips32isainfo.h
    rv_f_ext.h
    rv_f_ext.cpp
)

target_link_libraries(isa_lib PUBLIC elfio::elfio vsrtl::vsrtl Signals::Signals)
