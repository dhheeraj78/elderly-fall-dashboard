idf_component_register(
    SRCS "main.c"
         "fall_feature_extraction.c"
         "fall_state_machine.c"
         "fall_trees.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_timer esp_psram esp_wifi esp_http_client nvs_flash esp-tls mbedtls freertos log esp_system esp_event esp_netif heap
)
