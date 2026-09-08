/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file: image.h
 * @brief: OTA 镜像布局解析，与 tools/main.py 的打包格式一一对应
 *         纯 C 实现：无动态内存、无文件 IO、无标准库输出，可直接在 bootloader 中调用
 *         本模块只做「解析」：按布局切出各字段，不做任何校验、解密、比较（见 read.h）
 * 镜像布局(is_front=1): crc32(4B) | version | tag | aux | payload
 * 镜像布局(is_front=0): payload | aux | version | tag | crc32(4B)
 * aux 布局: CRC=空  SHA=sha256(32B)  GCM=nonce(12B)+tag(16B)  CBC=iv(16B)  CBC_SHA=iv(16B)+hmac(32B)
 * 注意: version/tag 为变长字符串且格式内不记录长度，必须由调用方提供
 * 接口约定: 统一返回 err.h 中的错误码，结果一律通过 [out] 指针参数返回
 */
#ifndef IMAGE_H
#define IMAGE_H

#include <stddef.h>
#include <stdint.h>

#include "err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 镜像中 CRC32 字段长度（小端） */
#define IMAGE_CRC_LEN 4u

/** 打包时的校验/加密模式，决定 aux 长度 */
typedef enum
{
    IMAGE_CHECK_CRC = 0, /**< aux 0B  */
    IMAGE_CHECK_SHA,     /**< aux 32B */
    IMAGE_CHECK_GCM,     /**< aux 28B */
    IMAGE_CHECK_CBC,     /**< aux 16B */
    IMAGE_CHECK_CBC_SHA  /**< aux 48B */
} image_check_t;

/** 解析结果：全部为指向输入缓冲区的指针，不拷贝数据 */
typedef struct
{
    const uint8_t *payload; /**< 固件本体（加密模式下为密文） */
    size_t payload_len;
    const uint8_t *aux;     /**< nonce/iv/tag/sha256 等附加数据 */
    size_t aux_len;
    const uint8_t *version; /**< 版本字符串，非 '\0' 结尾 */
    size_t version_len;
    const uint8_t *tag;     /**< 标签字符串，非 '\0' 结尾 */
    size_t tag_len;
    uint32_t crc_stored;    /**< 镜像中记录的 CRC32（小端），是否一致由 read.h 判定 */
} image_view_t;

/**
 * @brief 查询某模式的 aux 长度
 * @param mode    [in]  校验/加密模式
 * @param out_len [out] aux 字节数，不可为 NULL
 * @return ERR_OK / ERR_ARG
 */
int image_aux_len(image_check_t mode, size_t *out_len);

/**
 * @brief 查询镜像除 payload 外的固定开销
 * @param mode          [in]  校验/加密模式
 * @param version_len   [in]  version 字符串长度
 * @param tag_len       [in]  tag 字符串长度
 * @param out_overhead  [out] crc + version + tag + aux 总长度，不可为 NULL
 * @return ERR_OK / ERR_ARG（模式不支持、out_overhead 为 NULL、长度和溢出）
 */
int image_overhead(image_check_t mode, size_t version_len, size_t tag_len,
                   size_t *out_overhead);

/**
 * @brief 按布局把镜像切分为 payload/aux/version/tag，并读出镜像中记录的 CRC32
 * @param buf          [in]  镜像数据
 * @param len          [in]  镜像长度
 * @param mode         [in]  打包模式
 * @param version_len  [in]  version 字符串长度（打包时决定）
 * @param tag_len      [in]  tag 字符串长度（打包时决定）
 * @param is_front     [in]  非 0 表示元数据在头部，0 表示在尾部
 * @param out_view     [out] 解析结果，不可为 NULL
 * @return ERR_OK / ERR_ARG / ERR_TOO_SMALL
 */
int image_parse(const uint8_t *buf, size_t len, image_check_t mode,
                size_t version_len, size_t tag_len, int is_front,
                image_view_t *out_view);

#ifdef __cplusplus
}
#endif

#endif /* IMAGE_H */
