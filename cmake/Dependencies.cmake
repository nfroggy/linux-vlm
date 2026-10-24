find_package(X11 REQUIRED COMPONENTS X11_xcb)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
find_path(CRYPT_INCLUDE_DIR NAMES crypt.h REQUIRED)
find_library(CRYPT_LIBRARY NAMES crypt REQUIRED)
mark_as_advanced(CRYPT_INCLUDE_DIR CRYPT_LIBRARY)

include(CheckIncludeFile)
include(CheckCSourceCompiles)
set(CMAKE_REQUIRED_INCLUDES "${X11_INCLUDE_DIR}" "${X11_X11_xcb_INCLUDE_PATH}")
check_include_file(X11/Xlib-xcb.h HAVE_X11_XLIB_XCB_H)
unset(CMAKE_REQUIRED_INCLUDES)
check_include_file(dlfcn.h HAVE_DLFCN_H)
check_c_source_compiles("#define _GNU_SOURCE
#include <ucontext.h>
int main(void) { ucontext_t context; return sizeof(context) == 0; }"
    HAVE_UCONTEXT_T)
if(NOT HAVE_X11_XLIB_XCB_H OR NOT HAVE_DLFCN_H OR NOT HAVE_UCONTEXT_T)
    message(FATAL_ERROR "Xlib-xcb.h, dlfcn.h, and ucontext_t are required to build the VLM.")
endif()
