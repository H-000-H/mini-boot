/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file: algorithm.h
 * @brief: OTA 算法接口：SHA-256、HMAC-SHA-256、AES-CBC、AES-GCM
 *         除 crc_generic（见 crc.h）外，所有函数返回 0 表示成功，
 *         非 0 为 mbedtls 错误码
 */
#ifndef ALGORITHM_H
#define ALGORITHM_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stddef.h>
#include <stdint.h>

/**
 * @brief SHA-256 一次性计算
 * @param data:   输入数据缓冲区
 * @param len:    输入数据长度（字节）
 * @param output: 摘要输出缓冲区（32 字节）
 * @return 0 成功，非 0 为 mbedtls 错误码
 */
int sha256(const uint8_t *data, size_t len, uint8_t output[32]);

/**
 * @brief HMAC-SHA-256 一次性计算
 * @param key:      密钥缓冲区
 * @param key_len:  密钥长度（字节）
 * @param data:     输入数据缓冲区
 * @param data_len: 输入数据长度（字节）
 * @param output:   摘要输出缓冲区（32 字节）
 * @return 0 成功，非 0 为 mbedtls 错误码
 */
int hmac_sha256(const uint8_t *key, size_t key_len,
                const uint8_t *data, size_t data_len,
                uint8_t output[32]);

/**
 * @brief AES-GCM 加密
 * @param input:     明文缓冲区
 * @param input_len: 明文长度（字节）
 * @param output:    密文输出缓冲区（长度 >= input_len）
 * @param iv:        初始向量（建议 12 字节）
 * @param iv_len:    初始向量长度（字节）
 * @param key:       密钥（16/24/32 字节，对应 AES-128/192/256）
 * @param key_len:   密钥长度（字节）
 * @param tag:       认证标签输出缓冲区
 * @param tag_len:   标签长度（字节，<= 16）
 * @return 0 成功，非 0 为 mbedtls 错误码
 */
int aes_encrypt_gcm(const uint8_t *input, size_t input_len,
                    uint8_t *output,
                    const uint8_t *iv, size_t iv_len,
                    const uint8_t *key, size_t key_len,
                    uint8_t *tag, size_t tag_len);

/**
 * @brief AES-GCM 解密（含认证标签校验）
 * @param input:     密文缓冲区
 * @param input_len: 密文长度（字节）
 * @param output:    明文输出缓冲区（长度 >= input_len）
 * @param iv:        初始向量（必须与加密一致）
 * @param iv_len:    初始向量长度（字节）
 * @param key:       密钥
 * @param key_len:   密钥长度（字节）
 * @param tag:       期望的认证标签（由发送方提供）
 * @param tag_len:   标签长度（字节，<= 16）
 * @return 0 成功；标签不匹配返回 MBEDTLS_ERR_GCM_AUTH_FAILED
 */
int aes_decrypt_gcm(const uint8_t *input, size_t input_len,
                    uint8_t *output,
                    const uint8_t *iv, size_t iv_len,
                    const uint8_t *key, size_t key_len,
                    const uint8_t *tag, size_t tag_len);

/**
 * @brief AES-CBC 加密（PKCS 填充由调用方负责，input_len 须为 16 的倍数）
 * @param input:     明文缓冲区
 * @param input_len: 明文长度（字节，16 的倍数）
 * @param output:    密文输出缓冲区
 * @param iv:        初始向量，16 字节；注意调用后 iv 被覆盖，需调用方自存副本
 * @param key:       密钥（16/24/32 字节）
 * @param key_len:   密钥长度（字节）
 * @return 0 成功，非 0 为 mbedtls 错误码
 */
int aes_encrypt_cbc(const uint8_t *input, size_t input_len,
                    uint8_t *output,
                    uint8_t iv[16],
                    const uint8_t *key, size_t key_len);

/**
 * @brief AES-CBC 解密（PKCS 去填充由调用方负责，input_len 须为 16 的倍数）
 * @param input:     密文缓冲区
 * @param input_len: 密文长度（字节，16 的倍数）
 * @param output:    明文输出缓冲区
 * @param iv:        初始向量，16 字节；注意调用后 iv 被覆盖，需调用方自存副本
 * @param key:       密钥（16/24/32 字节）
 * @param key_len:   密钥长度（字节）
 * @return 0 成功，非 0 为 mbedtls 错误码
 */
int aes_decrypt_cbc(const uint8_t *input, size_t input_len,
                    uint8_t *output,
                    uint8_t iv[16],
                    const uint8_t *key, size_t key_len);

/**
 * @brief AES-CBC 加密 + HMAC-SHA-256 认证（encrypt-then-MAC）
 *        对 IV || 密文整体计算 HMAC；加密密钥与 MAC 密钥必须相互独立
 * @param input:       明文缓冲区
 * @param input_len:   明文长度（字节，16 的倍数且非 0）
 * @param output:      密文输出缓冲区（长度 >= input_len）
 * @param iv:          初始向量，16 字节（调用方生成/指定）
 * @param enc_key:     加密密钥（16/24/32 字节）
 * @param enc_key_len: 加密密钥长度（字节）
 * @param mac_key:     MAC 密钥
 * @param mac_key_len: MAC 密钥长度（字节）
 * @param mac:         认证标签输出缓冲区（32 字节）
 * @return 0 成功，非 0 为 mbedtls 错误码
 */
int aes_cbc_hmac_encrypt(const uint8_t *input, size_t input_len,
                         uint8_t *output,
                         const uint8_t iv[16],
                         const uint8_t *enc_key, size_t enc_key_len,
                         const uint8_t *mac_key, size_t mac_key_len,
                         uint8_t mac[32]);

/**
 * @brief HMAC-SHA-256 认证 + AES-CBC 解密（verify-then-decrypt）
 *        先对 IV || 密文验 HMAC（常量时间比较），通过后才解密
 * @param input:       密文缓冲区
 * @param input_len:   密文长度（字节，16 的倍数且非 0）
 * @param output:      明文输出缓冲区（长度 >= input_len）
 * @param iv:          初始向量，16 字节（必须与加密一致）
 * @param enc_key:     加密密钥
 * @param enc_key_len: 加密密钥长度（字节）
 * @param mac_key:     MAC 密钥
 * @param mac_key_len: MAC 密钥长度（字节）
 * @param mac:         期望的认证标签（由发送方提供）
 * @return 0 成功；MAC 不匹配返回 MBEDTLS_ERR_CIPHER_AUTH_FAILED
 */
int aes_cbc_hmac_decrypt(const uint8_t *input, size_t input_len,
                         uint8_t *output,
                         const uint8_t iv[16],
                         const uint8_t *enc_key, size_t enc_key_len,
                         const uint8_t *mac_key, size_t mac_key_len,
                         const uint8_t mac[32]);
#ifdef __cplusplus
}
#endif
#endif /* ALGORITHM_H */
