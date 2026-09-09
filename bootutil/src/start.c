/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @file start.c
 * @brief ota的启动实现文件。
 * @author H-000-H
 * @note  ota_status 为本文件内 static：符号不导出，外部无法 extern 触碰，
 *        只能通过 start.h 声明的函数读写（信息隐藏 + 全局唯一状态）
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
/* OTA 状态字节（bit 定义见 start.h） */
static volatile uint8_t ota_status = 0;
static uint8_t load_buffer[MINI_BOOT_LOAD_MAX];/*默认走固定静态数组的如果内存想要优化可以放进栈里面但是栈小容易出问题固此处不放栈*/
static uint8_t verify_scratch[MINI_BOOT_LOAD_MAX]; /* 校验分块读取临时缓冲 */
static uint32_t s_downloaded_size = 0U; /* 最近一次成功下载的镜像总长度，0 表示尚无有效下载 */
/* 回滚尝试计数（RAM 态：跨复位持久化需接入 flash 状态区，待状态区设计后替换） */
static uint8_t s_try_left = 0U;     /* 剩余启动尝试次数，0 = 未装填/已确认/已回滚 */
static uint8_t s_old_partition = 0U; /* 激活新镜像前的旧分区（回滚目标） */
/* ---------------- 设置 ---------------- */
void ota_open(void)
{
    ota_status |= 0x01U;
}

void ota_close(void)
{
    ota_status &= (uint8_t)~0x01U;
}

void ota_double_open(void)
{
    ota_status |= 0x02U;
}

void ota_double_close(void)
{
    ota_status &= (uint8_t)~0x02U;
}

void ota_rollback_open(void)
{
    ota_status |= 0x04U;
}

void ota_rollback_close(void)
{
    ota_status &= (uint8_t)~0x04U;
}
void ota_set_partition_image_0(void) /* bit6 写入当前分区（0=image_0 1=image_1） */
{
    ota_status &= (uint8_t)~0x40U;
}

void ota_set_partition_image_1(void) /* bit6 写入当前分区（0=image_0 1=image_1） */
{
    ota_status |= 0x40U;
}
#if defined (DEBUG)
void ota_force_open(void)
{
    ota_status |= 0x08U;
}

void ota_force_close(void)
{
    ota_status &= (uint8_t)~0x08U;
}
#endif

void ota_fail_set(uint8_t code)
{
    ota_status = (uint8_t)((ota_status & (uint8_t)~0x30U) |(uint8_t)((code & 0x03U) << 4));
}

/* ---------------- 读取 ---------------- */
uint8_t mini_boot_get_ota_status(void)
{
    return ota_status;
}

uint8_t ota_is_open(void)
{
    return (uint8_t)((ota_status >> 0) & 0x01U);
}

uint8_t ota_is_double(void)
{
    return (uint8_t)((ota_status >> 1) & 0x01U);
}

uint8_t ota_is_rollback(void)
{
    return (uint8_t)((ota_status >> 2) & 0x01U);
}

#if defined (DEBUG)
uint8_t ota_is_force(void)
{
    return (uint8_t)((ota_status >> 3) & 0x01U);
}
#endif

uint8_t ota_fail_get(void)
{
    return (uint8_t)((ota_status >> 4) & 0x03U);
}

uint8_t ota_current_partition_get(void)
{
    return (uint8_t)((ota_status >> 6) & 0x01U);
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
    return (ota_current_partition_get() != 0U) ? (uint32_t)FLASH_AREA_ID_IMAGE_0
                                               : (uint32_t)FLASH_AREA_ID_IMAGE_1;
}

/* ---------------- OTA 主流程 ---------------- */
/* 镜像读取回调上下文：area 只在发起校验前解析/打开一次 */
typedef struct
{
    const flash_area_t *area; /* 镜像所在区域；镜像须从 area 偏移 0 开始，offset 直接透传 */
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
    int rc = flash_area_open(flash_inactive_area_id(), &area);
    if (rc != ERR_OK)
    {
        ota_fail_set(2U); /* 写 flash 失败 */
        return rc;
    }

    /*统一擦除函数都是从最开始就开擦除了所以直接一开始擦除足够内存 */
    rc = flash_area_erase_operation(area, 0U, total_len);
    if (rc != ERR_OK)
    {
        ota_fail_set(2U);
        return rc;
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
        rc = hook(param, load_buffer, want, &raw);
        if ((rc != ERR_OK) || (raw <= 0) || ((uint32_t)raw > want))
        {
            ota_fail_set(1U); /* 读 bin 失败 */
            return ERR_TRANSMIT;
        }

        rc = flash_area_write_operation(area, received, load_buffer, (uint32_t)raw);
        if (rc != ERR_OK)
        {
            ota_fail_set(2U);
            return rc;
        }
        received += (uint32_t)raw;
    }

    s_downloaded_size = total_len;
    return ERR_OK;
}

int mini_boot_start_ota(uint8_t try_num)
{
    if (!ota_is_open())
    {
        return ERR_OTA_OPEN;
    }
    if (s_downloaded_size == 0U)
    {
        return ERR_ARG; /* 尚未成功下载过镜像 */
    }

    /* 校验对象 = 刚下载的新镜像（非当前分区）；meta 在镜像末尾，长度用下载记录值 */
    flash_read_ctx_t rctx = {NULL};
    int rc = flash_area_open(flash_inactive_area_id(), &rctx.area);
    if (rc != ERR_OK)
    {
        ota_fail_set(2U);
        return rc;
    }

    image_read_cfg_t cfg = {0}; /* CRC/SHA 模式无需密钥；加密模式需外部提供 */
    uint32_t crc = 0U;
    rc = image_verify_stream(flash_image_read_fn, &rctx, s_downloaded_size, &cfg,
                             verify_scratch, sizeof(verify_scratch), &crc);
    if (rc != ERR_OK)
    {
        ota_fail_set(3U); /* 校验失败 */
        return rc;
    }

    /* 校验通过：激活新分区（bit6 切到刚下载的分区），并按需装填回滚尝试计数 */
    s_old_partition = ota_current_partition_get();
    if (flash_inactive_area_id() == (uint32_t)FLASH_AREA_ID_IMAGE_1)
    {
        ota_set_partition_image_1();
    }
    else
    {
        ota_set_partition_image_0();
    }
    if (ota_is_rollback() && (try_num > 0U))
    {
        s_try_left = try_num;
    }
    return ERR_OK;
}

uint8_t mini_boot_try_consume(void)
{
    if (s_try_left == 0U)
    {
        return 0U;
    }
    s_try_left--;
    if (s_try_left == 0U)
    {
        /* 尝试耗尽：切回激活前的旧分区，完成回滚 */
        if (s_old_partition != 0U)
        {
            ota_set_partition_image_1();
        }
        else
        {
            ota_set_partition_image_0();
        }
        ota_fail_set(3U);
    }
    return s_try_left;
}

void mini_boot_confirm_ota(void)
{
    s_try_left = 0U;
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
