add_library(vlm_life_support STATIC
    cold_load.c console.c disks.c initialization.c network.c
    message_channels.c polling.c queues.c signals.c unixcrypt.c)
target_link_libraries(vlm_life_support PUBLIC vlm_options)
