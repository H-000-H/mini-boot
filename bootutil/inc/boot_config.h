#ifndef BOOT_CONFIG_H
#define BOOT_CONFIG_H
#if __has_include(<config.h>) && __has_include(<compiler_compat.h>)
#include <config.h>
#elif __has_include(<mini_boot_config.h>)
#include "mini_boot_config.h"
#endif

#if CONFIG_BOOT_APP_ADDR_1
#elif defined(CONFIG_BOOT_APP_ADDR_1)
#else
#warning  "please set app address 1"
#endif

#if CONFIG_FLASH_START_ADDR
#define FLASH_START_ADDR  CONFIG_FLASH_START_ADDR
#elif defined(FLASH_START_ADDR)
#else
#define FLASH_START_ADDR    0X8000000
#endif
#if BOOT_DUAL_PART_ENABLE
#if CONFIG_BOOT_APP_ADDR_2
#elif defined(CONFIG_BOOT_APP_ADDR_2)
#else
#warning  "please set app address 2"
#endif
#endif

#define CONFIG_CRC_ENABLE 1
#if CONFIG_CRC_ENABLE
#if CONFIG_CRC_TABLE_SIZE
#define CRC_TABLE_SIZE CONFIG_CRC_TABLE_SIZE
#else
#define CRC_TABLE_SIZE    256 /* 256 项表不允许改数值 这里这样只是默认以后可能会变成增大但是现在不允许改 */
#endif

#if defined(__cplusplus)
#else
_Static_assert(CRC_TABLE_SIZE == 256, "CRC_TABLE_SIZE must be 256");
#endif

#define CRC_MODE_BITWISE 0
#define CRC_MODE_TABLE 1
#if CONFIG_CRC_MODE
#define CRC_MODE CONFIG_CRC_MODE
#else
#define CRC_MODE    CRC_MODE_TABLE
#endif

#endif /* CONFIG_CRC_ENABLE */

/* ---------- 镜像读取能力开关 ----------
 * IMAGE_CRYPTO_ENABLE: 1 编入 SHA/HMAC/AES 分支, 支持 SHA/GCM/CBC/CBC_SHA 镜像(需链接 mbedcrypto);
 *                      0 只支持 CRC 模式, read.c 里的解密代码整段不编。
 * 注意: 关闭后还要 -ffunction-sections -fdata-sections + -Wl,--gc-sections 才能剔除 mbedtls。
 */
#if defined(CONFIG_IMAGE_CRYPTO)
#define IMAGE_CRYPTO_ENABLE     CONFIG_IMAGE_CRYPTO
#elif defined(IMAGE_CRYPTO_ENABLE)
#else
#define IMAGE_CRYPTO_ENABLE     1
#endif

/* ---------- 镜像 CRC 模型参数 ----------
 * 镜像校验使用的 CRC 模型，必须与 tools/main.py 打包时的参数一致
 * (--crc_init/--crc_refin/--crc_refout/--crc_xor_out/--crc_poly)，默认标准 CRC-32。
 * 改模型: 直接改下面 #else 分支的默认值, 或在别处定义 CRC_MODEL_XXX / CONFIG_CRC_XXX。
 * refin/refout/xor_out/width 都可能合法取 0, 所以用 defined() 
 */
#if defined(CONFIG_CRC_INIT)
#define CRC_MODEL_INIT      CONFIG_CRC_INIT
#elif defined(CRC_MODEL_INIT)
#else
#define CRC_MODEL_INIT      0xFFFFFFFFu
#endif

#if defined(CONFIG_CRC_REFIN)
#define CRC_MODEL_REFIN     CONFIG_CRC_REFIN
#elif defined(CRC_MODEL_REFIN)
#else
#define CRC_MODEL_REFIN     1
#endif

#if defined(CONFIG_CRC_REFOUT)
#define CRC_MODEL_REFOUT    CONFIG_CRC_REFOUT
#elif defined(CRC_MODEL_REFOUT)
#else
#define CRC_MODEL_REFOUT    1
#endif

#if defined(CONFIG_CRC_XOR_OUT)
#define CRC_MODEL_XOR_OUT   CONFIG_CRC_XOR_OUT
#elif defined(CRC_MODEL_XOR_OUT)
#else
#define CRC_MODEL_XOR_OUT   0xFFFFFFFFu
#endif

#if defined(CONFIG_CRC_POLY)
#define CRC_MODEL_POLY      CONFIG_CRC_POLY
#elif defined(CRC_MODEL_POLY)
#else
#define CRC_MODEL_POLY      0x04C11DB7u
#endif

#if defined(CONFIG_CRC_WIDTH)
#define CRC_MODEL_WIDTH     CONFIG_CRC_WIDTH
#elif defined(CRC_MODEL_WIDTH)
#else
#define CRC_MODEL_WIDTH     32u
#endif

#endif /* BOOT_CONFIG_H */
