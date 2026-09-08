/*
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @author: H-000-H
 * @file: flash.c
 * @brief: flash 操作接口的弱定义，方便移植到不同平台(平台自实现 flash_area_erase/write/read_operation)
 */
#include "flash.h"
#include "err.h"
__attribute__((weak)) int flash_area_erase_operation(const flash_area_t *area, uint32_t off, uint32_t len)
{
    (void)area;
    (void)off;
    (void)len;
    return ERR_NOT_SUPPORTED;
}
__attribute__((weak)) int flash_area_write_operation(const flash_area_t *area, uint32_t off, const void *buf, uint32_t len)
{
    (void)area;
    (void)off;
    (void)buf;
    (void)len;
    return ERR_NOT_SUPPORTED;
    
}

__attribute__((weak)) int flash_area_read_operation(const flash_area_t *area, uint32_t off, void *buf, uint32_t len)
{
    (void)area;
    (void)off;
    (void)buf;
    (void)len;
    return ERR_NOT_SUPPORTED;
}