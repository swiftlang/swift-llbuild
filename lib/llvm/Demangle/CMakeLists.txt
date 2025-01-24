add_llbuild_library(llvmDemangle STATIC
    ItaniumDemangle.cpp
    MicrosoftDemangle.cpp
)

set_property(GLOBAL APPEND PROPERTY LLBuild_EXPORTS llvmDemangle)
