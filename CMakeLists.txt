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

# ==== 目标核选择：option 注入机制 ====
# 四选一，开启对应 MINI_BOOT_Mx 即选择目标核，-mcpu / CMSIS 核头 / ports/<core> 随之注入：d
option(MINI_BOOT_M0 "选择 Cortex-M0 为目标核（无 VTOR）" OFF)
option(MINI_BOOT_M0P "选择 Cortex-M0+ 为目标核（有 VTOR，注意与 M0 区分）" OFF)
option(MINI_BOOT_M3 "选择 Cortex-M3 为目标核" OFF)
#TODO 需要最后改为off
option(MINI_BOOT_M4 "选择 Cortex-M4 为目标核(默认测试,STM32F407)" ON)
option(MINI_BOOT_M7 "选择 Cortex-M7 为目标核" OFF)

set(MINI_BOOT_CORES M0 M0P M3 M4 M7)
set(MINI_BOOT_CORE_SELECTED "")
set(_mini_boot_on_count 0)
foreach(_core IN LISTS MINI_BOOT_CORES)
    if(MINI_BOOT_${_core})
        math(EXPR _mini_boot_on_count "${_mini_boot_on_count} + 1")
        set(MINI_BOOT_CORE_SELECTED ${_core})
    endif()
endforeach()

if(_mini_boot_on_count GREATER 1)
    message(FATAL_ERROR "MINI_BOOT_M0/M0P/M3/M4/M7 同时只能开启一个，请检查 preset 或 CMake 缓存")
endif()

if(MINI_BOOT_CORE_SELECTED STREQUAL "M0")
    set(MINI_BOOT_ARCH_FLAGS -mcpu=cortex-m0 -mthumb)
elseif(MINI_BOOT_CORE_SELECTED STREQUAL "M0P")
    set(MINI_BOOT_ARCH_FLAGS -mcpu=cortex-m0plus -mthumb)
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

# 选中核的宏（如 MINI_BOOT_M4=1）：必须作用于**所有**目标。
# 核心库头文件（boot_redef.h）靠它选择核相关分支
add_compile_definitions(MINI_BOOT_${MINI_BOOT_CORE_SELECTED}=1)

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
bootutil/inc/start.h
)

target_include_directories(${CMAKE_PROJECT_NAME}
PUBLIC  ${MINI_BOOT_INC}
)

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
set(FLASH_APP_START 0x08010000 CACHE STRING "Start address of the application in flash" FORCE)
set(FLASH_APP_LENGTH 0x10000 CACHE STRING "Length of the application in flash" FORCE)
set(RAM_APP_START 0x20000000 CACHE STRING "Start address of the application in RAM" FORCE)
set(RAM_APP_LENGTH 0x4000 CACHE STRING "Length of the application in RAM" FORCE)

# 同一份值喂给 C 代码：boot_config.h 用 CONFIG_XXX 接收，保证 .ld 与 C 里的相同
add_compile_definitions(
    CONFIG_FLASH_APP_START=${FLASH_APP_START}
    CONFIG_FLASH_APP_LENGTH=${FLASH_APP_LENGTH}
    CONFIG_RAM_APP_START=${RAM_APP_START}
    CONFIG_RAM_APP_LENGTH=${RAM_APP_LENGTH}
)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/ota_memory.ld.in
    ${CMAKE_CURRENT_BINARY_DIR}/mini_boot_ota_memory.ld
    @ONLY
)


target_link_options(${CMAKE_PROJECT_NAME} PUBLIC
    "-T${CMAKE_CURRENT_BINARY_DIR}/mini_boot_ota_memory.ld"
)
# ==== 平台层 ports/<core>：随 option 注入 ====
# 芯片相关的一切（启动文件、自研寄存器定义、核选择宏）
if(MINI_BOOT_CORE_SELECTED)
    string(TOLOWER ${MINI_BOOT_CORE_SELECTED} MINI_BOOT_CORE_DIR)
    set(MINI_BOOT_PORT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ports/${MINI_BOOT_CORE_DIR})
    file(GLOB MINI_BOOT_PLATFORM_SRC CONFIGURE_DEPENDS
        ${MINI_BOOT_PORT_DIR}/*.c
        ${MINI_BOOT_PORT_DIR}/*.s
    )
    if(MINI_BOOT_PLATFORM_SRC)
        add_library(platform STATIC ${MINI_BOOT_PLATFORM_SRC})
        # 平台层要实现核心层声明的钩子（boot_redef.h/err.h），因此需要核心层头文件路径
        target_include_directories(platform
        PUBLIC  ${MINI_BOOT_PORT_DIR}
                ${CMAKE_CURRENT_SOURCE_DIR}/bootutil/inc
        )
        # 核宏已由目录级 add_compile_definitions 统一注入，此处无需重复
        target_link_libraries(${CMAKE_PROJECT_NAME}
        PUBLIC platform
        )
    else()
        message(STATUS "ports/${MINI_BOOT_CORE_DIR} 下暂无源文件，跳过 platform 目标")
    endif()
endif()
