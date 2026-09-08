cmake_minimum_required(VERSION 3.16)

set(CMAKE_PROJECT_NAME mini_boot)

project(${CMAKE_PROJECT_NAME} C ASM)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

file(GLOB MINI_BOOT_SRC CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/bootutil/src/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/algorithm/src/*.c
)

set(MINI_BOOT_INC
    ${CMAKE_CURRENT_SOURCE_DIR}/bootutil/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/algorithm/inc
)

set(MBEDTLS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib/mbedtls)

add_library(mbedcrypto STATIC
    ${MBEDTLS_DIR}/library/aes.c
    ${MBEDTLS_DIR}/library/cipher.c
    ${MBEDTLS_DIR}/library/cipher_wrap.c
    ${MBEDTLS_DIR}/library/constant_time.c
    ${MBEDTLS_DIR}/library/gcm.c
    ${MBEDTLS_DIR}/library/md.c
    ${MBEDTLS_DIR}/library/sha256.c
    ${MBEDTLS_DIR}/library/platform_util.c
)

target_include_directories(mbedcrypto
PUBLIC  ${MBEDTLS_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/configs
)

target_compile_definitions(mbedcrypto
PUBLIC  MBEDTLS_CONFIG_FILE=\"mbedtls_config_boot.h\"
)

add_library(${CMAKE_PROJECT_NAME}
STATIC  ${MINI_BOOT_SRC}
bootutil/inc/read.h
bootutil/src/read.c
bootutil/inc/flash.h
bootutil/src/flash.c
)

target_include_directories(${CMAKE_PROJECT_NAME}
PUBLIC  ${MINI_BOOT_INC}
)

target_link_libraries(${CMAKE_PROJECT_NAME}
PUBLIC mbedcrypto
)

# TODO: 此处预留自定义命令（当前为空调用会导致 CMake 报错，暂注释）
# add_custom_command(
#
# )


