# src/CMakeLists.txt  ← これに置き換え

file(GLOB_RECURSE APP_SRCS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

idf_component_register(
    SRCS ${APP_SRCS}
    INCLUDE_DIRS "."
    REQUIRES esp_eth esp_netif driver lwip
)
