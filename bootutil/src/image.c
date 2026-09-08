/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file: image.c
 * @brief: OTA 镜像布局解析实现（见 image.h）
 *        本文件只做解析：算偏移、切字段、读 CRC；不含任何校验/解密逻辑
 */
#include "image.h"

int image_aux_len(image_check_t mode, size_t *out_len)
{
    size_t len;

    if (out_len == NULL)
    {
        return ERR_ARG;
    }

    switch (mode)
    {
    case IMAGE_CHECK_CRC:     len = 0u;                                     break;
    case IMAGE_CHECK_SHA:     len = 32u;                                    break;
    case IMAGE_CHECK_GCM:     len = (size_t)(12u + 16u);                    break;
    case IMAGE_CHECK_CBC:     len = 16u;                                    break;
    case IMAGE_CHECK_CBC_SHA: len = (size_t)(16u + 32u);                    break;
    default:                  return ERR_ARG;
    }

    *out_len = len;
    return ERR_OK;
}

int image_overhead(image_check_t mode, size_t version_len, size_t tag_len,
                   size_t *out_overhead)
{
    size_t aux_len = 0;
    int rc;

    if (out_overhead == NULL)
    {
        return ERR_ARG;
    }

    rc = image_aux_len(mode, &aux_len);
    if (rc != ERR_OK)
    {
        return rc;
    }

    /* 逐项比较，避免 version_len/tag_len 取极大值时加法回绕 */
    if (version_len > (SIZE_MAX - IMAGE_CRC_LEN) ||
        tag_len > (SIZE_MAX - IMAGE_CRC_LEN - version_len) ||
        aux_len > (SIZE_MAX - IMAGE_CRC_LEN - version_len - tag_len))
    {
        return ERR_OVERFLOW;
    }

    *out_overhead = IMAGE_CRC_LEN + version_len + tag_len + aux_len;
    return ERR_OK;
}

int image_parse(const uint8_t *buf, size_t len, image_check_t mode,
                size_t version_len, size_t tag_len, int is_front,
                image_view_t *out_view)
{
    size_t overhead = 0;
    size_t aux_len = 0;
    const uint8_t *crc_bytes;
    int rc;

    if (buf == NULL || out_view == NULL)
    {
        return ERR_ARG;
    }

    rc = image_overhead(mode, version_len, tag_len, &overhead);
    if (rc != ERR_OK)
    {
        return rc;
    }
    if (len < overhead)
    {
        return ERR_TOO_SMALL;
    }

    rc = image_aux_len(mode, &aux_len);
    if (rc != ERR_OK)
    {
        return rc;
    }

    out_view->aux_len = aux_len;
    out_view->version_len = version_len;
    out_view->tag_len = tag_len;
    out_view->payload_len = len - overhead;

    if (is_front)
    {
        /* crc | version | tag | aux | payload */
        crc_bytes = buf;
        out_view->version = buf + IMAGE_CRC_LEN;
        out_view->tag = out_view->version + version_len;
        out_view->aux = out_view->tag + tag_len;
        out_view->payload = out_view->aux + aux_len;
    }
    else
    {
        /* payload | aux | version | tag | crc */
        out_view->payload = buf;
        out_view->aux = out_view->payload + out_view->payload_len;
        out_view->version = out_view->aux + aux_len;
        out_view->tag = out_view->version + version_len;
        crc_bytes = out_view->tag + tag_len;
    }

    out_view->crc_stored = (uint32_t)crc_bytes[0] |
                           ((uint32_t)crc_bytes[1] << 8) |
                           ((uint32_t)crc_bytes[2] << 16) |
                           ((uint32_t)crc_bytes[3] << 24);
    return ERR_OK;
}
