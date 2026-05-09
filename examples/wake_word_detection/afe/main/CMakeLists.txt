set(requires
    hardware_driver
    player
    )

idf_component_register(SRCS main.c cmd_handler.c gpio_ctrl.c gpio_led.c gpio_fan.c gpio_buzzer.c
                       INCLUDE_DIRS "."
                       REQUIRES ${requires})

