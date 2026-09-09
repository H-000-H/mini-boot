/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 * @file start.h
 * @brief ota的启动头文件。ota使用只需调用该头文件即可。
 * @author H-000-H
 * @note  状态变量本体是 start.c 内的 static（对外彻底隐藏），
 *        外部只能通过本文件声明的函数读写，不要尝试 extern 它。
 */
#ifndef BOOTUTIL_INC_START_H
#define BOOTUTIL_INC_START_H
#if defined(__cplusplus)
extern "C"
{
#endif
#include <stdint.h>

/* ---------------- OTA 状态位定义 ----------------
 * bit0   OTA 开关              0=关 1=开
 * bit1   双分区开关            0=关 1=开
 * bit2   回滚开关              0=关 1=开
 * bit3   强制 OTA              仅 DEBUG 编译有效
 * bit4~5 失败码                0=成功 1=读bin失败 2=写flash失败 3=校验失败
 * bit 6  目前分区              0=image_0 1=image_1
 */

/* ---- 设置 ---- */
void ota_open(void);             /* bit0 置 1：开启 OTA */
void ota_close(void);            /* bit0 清 0：关闭 OTA */
void ota_double_open(void);      /* bit1 置 1：开启双分区 */
void ota_double_close(void);     /* bit1 清 0：关闭双分区 */
void ota_rollback_open(void);    /* bit2 置 1：开启回滚 */
void ota_rollback_close(void);   /* bit2 清 0：关闭回滚 */
#if defined (DEBUG)
void ota_force_open(void);       /* bit3 置 1：强制 OTA（仅 DEBUG 编译有效） */
void ota_force_close(void);      /* bit3 清 0 */
#endif
void ota_fail_set(uint8_t code); /* bit4~5 写入失败码（0~3，超出按低 2 位截断） */
void ota_set_partition_image_0(void); /* bit6 写入当前分区（0=image_0 1=image_1） */
void ota_set_partition_image_1(void); /* bit6 写入当前分区（0=image_0 1=image_1） */
/* ---- 读取 ---- */
uint8_t mini_boot_get_ota_status(void); /* 整个状态字节 */
uint8_t ota_is_open(void);              /* 读 bit0 */
uint8_t ota_is_double(void);            /* 读 bit1 */
uint8_t ota_is_rollback(void);          /* 读 bit2 */
#if defined (DEBUG)
uint8_t ota_is_force(void);             /* 读 bit3 */
#endif
uint8_t ota_fail_get(void);             /* 读 bit4~5 解码后的失败码（0~3） */
uint8_t ota_current_partition_get(void); /* 读 bit6 当前分区（0=image_0 1=image_1） */

/* ---- OTA 主流程 ---- */
int mini_boot_source_download_stream();
int mini_boot_start_ota(void);
int mini_boot_pause_ota(void);
#if defined (DEBUG)
int mini_boot_start_ota_force(void);
#endif
#if defined(__cplusplus)
}
#endif

#endif /* BOOTUTIL_INC_START_H */
