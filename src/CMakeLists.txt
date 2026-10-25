project(VoxelCoreSrc)

file(GLOB_RECURSE headers ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp)
file(GLOB_RECURSE sources ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
list(REMOVE_ITEM sources ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)

# ---- Android port log helper -------------------------------------------------
# Every message is prefixed with [VC-ANDROID] so that build logs can be grepped
# in one shot from CI artifacts:  grep "\[VC-ANDROID\]" build.log
function(vc_android_log)
    if(ANDROID OR VC_ANDROID_PORT)
        message(STATUS "[VC-ANDROID] ${ARGN}")
    endif()
endfunction()

vc_android_log("============================================================")
vc_android_log("src/CMakeLists.txt entered (Android branch active)")
vc_android_log("CMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}")
vc_android_log("ANDROID_ABI=${ANDROID_ABI} ANDROID_PLATFORM=${ANDROID_PLATFORM}")
vc_android_log("ANDROID_STL=${ANDROID_STL}")
vc_android_log("CMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")
vc_android_log("CMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}")
vc_android_log("CMAKE_VERSION=${CMAKE_VERSION}")
vc_android_log("source dir=${CMAKE_CURRENT_SOURCE_DIR}")
vc_android_log("============================================================")

if(ANDROID)
    # Native-activity entry point replaces main.cpp on Android; the engine is
    # launched from platform/android VoxelCoreActivity -> nativeSetup().
    list(REMOVE_ITEM sources ${CMAKE_CURRENT_SOURCE_DIR}/window/detail/GLFWWindow.cpp)
    vc_android_log("GLFWWindow.cpp EXCLUDED from sources")
    vc_android_log("AndroidWindow.cpp present: $<BOOL:${CMAKE_CURRENT_SOURCE_DIR}/window/detail/AndroidWindow.cpp>")
    file(GLOB android_window_src ${CMAKE_CURRENT_SOURCE_DIR}/window/detail/AndroidWindow.cpp)
    if(NOT android_window_src)
        vc_android_log("ERROR: AndroidWindow.cpp NOT FOUND at ${CMAKE_CURRENT_SOURCE_DIR}/window/detail/")
    else()
        vc_android_log("AndroidWindow.cpp FOUND and compiled")
    endif()
endif()

add_library(VoxelCoreSrc STATIC ${sources} ${headers})

find_package(glm REQUIRED)

if(ANDROID)
    # ------------------------------------------------------------------
    # Android dependency strategy (first iteration):
    #   * GLEW      -> local shim (platform/android/glew_shim), proxies to GLES3
    #   * GLFW      -> not used; window/input implemented in AndroidWindow.cpp,
    #                  constants provided by src/window/glfw_compat.hpp
    #   * EGL/GLESv3/android/log -> NDK system libraries
    #   * zlib/png/vorbis/luajit/openal/freetype/curl/openssl/entt -> NDK
    #                  system libs where available (zlib, png), otherwise stub
    #                  targets (see dev/cmake/AndroidStubs.cmake). The stubs are
    #                  intentionally weak symbols so that linking succeeds; any
    #                  runtime call into a stub aborts with a clear log line.
    # ------------------------------------------------------------------
    set(VC_NO_GLFW ON)
    add_compile_definitions(VC_NO_GLFW)
    vc_android_log("VC_NO_GLFW=ON defined for all targets")

    include(${CMAKE_CURRENT_SOURCE_DIR}/../dev/cmake/AndroidStubs.cmake)
    vc_android_log("AndroidStubs.cmake included")

    target_include_directories(VoxelCoreSrc PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../platform/android/glew_shim)
    vc_android_log("GLEW shim include dir added: platform/android/glew_shim")

    find_library(ANDROID_LOG_LIB log)
    find_library(ANDROID_LIB android)
    find_library(EGL_LIB EGL)
    find_library(GLESV3_LIB GLESv3)
    find_library(Z_LIB z)
    find_library(PNG_LIB png)
    foreach(_l ANDROID_LOG_LIB ANDROID_LIB EGL_LIB GLESV3_LIB Z_LIB PNG_LIB)
        if(${_l})
            vc_android_log("found NDK system lib ${_l}=${${_l}}")
        else()
            vc_android_log("MISSING NDK system lib: ${_l}")
        endif()
    endforeach()

    target_link_libraries(VoxelCoreSrc
        PUBLIC glm::glm luajit_stub entt_stub
        PRIVATE
            ${ANDROID_LOG_LIB} ${ANDROID_LIB} ${EGL_LIB} ${GLESV3_LIB}
            ${Z_LIB} ${PNG_LIB}
            glew_stub glfw_stub openal_stub curl_stub openssl_stub
            freetype_stub vorbis_stub)
    vc_android_log("target_link_libraries configured for Android")
    vc_android_log("link order: system(EGL,GLESv3,z,png,log,android) + stubs")
else()
find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    # specific for vcpkg
    find_package(OpenAL CONFIG REQUIRED)
else()
    find_package(OpenAL REQUIRED)
    if(NOT TARGET OpenAL::OpenAL)
        add_library(OpenAL::OpenAL UNKNOWN IMPORTED)
        set_target_properties(OpenAL::OpenAL PROPERTIES
            IMPORTED_LOCATION "${OPENAL_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${OPENAL_INCLUDE_DIR}"
        )
    endif()
endif()
find_package(ZLIB REQUIRED)
find_package(PNG REQUIRED)
find_package(CURL REQUIRED)
find_package(OpenSSL 3.0 REQUIRED COMPONENTS Crypto)
find_package(glfw3 REQUIRED)
find_package(Freetype REQUIRED)
if(NOT APPLE)
    find_package(EnTT REQUIRED)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    find_package(vorbis REQUIRED)
    
    set(LUAJIT_PACKAGE_DIR "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
    if(VCPKG_TARGET_TRIPLET MATCHES "static")
        add_library(luajit STATIC IMPORTED)
        set_target_properties(
            luajit
            PROPERTIES
                IMPORTED_LOCATION
                "${LUAJIT_PACKAGE_DIR}/lib/libluajit-5.1.a"
                INTERFACE_INCLUDE_DIRECTORIES
                "${LUAJIT_PACKAGE_DIR}/include/"
        )
    else()
        if(EXISTS "${LUAJIT_PACKAGE_DIR}/include/luajit-2.1")
            set(LUAJIT_INCLUDE_DIR "${LUAJIT_PACKAGE_DIR}/include/luajit-2.1")
        else()
            set(LUAJIT_INCLUDE_DIR "${LUAJIT_PACKAGE_DIR}/include/luajit")
        endif()

        add_library(luajit SHARED IMPORTED)
        set_target_properties(
            luajit
            PROPERTIES
                IMPORTED_LOCATION
                "${LUAJIT_PACKAGE_DIR}/bin/lua51.dll"
                IMPORTED_IMPLIB
                "${LUAJIT_PACKAGE_DIR}/lib/lua51.lib"
                INTERFACE_INCLUDE_DIRECTORIES "${LUAJIT_INCLUDE_DIR}"
        )
    endif()

    add_library(luajit::luajit ALIAS luajit)
else()
    find_package(PkgConfig)

    pkg_check_modules(luajit REQUIRED IMPORTED_TARGET luajit)
    pkg_check_modules(vorbis REQUIRED IMPORTED_TARGET vorbis)
    pkg_check_modules(vorbisfile REQUIRED IMPORTED_TARGET vorbisfile)
    add_library(Vorbis::vorbis ALIAS PkgConfig::vorbis)
    add_library(Vorbis::vorbisfile ALIAS PkgConfig::vorbisfile)
    add_library(luajit::luajit ALIAS PkgConfig::luajit)
endif()

target_include_directories(VoxelCoreSrc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(
    VoxelCoreSrc
    PRIVATE glfw
            OpenGL::GL
            GLEW::GLEW
            ZLIB::ZLIB
            PNG::PNG
            CURL::libcurl
            OpenSSL::Crypto
            OpenAL::OpenAL
            Vorbis::vorbis
            Vorbis::vorbisfile
            Freetype::Freetype
    PUBLIC luajit::luajit
           glm::glm # Need public for src/delegates.hpp, which including to
                    # main.cpp
)
endif()  # ANDROID / desktop dependency branch

target_compile_options(
    VoxelCoreSrc
    PUBLIC $<$<CXX_COMPILER_ID:MSVC>:
           /utf-8
           /MP
           /D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR
           /W4
           /wd4244 # conversion from 'a' to 'T', possible loss of data
           /wd4267 # conversion from 'size_t' to 'int', possible loss of data
           /wd4245 # conversion from 'int' to 'const size_t', signed/unsigned
                   # mismatch
           /wd4100 # unreferenced formal parameter
           /wd4457 # declaration of 'var' hides function parameter 
           /wd4458 # declaration of 'var' hides class member
           /wd4459 # declaration of 'var' hides global declaration 
           /wd4101 # 'var': unreferenced local variable
           /wd4388 # 'token' : signed/unsigned mismatch
           /wd4018 # '>': signed/unsigned mismatch
           >
           $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:
           -Wall
           -Wextra
           # additional warnings
           -Wformat-nonliteral
           #-Wsign-conversion
           -Wcast-align
           -Wpointer-arith
           -Wundef
           -Wwrite-strings
           -Wno-unused-parameter
           -Wno-sign-compare
           -Wno-unknown-pragmas
           >
           $<$<CXX_COMPILER_ID:GNU>:
           -Wduplicated-branches
           -Wduplicated-cond
           >)

target_link_options(
    VoxelCoreSrc
    PUBLIC
    $<$<CXX_COMPILER_ID:GNU>:
    -no-pie
    >
    # Need for static compilation on Windows with clang TODO: Make single build
    # on Windows to avoid dependence on combinations of platforms and compilers
    # and make it independent
    $<$<PLATFORM_ID:Windows>:$<$<CXX_COMPILER_ID:Clang>:-static>>)
