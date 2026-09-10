cmake_minimum_required(VERSION 3.16)

set(CMAKE_PROJECT_NAME mini_boot)

#clangd要吃长路径
set(MINI_BOOT_GCC_JUNCTION "${CMAKE_CURRENT_SOURCE_DIR}/.arm-gcc/bin/arm-none-eabi-gcc.exe")
if(EXISTS "${MINI_BOOT_GCC_JUNCTION}")
    set(CMAKE_C_COMPILER   "${MINI_BOOT_GCC_JUNCTION}" CACHE FILEPATH "C compiler (space-free path)" FORCE)
    set(CMAKE_ASM_COMPILER "${MINI_BOOT_GCC_JUNCTION}" CACHE FILEPATH "ASM compiler (space-free path)" FORCE)
else()
    find_program(MINI_BOOT_GCC_PATH arm-none-eabi-gcc)
    if(MINI_BOOT_GCC_PATH)
        set(CMAKE_C_COMPILER   "${MINI_BOOT_GCC_PATH}" CACHE FILEPATH "C compiler" FORCE)
        set(CMAKE_ASM_COMPILER "${MINI_BOOT_GCC_PATH}" CACHE FILEPATH "ASM compiler" FORCE)
    endif()
endif()

project(${CMAKE_PROJECT_NAME} C ASM)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 目标核由构建方提供：本仓库不代管 -mcpu，也不注入 MINI_BOOT_Mx。
# 板级工程/父工程自行传 -mcpu=cortex-mX -mthumb 并定义 MINI_BOOT_M0/M0P/M3/M4/M7 之一
# （arch/arm/cortex-m/cortex_m.S 靠该宏选择核差异；漏定义会被汇编里的 #error 拦住）。

# 仅为 IDE/clangd：把工具链的 sysroot 头路径写进 compile_commands.json 不要碰这个这个是使用clangd必须有的
get_filename_component(ARM_TOOLCHAIN_ROOT "${CMAKE_C_COMPILER}/../.." ABSOLUTE)
file(GLOB ARM_GCC_INTERNAL_INC "${ARM_TOOLCHAIN_ROOT}/lib/gcc/arm-none-eabi/*/include")
set(ARM_TOOLCHAIN_SYS_INC
    ${ARM_GCC_INTERNAL_INC}
    ${ARM_TOOLCHAIN_ROOT}/arm-none-eabi/include
)
foreach(_sys_inc IN LISTS ARM_TOOLCHAIN_SYS_INC)
    if(EXISTS "${_sys_inc}")
        add_compile_options("-I${_sys_inc}")
    endif()
endforeach()

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

set(MBEDTLS_CFG_FILE "\"mbedtls_config_boot.h\"")
set(MBEDTLS_BOOT_INC ${MBEDTLS_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/configs)

target_include_directories(mbedcrypto PRIVATE ${MBEDTLS_BOOT_INC})
target_compile_definitions(mbedcrypto PRIVATE MBEDTLS_CONFIG_FILE=${MBEDTLS_CFG_FILE})

# TODO: 此处预留自定义命令（当前为空调用会导致 CMake 报错，暂注释）
# add_custom_command(
#
# )

add_library(${CMAKE_PROJECT_NAME}
STATIC  ${MINI_BOOT_SRC}
bootutil/inc/read.h
bootutil/src/read.c
bootutil/inc/start.h
bootutil/inc/boot_keys.h
)

target_include_directories(${CMAKE_PROJECT_NAME}
PUBLIC  ${MINI_BOOT_INC}
)

# read.c / boot_keys.c 会 include mbedtls 头：同样 PRIVATE 注入，不传播给消费者
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${MBEDTLS_BOOT_INC})
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE MBEDTLS_CONFIG_FILE=${MBEDTLS_CFG_FILE})

# 链接保持 PUBLIC：静态库，消费者需一起链 mbedcrypto，但不需要它的头/配置宏
target_link_libraries(${CMAKE_PROJECT_NAME}
PUBLIC mbedcrypto
)

# ==== 体系结构层 arch/arm/cortex-m：核相关原语（汇编实现，与芯片厂商无关）====
set(MINI_BOOT_ARCH_DIR ${CMAKE_CURRENT_SOURCE_DIR}/arch/arm/cortex-m)
file(GLOB MINI_BOOT_ARCH_SRC CONFIGURE_DEPENDS
    ${MINI_BOOT_ARCH_DIR}/*.S
)
add_library(arch STATIC ${MINI_BOOT_ARCH_SRC})
target_include_directories(arch
PUBLIC  ${MINI_BOOT_ARCH_DIR}
)
target_link_libraries(${CMAKE_PROJECT_NAME}
PUBLIC arch
)
# SRAM 区间：boot 用它校验 app 向量表的初始 SP 是否落在合法 RAM（boot.c），
# 必须与板级链接脚本的 RAM 区间一致 -> 由 CMake 主动注入（-DCONFIG_SRAM_*）
set(SRAM_START_ADDR 0x20000000 CACHE STRING "Start address of SRAM" FORCE)
set(SRAM_SIZE 0x00020000 CACHE STRING "Size of SRAM in bytes" FORCE)
add_compile_definitions(
    CONFIG_SRAM_START_ADDR=${SRAM_START_ADDR}
    CONFIG_SRAM_SIZE=${SRAM_SIZE}
)

# 链接脚本不在这里生成：由板级工程提供（各厂商不同），本仓库不产出也不注入 .ld

# 平台层（ports/<core>、启动文件、厂商寄存器定义）由板级工程自行构建并链接，
# 本仓库只提供与厂商无关的核心层 + 体系结构层（arch/arm/cortex-m）
