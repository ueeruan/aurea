# =============================================================================
#  compile_metal.cmake — um shader SPIR-V → .metallib embutido (script -P).
#
#  Chamado por cmake/AureaShaders.cmake para CADA .spv no build do iOS:
#
#    1. aurea-metal-compiler: .spv → blob MSL (RAW) + RAW.metal, e — só para o
#       fragment shader que escreve location 1 — RAW.c0.metal, a variante
#       `fs_main_c0` com a saída de cor 0 apenas (ver tools/metal-shaders/main.cpp);
#    2. xcrun metal -c em cada .metal → .air;
#    3. xcrun metallib com TODOS os .air → um .metallib por shader (as duas
#       entradas, `fs_main` e `fs_main_c0`, na mesma biblioteca);
#    4. pack_metallib.py: o .metallib entra no blob AUREAMSL (flag 1).
#
#  Por que um script e não quatro COMMANDs: a variante c0 é opcional (o
#  compilador decide pelo SPIR-V), e um add_custom_command não ramifica.
#
#  Variáveis: TOOL XCRUN SDK STD MIN SPV RAW AIR LIB OUT PYTHON PACK
# =============================================================================
foreach(_v TOOL XCRUN SDK STD MIN SPV RAW AIR LIB OUT PYTHON PACK)
    if(NOT DEFINED ${_v})
        message(FATAL_ERROR "compile_metal.cmake: falta -D${_v}")
    endif()
endforeach()

function(_run)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE _r)
    if(NOT _r EQUAL 0)
        string(REPLACE ";" " " _cmd "${ARGN}")
        message(FATAL_ERROR "compile_metal.cmake: falhou (${_r}): ${_cmd}")
    endif()
endfunction()

_run("${TOOL}" "${SPV}" "${RAW}")
_run("${XCRUN}" --sdk "${SDK}" metal -c "-std=${STD}" "${MIN}" "${RAW}.metal" -o "${AIR}")
set(_airs "${AIR}")
if(EXISTS "${RAW}.c0.metal")
    _run("${XCRUN}" --sdk "${SDK}" metal -c "-std=${STD}" "${MIN}" "${RAW}.c0.metal" -o "${AIR}.c0")
    list(APPEND _airs "${AIR}.c0")
else()
    file(REMOVE "${AIR}.c0")
endif()
_run("${XCRUN}" --sdk "${SDK}" metallib ${_airs} -o "${LIB}")
_run("${PYTHON}" "${PACK}" "${RAW}" "${LIB}" "${OUT}")
