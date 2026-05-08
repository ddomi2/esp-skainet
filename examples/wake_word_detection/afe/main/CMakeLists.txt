set(requires
    hardware_driver
    player
    )

idf_component_register(SRCS main.c cmd_handler.c gpio_ctrl.c
                       INCLUDE_DIRS "."
                       REQUIRES ${requires})

