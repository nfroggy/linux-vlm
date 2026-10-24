set(common_sources world_tools.c utilities.c spy.c)
add_executable(genera main.c ${common_sources})
add_executable(byteswap_world byteswap_world.c ${common_sources})
foreach(program genera byteswap_world)
    target_link_libraries(${program} PRIVATE
        vlm_emulator vlm_life_support vlm_stub)
    target_link_options(${program} PRIVATE -rdynamic)
endforeach()
install(TARGETS genera byteswap_world RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
