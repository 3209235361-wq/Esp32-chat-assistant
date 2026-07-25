idf_component_register(SRCS "led.c" "key.c"
                    INCLUDE_DIRS "."
                    REQUIRES "driver" "freertos")