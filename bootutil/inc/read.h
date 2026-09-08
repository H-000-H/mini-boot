/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file: read.h
 * @brief: OTA 镜像读取：按模式完成「完整性校验 + 取明文」，各种模式的处理分支都在这里
 *         依赖 image.h（只解析布局）与 algorithm.h（SHA/HMAC/AES）
 * 各模式处理内容（与 tools/main.py 打包时一一对应）:
 *   CRC     : 对 payload 算 CRC32，与镜像记录的 crc 比较；payload 即明文
 *             CRC 模型由 boot_config.h 的 CRC_MODEL_* 决定（可用 CONFIG_CRC_* 覆盖），
 *             必须与 tools/main.py 的打包参数一致
 *   SHA     : 对 payload 算 SHA-256，与 aux(32B) 比较；payload 即明文
 *   GCM     : 用 aux 前 12B 作 nonce、后 16B 作 tag 做 AES-GCM 解密并认证
 *   CBC     : 用 aux(16B) 作 iv 做 AES-CBC 解密，再做 PKCS#7 去填充
 *   CBC_SHA : 先用 HMAC-SHA-256 校验 IV||密文（与 algorithm.h 的 aes_cbc_hmac_* 一致，
 *             打包端 tools/main.py 必须用同样的覆盖范围），通过后才做 AES-CBC 解密与
 *             PKCS#7 去填充（verify-then-decrypt，认证不过绝不解密）
 * 编译开关: boot_config.h 的 IMAGE_CRYPTO_ENABLE(默认 1) 决定是否编入 SHA/HMAC/AES 分支
 *          （需链接 mbedcrypto）；置 0 时这些模式直接返回 ERR_UNSUPPORTED，便于纯 CRC 场景裁剪体积
 */
#ifndef READ_H
#define READ_H

#include <stddef.h>
#include <stdint.h>

#include "err.h"
#include "image.h"

#ifdef __cplusplus
extern "C" {
#endif

/** AES-GCM/CBC 的 nonce/iv 长度与 GCM 标签长度 */
#define READ_GCM_NONCE_LEN 12u
#define READ_GCM_TAG_LEN   16u
#define READ_CBC_IV_LEN    16u
/** SHA-256 / HMAC-SHA-256 摘要长度 */
#define READ_HASH_LEN      32u
/** AES-CBC 分组长度，密文长度必须是它的整数倍 */
#define READ_CBC_BLOCK_LEN 16u

/** 读取配置，全部为入参 */
typedef struct
{
    image_check_t mode;     /**< [in] 打包模式 */
    size_t version_len;     /**< [in] version 字符串长度 */
    size_t tag_len;         /**< [in] tag 字符串长度 */
    int is_front;           /**< [in] 非 0 表示元数据在头部 */
    const uint8_t *key;     /**< [in] 加密密钥，GCM/CBC/CBC_SHA 必填，其余模式可为 NULL */
    size_t key_len;         /**< [in] 加密密钥长度，16/24/32 字节 */
    const uint8_t *mac_key; /**< [in] MAC 密钥，仅 CBC_SHA 使用；为 NULL 时回退用 key
                                 （生产环境应与加密密钥独立，见 algorithm.h） */
    size_t mac_key_len;     /**< [in] mac_key 长度；mac_key 为 NULL 时忽略本字段 */
} image_read_cfg_t;

/**
 * @brief 解析并校验镜像完整性，不产出明文
 *        CRC/SHA 模式会真正比对摘要；加密模式只做解析与 aux 长度检查，
 *        真正的认证必须走 image_read（因为要解密才能验证）
 * @param buf           [in]  镜像数据
 * @param len           [in]  镜像长度
 * @param cfg           [in]  读取配置，不可为 NULL
 * @param out_view      [out] 解析结果，可为 NULL（不关心时）
 * @param out_crc_calc  [out] 实算 CRC32，可为 NULL（仅 CRC 模式有效）
 * @return ERR_OK 校验通过；否则见 err.h
 */
int image_verify(const uint8_t *buf, size_t len, const image_read_cfg_t *cfg,
                 image_view_t *out_view, uint32_t *out_crc_calc);

/**
 * @brief 解析 + 校验 + 取明文 payload（加密模式会解密后写出）
 *        CRC/SHA 模式：out_payload 可为 NULL，表示只校验；也可直接拷出明文
 *        GCM/CBC/CBC_SHA 模式：out_payload 必须提供，容量需 >= payload_len
 * @param buf            [in]  镜像数据
 * @param len            [in]  镜像长度
 * @param cfg            [in]  读取配置，不可为 NULL
 * @param out_payload    [out] 明文输出缓冲区，CRC/SHA 模式可为 NULL
 * @param payload_cap    [in]  out_payload 容量（字节）
 * @param out_view       [out] 解析结果，可为 NULL
 * @param out_plain_len  [out] 明文实际长度（去填充后），可为 NULL
 * @param out_crc_calc   [out] 实算 CRC32，可为 NULL（仅 CRC 模式有效）
 * @return ERR_OK 成功；否则见 err.h
 */
int image_read(const uint8_t *buf, size_t len, const image_read_cfg_t *cfg,
               uint8_t *out_payload, size_t payload_cap,
               image_view_t *out_view, size_t *out_plain_len,
               uint32_t *out_crc_calc);

#ifdef __cplusplus
}
#endif

#endif /* READ_H */
