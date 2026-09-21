# =============================================================================
#  Testes do motor.
#
#  Rodam no HOST, sem GPU e sem janela. Isso é deliberado: a timeline, a
#  animação, os comandos, o grafo de um frame (com backend falso) e a
#  serialização precisam funcionar sem backend gráfico — e é o que permite ao
#  CI verificar cada mudança sem aparelho na mão.
#
#  Os mesmos arquivos de teste compilam para Android e iOS; o alvo `aurea_tests`
#  é só uma conveniência para rodar rápido durante o desenvolvimento.
# =============================================================================
add_executable(aurea_tests
    main.cpp
    test_math.cpp
    test_animation.cpp
    test_core.cpp
    test_timeline.cpp
    test_render.cpp
    test_serialization.cpp
    test_engine.cpp
)

target_include_directories(aurea_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}          # TestFramework.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_link_libraries(aurea_tests PRIVATE aurea_core)

target_compile_options(aurea_tests PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive- /EHsc- /Zc:__cplusplus /utf-8>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -fno-exceptions>
)

target_compile_definitions(aurea_tests PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
)

if(NOT MSVC AND NOT ANDROID AND NOT AUREA_PLATFORM_IOS)
    find_package(Threads REQUIRED)
    target_link_libraries(aurea_tests PRIVATE Threads::Threads)
endif()

add_test(NAME aurea_engine_tests COMMAND aurea_tests)

# Os testes escrevem arquivos temporários no diretório de build, não no
# diretório do usuário. Rodar daqui deixa os temporários visíveis e apagáveis.
set_tests_properties(aurea_engine_tests PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    TIMEOUT 300
)
