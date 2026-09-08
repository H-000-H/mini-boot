/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @file: boot_redef.h
 * @author: H-000-H
 * @brief: 平台适应接口 非os直接cmsis库包装
 */
#ifndef BOOTUTIL_INC_BOOT_REDEF_H
#define BOOTUTIL_INC_BOOT_REDEF_H
#ifdef __cplusplus
extern "C" {
#endif
/* ---------------- 重新定义 CMSIS 接口 ---------------- */
/**
 * @brief: 平台适应接口 非os直接cmsis库包装
 * @note:用完必须undef掉,否则会污染其他文件
 */
#if defined(MINI_BOOT_M0)
#define __CMSIS_GENERIC
#include "core_cm0.h"
#undef __CMSIS_GENERIC
#elif defined(MINI_BOOT_M3)
#define __CMSIS_GENERIC
#include "core_cm3.h"
#undef __CMSIS_GENERIC
#elif defined(MINI_BOOT_M4)
#define __CMSIS_GENERIC
#include "core_cm4.h"
#undef __CMSIS_GENERIC
#elif defined(MINI_BOOT_M7)
#define __CMSIS_GENERIC
#include "core_cm7.h"
#undef __CMSIS_GENERIC
#endif

static inline int mini_boot_irq_disable(void)
{
#if defined(MINI_BOOT_M0) || defined(MINI_BOOT_M3) || defined(MINI_BOOT_M4) || defined(MINI_BOOT_M7)
    int state = (int)__get_PRIMASK(); /* CMSIS 标准接口（无参返回值），先取当前 PRIMASK */
    __disable_irq();
#elif defined(MINI_BOOT_RISCV)
//TODO riscv
#warning "未定义risc-v,mini_boot_irq_disable() 直接返回 0"
    int state = 0;
#else
#error "未定义目标核,mini_boot_irq_disable() 直接返回 0"
    int state = 0;
#endif
    return state;

}
static inline void mini_boot_irq_enable(int state)
{
#if defined(MINI_BOOT_M0) || defined(MINI_BOOT_M3) || defined(MINI_BOOT_M4) || defined(MINI_BOOT_M7)
    __asm__ volatile("msr primask, %0" : : "r"(state) : "memory");
#else
#endif
}
#ifdef __cplusplus
}
#endif

#endif /* BOOTUTIL_INC_BOOT_REDEF_H */