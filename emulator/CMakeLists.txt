add_library(vlm_emulator STATIC interfac.c interpds.c externals.c memory.c)
target_link_libraries(vlm_emulator PUBLIC vlm_options)

if(NOT VLM_C_ENGINE)
    target_link_libraries(vlm_emulator PUBLIC vlm_stub)
endif()
