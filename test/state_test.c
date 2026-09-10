/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @file state_test.c
 * @brief OTA 持久化状态模块的 PC 端单元测试（含 flash 状态扇区后端 + NOR 保真 mock）。
 * @note  mock 的 write 按位相与（NOR 只能 1->0）、erase 置 0xFF，尽量贴近真实 flash，
 *        以便验证"追加日志 / 半写截断 / 写满擦除"这些关键行为。
 * 用法: cmake -S test -B test/build -G Ninja && cmake --build test/build --target state_test
 *       .\build\state_test
 */
#include "ota_state.h"
#include "flash.h"
#include "err.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MOCK_SIZE 64u /* 8 条记录（8B/条），小扇区方便把"写满->擦除"跑出来 */

static uint8_t g_flash[MOCK_SIZE];
static int g_fail = 0;

#define CHECK(cond, msg)                          \
    do {                                          \
        if (cond) {                               \
            printf("ok   : %s\n", (msg));         \
        } else {                                  \
            printf("FAIL : %s\n", (msg));         \
            g_fail++;                             \
        }                                         \
    } while (0)

static const flash_area_t g_state_area =
{
    (uint32_t)FLASH_AREA_ID_STATE, 0u, 0u, MOCK_SIZE
};

static int mock_open(uint32_t fa_id, const flash_area_t **area)
{
    if (area == NULL)
    {
        return ERR_ARG;
    }
    if (fa_id != (uint32_t)FLASH_AREA_ID_STATE)
    {
        return ERR_NOT_SUPPORTED;
    }
    *area = &g_state_area;
    return ERR_OK;
}

static int mock_erase(const flash_area_t *area, uint32_t off, uint32_t len)
{
    if ((area != &g_state_area) || ((off + len) > MOCK_SIZE))
    {
        return ERR_ARG;
    }
    memset(g_flash + off, 0xFF, len);
    return ERR_OK;
}

static int mock_write(const flash_area_t *area, uint32_t off, const void *buf, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)buf;
    uint32_t i;

    if ((area != &g_state_area) || (buf == NULL) || ((off + len) > MOCK_SIZE))
    {
        return ERR_ARG;
    }
    for (i = 0u; i < len; i++)
    {
        g_flash[off + i] &= p[i]; /* NOR：只能把 1 写成 0 */
    }
    return ERR_OK;
}

static int mock_read(const flash_area_t *area, uint32_t off, void *buf, uint32_t len)
{
    if ((area != &g_state_area) || (buf == NULL) || ((off + len) > MOCK_SIZE))
    {
        return ERR_ARG;
    }
    memcpy(buf, g_flash + off, len);
    return ERR_OK;
}

static const flash_ops_t g_mock_ops = { mock_open, mock_erase, mock_write, mock_read };

int main(void)
{
    uint32_t word = 0u;
    uint32_t record[OTA_STATE_RECORD_WORDS];
    uint32_t a, b, c_word, d, last, resolve;
    uint32_t i;

    memset(g_flash, 0xFF, sizeof(g_flash));

    /* ---- 未注册后端 ---- */
    CHECK(ota_state_load(&word) == ERR_NOT_SUPPORTED, "load before ops_register -> ERR_NOT_SUPPORTED");
    CHECK(ota_state_ops_register(NULL) == ERR_ARG, "register NULL ops -> ERR_ARG");
    CHECK(flash_ops_register(&g_mock_ops) == ERR_OK, "register mock flash ops");
    CHECK(ota_state_load(&word) == ERR_NOT_SUPPORTED, "load before state backend -> ERR_NOT_SUPPORTED");

    CHECK(ota_state_flash_register() == ERR_OK, "register flash state backend");
    CHECK(ota_state_load(&word) == ERR_OTA_STATE, "blank sector -> ERR_OTA_STATE");

    /* ---- 按位打包 + 持久化 ---- */
    a = 0u;
    a = ota_state_bit_put(a, OTA_STATE_BIT_OPEN, 1u);
    a = ota_state_bit_put(a, OTA_STATE_BIT_CURRENT, 1u);
    a = ota_state_bit_put(a, OTA_STATE_BIT_PENDING, 1u);
    a = ota_state_fail_put(a, 3u);

    CHECK(ota_state_store(a) == ERR_OK, "store A");
    CHECK(ota_state_load(&word) == ERR_OK, "load A ok");
    CHECK(ota_state_bit_get(word, OTA_STATE_BIT_OPEN) == 1u, "A.open == 1");
    CHECK(ota_state_bit_get(word, OTA_STATE_BIT_CURRENT) == 1u, "A.current == 1");
    CHECK(ota_state_bit_get(word, OTA_STATE_BIT_PENDING) == 1u, "A.pending == 1");
    CHECK(ota_state_fail_get(word) == 3u, "A.fail == 3");
    CHECK((word & OTA_STATE_MAGIC_MASK) == (OTA_STATE_MAGIC_VALUE << OTA_STATE_MAGIC_SHIFT),
          "A.magic == 0xA5");

    /* ---- 最新者胜出（位置最靠后的有效记录） ---- */
    b = ota_state_fail_put(0u, 1u);
    c_word = ota_state_bit_put(0u, OTA_STATE_BIT_ROLLBACK, 1u);
    CHECK(ota_state_store(b) == ERR_OK, "store B");
    CHECK(ota_state_store(c_word) == ERR_OK, "store C");
    CHECK((ota_state_load(&word) == ERR_OK) &&
          (ota_state_bit_get(word, OTA_STATE_BIT_ROLLBACK) == 1u), "latest == C");

    /* ---- 模拟半写：只写状态字，校验字仍为 0xFF ---- */
    ota_state_record_build(record, a);
    CHECK(mock_write(&g_state_area, 24u, &record[0], 4u) == ERR_OK, "simulate torn write (state word only)");
    CHECK((ota_state_load(&word) == ERR_OK) &&
          (ota_state_bit_get(word, OTA_STATE_BIT_ROLLBACK) == 1u),
          "torn record truncated, latest stays C");

    /* ---- 落点非空白 -> 先擦再写，最新仍可取 ---- */
    d = ota_state_bit_put(0u, OTA_STATE_BIT_FORCE, 1u);
    CHECK(ota_state_store(d) == ERR_OK, "store D after torn record");
    CHECK((ota_state_load(&word) == ERR_OK) &&
          (ota_state_bit_get(word, OTA_STATE_BIT_FORCE) == 1u), "latest == D");

    /* ---- 连续写到写满，触发擦除后仍取到最新 ---- */
    last = 0u;
    for (i = 0u; i < 14u; i++)
    {
        last = ota_state_fail_put(0u, i & 0x3u);
        if (ota_state_store(last) != ERR_OK)
        {
            CHECK(0, "store during fill");
        }
    }
    CHECK((ota_state_load(&word) == ERR_OK) &&
          (ota_state_fail_get(word) == ota_state_fail_get(last)), "latest survives fill + erase");

    /* ---- 记录校验 ---- */
    ota_state_record_build(record, a);
    CHECK(ota_state_record_valid(record) == 1, "built record is valid");
    record[1] ^= 0x1u;
    CHECK(ota_state_record_valid(record) == 0, "bad crc rejected");
    ota_state_record_build(record, a);
    record[0] ^= (1u << OTA_STATE_MAGIC_SHIFT);
    CHECK(ota_state_record_valid(record) == 0, "bad magic rejected");

    /* ---- 待确认回滚规则（ota_state_resolve_pending） ---- */
    CHECK(ota_state_resolve_pending(NULL, 3u) == 0, "resolve(NULL) -> 0");

    resolve = ota_state_bit_put(0u, OTA_STATE_BIT_ROLLBACK, 1u);
    CHECK(ota_state_resolve_pending(&resolve, 3u) == 0, "no pending -> no rollback");
    CHECK(ota_state_bit_get(resolve, OTA_STATE_BIT_ROLLBACK) == 1u, "no rollback keeps other bits");

    resolve = ota_state_bit_put(0u, OTA_STATE_BIT_PENDING, 1u); /* current = 0 */
    CHECK(ota_state_resolve_pending(&resolve, 3u) == 1, "pending @current0 -> rollback");
    CHECK(ota_state_bit_get(resolve, OTA_STATE_BIT_CURRENT) == 1u, "rollback: current 0 -> 1");
    CHECK(ota_state_bit_get(resolve, OTA_STATE_BIT_PENDING) == 0u, "rollback: clears pending");
    CHECK(ota_state_fail_get(resolve) == 3u, "rollback: records fail code");

    resolve = ota_state_bit_put(0u, OTA_STATE_BIT_CURRENT, 1u);
    resolve = ota_state_bit_put(resolve, OTA_STATE_BIT_PENDING, 1u); /* current = 1 */
    CHECK(ota_state_resolve_pending(&resolve, 3u) == 1, "pending @current1 -> rollback");
    CHECK(ota_state_bit_get(resolve, OTA_STATE_BIT_CURRENT) == 0u, "rollback: current 1 -> 0");

    /* ---- 位工具 ---- */
    CHECK(ota_state_fail_put(0u, 0x7u) == (3u << OTA_STATE_FAIL_SHIFT), "fail code clipped to 2 bits");
    CHECK(ota_state_bit_put(0x2u, OTA_STATE_BIT_OPEN, 1u) == 0x3u, "bit_put sets bit");
    CHECK(ota_state_bit_put(0x3u, OTA_STATE_BIT_OPEN, 0u) == 0x2u, "bit_put clears bit");

    if (g_fail != 0)
    {
        printf("\nSTATE TEST: FAIL (%d checks)\n", g_fail);
        return 1;
    }
    printf("\nSTATE TEST: PASS\n");
    return 0;
}
