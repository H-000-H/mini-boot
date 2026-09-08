/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @file: boot.h
 * @author: H-000-H
 * @brief: 
    - bootloader 主流程接口（平台无关）该文件的分区地址 vtor等等不会给函数形式只有宏定义
    - 你只需要改 boot_config.h 里的宏定义就可以了除非你做调试不然不需要管这个文件
 */

#ifndef BOOTUTIL_INC_BOOT_H
#define BOOTUTIL_INC_BOOT_H
#if defined(__cplusplus)
extern "C" 
{
#endif
#include <stdint.h>
/**
 * @brief 强行切换分区，失败也会尝试跳转到该分区应用程序
 * @param app_addr: 应用程序入口地址
 * @return: 0 表示成功，负数为失败原因（见 err.h）
 * @note: 该函数只在调试阶段使用,用于错误的分区切换测试
 */
#if defined(BOOT_DEBUG)
int boot_jump_switch_force(uint32_t app_addr);
#endif

#if defined(__cplusplus)
}
#endif

#endif /* BOOTUTIL_INC_BOOT_H */