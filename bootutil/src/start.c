/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @file start.c
 * @brief ota的启动实现文件。
 * @author H-000-H
 * @note  运行时状态字 s_ota_state 为本文件内 static：符号不导出，外部无法 extern 触碰，
 *        只能通过 start.h 声明的函数读写（信息隐藏 + 全局唯一状态）；
 *        其中需要跨复位保留的位由 ota_state 状态区承载（位定义见 ota_state.h）
 */
#include "start.h"
#include "flash.h"
#include "err.h"
#include "sys/_intsup.h"
#include <stdint.h>
#include <stddef.h>
#include "boot_config.h"
#include "memory.h"
#include "read.h"
#include "boot_keys.h"
#include "ota_state.h"

/* OTA 运行时状态字（位定义见 ota_state.h */
static volatile uint32_t s_ota_state = 0u;
static uint8_t load_buffer[MINI_BOOT_LOAD_MAX];/*默认走固定静态数组的如果内存想要优化可以放进栈里面但是栈小容易出问题固此处不放栈*/
static uint8_t verify_scratch[MINI_BOOT_LOAD_MAX]; /* 校验分块读取临时缓冲 */
static uint32_t s_downloaded_size = 0U; /* 最近一次成功下载的镜像总长度，0 表示尚无有效下载 */

/* 只把需要跨复位保留的位同步到持久状态区；后端未注册时静默失败（早期启动阶段） */
static void state_sync(void)
{
    (void)ota_state_store(s_ota_state & OTA_STATE_DURABLE_MASK);
}
/* ---------------- 设置 ---------------- */
/* 开关位属运行期配置（app 每次启动自行设置），只改 RAM，不落盘 */
void ota_open(void)
{
    s_ota_state = ota_state_bit_put(s_ota_state, OTA_STATE_BIT_OPEN, 1u);
}

void ota_close(void)
{
    s_ota_state = ota_state_bit_put(s_ota_state, OTA_STATE_BIT_OPEN, 0u);
}

void ota_rollback_open(void)
{
    s_ota_state = ota_state_bit_put(s_ota_state, OTA_STATE_BIT_ROLLBACK, 1u);
}

void ota_rollback_close(void)
{
    s_ota_state = ota_state_bit_put(s_ota_state, OTA_STATE_BIT_ROLLBACK, 0u);
}

/* 分区属持久位：boot 复位后要据此跳转，改动即落盘 */
void ota_set_partition_image_0(void)
{
    s_ota_state = ota_state_bit_put(s_ota_state, OTA_STATE_BIT_CURRENT, 0u);
    state_sync();
}

void ota_set_partition_image_1(void)
{
    s_ota_state = ota_state_bit_put(s_ota_state, OTA_STATE_BIT_CURRENT, 1u);
    state_sync();
}
#if defined (DEBUG)
void ota_force_open(void)
{
    s_ota_state = ota_state_bit_put(s_ota_state, OTA_STATE_BIT_FORCE, 1u);
}

void ota_force_close(void)
{
    s_ota_state = ota_state_bit_put(s_ota_state, OTA_STATE_BIT_FORCE, 0u);
}
#endif

/* 失败码属持久位：要能跨复位被上层读到，改动即落盘 */
void ota_fail_set(uint8_t code)
{
    s_ota_state = ota_state_fail_put(s_ota_state, code);
    state_sync();
}

/* ---------------- 读取 ---------------- */
uint8_t mini_boot_get_ota_status(void)
{
    return (uint8_t)(s_ota_state & 0xFFu);
}

uint8_t ota_is_open(void)
{
    return (uint8_t)ota_state_bit_get(s_ota_state, OTA_STATE_BIT_OPEN);
}

uint8_t ota_is_double(void)
{
    /* 只读：双分区是编译期就锁死的能力 */
    return (uint8_t)OTA_DUAL_PARTITION;
}

uint8_t ota_is_rollback(void)
{
    return (uint8_t)ota_state_bit_get(s_ota_state, OTA_STATE_BIT_ROLLBACK);
}

#if defined (DEBUG)
uint8_t ota_is_force(void)
{
    return (uint8_t)ota_state_bit_get(s_ota_state, OTA_STATE_BIT_FORCE);
}
#endif

uint8_t ota_fail_get(void)
{
    return (uint8_t)ota_state_fail_get(s_ota_state);
}

uint8_t ota_current_partition_get(void)
{
    return (uint8_t)ota_state_bit_get(s_ota_state, OTA_STATE_BIT_CURRENT);
}

uint8_t ota_is_pending(void)
{
    return (uint8_t)ota_state_bit_get(s_ota_state, OTA_STATE_BIT_PENDING);
}

/* ---------------- 启动恢复 ----------------
 * boot 选区前调用一次：恢复持久位；若上次激活的新镜像 app 未确认（pending），
 * 则回滚到另一个分区并记录失败码。首次上电/无有效记录时走默认值。
 */
int mini_boot_state_load(void)
{
    uint32_t loaded = 0u;

    if (ota_state_load(&loaded) == ERR_OK)
    {
        /* 只恢复持久位；开关位是运行期配置，不被持久值覆盖 */
        s_ota_state = (s_ota_state & ~OTA_STATE_DURABLE_MASK) |
                      (loaded & OTA_STATE_DURABLE_MASK);
    }
    else
    {
        s_ota_state &= ~OTA_STATE_DURABLE_MASK; /* 无有效状态：清持久位走默认 */
    }

    if (ota_state_bit_get(s_ota_state, OTA_STATE_BIT_PENDING) != 0u)
    {
        /* pending 置位说明上次激活的新镜像没被 app 确认：回滚到另一分区并落盘 */
        uint32_t resolved = s_ota_state;
        if (ota_state_resolve_pending(&resolved, OTA_FAIL_VERIFY) != 0)
        {
            s_ota_state = resolved;
            state_sync();
        }
    }
    return ERR_OK;
}

/* ---------------- flash 区域选择 ----------------
 * 当前分区   = bit6 指向的镜像区（boot 时跳转前校验用）
 * 非当前分区 = 另一片镜像区（下载目标 / 校验刚下载的镜像用）
 * 单分区模式下下载/校验目标固定为 image_0
 */
static uint32_t flash_active_area_id(void)
{
    return (ota_current_partition_get() != 0U) ? (uint32_t)FLASH_AREA_ID_IMAGE_1
                                               : (uint32_t)FLASH_AREA_ID_IMAGE_0;
}

static uint32_t flash_inactive_area_id(void)
{
    if (!ota_is_double())
    {
        return (uint32_t)FLASH_AREA_ID_IMAGE_0;
    }
    return (ota_current_partition_get() != 0U) ? (uint32_t)FLASH_AREA_ID_IMAGE_0: (uint32_t)FLASH_AREA_ID_IMAGE_1;
}

/* ---------------- OTA 主流程 ---------------- */
/* 镜像读取回调上下文：area 只在发起校验前解析/打开一次 */
typedef struct
{
    const flash_area_t *area; /* 镜像所在区域；镜像须从 area 偏移 0 开始，offset 移到数据区 */
} flash_read_ctx_t;

static int flash_image_read_fn(void *ctx, uint32_t offset, uint8_t *buf, uint32_t len)
{
    const flash_read_ctx_t *c = (const flash_read_ctx_t *)ctx;
    if ((c == NULL) || (c->area == NULL))
    {
        return ERR_ARG;
    }
    return flash_area_read_operation(c->area, offset, buf, len);
}

int mini_boot_source_download_stream(down_load_hook hook, void *param, uint32_t total_len)
{
    if (!ota_is_open())
    {
#if defined(DEBUG)
        /* TODO: 调试钩子 */
#endif
        return ERR_OTA_OPEN;
    }
    if ((hook == NULL) || (total_len == 0U))
    {
        return ERR_ARG;
    }

    /* 下载目标：非当前运行分区（单分区即 image_0） */
    const flash_area_t *area = NULL;
    int result = flash_area_open(flash_inactive_area_id(), &area);
    if (result != ERR_OK)
    {
        ota_fail_set(OTA_FAIL_WRITE);
        return result;
    }

    /*统一擦除函数都是从最开始就开擦除了所以直接一开始擦除足够内存 */
    result = flash_area_erase_operation(area, 0U, total_len);
    if (result != ERR_OK)
    {
        ota_fail_set(OTA_FAIL_WRITE);
        return result;
    }

    uint32_t received = 0U;
    while (received < total_len)
    {
        uint32_t want = total_len - received;
        if (want > MINI_BOOT_LOAD_MAX)
        {
            want = MINI_BOOT_LOAD_MAX;
        }

        int raw = 0;
        result = hook(param, load_buffer, want, &raw);
        if ((result != ERR_OK) || (raw <= 0) || ((uint32_t)raw > want))
        {
            ota_fail_set(OTA_FAIL_READ);
            return ERR_TRANSMIT;
        }

        result = flash_area_write_operation(area, received, load_buffer, (uint32_t)raw);
        if (result != ERR_OK)
        {
            ota_fail_set(OTA_FAIL_WRITE);
            return result;
        }
        received += (uint32_t)raw;
    }

    s_downloaded_size = total_len;
    return ERR_OK;
}

int mini_boot_start_ota(void)
{
    if (!ota_is_open())
    {
        return ERR_OTA_OPEN;
    }
    if (s_downloaded_size == 0U)
    {
        return ERR_ARG; /* 尚未成功下载过镜像 */
    }

    /* 校验对象 = 刚下载的新镜像（非当前分区）；meta 在标记末尾无论是否front，长度用下载记录值 */
    flash_read_ctx_t rctx = {NULL};
    int rc = flash_area_open(flash_inactive_area_id(), &rctx.area);
    if (rc != ERR_OK)
    {
        ota_fail_set(OTA_FAIL_WRITE);
        return rc;
    }

    image_read_cfg_t cfg = {0}; /* CRC/SHA 模式无需密钥；加密模式需先 boot_key_set() 装入 */
#if IMAGE_CRYPTO_ENABLE
    cfg.key = boot_key_get(&cfg.key_len);
#endif
    uint32_t crc = 0U;
    rc = image_verify_stream(flash_image_read_fn, &rctx, s_downloaded_size, &cfg,
                             verify_scratch, sizeof(verify_scratch), &crc);
#if IMAGE_CRYPTO_ENABLE
    boot_key_wipe(); /*无论成败都会清密钥避免上层拿密钥 */
#endif
    if (rc != ERR_OK)
    {
        ota_fail_set(OTA_FAIL_VERIFY);
        return rc;
    }

    /* 校验通过：激活新分区（bit6 切到刚下载的分区），一次性落盘 */
    uint32_t current = (flash_inactive_area_id() == (uint32_t)FLASH_AREA_ID_IMAGE_1) ? 1u : 0u;
    s_ota_state = ota_state_bit_put(s_ota_state, OTA_STATE_BIT_CURRENT, current);
    /* 双分区 + 回滚开启时才需要待确认：单分区没有可回退的旧镜像 */
    s_ota_state = ota_state_bit_put(s_ota_state, OTA_STATE_BIT_PENDING,(ota_is_double() && ota_is_rollback()) ? 1u : 0u);
    state_sync();
    return ERR_OK;
}

void mini_boot_confirm_ota(void)
{
    /* app 运行正常：清 pending，下次复位不再回滚 */
    s_ota_state = ota_state_bit_put(s_ota_state, OTA_STATE_BIT_PENDING, 0u);
    state_sync();
}

int mini_boot_pause_ota(void)
{
    // TODO: Implement pause OTA logic
    return 0;
}

#if defined (DEBUG)
int mini_boot_start_ota_force(void)
{
    // TODO: Implement force start OTA logic
    return 0;
}
#endif
