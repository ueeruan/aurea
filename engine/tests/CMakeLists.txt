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
    test_regression_2128.cpp
    test_regression_2129.cpp
    test_regression_2130.cpp
    test_regression_2132.cpp
    test_regression_2133.cpp
    test_regression_2134.cpp
    test_math.cpp
    test_animation.cpp
    test_core.cpp
    test_timeline.cpp
    test_render.cpp
    test_motion_blur.cpp
    test_serialization.cpp
    test_engine.cpp
    test_media.cpp
    test_proxy.cpp
    test_ai.cpp
    test_upscale_color.cpp
    test_yuv_layout.cpp
    test_p010_upload.cpp
    test_export_sink_startup.cpp
    test_aac_output_integrity.cpp
    test_gpu.cpp
    test_scene3d.cpp
    test_scene_environment_jobs.cpp
    test_morph_deformation.cpp
    test_ground_placement.cpp
    test_model_budget.cpp
    test_text3d.cpp
    test_text3d_anim.cpp
    test_text3d_shared.cpp
    test_hdri.cpp
    test_cube_lut.cpp
    test_transform3d_keys.cpp
    test_scene_pick.cpp
    test_beta_heavy.cpp
    test_camera_lens.cpp
    test_scene_param_keys.cpp
    test_builtin_property_schema.cpp
    test_effect3d_sweep.cpp
    test_audio.cpp
    test_audio_fx.cpp
    test_audio_transport.cpp
    test_captions.cpp
    test_cliptime.cpp
    test_beats.cpp
    test_edit.cpp
    test_trackball.cpp
    test_tracking.cpp
    test_motion_geometry.cpp
    test_motion_extras.cpp
    test_tracking_flow.cpp
    test_text.cpp
    test_presets.cpp
    test_alight_motion.cpp
    test_psd.cpp
    test_grid.cpp
    test_foreground.cpp
    test_roto.cpp
    test_expr.cpp
    test_keyframe_shape.cpp
    test_vector.cpp
    test_vector_gpu.cpp
    test_editing_tools.cpp
    test_export.cpp
    test_export_recovery.cpp
    test_export_scheduler.cpp
    test_image_export.cpp
    test_project_settings.cpp
    test_perf.cpp
    test_timeline_scale.cpp
    test_stability.cpp
    test_memory.cpp
    test_perf_render.cpp
    test_device.cpp
    test_address_space.cpp
    test_startup.cpp
    test_heavy.cpp
    test_i18n.cpp
    test_project_package.cpp
    test_model_probe.cpp
    test_layer_anim.cpp
    test_rig.cpp
    test_shape3d.cpp
    test_shape3d_split.cpp
)

target_include_directories(aurea_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}          # TestFramework.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_link_libraries(aurea_tests PRIVATE aurea_core)
if(MSVC)
    target_compile_options(aurea_tests PRIVATE /MP2)
    target_link_options(aurea_tests PRIVATE /DEBUG)
    target_link_libraries(aurea_tests PRIVATE dbghelp)
endif()

# Os testes visuais usam o backend Vulkan real quando o host tem cabeçalhos
# (a GPU em si é verificada em runtime: sem driver, os testes avisam e pulam).
if(TARGET aurea_vulkan)
    target_link_libraries(aurea_tests PRIVATE aurea_vulkan)
    target_compile_definitions(aurea_tests PRIVATE AUREA_TEST_VULKAN=1)
endif()
target_compile_definitions(aurea_tests PRIVATE
    AUREA_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden"
    AUREA_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
    AUREA_APP_PRESETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../android/app/src/main/assets/presets"
    # Fase 8A: baseline de performance (AUREA_BENCH=baseline grava, =compare lê).
    AUREA_PERF_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../docs/performance")

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
    # Includes real long-clip tracking and GPU/export regressions; the coherent
    # Windows audit already exceeds five minutes without a stalled test.
    TIMEOUT 1800
)
