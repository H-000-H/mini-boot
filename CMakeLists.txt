cmake_minimum_required(VERSION 3.16)

set(CMAKE_PROJECT_NAME mini_boot)

# clangd 需要从驱动名（arm-none-eabi-gcc）推断 ARM 交叉目标；而 CMake 会把含空格的
# 编译器路径转成 8.3 短名（AR19DD~1.EXE）写进编译数据库，导致 clangd 目标推断失败、
# 按 x86 主机解析、头文件大面积报错。
# 解法：用无空格的目录联接 .arm-gcc 指向工具链根目录，编译命令里保留完整可识别的驱动名。
# 注意：不能用 REALPATH，它会把联接还原回带空格的真实路径，前功尽弃。
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

# ==== 目标核选择：option 注入机制 ====
# 四选一，开启对应 MINI_BOOT_Mx 即选择目标核，-mcpu / CMSIS 核头 / ports/<core> 随之注入：d
option(MINI_BOOT_M0 "选择 Cortex-M0 为目标核" OFF)
option(MINI_BOOT_M3 "选择 Cortex-M3 为目标核" OFF)
#TODO 需要最后改为off
option(MINI_BOOT_M4 "选择 Cortex-M4 为目标核(默认测试,STM32F407)" ON)
option(MINI_BOOT_M7 "选择 Cortex-M7 为目标核" OFF)

set(MINI_BOOT_CORES M0 M3 M4 M7)
set(MINI_BOOT_CORE_SELECTED "")
set(_mini_boot_on_count 0)
foreach(_core IN LISTS MINI_BOOT_CORES)
    if(MINI_BOOT_${_core})
        math(EXPR _mini_boot_on_count "${_mini_boot_on_count} + 1")
        set(MINI_BOOT_CORE_SELECTED ${_core})
    endif()
endforeach()

if(_mini_boot_on_count GREATER 1)
    message(FATAL_ERROR "MINI_BOOT_M0/M3/M4/M7 同时只能开启一个，请检查 preset 或 CMake 缓存")
endif()

if(MINI_BOOT_CORE_SELECTED STREQUAL "M0")
    set(MINI_BOOT_ARCH_FLAGS -mcpu=cortex-m0 -mthumb)
elseif(MINI_BOOT_CORE_SELECTED STREQUAL "M3")
    set(MINI_BOOT_ARCH_FLAGS -mcpu=cortex-m3 -mthumb)
elseif(MINI_BOOT_CORE_SELECTED STREQUAL "M4")
    set(MINI_BOOT_ARCH_FLAGS -mcpu=cortex-m4 -mthumb)
elseif(MINI_BOOT_CORE_SELECTED STREQUAL "M7")
    set(MINI_BOOT_ARCH_FLAGS -mcpu=cortex-m7 -mthumb)
endif()

if(MINI_BOOT_ARCH_FLAGS)
    # 按目标核指令集编译
    add_compile_options(${MINI_BOOT_ARCH_FLAGS})
endif()

# ==== 工具链系统头目录（newlib / GCC 内置头）====
# 目的：把 string.h/stdint.h 等标准头的路径显式写进编译命令（compile_commands.json），
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

target_include_directories(mbedcrypto
PUBLIC  ${MBEDTLS_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/configs
)

#使用mbedtls_config_boot.h裁剪
set(MBEDTLS_CFG_FILE "\"mbedtls_config_boot.h\"")

target_compile_definitions(mbedcrypto
PUBLIC  MBEDTLS_CONFIG_FILE=${MBEDTLS_CFG_FILE}
)

# TODO: 此处预留自定义命令（当前为空调用会导致 CMake 报错，暂注释）
# add_custom_command(
#
# )

add_library(${CMAKE_PROJECT_NAME}
STATIC  ${MINI_BOOT_SRC}
bootutil/inc/read.h
bootutil/src/read.c
)

target_include_directories(${CMAKE_PROJECT_NAME}
PUBLIC  ${MINI_BOOT_INC}
)

target_link_libraries(${CMAKE_PROJECT_NAME}
PUBLIC mbedcrypto
)

# ==== 平台层 ports/<core>：随 option 注入 ====
# 芯片相关的一切（启动文件、CMSIS-Core 核头、核选择宏）
if(MINI_BOOT_CORE_SELECTED)
    string(TOLOWER ${MINI_BOOT_CORE_SELECTED} MINI_BOOT_CORE_DIR)
    set(MINI_BOOT_PORT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ports/${MINI_BOOT_CORE_DIR})
    file(GLOB MINI_BOOT_PLATFORM_SRC CONFIGURE_DEPENDS
        ${MINI_BOOT_PORT_DIR}/*.c
        ${MINI_BOOT_PORT_DIR}/*.s
    )
    if(MINI_BOOT_PLATFORM_SRC)
        add_library(platform STATIC ${MINI_BOOT_PLATFORM_SRC})
        target_include_directories(platform
        PUBLIC  ${MINI_BOOT_PORT_DIR}
                ${CMAKE_CURRENT_SOURCE_DIR}/lib/CMSIS_6-6.3.0/CMSIS/Core/Include
        )
        # 选中核的宏（如 MINI_BOOT_M4=1），平台代码可据此选择核头/区分核
        target_compile_definitions(platform
        PUBLIC  MINI_BOOT_${MINI_BOOT_CORE_SELECTED}=1
        )
        target_link_libraries(${CMAKE_PROJECT_NAME}
        PUBLIC platform
        )
    else()
        message(STATUS "ports/${MINI_BOOT_CORE_DIR} 下暂无源文件，跳过 platform 目标")
    endif()
endif()
