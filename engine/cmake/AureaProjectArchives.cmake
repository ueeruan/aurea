include(FetchContent)

function(aurea_add_project_archive_reader)
    set(BUILD_SHARED_LIBS OFF)
    foreach(feature TAR CPIO CAT UNZIP TEST INSTALL WERROR ACL XATTR OPENSSL CNG MD
            LIBB2 LIBXML2 BSDXML EXPAT WIN32_XMLLITE PCREPOSIX PCRE2POSIX LZ4 LZMA ZSTD BZip2)
        set(ENABLE_${feature} OFF)
    endforeach()
    # Use the platform zlib on mobile. Windows development builds have no SDK zlib.
    set(ENABLE_ZLIB ON)
    if(WIN32)
        set(ENABLE_ZLIB OFF)
        set(ZLIB_BUILD_TESTING OFF)
        set(ZLIB_BUILD_SHARED OFF)
        set(ZLIB_INSTALL OFF)
        FetchContent_Declare(aurea_archive_zlib
            URL https://zlib.net/fossils/zlib-1.3.2.tar.gz
            URL_HASH SHA256=bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16)
        FetchContent_MakeAvailable(aurea_archive_zlib)
    else()
        find_package(ZLIB REQUIRED)
    endif()
    FetchContent_Declare(aurea_libarchive
        URL https://libarchive.org/downloads/libarchive-3.8.11.tar.xz
        URL_HASH SHA256=a3dc0872b1b39da34dc18ef1b944f0f11ffd37425d70ae04be679279f1cf43d2)
    FetchContent_MakeAvailable(aurea_libarchive)
    set_target_properties(archive_static PROPERTIES POSITION_INDEPENDENT_CODE ON OUTPUT_NAME aurea_project_archive)
    set_property(TARGET archive_static PROPERTY INTERFACE_INCLUDE_DIRECTORIES "")
    set(AUREA_ARCHIVE_INCLUDE_DIR "${aurea_libarchive_SOURCE_DIR}/libarchive" PARENT_SCOPE)
    if(WIN32)
        target_compile_definitions(archive_static PRIVATE HAVE_ZLIB_H=1)
        set_property(TARGET archive_static APPEND PROPERTY LINK_LIBRARIES zlibstatic)
        set_property(TARGET archive_static APPEND PROPERTY INTERFACE_LINK_LIBRARIES "$<LINK_ONLY:zlibstatic>")
    endif()
endfunction()

aurea_add_project_archive_reader()
