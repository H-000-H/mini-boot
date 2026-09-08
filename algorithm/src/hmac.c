/**
 * @copyright: SPDX-License-Identifier: Apache-2.0
 * @author:  H-000-H
 * @file: hmac.c
 * @brief: HMAC-SHA-256 封装（mbedtls md 接口），返回 0 表示成功
 */
#include <stddef.h>
#include "algorithm.h"
#include "mbedtls/aes.h"
#include "mbedtls/cipher.h"
#include "mbedtls/md.h"

/* HMAC-SHA-256 摘要长度（byte） */
#define HMAC_SHA256_LEN 32u
/* AES 分组长度（byte） */
#define AES_BLOCK_LEN 16u

/**
 * @brief HMAC-SHA-256 一次性计算
 * @param key:      密钥缓冲区
 * @param key_len:  密钥长度（byte）
 * @param data:     输入数据缓冲区
 * @param data_len: 输入数据长度（byte）
 * @param output:   摘要输出缓冲区（32 byte）
 * @return 0 成功，非 0 为 mbedtls 错误码
 */
int hmac_sha256(const unsigned char *key, size_t key_len,
                const unsigned char *data, size_t data_len,
                unsigned char output[32])
{
    const mbedtls_md_info_t *md_info;

    md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (md_info == NULL)
    {
        return MBEDTLS_ERR_MD_BAD_INPUT_DATA;
    }

    return mbedtls_md_hmac(md_info, key, key_len, data, data_len, output);
}

/**
 * @brief 内部辅助：对两段缓冲区（IV || 数据）连续计算 HMAC-SHA-256
 *        （HMAC-SHA-256 一次性接口只接受单段数据，需增量 API 拼接）
 * @param key:      密钥缓冲区
 * @param key_len:  密钥长度（byte）
 * @param buf1:     第一段缓冲区
 * @param len1:     第一段长度（byte）
 * @param buf2:     第二段缓冲区
 * @param len2:     第二段长度（byte）
 * @param output:   摘要输出缓冲区（32 byte）
 * @return 0 成功，非 0 为 mbedtls 错误码
 */
static int hmac_sha256_two_buf(const unsigned char *key, size_t key_len,
                               const unsigned char *buf1, size_t len1,
                               const unsigned char *buf2, size_t len2,
                               unsigned char output[HMAC_SHA256_LEN])
{
    mbedtls_md_context_t ctx;
    const mbedtls_md_info_t *md_info;
    int ret;

    md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (md_info == NULL)
    {
        return MBEDTLS_ERR_MD_BAD_INPUT_DATA;
    }

    mbedtls_md_init(&ctx);

    ret = mbedtls_md_setup(&ctx, md_info, 1);
    if (ret != 0)
    {
        goto cleanup;
    }

    ret = mbedtls_md_hmac_starts(&ctx, key, key_len);
    if (ret != 0)
    {
        goto cleanup;
    }

    ret = mbedtls_md_hmac_update(&ctx, buf1, len1);
    if (ret != 0)
    {
        goto cleanup;
    }

    ret = mbedtls_md_hmac_update(&ctx, buf2, len2);
    if (ret != 0)
    {
        goto cleanup;
    }

    ret = mbedtls_md_hmac_finish(&ctx, output);

cleanup:
    mbedtls_md_free(&ctx);
    return ret;
}

/**
 * @brief AES-CBC 加密 + HMAC-SHA-256 认证（encrypt-then-MAC）
 *        对 IV || 密文整体计算 HMAC，密钥与 MAC 密钥必须独立
 * @param input:       明文缓冲区
 * @param input_len:   明文长度（byte，16 的倍数且非 0）
 * @param output:      密文输出缓冲区（长度 >= input_len）
 * @param iv:          初始向量，16 byte（调用方生成/指定）
 * @param enc_key:     加密密钥（16/24/32 byte）
 * @param enc_key_len: 加密密钥长度（byte）
 * @param mac_key:     MAC 密钥（与 enc_key 相互独立）
 * @param mac_key_len: MAC 密钥长度（byte）
 * @param mac:         认证标签输出缓冲区（32 byte）
 * @return 0 成功，非 0 为 mbedtls 错误码
 */
int aes_cbc_hmac_encrypt(const unsigned char *input, size_t input_len,
                         unsigned char *output,
                         const unsigned char iv[AES_BLOCK_LEN],
                         const unsigned char *enc_key, size_t enc_key_len,
                         const unsigned char *mac_key, size_t mac_key_len,
                         unsigned char mac[HMAC_SHA256_LEN])
{
    unsigned char iv_work[AES_BLOCK_LEN];
    int ret;

    if (input_len == 0 || (input_len % AES_BLOCK_LEN) != 0)
    {
        return MBEDTLS_ERR_AES_INVALID_INPUT_LENGTH;
    }

    /* mbedtls 的 crypt_cbc 会覆盖 iv，先留工作副本保护调用方数据 */
    for (size_t i = 0; i < AES_BLOCK_LEN; i++)
    {
        iv_work[i] = iv[i];
    }

    ret = aes_encrypt_cbc(input, input_len, output, iv_work,
                          enc_key, enc_key_len);
    if (ret != 0)
    {
        return ret;
    }

    return hmac_sha256_two_buf(mac_key, mac_key_len,
                               iv, AES_BLOCK_LEN, output, input_len, mac);
}

/**
 * @brief HMAC-SHA-256 认证 + AES-CBC 解密（verify-then-decrypt）
 *        先对 IV || 密文验 HMAC（常量时间比较），通过后才解密
 * @param input:       密文缓冲区
 * @param input_len:   密文长度（byte，16 的倍数且非 0）
 * @param output:      明文输出缓冲区（长度 >= input_len）
 * @param iv:          初始向量，16 byte（必须与加密一致）
 * @param enc_key:     加密密钥
 * @param enc_key_len: 加密密钥长度（byte）
 * @param mac_key:     MAC 密钥
 * @param mac_key_len: MAC 密钥长度（byte）
 * @param mac:         期望的认证标签（由发送方提供）
 * @return 0 成功；MAC 不匹配返回 MBEDTLS_ERR_CIPHER_AUTH_FAILED
 */
int aes_cbc_hmac_decrypt(const unsigned char *input, size_t input_len,
                         unsigned char *output,
                         const unsigned char iv[AES_BLOCK_LEN],
                         const unsigned char *enc_key, size_t enc_key_len,
                         const unsigned char *mac_key, size_t mac_key_len,
                         const unsigned char mac[HMAC_SHA256_LEN])
{
    unsigned char iv_work[AES_BLOCK_LEN];
    unsigned char check_mac[HMAC_SHA256_LEN];
    unsigned char diff = 0;
    int ret;

    if (input_len == 0 || (input_len % AES_BLOCK_LEN) != 0)
    {
        return MBEDTLS_ERR_AES_INVALID_INPUT_LENGTH;
    }

    /* 先验 MAC 再解密：篡改后的密文不得进入解密器 */
    ret = hmac_sha256_two_buf(mac_key, mac_key_len,
                              iv, AES_BLOCK_LEN, input, input_len, check_mac);
    if (ret != 0)
    {
        return ret;
    }

    /* 常量时间比较，防侧信道 */
    for (size_t i = 0; i < HMAC_SHA256_LEN; i++)
    {
        diff |= (unsigned char)(check_mac[i] ^ mac[i]);
    }
    if (diff != 0)
    {
        return MBEDTLS_ERR_CIPHER_AUTH_FAILED;
    }

    for (size_t i = 0; i < AES_BLOCK_LEN; i++)
    {
        iv_work[i] = iv[i];
    }

    return aes_decrypt_cbc(input, input_len, output, iv_work,
                           enc_key, enc_key_len);
}
