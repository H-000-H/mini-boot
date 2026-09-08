# PC 端镜像校验工具的独立构建（与固件工程解耦，不参与根 CMakeLists 的 MCU 构建）
# 用法:
#   cmake -S test -B test/build -G Ninja
#   cmake --build test/build
#   运行(需在 test 目录下, 程序按相对路径读 bin):
#   .\build\pc_check.exe image.bin test.bin
# 需要 SHA/GCM/CBC 解密支持时:
#   cmake -S test -B test/build -G Ninja -DTOOL_ENABLE_CRYPTO=ON
cmake_minimum_required(VERSION 3.16)
set(CMAKE_PROJECT_NAME pc_check)

project(pc_check C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 默认只支持 CRC 模式: 不用编 mbedtls
option(TOOL_ENABLE_CRYPTO "编入 SHA/HMAC/AES 解密支持(需编译 mbedtls, 会慢一些)" OFF)

set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_executable(${CMAKE_PROJECT_NAME}
    main.cpp
    ${ROOT}/bootutil/src/image.c
    ${ROOT}/bootutil/src/read.c
    ${ROOT}/bootutil/src/err.c
    ${ROOT}/algorithm/src/crc.c
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${ROOT}/bootutil/inc
    ${ROOT}/algorithm/inc
)

# 与 boot_config.h 的 IMAGE_CRYPTO_ENABLE 对接: OFF -> 0, ON -> 1
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    IMAGE_CRYPTO_ENABLE=$<IF:$<BOOL:${TOOL_ENABLE_CRYPTO}>,1,0>
)

if(TOOL_ENABLE_CRYPTO)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        ${ROOT}/algorithm/src/aes.c
        ${ROOT}/algorithm/src/hmac.c
        ${ROOT}/algorithm/src/sha.c
    )

    add_library(mbedcrypto STATIC
        ${ROOT}/lib/mbedtls/library/aes.c
        ${ROOT}/lib/mbedtls/library/cipher.c
        ${ROOT}/lib/mbedtls/library/cipher_wrap.c
        ${ROOT}/lib/mbedtls/library/constant_time.c
        ${ROOT}/lib/mbedtls/library/gcm.c
        ${ROOT}/lib/mbedtls/library/md.c
        ${ROOT}/lib/mbedtls/library/sha256.c
        ${ROOT}/lib/mbedtls/library/platform_util.c
    )

    target_include_directories(mbedcrypto PUBLIC
        ${ROOT}/lib/mbedtls/include
        ${ROOT}/configs
    )

    target_compile_definitions(mbedcrypto PUBLIC
        MBEDTLS_CONFIG_FILE="mbedtls_config_boot.h"
    )

    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE mbedcrypto)
endif()
