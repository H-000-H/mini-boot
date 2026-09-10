/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 * @file start.h
 * @brief ota的启动头文件。ota使用只需调用该头文件即可。
 * @author H-000-H
 * @note  状态变量本体是 start.c 内的 static（对外彻底隐藏），
 *        外部只能通过本文件声明的函数读写，不要尝试 extern 它。
 *  boot:   flash ──load()──► s_ota_state（只回填持久位，开关位留给 app 设置）
 *  运行:   s_ota_state ──state_sync()──► flash（只在 pending/分区/失败码变化时）
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
 * bit1   保留                  （双分区是编译期能力，宏 OTA_DUAL_PARTITION）
 * bit2   回滚开关              0=关 1=开
 * bit3   强制 OTA              仅 DEBUG 编译有效
 * bit4~5 失败码                0=成功 1=读bin失败 2=写flash失败 3=校验失败
 * bit 6  目前分区              0=image_0 1=image_1
 * bit 7  pending               1=新镜像已激活但 app 未确认，复位后回滚
 */

/* ---- 失败码（ota_fail_set 入参 / ota_fail_get 返回值）---- */
#define OTA_FAIL_NONE   0U /* 成功，无失败 */
#define OTA_FAIL_READ   1U /* 读 bin 失败 */
#define OTA_FAIL_WRITE  2U /* 写 flash 失败 */
#define OTA_FAIL_VERIFY 3U /* 校验失败 */

/* ---- 设置 ---- */
void ota_open(void);             /* bit0 置 1：开启 OTA */
void ota_close(void);            /* bit0 清 0：关闭 OTA */
void ota_rollback_open(void);    /* bit2 置 1：开启回滚 */
void ota_rollback_close(void);   /* bit2 清 0：关闭回滚 */
#if defined (DEBUG)
void ota_force_open(void);       /* bit3 置 1：强制 OTA（仅 DEBUG 编译有效） */
void ota_force_close(void);      /* bit3 清 0 */
#endif
void ota_fail_set(uint8_t code); /* bit4~5 写入失败码（OTA_FAIL_xxx，超出按低 2 位截断） */

/* ---- 读取 ---- */
uint8_t mini_boot_get_ota_status(void); /* 整个状态字节 */
uint8_t ota_is_open(void);              /* 读 bit0 */
uint8_t ota_is_double(void);            /* 只读：双分区能力（编译期常量 OTA_DUAL_PARTITION） */
uint8_t ota_is_rollback(void);          /* 读 bit2 */
#if defined (DEBUG)
uint8_t ota_is_force(void);             /* 读 bit3 */
#endif
uint8_t ota_fail_get(void);             /* 读 bit4~5 解码后的失败码（OTA_FAIL_xxx） */
uint8_t ota_is_pending(void);           /* 读 bit7 待确认标志 */
uint8_t ota_current_partition_get(void); /* 读 bit6 当前分区（0=image_0 1=image_1） */

/* ---- OTA 主流程 ---- */
/**
 * @brief 下载钩子（平台实现：从网络/串口等来源取数填充 buf）
 * @param[in] param 用户透传参数
 * @param[out] buf 本次数据缓冲区（最多写 want 字节，不得越界）
 * @param[in] want 本次请求的字节数（不超过 MINI_BOOT_LOAD_MAX，末段为剩余字节数）
 * @param[out] out_len 实际取到的字节数（0 < out_len <= want，允许小于 want）
 * @return ERR_OK 成功；其他值按传输失败处理
 */
typedef int (*down_load_hook)(void *param, uint8_t *buf, uint32_t want, int *out_len);

/**
 * @brief 流式下载固件到目标分区（内部先 open 目标 area 并一次性擦除后分段写入）
 * @param[in] hook 自己的数据传输钩子
 * @param[in] param 透传给钩子的用户参数
 * @param[in] total_len 固件总长度（字节）
 * @return ERR_OK 成功，负数为失败原因（见 err.h）
 */
int mini_boot_source_download_stream(down_load_hook hook,void* param,uint32_t total_len);

/**
 * @brief 校验刚下载的镜像（非当前分区），通过后激活新分区；双分区+回滚开启时置 pending
 * @return ERR_OK 校验通过并已激活新分区；负数为失败原因（见 err.h）
 * @note  激活后需由 app 调用 mini_boot_confirm_ota() 确认；未确认则下次复位由
 *        mini_boot_state_load() 回滚到旧分区
 * @note  加密镜像（GCM/CBC/CBC_SHA）需先经 boot_keys.h 的 boot_key_set() 装入密钥，
 *        校验结束后无论成败内部自动 wipe；CRC/SHA 模式无需密钥
 */
int mini_boot_start_ota(void);

/**
 * @brief 启动时恢复持久状态（boot 选区前调用一次）
 * @note  若上次激活的新镜像未被 app 确认（pending），内部回滚到另一分区并记录失败码
 * @return ERR_OK（无有效状态时以默认值继续）
 */
int mini_boot_state_load(void);

/**
 * @brief 新镜像运行确认（app 侧运行正常后调用）：清 pending，不再回滚
 */
void mini_boot_confirm_ota(void);

int mini_boot_pause_ota(void);
#if defined (DEBUG)
int mini_boot_start_ota_force(void);
#endif
#if defined(__cplusplus)
}
#endif

#endif /* BOOTUTIL_INC_START_H */
