idf_component_register(
    SRCS "main.c" "node_dashboard.c" "ranging_log.c" "uwb_ranging.c" "tdoa_radio.c" "tdoa_gateway.c"
    PRIV_REQUIRES dw3110 esp_driver_gpio esp_driver_spi esp_driver_usb_serial_jtag esp_event esp_http_server
                  esp_hw_support esp_netif esp_timer esp_wifi freertos log lwip nvs_flash
    EMBED_TXTFILES "dashboard.html"
)

option(UWM_TDOA_BENCH "Run the three-board UWB timestamp bench instead of TWR" OFF)
if(UWM_TDOA_BENCH)
    set(UWM_TDOA_MAIN_ID 1003 CACHE STRING "Bench clock-reference Node ID")
    set(UWM_TDOA_TAG_ID 1001 CACHE STRING "Bench blinking tag Node ID")
    target_sources(${COMPONENT_LIB} PRIVATE tdoa_bench.c)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        UWM_TDOA_BENCH=1 UWM_TDOA_MAIN_ID=${UWM_TDOA_MAIN_ID} UWM_TDOA_TAG_ID=${UWM_TDOA_TAG_ID})
endif()
