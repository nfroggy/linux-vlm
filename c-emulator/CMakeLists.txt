set(VLM_C_ICACHE_ENTRIES 65536 CACHE STRING
    "Decoded instruction cache entries for the C engine (power of two, at least 64)")
if(NOT VLM_C_ICACHE_ENTRIES MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "VLM_C_ICACHE_ENTRIES must be a power of two, at least 64")
endif()
math(EXPR icache_remainder "${VLM_C_ICACHE_ENTRIES} & (${VLM_C_ICACHE_ENTRIES} - 1)")
if(VLM_C_ICACHE_ENTRIES LESS 64 OR NOT icache_remainder EQUAL 0)
    message(FATAL_ERROR "VLM_C_ICACHE_ENTRIES must be a power of two, at least 64")
endif()
target_compile_definitions(vlm_emulator PUBLIC
    VLM_C_ICACHE_ENTRIES=${VLM_C_ICACHE_ENTRIES})

target_sources(vlm_emulator PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/emulator.c
    ${CMAKE_CURRENT_SOURCE_DIR}/dispatch.c
    ${CMAKE_CURRENT_SOURCE_DIR}/traps.c
    ${CMAKE_CURRENT_SOURCE_DIR}/host.c)
# IEEE single-float arithmetic and overflow tests require strict FP semantics.
set_property(SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/emulator.c
    ${CMAKE_CURRENT_SOURCE_DIR}/host.c
    DIRECTORY ${PROJECT_SOURCE_DIR}/emulator
    APPEND PROPERTY COMPILE_OPTIONS -fno-fast-math)
