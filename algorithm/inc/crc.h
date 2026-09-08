/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file: crc.h
 * @brief: CRC 校验接口，与 tools/crc/image_crc.py 中 crc_generic 保持一致；
 *         引擎通过宏 CRC_USE_TABLE 编译期选择，两种实现结果完全一致
 */
#ifndef CRC_H
#define CRC_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief 通用 CRC 计算（算法与 image_crc.py 的 crc_generic 一致）
 *        引擎由编译期宏 CRC_USE_TABLE 选择（见 crc.c）：
 *        1（默认）—— 查表法，约 1KB 静态表，大数据量快 5~8 倍，适合固件镜像校验；
 *        0        —— 逐位法，零额外 RAM，适合小数据量/资源紧张场景
 * @param data     数据缓冲区
 * @param length   数据长度（字节）
 * @param init     寄存器初始值
 * @param refin    输入字节是否按位反转（非 0 为是）
 * @param refout   最终结果是否按位反转（非 0 为是）
 * @param xor_out  最终异或输出值
 * @param poly     生成多项式（MSB 表示）
 * @param width    CRC 位宽（1~32）
 * @return CRC 校验值；width 非法时返回 0
 */
uint32_t crc_generic(const uint8_t *data, size_t length, uint32_t init,
                     int refin, int refout, uint32_t xor_out,
                     uint32_t poly, uint8_t width);

#endif /* CRC_H */
