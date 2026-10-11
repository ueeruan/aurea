# Export is a single FFmpeg implementation on every platform. Dependencies are
# built by tools/build_ffmpeg.py, never substituted with the removed exporters.
set(AUREA_FFMPEG_ROOT "$ENV{AUREA_FFMPEG_ROOT}" CACHE PATH
    "Prefix containing pinned FFmpeg and OpenH264 include/ and lib/ directories")
set(AUREA_FFMPEG_ANDROID_ROOT "" CACHE PATH "Parent prefix containing native Android ABI directories")
if(ANDROID AND AUREA_FFMPEG_ANDROID_ROOT)
    # Gradle can switch a cached CMake build to a newly validated ABI family.
    set(AUREA_FFMPEG_ROOT "${AUREA_FFMPEG_ANDROID_ROOT}/${ANDROID_ABI}")
endif()
if(NOT AUREA_FFMPEG_ROOT)
    get_filename_component(_aurea_workspace "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
    if(ANDROID)
        if(AUREA_FFMPEG_ANDROID_ROOT)
            set(AUREA_FFMPEG_ROOT "${AUREA_FFMPEG_ANDROID_ROOT}/${ANDROID_ABI}")
        else()
            set(AUREA_FFMPEG_ROOT "${_aurea_workspace}/build/ffmpeg/android/${ANDROID_ABI}")
        endif()
    elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        if(CMAKE_OSX_SYSROOT MATCHES "simulator")
            set(_aurea_ffmpeg_platform "ios-simulator")
        else()
            set(_aurea_ffmpeg_platform "ios")
        endif()
        set(AUREA_FFMPEG_ROOT "${_aurea_workspace}/build/ffmpeg/${_aurea_ffmpeg_platform}/${CMAKE_OSX_ARCHITECTURES}")
    else()
        set(AUREA_FFMPEG_ROOT "${_aurea_workspace}/build/ffmpeg/host")
    endif()
endif()
set(AUREA_FFMPEG_ROOT "${AUREA_FFMPEG_ROOT}" CACHE PATH
    "Prefix containing pinned FFmpeg and OpenH264 include/ and lib/ directories" FORCE)

if(NOT EXISTS "${AUREA_FFMPEG_ROOT}/include/libavcodec/avcodec.h")
    message(FATAL_ERROR "AUREA export requires FFmpeg development libraries. Build them with tools/build_ffmpeg.py --platform android --arch ${ANDROID_ABI} --ndk <NDK> (or --platform ios on macOS), or set AUREA_FFMPEG_ROOT to a native prefix. Missing: ${AUREA_FFMPEG_ROOT}/include/libavcodec/avcodec.h")
endif()

add_library(aurea_ffmpeg INTERFACE)
target_include_directories(aurea_ffmpeg INTERFACE "${AUREA_FFMPEG_ROOT}/include")
target_compile_definitions(aurea_ffmpeg INTERFACE AUREA_HAS_FFMPEG=1)
# Preserve archive order: avformat calls avcodec; codec calls resampler/utils.
foreach(_component avformat avcodec swscale swresample avutil)
    unset(_aurea_${_component} CACHE)
    find_library(_aurea_${_component} NAMES ${_component} lib${_component}
        PATHS "${AUREA_FFMPEG_ROOT}/lib" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH REQUIRED)
    add_library(aurea_ffmpeg_${_component} UNKNOWN IMPORTED)
    set_target_properties(aurea_ffmpeg_${_component} PROPERTIES
        IMPORTED_LOCATION "${_aurea_${_component}}")
    target_link_libraries(aurea_ffmpeg INTERFACE aurea_ffmpeg_${_component})
endforeach()
unset(_aurea_openh264 CACHE)
find_library(_aurea_openh264 NAMES openh264 libopenh264
    PATHS "${AUREA_FFMPEG_ROOT}/lib" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
if(_aurea_openh264)
    target_link_libraries(aurea_ffmpeg INTERFACE "${_aurea_openh264}")
endif()
unset(_aurea_kvazaar CACHE)
find_library(_aurea_kvazaar NAMES kvazaar libkvazaar
    PATHS "${AUREA_FFMPEG_ROOT}/lib" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
if(_aurea_kvazaar)
    target_link_libraries(aurea_ffmpeg INTERFACE "${_aurea_kvazaar}")
endif()
unset(_aurea_x265 CACHE)
find_library(_aurea_x265 NAMES x265 libx265
    PATHS "${AUREA_FFMPEG_ROOT}/lib" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
if(_aurea_x265)
    target_link_libraries(aurea_ffmpeg INTERFACE "${_aurea_x265}")
endif()
if(ANDROID)
    # These codec archives are private to libaurea.so. Bind their symbols
    # locally, including FFmpeg's x86 CABAC inline assembly references, so ELF
    # shared-library links preserve PIC without disabling optimized routines.
    target_link_options(aurea_ffmpeg INTERFACE
        "LINKER:--exclude-libs,libavformat.a:libavcodec.a:libswscale.a:libswresample.a:libavutil.a:libopenh264.a:libkvazaar.a:libx265.a")
    target_link_libraries(aurea_ffmpeg INTERFACE mediandk android log z m dl)
elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    target_link_libraries(aurea_ffmpeg INTERFACE z bz2 iconv
        "-framework VideoToolbox" "-framework CoreMedia" "-framework CoreVideo"
        "-framework AudioToolbox" "-framework Security" "-framework Foundation")
elseif(UNIX)
    target_link_libraries(aurea_ffmpeg INTERFACE z m dl pthread)
endif()
message(STATUS "AUREA export: FFmpeg native prefix ${AUREA_FFMPEG_ROOT}")
