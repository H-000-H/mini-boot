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
/* OTA 状态字节（bit 定义见 start.h） */
static volatile uint8_t ota_status = 0;

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

/* ---------------- OTA 主流程 ---------------- */

int mini_boot_source_download_stream(uint8_t*source)
{
    if(!ota_is_open())
    {
        
        return ERR_OTA_OPEN;
    }

    return ERR_OK;
}
int mini_boot_start_ota(void)
{
    if(!ota_is_open())
        return ERR_OTA_OPEN;
    flash_area_t* area;
    if(ota_is_double())
    {
        /*开启双分区逻辑*/
        uint8_t image_partition =  ota_current_partition_get();
        flash_area_open(image_partition, (const flash_area_t **)&area);
        
    }
    else 
    {
    
    }
    return 0;
    
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
