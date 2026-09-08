#include "algorithm.h"
#include "mbedtls/sha256.h"
int sha256(const uint8_t *data, size_t len, uint8_t output[32])
{
    mbedtls_sha256_context ctx;
    int ret;

    mbedtls_sha256_init(&ctx);
    ret  = mbedtls_sha256_starts_ret(&ctx, 0);
    ret |= mbedtls_sha256_update_ret(&ctx, data, len);
    ret |= mbedtls_sha256_finish_ret(&ctx, output);
    mbedtls_sha256_free(&ctx);
    return ret;
}
