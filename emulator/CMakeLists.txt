add_library(vlm_emulator STATIC interfac.c interpds.c externals.c memory.c)
target_link_libraries(vlm_emulator PUBLIC vlm_options)
