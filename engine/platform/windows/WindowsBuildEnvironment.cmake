# MSVC translations can be decoded incorrectly by CMake's compiler probe,
# producing a mojibake /showIncludes prefix and losing Ninja header dependencies.
# tools/build_aurea_pc.py runs a compiler probe and passes its actual prefix.
# VSLANG=1033 falls back to Portuguese when only clui.dll/1046 is installed.
if(MSVC)
    if(AUREA_MSVC_INCLUDE_PREFIX)
        set(CMAKE_CL_SHOWINCLUDES_PREFIX "${AUREA_MSVC_INCLUDE_PREFIX}")
    endif()
endif()
