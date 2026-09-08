/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file: read.c
 * @brief: OTA 镜像读取实现（见 read.h）：各模式的校验与解密分支都在这里
 *        布局解析全部交给 image.h，本文件不关心偏移怎么算
 */
#include "read.h"

#include <string.h>

#include "boot_config.h" /* CRC_MODEL_*：镜像使用的 CRC 模型，可用 CONFIG_CRC_* 覆盖 */
#include "crc.h"

#if IMAGE_CRYPTO_ENABLE
#include "algorithm.h"
#include "mbedtls/cipher.h" /* MBEDTLS_ERR_CIPHER_AUTH_FAILED */
#endif

#if IMAGE_CRYPTO_ENABLE
/*
 * @brief: 加密模式下的缓冲区比较，每次都比较所有字节，避免提前返回导致加密无效增强密钥的安全性
 * @param a: 待比较的第一个缓冲区
 * @param b: 待比较的第二个缓冲区
 * @param len: 比较长度
 * @return: 相等返回 1，不等返回 0
*/
static int ct_equal(const uint8_t *a, const uint8_t *b, size_t len)
{
    uint8_t diff = 0;
    size_t i;

    for (i = 0; i < len; i++)
    {
        diff = (uint8_t)(diff | (uint8_t)(a[i] ^ b[i]));
    }
    return diff == 0;
}
#endif

static uint32_t calc_crc(const image_view_t *view)
{
    return crc_generic(view->payload, view->payload_len, CRC_MODEL_INIT, CRC_MODEL_REFIN,
                       CRC_MODEL_REFOUT, CRC_MODEL_XOR_OUT, CRC_MODEL_POLY,
                       CRC_MODEL_WIDTH);
}

/** 明文模式（CRC/SHA）：out 为 NULL 时只报长度不拷贝 */
static int copy_plain(const image_view_t *view, uint8_t *out, size_t cap,
                      size_t *out_plain_len)
{
    if (out_plain_len != NULL)
    {
        *out_plain_len = view->payload_len;
    }
    if (out == NULL)
    {
        return ERR_OK;
    }
    if (cap < view->payload_len)
    {
        return ERR_BUF_TOO_SMALL;
    }
    memcpy(out, view->payload, view->payload_len);
    return ERR_OK;
}

/* ---------------- 各模式处理分支 ---------------- */

static int check_sha(const image_view_t *view)
{
#if IMAGE_CRYPTO_ENABLE
    uint8_t digest[READ_HASH_LEN];

    if (view->aux_len < READ_HASH_LEN)
    {
        return ERR_TOO_SMALL;
    }
    if (sha256(view->payload, view->payload_len, digest) != 0)
    {
        return ERR_HASH_FAILED;
    }
    return ct_equal(digest, view->aux, READ_HASH_LEN) ? ERR_OK : ERR_HASH_MISMATCH;
#else
    (void)view;
    return ERR_UNSUPPORTED;
#endif
}

#if IMAGE_CRYPTO_ENABLE
/** PKCS#7 去填充，返回真实明文长度 */
static int pkcs7_unpad(const uint8_t *data, size_t len, size_t *out_len)
{
    uint8_t pad;
    size_t i;

    if (data == NULL || out_len == NULL || len == 0 || (len % READ_CBC_BLOCK_LEN) != 0)
    {
        return ERR_ARG;
    }

    pad = data[len - 1];
    if (pad == 0 || pad > READ_CBC_BLOCK_LEN || (size_t)pad > len)
    {
        return ERR_PADDING;
    }
    for (i = 0; i < (size_t)pad; i++)
    {
        if (data[len - 1 - i] != pad)
        {
            return ERR_PADDING;
        }
    }

    *out_len = len - pad;
    return ERR_OK;
}

/** AES-CBC 解密 + 去填充（iv 取自 aux 前 16B） */
static int cbc_decrypt(const image_view_t *view, const uint8_t *key, size_t key_len,
                       uint8_t *out, size_t cap, size_t *out_plain_len)
{
    uint8_t iv[READ_CBC_IV_LEN];
    size_t plain_len = 0;
    int rc;

    if (view->aux_len < READ_CBC_IV_LEN)
    {
        return ERR_TOO_SMALL;
    }
    if (view->payload_len == 0 || (view->payload_len % READ_CBC_BLOCK_LEN) != 0)
    {
        return ERR_ARG;
    }
    if (cap < view->payload_len)
    {
        return ERR_BUF_TOO_SMALL;
    }

    /* aes_decrypt_cbc 会覆盖 iv，用本地副本，避免改写只读的镜像数据 */
    memcpy(iv, view->aux, READ_CBC_IV_LEN);
    if (aes_decrypt_cbc(view->payload, view->payload_len, out, iv, key, key_len) != 0)
    {
        return ERR_DECRYPT_FAILED;
    }

    rc = pkcs7_unpad(out, view->payload_len, &plain_len);
    if (rc != ERR_OK)
    {
        return rc;
    }

    *out_plain_len = plain_len;
    return ERR_OK;
}
#endif /* IMAGE_READ_ENABLE_CRYPTO */

static int gcm_decrypt(const image_view_t *view, const uint8_t *key, size_t key_len,
                       uint8_t *out, size_t cap, size_t *out_plain_len)
{
#if IMAGE_CRYPTO_ENABLE
    if (view->aux_len < (size_t)(READ_GCM_NONCE_LEN + READ_GCM_TAG_LEN))
    {
        return ERR_TOO_SMALL;
    }
    if (cap < view->payload_len)
    {
        return ERR_BUF_TOO_SMALL;
    }
    /* GCM 解密失败只可能是标签不符或参数错误，统一按认证失败处理 */
    if (aes_decrypt_gcm(view->payload, view->payload_len, out,
                        view->aux, READ_GCM_NONCE_LEN, key, key_len,
                        view->aux + READ_GCM_NONCE_LEN, READ_GCM_TAG_LEN) != 0)
    {
        return ERR_AUTH_FAILED;
    }

    *out_plain_len = view->payload_len;
    return ERR_OK;
#else
    (void)view;
    (void)key;
    (void)key_len;
    (void)out;
    (void)cap;
    (void)out_plain_len;
    return ERR_UNSUPPORTED;
#endif
}

static int cbc_decrypt_dispatch(const image_view_t *view, const uint8_t *key,
                                size_t key_len, uint8_t *out, size_t cap,
                                size_t *out_plain_len)
{
#if IMAGE_CRYPTO_ENABLE
    return cbc_decrypt(view, key, key_len, out, cap, out_plain_len);
#else
    (void)view;
    (void)key;
    (void)key_len;
    (void)out;
    (void)cap;
    (void)out_plain_len;
    return ERR_UNSUPPORTED;
#endif
}

/** CBC_SHA：先对 IV||密文 验 HMAC，通过后才解密（覆盖范围须与打包端一致） */
static int cbc_sha_decrypt(const image_view_t *view, const uint8_t *key, size_t key_len,
                           const uint8_t *mac_key, size_t mac_key_len,
                           uint8_t *out, size_t cap, size_t *out_plain_len)
{
#if IMAGE_CRYPTO_ENABLE
    size_t plain_len = 0;
    int rc;

    if (view->aux_len < (size_t)(READ_CBC_IV_LEN + READ_HASH_LEN))
    {
        return ERR_TOO_SMALL;
    }
    if (view->payload_len == 0 || (view->payload_len % READ_CBC_BLOCK_LEN) != 0)
    {
        return ERR_ARG;
    }
    if (cap < view->payload_len)
    {
        return ERR_BUF_TOO_SMALL;
    }

    /* 组合接口内部即 verify-then-decrypt：先验 IV||密文 的 HMAC，不过不解密 */
    rc = aes_cbc_hmac_decrypt(view->payload, view->payload_len, out,
                              view->aux, key, key_len,
                              mac_key, mac_key_len,
                              view->aux + READ_CBC_IV_LEN);
    if (rc == MBEDTLS_ERR_CIPHER_AUTH_FAILED)
    {
        return ERR_AUTH_FAILED;
    }
    if (rc != 0)
    {
        return ERR_DECRYPT_FAILED;
    }

    rc = pkcs7_unpad(out, view->payload_len, &plain_len);
    if (rc != ERR_OK)
    {
        return rc;
    }

    *out_plain_len = plain_len;
    return ERR_OK;
#else
    (void)view;
    (void)key;
    (void)key_len;
    (void)mac_key;
    (void)mac_key_len;
    (void)out;
    (void)cap;
    (void)out_plain_len;
    return ERR_UNSUPPORTED;
#endif
}

/* ---------------- 对外接口 ---------------- */

/** 加密模式共用的入参检查 */
static int check_crypto_args(const image_read_cfg_t *cfg, const uint8_t *out_payload)
{
    if (cfg->key == NULL)
    {
        return ERR_ARG;
    }
    if (cfg->key_len != 16u && cfg->key_len != 24u && cfg->key_len != 32u)
    {
        return ERR_ARG;
    }
    /* 解密必须给出输出缓冲区，没有地方可写就无法完成认证 */
    if (out_payload == NULL)
    {
        return ERR_ARG;
    }
    return ERR_OK;
}

int image_verify(const uint8_t *buf, size_t len, const image_read_cfg_t *cfg,
                 image_view_t *out_view, uint32_t *out_crc_calc)
{
    image_view_t view;
    int rc;

    if (buf == NULL || cfg == NULL)
    {
        return ERR_ARG;
    }

    rc = image_parse(buf, len, cfg->mode, cfg->version_len, cfg->tag_len,
                     cfg->is_front, &view);
    if (rc != ERR_OK)
    {
        return rc;
    }
    if (out_view != NULL)
    {
        *out_view = view;
    }

    switch (cfg->mode)
    {
    case IMAGE_CHECK_CRC:
    {
        uint32_t calc = calc_crc(&view);

        if (out_crc_calc != NULL)
        {
            *out_crc_calc = calc;
        }
        return (calc == view.crc_stored) ? ERR_OK : ERR_CRC_MISMATCH;
    }
    case IMAGE_CHECK_SHA:
        return check_sha(&view);
    case IMAGE_CHECK_GCM:
        /* 未解密无法认证，此处只确认 aux 长度够放 nonce+tag */
        return (view.aux_len >= (size_t)(READ_GCM_NONCE_LEN + READ_GCM_TAG_LEN))
               ? ERR_OK : ERR_TOO_SMALL;
    case IMAGE_CHECK_CBC:
        return (view.aux_len >= READ_CBC_IV_LEN) ? ERR_OK : ERR_TOO_SMALL;
    case IMAGE_CHECK_CBC_SHA:
        return (view.aux_len >= (size_t)(READ_CBC_IV_LEN + READ_HASH_LEN))
               ? ERR_OK : ERR_TOO_SMALL;
    default:
        return ERR_ARG;
    }
}

int image_read(const uint8_t *buf, size_t len, const image_read_cfg_t *cfg,
               uint8_t *out_payload, size_t payload_cap,
               image_view_t *out_view, size_t *out_plain_len,
               uint32_t *out_crc_calc)
{
    image_view_t view;
    int rc;

    if (buf == NULL || cfg == NULL)
    {
        return ERR_ARG;
    }

    rc = image_parse(buf, len, cfg->mode, cfg->version_len, cfg->tag_len,
                     cfg->is_front, &view);
    if (rc != ERR_OK)
    {
        return rc;
    }
    if (out_view != NULL)
    {
        *out_view = view;
    }

    switch (cfg->mode)
    {
    case IMAGE_CHECK_CRC:
    {
        uint32_t calc = calc_crc(&view);

        if (out_crc_calc != NULL)
        {
            *out_crc_calc = calc;
        }
        if (calc != view.crc_stored)
        {
            return ERR_CRC_MISMATCH;
        }
        return copy_plain(&view, out_payload, payload_cap, out_plain_len);
    }
    case IMAGE_CHECK_SHA:
        rc = check_sha(&view);
        if (rc != ERR_OK)
        {
            return rc;
        }
        return copy_plain(&view, out_payload, payload_cap, out_plain_len);
    case IMAGE_CHECK_GCM:
        rc = check_crypto_args(cfg, out_payload);
        if (rc != ERR_OK)
        {
            return rc;
        }
        return gcm_decrypt(&view, cfg->key, cfg->key_len, out_payload, payload_cap,
                           out_plain_len);
    case IMAGE_CHECK_CBC:
        rc = check_crypto_args(cfg, out_payload);
        if (rc != ERR_OK)
        {
            return rc;
        }
        return cbc_decrypt_dispatch(&view, cfg->key, cfg->key_len, out_payload,
                                    payload_cap, out_plain_len);
    case IMAGE_CHECK_CBC_SHA:
    {
        /* 未单独给出 MAC 密钥时回退用加密密钥 */
        const uint8_t *mac_key = (cfg->mac_key != NULL) ? cfg->mac_key : cfg->key;
        size_t mac_key_len = (cfg->mac_key != NULL) ? cfg->mac_key_len : cfg->key_len;

        rc = check_crypto_args(cfg, out_payload);
        if (rc != ERR_OK)
        {
            return rc;
        }
        return cbc_sha_decrypt(&view, cfg->key, cfg->key_len, mac_key, mac_key_len,
                               out_payload, payload_cap, out_plain_len);
    }
    default:
        return ERR_ARG;
    }
}
