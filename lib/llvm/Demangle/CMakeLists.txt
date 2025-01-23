add_llbuild_library(LLVMDemangle STATIC
    ItaniumDemangle.cpp
    MicrosoftDemangle.cpp
)

set_property(GLOBAL APPEND PROPERTY LLBuild_EXPORTS LLVMDemangle)
