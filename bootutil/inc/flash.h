/*
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @author: H-000-H
 * @file: flash.h
 * @brief: flash 操作接口的弱定义，方便移植到不同平台(平台自实现 flash_area_erase/write/read_operation)
 */
#ifndef BOOTUTIL_INC_FLASH_H
#define BOOTUTIL_INC_FLASH_H
#if defined(__cplusplus)
extern "C" {
#endif
typedef struct flash_area flash_area_t;
typedef struct flash_sector flash_sector_t;
#include <stdint.h>

typedef enum 
{
    FLASH_AREA_ID_IMAGE_0=0,
    FLASH_AREA_ID_IMAGE_1,
    FLASH_AREA_ID_BOOTLOADER,
} flash_area_id_t;
/**
 * @brief: flash 扇区信息 桥接结构体
 * @param fs_off: 相对 flash 区域起始偏移（和 *_operation 的 off 一致）
 * @param fs_size: sector 大小
 */
struct flash_sector
{
    uint32_t fs_off;   // 相对 area 起始偏移（和 *_operation 的 off 一致）
    uint32_t fs_size;  // sector 大小
};

struct flash_area 
{
    uint32_t fa_id;             /**< flash 区域 ID*/
    uint32_t fa_device_id;      /**< flash 外部设备 ID */
    uint32_t fa_offset;         /**< flash 区域在设备上的偏移*/
    uint32_t fa_size;           /**< flash 区域大小 */
};

/**
 * @brief: 打开 flash 区域
 * @param fa_id: flash 区域 ID
 * @param area: 输出 flash 区域描述
 * @return: 0 表示成功，负数为失败原因（见 err.h）
 */
int flash_area_open(uint32_t fa_id, const flash_area_t **area);

/**
 * @brief: 擦除 flash 区域
 * @param area: flash 区域描述
 * @param off: 擦除偏移（相对于区域起始地址）
 * @param len: 擦除长度
 * @return: 0 表示成功，负数为失败原因（见 err.h）
 */
int flash_area_erase_operation(const flash_area_t *area, uint32_t off, uint32_t len);

/**
 * @brief: 写 flash 区域
 * @param area: flash 区域描述
 * @param off: 写入偏移（相对于区域起始地址）
 * @param buf: 待写入数据缓冲区
 * @param len: 待写入数据长度
 * @return: 0 表示成功，负数为失败原因（见 err.h）
 */
int flash_area_write_operation(const flash_area_t *area, uint32_t off, const void *buf, uint32_t len);

/**
 * @brief: 读 flash 区域
 * @param area: flash 区域描述
 * @param off: 读取偏移（相对于区域起始地址）
 * @param buf: 输出缓冲区
 * @param len: 待读取数据长度
 * @return: 0 表示成功，负数为失败原因（见 err.h）
 */
int flash_area_read_operation(const flash_area_t *area, uint32_t off, void *buf, uint32_t len);

/**
 * @brief: 获取 flash 区域的扇区信息
 * @param area[in]: flash 区域描述
 * @param max_count[in]: 输入参数，sectors 数组容量
 * @param sectors[out]: 输出参数，扇区信息数组
 * @param count[out]: 输出参数，实际扇区数量
 * @return: 0 表示成功，负数为失败原因（见 err.h）
 */
int flash_area_get_sectors(const flash_area_t *area,uint32_t max_count,flash_sector_t *sectors,uint32_t *count);       

#if defined(__cplusplus)
}
#endif
#endif /* BOOTUTIL_INC_FLASH_H */