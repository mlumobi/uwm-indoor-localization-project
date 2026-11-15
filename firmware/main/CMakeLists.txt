idf_component_register(
    SRCS "main.c" "node_dashboard.c" "uwb_ranging.c"
    PRIV_REQUIRES dw3110 esp_driver_gpio esp_driver_spi esp_event esp_http_server
                  esp_hw_support esp_netif esp_timer esp_wifi freertos log nvs_flash
    EMBED_TXTFILES "dashboard.html"
)
