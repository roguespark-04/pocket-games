file(GLOB UI_SRCS ui/*.c)
file(GLOB HAL_SRCS hal/*.c)
idf_component_register(SRCS "main.c" ${UI_SRCS} ${HAL_SRCS}
  INCLUDE_DIRS "." "ui" "hal"
  REQUIRES esp_lcd driver esp_wifi nvs_flash esp_netif esp_event esp_timer esp_driver_i2s esp_driver_i2c esp_driver_gpio esp_http_client esp_https_ota app_update json mbedtls esp_app_format)
# Wi-Fi password comes from the build environment only; written into the (git-ignored) build dir, never the source tree.
set(_pw "$ENV{COMPANION_WIFI_PASSWORD}")
string(REPLACE "\\" "\\\\" _pw "${_pw}")
string(REPLACE "\"" "\\\"" _pw "${_pw}")
file(WRITE ${CMAKE_BINARY_DIR}/gen/wifi_secret.h.tmp "#define WIFI_PASS \"${_pw}\"\n")
file(COPY_FILE ${CMAKE_BINARY_DIR}/gen/wifi_secret.h.tmp ${CMAKE_BINARY_DIR}/gen/wifi_secret.h ONLY_IF_DIFFERENT)
file(REMOVE ${CMAKE_BINARY_DIR}/gen/wifi_secret.h.tmp)
target_include_directories(${COMPONENT_LIB} PRIVATE ${CMAKE_BINARY_DIR}/gen)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-misleading-indentation)
