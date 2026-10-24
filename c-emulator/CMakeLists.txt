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
