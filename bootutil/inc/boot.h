/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @file: boot.h
 * @author: H-000-H
 */

#ifndef BOOTUTIL_INC_BOOT_H
#define BOOTUTIL_INC_BOOT_H
#if defined(__cplusplus)
extern "C" 
{
#endif
#include <stdint.h>
typedef struct mini_boot_app_area mini_boot_app_area_t ;
struct mini_boot_app_area
{
    uint32_t initial_addr;
    uint32_t size;
};

/**
 * @brief: 应用程序复位入口函数
 */
typedef void(*fn_entry)(void) ;
/**
 * @brief 强行切换分区，失败也会尝试跳转到该分区应用程序
 * @param app_addr: 应用程序入口地址
 * @return: 0 表示成功，负数为失败原因（见 err.h）
 * @note: 该函数只在调试阶段使用,用于错误的分区切换测试
 */
#if defined(BOOT_DEBUG)
int boot_jump_switch_force(mini_boot_app_area_t app_area);
#endif
/**
 * @brief 启动 OTA 流程
 * @return: 0 表示成功，负数为失败原因（见 err.h）
 */
int ota_start(void);

int boot_jump_switch_app(mini_boot_app_area_t app_area);
/**
 * @brief 启动应用程序
 * @return: 0 表示成功，负数为失败原因（见 err.h）
 */
int app_start(void);
#if defined(__cplusplus)
}
#endif

#endif /* BOOTUTIL_INC_BOOT_H */