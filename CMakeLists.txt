cmake_minimum_required(VERSION 3.20)
project(vlm VERSION 0.99.1 LANGUAGES C)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "Use an out-of-source build: cmake -S . -B build")
endif()
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR
   NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$" OR
   NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "This build supports 64-bit x86 Linux.")
endif()
if(NOT CMAKE_C_COMPILER_ID STREQUAL "GNU")
    message(FATAL_ERROR "This build requires GCC. Configure with -DCMAKE_C_COMPILER=gcc.")
endif()

# C23 changes both bool and old-style function declarations used by the VLM.
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release RelWithDebInfo MinSizeRel)
endif()

option(VLM_FAST "Use -Ofast in Release and RelWithDebInfo builds" ON)
option(VLM_NATIVE "Optimize for the build machine's CPU (-march=native)" ON)
option(VLM_AUTOSTART "Boot the Genera world automatically" ON)
option(VLM_USE_TAP "Use TAP networking (OFF selects TUN)" ON)
option(VLM_DEBUG_DISK "Enable disk debugging" OFF)
option(VLM_DEBUG_NETWORK "Enable network debugging" OFF)
foreach(protocol IP CHAOS ARP ICMP)
    option(VLM_DEBUG_NETWORK_${protocol} "Enable ${protocol} network debugging" OFF)
endforeach()
option(VLM_C_ENGINE "Use the direct C Ivory interpreter" ON)
option(VLM_REGENERATE_STUBS "Regenerate interpreter sources with CLISP" OFF)
if(VLM_C_ENGINE AND VLM_REGENERATE_STUBS)
    message(FATAL_ERROR "VLM_REGENERATE_STUBS requires -DVLM_C_ENGINE=OFF.")
endif()

include(cmake/Dependencies.cmake)
include(GNUInstallDirs)
include(CTest)

configure_file(cmake/config.h.in generated/config.h)

add_library(vlm_options INTERFACE)
target_include_directories(vlm_options INTERFACE
    "${PROJECT_BINARY_DIR}/generated"
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/life-support"
    "${PROJECT_SOURCE_DIR}/emulator"
    "${PROJECT_SOURCE_DIR}/x86_64-emulator"
    "${CRYPT_INCLUDE_DIR}"
    "${X11_X11_xcb_INCLUDE_PATH}")
target_compile_options(vlm_options INTERFACE -Wall -fno-strict-aliasing)
if(VLM_NATIVE)
    target_compile_options(vlm_options INTERFACE -march=native)
endif()
if(VLM_FAST)
    target_compile_options(vlm_options INTERFACE
        "$<$<CONFIG:Release,RelWithDebInfo>:-Ofast>")
else()
    target_compile_options(vlm_options INTERFACE
        "$<$<CONFIG:Release,RelWithDebInfo>:-O2>")
endif()
target_compile_definitions(vlm_options INTERFACE GENERA)
if(VLM_AUTOSTART)
    target_compile_definitions(vlm_options INTERFACE AUTOSTART)
endif()
if(VLM_USE_TAP)
    target_compile_definitions(vlm_options INTERFACE USE_TAP)
else()
    target_compile_definitions(vlm_options INTERFACE USE_TUN)
endif()
if(VLM_DEBUG_DISK)
    target_compile_definitions(vlm_options INTERFACE DEBUG_DISK)
endif()
if(VLM_DEBUG_NETWORK)
    target_compile_definitions(vlm_options INTERFACE DEBUG_NETWORK=1)
    foreach(protocol IP CHAOS ARP ICMP)
        if(VLM_DEBUG_NETWORK_${protocol})
            target_compile_definitions(vlm_options INTERFACE DEBUG_${protocol}=1)
        endif()
    endforeach()
    if(VLM_DEBUG_NETWORK_ICMP AND NOT VLM_DEBUG_NETWORK_IP)
        target_compile_definitions(vlm_options INTERFACE DEBUG_IP=1)
    endif()
elseif(VLM_DEBUG_NETWORK_IP OR VLM_DEBUG_NETWORK_CHAOS OR
       VLM_DEBUG_NETWORK_ARP OR VLM_DEBUG_NETWORK_ICMP)
    message(WARNING "Protocol debug options require VLM_DEBUG_NETWORK=ON.")
endif()

# Xlib-xcb is loaded dynamically by life support; do not link libxcb here.
target_link_libraries(vlm_options INTERFACE
    X11::X11 Threads::Threads "${CRYPT_LIBRARY}" ${CMAKE_DL_LIBS} m)

add_subdirectory(emulator)
add_subdirectory(life-support)
if(VLM_C_ENGINE)
    target_compile_definitions(vlm_options INTERFACE VLM_C_ENGINE TRACING)
    add_subdirectory(c-emulator)
else()
    add_subdirectory(stub)
endif()
add_subdirectory(src)
