set(requires
    hardware_driver
    player
    esp_event
    esp_http_server
    esp_netif
    esp_wifi
    nvs_flash
    )

idf_component_register(SRCS main.c cmd_handler.c gpio_ctrl.c gpio_led.c gpio_fan.c gpio_buzzer.c
                            wifi_portal.c
                       INCLUDE_DIRS "."
                       REQUIRES ${requires})
