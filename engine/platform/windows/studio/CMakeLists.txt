# AUREA Studio: editor de PC (Windows x64). Interface Dear ImGui sobre Direct3D 11;
# a prévia é a swapchain Vulkan do motor numa janela filha.
set(AUREA_IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../third_party/imgui)
add_library(aurea_imgui STATIC
    ${AUREA_IMGUI_DIR}/imgui.cpp
    ${AUREA_IMGUI_DIR}/imgui_draw.cpp
    ${AUREA_IMGUI_DIR}/imgui_tables.cpp
    ${AUREA_IMGUI_DIR}/imgui_widgets.cpp
    ${AUREA_IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${AUREA_IMGUI_DIR}/backends/imgui_impl_dx11.cpp)
target_include_directories(aurea_imgui PUBLIC ${AUREA_IMGUI_DIR})
target_compile_definitions(aurea_imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(aurea_imgui PRIVATE /utf-8 /O2)

add_executable(aurea_studio WIN32
    StudioMain.cpp
    StudioUi.cpp
    PanelTimeline.cpp
    PanelControls.cpp
    PanelGraph.cpp
    PanelWorkspace.cpp
    Studio.rc)
set_target_properties(aurea_studio PROPERTIES OUTPUT_NAME "AUREA-Studio")
target_link_libraries(aurea_studio PRIVATE aurea_imgui aurea_windows_media aurea_vulkan d3d11 dxgi d3dcompiler dwmapi comdlg32 shell32 ole32)
target_compile_options(aurea_studio PRIVATE /utf-8 /EHsc /O2)
target_compile_definitions(aurea_studio PRIVATE _CRT_SECURE_NO_WARNINGS)
target_link_options(aurea_studio PRIVATE /MANIFEST:NO)
install(TARGETS aurea_studio RUNTIME DESTINATION .)
