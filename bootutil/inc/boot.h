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
 * @brief 校验 app 向量表并跳转过去（正常不返回）
 * @param app_area 目标分区的起始地址与大小
 * @return 0 成功（正常不返回）；负数为失败原因（见 err.h）
 * @note  跳哪个分区由调用方决定（按 ota_current_partition_get() 自行组装 app_area），
 *        因此不需要单独的"强制跳转"入口；向量表非法时直接返回 ERR_ARG，不会硬跳
 */
int boot_jump_switch_app(mini_boot_app_area_t app_area);
#if defined(__cplusplus)
}
#endif

#endif /* BOOTUTIL_INC_BOOT_H */
