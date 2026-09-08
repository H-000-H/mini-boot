/**
 * @copyright: SPDX-License-Identifier: Apache-2.0
 * @author:  H-000-H
 * @file: aes.c
 * @brief: AES-CBC 与 AES-GCM 加解密封装（mbedtls 2.28 接口），
 *         所有函数返回 0 表示成功，非 0 为 mbedtls 错误码
 */
#include <stddef.h>
#include "algorithm.h"
#include "mbedtls/aes.h"
#include "mbedtls/gcm.h"

/* GCM 内部校验标签缓冲上限（GCM 标准标签长度 16 字节） */
#define AES_GCM_MAX_TAG_LEN 16u

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
int aes_encrypt_gcm(const unsigned char *input, size_t input_len,
                    unsigned char *output,
                    const unsigned char *iv, size_t iv_len,
                    const unsigned char *key, size_t key_len,
                    unsigned char *tag, size_t tag_len)
{
    mbedtls_gcm_context gcm;
    int ret;

    mbedtls_gcm_init(&gcm);

    ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key,
                             (unsigned int)(key_len * 8u));
    if (ret != 0)
    {
        goto cleanup;
    }

    ret = mbedtls_gcm_starts(&gcm, MBEDTLS_GCM_ENCRYPT, iv, iv_len, NULL, 0);
    if (ret != 0)
    {
        goto cleanup;
    }

    ret = mbedtls_gcm_update(&gcm, input_len, input, output);
    if (ret != 0)
    {
        goto cleanup;
    }

    ret = mbedtls_gcm_finish(&gcm, tag, tag_len);

cleanup:
    mbedtls_gcm_free(&gcm);
    return ret;
}

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
int aes_decrypt_gcm(const unsigned char *input, size_t input_len,
                    unsigned char *output,
                    const unsigned char *iv, size_t iv_len,
                    const unsigned char *key, size_t key_len,
                    const unsigned char *tag, size_t tag_len)
{
    mbedtls_gcm_context gcm;
    unsigned char check_tag[AES_GCM_MAX_TAG_LEN];
    int ret;

    if (tag_len == 0 || tag_len > AES_GCM_MAX_TAG_LEN)
    {
        return MBEDTLS_ERR_GCM_BAD_INPUT;
    }

    mbedtls_gcm_init(&gcm);

    ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key,
                             (unsigned int)(key_len * 8u));
    if (ret != 0)
    {
        goto cleanup;
    }

    ret = mbedtls_gcm_starts(&gcm, MBEDTLS_GCM_DECRYPT, iv, iv_len, NULL, 0);
    if (ret != 0)
    {
        goto cleanup;
    }

    ret = mbedtls_gcm_update(&gcm, input_len, input, output);
    if (ret != 0)
    {
        goto cleanup;
    }

    ret = mbedtls_gcm_finish(&gcm, check_tag, tag_len);
    if (ret != 0)
    {
        goto cleanup;
    }

    /* mbedtls 2.28 的 GCM 解密不自动比对标签，需常量时间比较防篡改与侧信道 */
    unsigned char diff = 0;

    for (size_t i = 0; i < tag_len; i++)
    {
        diff |= (unsigned char)(check_tag[i] ^ tag[i]);
    }
    ret = (diff == 0) ? 0 : MBEDTLS_ERR_GCM_AUTH_FAILED;

cleanup:
    mbedtls_gcm_free(&gcm);
    return ret;
}

/**
 * @brief AES-CBC 加密（PKCS 填充由调用方负责，input_len 须为 16 的倍数）
 * @param input:     明文缓冲区
 * @param input_len: 明文长度（字节，16 的倍数）
 * @param output:    密文输出缓冲区
 * @param iv:        初始向量，16 字节；注意加解密后 iv 被覆盖
 * @param key:       密钥（16/24/32 字节）
 * @param key_len:   密钥长度（字节）
 * @return 0 成功，非 0 为 mbedtls 错误码
 */
int aes_encrypt_cbc(const unsigned char *input, size_t input_len,
                    unsigned char *output,
                    unsigned char iv[16],
                    const unsigned char *key, size_t key_len)
{
    mbedtls_aes_context aes;
    int ret;

    mbedtls_aes_init(&aes);

    ret = mbedtls_aes_setkey_enc(&aes, key, (unsigned int)(key_len * 8u));
    if (ret != 0)
    {
        goto cleanup;
    }

    ret = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, input_len,
                                iv, input, output);

cleanup:
    mbedtls_aes_free(&aes);
    return ret;
}

/**
 * @brief AES-CBC 解密（PKCS 去填充由调用方负责，input_len 须为 16 的倍数）
 * @param input:     密文缓冲区
 * @param input_len: 密文长度（字节，16 的倍数）
 * @param output:    明文输出缓冲区
 * @param iv:        初始向量，16 字节；注意加解密后 iv 被覆盖
 * @param key:       密钥（16/24/32 字节）
 * @param key_len:   密钥长度（字节）
 * @return 0 成功，非 0 为 mbedtls 错误码
 */
int aes_decrypt_cbc(const unsigned char *input, size_t input_len,
                    unsigned char *output,
                    unsigned char iv[16],
                    const unsigned char *key, size_t key_len)
{
    mbedtls_aes_context aes;
    int ret;

    mbedtls_aes_init(&aes);

    ret = mbedtls_aes_setkey_dec(&aes, key, (unsigned int)(key_len * 8u));
    if (ret != 0)
    {
        goto cleanup;
    }

    ret = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, input_len,
                                iv, input, output);

cleanup:
    mbedtls_aes_free(&aes);
    return ret;
}
