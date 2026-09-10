/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @file state_test.cpp
 * @brief OTA 持久化状态模块的 PC 端单元测试（含 flash 状态扇区后端 + NOR 保真 mock）。
 * @note  mock 的 write 按位相与（NOR 只能 1->0）、erase 置 0xFF，尽量贴近真实 flash，
 *        以便验证"追加日志 / 半写截断 / 写满擦除"这些关键行为。
 * 用法: cmake -S test -B test/build -G Ninja && cmake --build test/build --target state_test
 *       .\build\state_test
 */
#include "ota_state.h"
#include "flash.h"
#include "err.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <string_view>

namespace {

constexpr uint32_t kMockSize = 64u; /* 8 条记录（8B/条），小扇区方便把"写满->擦除"跑出来 */

/* NOR 保真 mock：write 按位相与（1 只能变 0），erase 全置 0xFF */
std::array<uint8_t, kMockSize> s_flash;
int s_fail = 0;

const flash_area_t s_state_area
{
    static_cast<uint32_t>(FLASH_AREA_ID_STATE), 0u, 0u, kMockSize
};

void check(bool cond, std::string_view msg)
{
    if (cond)
    {
        std::cout << "ok   : " << msg << '\n';
    }
    else
    {
        std::cout << "FAIL : " << msg << '\n';
        ++s_fail;
    }
}

int mock_open(uint32_t fa_id, const flash_area_t **area)
{
    if (area == nullptr)
    {
        return ERR_ARG;
    }
    if (fa_id != static_cast<uint32_t>(FLASH_AREA_ID_STATE))
    {
        return ERR_NOT_SUPPORTED;
    }
    *area = &s_state_area;
    return ERR_OK;
}

int mock_erase(const flash_area_t *area, uint32_t off, uint32_t len)
{
    if ((area != &s_state_area) || ((off + len) > kMockSize))
    {
        return ERR_ARG;
    }
    std::fill_n(s_flash.begin() + off, len, 0xFFu);
    return ERR_OK;
}

int mock_write(const flash_area_t *area, uint32_t off, const void *buf, uint32_t len)
{
    const auto *p = static_cast<const uint8_t *>(buf);

    if ((area != &s_state_area) || (p == nullptr) || ((off + len) > kMockSize))
    {
        return ERR_ARG;
    }
    for (uint32_t i = 0u; i < len; i++)
    {
        s_flash[off + i] &= p[i]; /* NOR：只能把 1 写成 0 */
    }
    return ERR_OK;
}

int mock_read(const flash_area_t *area, uint32_t off, void *buf, uint32_t len)
{
    auto *p = static_cast<uint8_t *>(buf);

    if ((area != &s_state_area) || (p == nullptr) || ((off + len) > kMockSize))
    {
        return ERR_ARG;
    }
    std::copy_n(s_flash.begin() + off, len, p);
    return ERR_OK;
}

int mock_get_sectors(const flash_area_t *area, uint32_t max_count,
                     flash_sector_t *sectors, uint32_t *count)
{
    if ((area != &s_state_area) || (sectors == nullptr) || (count == nullptr) || (max_count < 1u))
    {
        return ERR_ARG;
    }
    sectors[0].fs_off = 0u;
    sectors[0].fs_size = kMockSize;
    *count = 1u;
    return ERR_OK;
}

const flash_ops_t s_mock_ops = { mock_open, mock_erase, mock_write, mock_read, mock_get_sectors };

} /* namespace */

int main()
{
    constexpr uint32_t kTornWriteOffset = 24u; /* 第 4 条记录起始处：只写状态字、校验字保持 0xFF，模拟半写掉电 */
    constexpr uint32_t kMagicWord = OTA_STATE_MAGIC_VALUE << OTA_STATE_MAGIC_SHIFT;

    std::array<uint32_t, OTA_STATE_RECORD_WORDS> record{};
    uint32_t word = 0u;
    uint32_t a = 0u, b = 0u, c_word = 0u, d = 0u, last = 0u, resolve = 0u, full = 0u;

    s_flash.fill(0xFFu);

    /* ---- 未注册后端 ---- */
    check(ota_state_load(&word) == ERR_NOT_SUPPORTED, "load before ops_register -> ERR_NOT_SUPPORTED");
    check(ota_state_update(1u << OTA_STATE_BIT_PENDING, 0u) == ERR_NOT_SUPPORTED,
          "update before ops_register -> ERR_NOT_SUPPORTED");
    check(ota_state_ops_register(nullptr) == ERR_ARG, "register NULL ops -> ERR_ARG");
    check(flash_ops_register(&s_mock_ops) == ERR_OK, "register mock flash ops");

    /* ---- flash 扇区清单（可选 ops: get_sectors） ---- */
    flash_sector_t sec{};
    uint32_t sec_count = 0u;
    check((flash_area_get_sectors(&s_state_area, 1u, &sec, &sec_count) == ERR_OK) &&
          (sec_count == 1u) && (sec.fs_off == 0u) && (sec.fs_size == kMockSize),
          "get_sectors: area reported as one sector");
    {
        const flash_ops_t trimmed = { mock_open, mock_erase, mock_write, mock_read }; /* 未实现 get_sectors */
        check(flash_ops_register(&trimmed) == ERR_OK, "register trimmed ops (no get_sectors)");
        check(flash_area_get_sectors(&s_state_area, 1u, &sec, &sec_count) == ERR_NOT_SUPPORTED,
              "get_sectors unimplemented -> ERR_NOT_SUPPORTED");
        check(flash_ops_register(&s_mock_ops) == ERR_OK, "restore full mock ops");
    }
    check(ota_state_load(&word) == ERR_NOT_SUPPORTED, "load before state backend -> ERR_NOT_SUPPORTED");

    check(ota_state_flash_register() == ERR_OK, "register flash state backend");
    check(ota_state_load(&word) == ERR_OTA_STATE, "blank sector -> ERR_OTA_STATE");

    /* ---- 按位打包 + 持久化 ---- */
    a = ota_state_bit_put(a, OTA_STATE_BIT_OPEN, 1u);
    a = ota_state_bit_put(a, OTA_STATE_BIT_CURRENT, 1u);
    a = ota_state_bit_put(a, OTA_STATE_BIT_PENDING, 1u);
    a = ota_state_fail_put(a, 3u);

    check(ota_state_store(a) == ERR_OK, "store A");
    check(ota_state_load(&word) == ERR_OK, "load A ok");
    check(ota_state_bit_get(word, OTA_STATE_BIT_OPEN) == 1u, "A.open == 1");
    check(ota_state_bit_get(word, OTA_STATE_BIT_CURRENT) == 1u, "A.current == 1");
    check(ota_state_bit_get(word, OTA_STATE_BIT_PENDING) == 1u, "A.pending == 1");
    check(ota_state_fail_get(word) == 3u, "A.fail == 3");
    check((word & OTA_STATE_MAGIC_MASK) == kMagicWord, "A.magic == 0xA5");

    /* ---- 最新者胜出（位置最靠后的有效记录） ---- */
    b = ota_state_fail_put(0u, 1u);
    c_word = ota_state_bit_put(0u, OTA_STATE_BIT_ROLLBACK, 1u);
    check(ota_state_store(b) == ERR_OK, "store B");
    check(ota_state_store(c_word) == ERR_OK, "store C");
    check((ota_state_load(&word) == ERR_OK) &&
          (ota_state_bit_get(word, OTA_STATE_BIT_ROLLBACK) == 1u), "latest == C");

    /* ---- 模拟半写：只写状态字，校验字仍为 0xFF ---- */
    ota_state_record_build(record.data(), a);
    check(mock_write(&s_state_area, kTornWriteOffset, record.data(), sizeof(uint32_t)) == ERR_OK,
          "simulate torn write (state word only)");
    check((ota_state_load(&word) == ERR_OK) &&
          (ota_state_bit_get(word, OTA_STATE_BIT_ROLLBACK) == 1u),
          "torn record truncated, latest stays C");

    /* ---- 落点非空白 -> 先擦再写，最新仍可取 ---- */
    d = ota_state_bit_put(0u, OTA_STATE_BIT_FORCE, 1u);
    check(ota_state_store(d) == ERR_OK, "store D after torn record");
    check((ota_state_load(&word) == ERR_OK) &&
          (ota_state_bit_get(word, OTA_STATE_BIT_FORCE) == 1u), "latest == D");

    /* ---- 连续写到写满，触发擦除后仍取到最新 ---- */
    for (uint32_t i = 0u; i < 14u; i++)
    {
        last = ota_state_fail_put(0u, i & 0x3u);
        check(ota_state_store(last) == ERR_OK, "store during fill");
    }
    check((ota_state_load(&word) == ERR_OK) &&
          (ota_state_fail_get(word) == ota_state_fail_get(last)), "latest survives fill + erase");

    /* ---- 记录校验 ---- */
    ota_state_record_build(record.data(), a);
    check(ota_state_record_valid(record.data()) == 1, "built record is valid");
    record[1] ^= 0x1u;
    check(ota_state_record_valid(record.data()) == 0, "bad crc rejected");
    ota_state_record_build(record.data(), a);
    record[0] ^= (1u << OTA_STATE_MAGIC_SHIFT);
    check(ota_state_record_valid(record.data()) == 0, "bad magic rejected");

    /* ---- 待确认回滚规则（ota_state_resolve_pending） ---- */
    check(ota_state_resolve_pending(nullptr, 3u) == 0, "resolve(NULL) -> 0");

    resolve = ota_state_bit_put(0u, OTA_STATE_BIT_ROLLBACK, 1u);
    check(ota_state_resolve_pending(&resolve, 3u) == 0, "no pending -> no rollback");
    check(ota_state_bit_get(resolve, OTA_STATE_BIT_ROLLBACK) == 1u, "no rollback keeps other bits");

    resolve = ota_state_bit_put(0u, OTA_STATE_BIT_PENDING, 1u); /* current = 0 */
    check(ota_state_resolve_pending(&resolve, 3u) == 1, "pending @current0 -> rollback");
    check(ota_state_bit_get(resolve, OTA_STATE_BIT_CURRENT) == 1u, "rollback: current 0 -> 1");
    check(ota_state_bit_get(resolve, OTA_STATE_BIT_PENDING) == 0u, "rollback: clears pending");
    check(ota_state_fail_get(resolve) == 3u, "rollback: records fail code");

    resolve = ota_state_bit_put(0u, OTA_STATE_BIT_CURRENT, 1u);
    resolve = ota_state_bit_put(resolve, OTA_STATE_BIT_PENDING, 1u); /* current = 1 */
    check(ota_state_resolve_pending(&resolve, 3u) == 1, "pending @current1 -> rollback");
    check(ota_state_bit_get(resolve, OTA_STATE_BIT_CURRENT) == 0u, "rollback: current 1 -> 0");

    /* ---- 读-改-写：只动指定的持久位，其它位保留（app 侧 confirm 场景） ---- */
    full = ota_state_bit_put(0u, OTA_STATE_BIT_CURRENT, 1u);
    full = ota_state_bit_put(full, OTA_STATE_BIT_PENDING, 1u);
    full = ota_state_fail_put(full, 3u);
    check(ota_state_store(full) == ERR_OK, "state_update: seed {current=1,pending=1,fail=3}");

    /* app 那份 RAM 副本是 0，只清 pending（旧实现会把 current/fail 一起冲掉） */
    check(ota_state_update(1u << OTA_STATE_BIT_PENDING, 0u) == ERR_OK, "state_update: clear pending only");
    check(ota_state_load(&word) == ERR_OK, "state_update: reload");
    check(ota_state_bit_get(word, OTA_STATE_BIT_PENDING) == 0u, "state_update: pending cleared");
    check(ota_state_bit_get(word, OTA_STATE_BIT_CURRENT) == 1u, "state_update: current preserved");
    check(ota_state_fail_get(word) == 3u, "state_update: fail_code preserved");

    /* 只改失败码，其余位保留 */
    check(ota_state_update(OTA_STATE_FAIL_MASK, 1u << OTA_STATE_FAIL_SHIFT) == ERR_OK,
          "state_update: set fail=1 only");
    check(ota_state_load(&word) == ERR_OK, "state_update: reload2");
    check(ota_state_fail_get(word) == 1u, "state_update: fail updated");
    check(ota_state_bit_get(word, OTA_STATE_BIT_CURRENT) == 1u, "state_update: current still 1");

    /* ---- 位工具 ---- */
    check(ota_state_fail_put(0u, 0x7u) == (3u << OTA_STATE_FAIL_SHIFT), "fail code clipped to 2 bits");
    check(ota_state_bit_put(0x2u, OTA_STATE_BIT_OPEN, 1u) == 0x3u, "bit_put sets bit");
    check(ota_state_bit_put(0x3u, OTA_STATE_BIT_OPEN, 0u) == 0x2u, "bit_put clears bit");

    if (s_fail != 0)
    {
        std::cout << "\nSTATE TEST: FAIL (" << s_fail << " checks)\n";
        return 1;
    }
    std::cout << "\nSTATE TEST: PASS\n";
    return 0;
}
