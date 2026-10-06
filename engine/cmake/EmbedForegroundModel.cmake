# Same bounded MSVC hex embedding as MiDaS, with separate linker symbols.
include("${CMAKE_CURRENT_LIST_DIR}/EmbedDepthModel.cmake")
file(READ "${OUTPUT}" source)
string(REPLACE "depth_model_" "foreground_model_" source "${source}")
file(WRITE "${OUTPUT}" "${source}")
