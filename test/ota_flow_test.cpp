/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @file ota_flow_test.cpp
 * @brief OTA 全流程 PC 端模拟测试：boot → 下载 → 校验激活 → 确认 / 回滚。
 * @note  用 RAM 模拟"多区域 flash"（NOR 保真：write 按位相与、erase 置 0xFF），
 *        跑的是真实的 start.c / ota_state / flash.c / read.c 代码路径（不是复刻逻辑）。
 *
 *        "复位"的模拟：进程内 static 不会真被清零，所以每轮"复位"用
 *        ota_close()/ota_rollback_close()（开关位本就是 RAM 态）+ mini_boot_state_load()
 *        （持久位会整体从介质重载）还原到上电初态；s_downloaded_size 这类
 *        boot 会话内的量在真实复位后会清零，本测试不依赖它跨"复位"延续。
 *
 * 编译: 见 test/CMakeLists.txt 的 ota_flow_test 目标（CONFIG_OTA_DUAL_PARTITION=1，关加密）
 */
#include "start.h"
#include "ota_state.h"
#include "flash.h"
#include "err.h"
#include "read.h"
#include "boot_config.h"
#include "crc.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

/* ============================ RAM flash 模拟 ============================ */
constexpr uint32_t kAreaSize  = 0x1000u; /* 每个镜像区 4KB */
constexpr uint32_t kStateSize = 0x0100u; /* 状态区（mock 不区分扇区） */
constexpr uint32_t kFlashSize = 0x4000u;

std::array<uint8_t, kFlashSize> s_flash{};
int s_fail = 0;

const flash_area_t s_areas[] =
{
    { static_cast<uint32_t>(FLASH_AREA_ID_BOOTLOADER), 0u, 0x0000u, kAreaSize  },
    { static_cast<uint32_t>(FLASH_AREA_ID_IMAGE_0),    0u, 0x1000u, kAreaSize  },
    { static_cast<uint32_t>(FLASH_AREA_ID_STATE),      0u, 0x2000u, kStateSize },
    { static_cast<uint32_t>(FLASH_AREA_ID_IMAGE_1),    0u, 0x3000u, kAreaSize  },
};

void check(bool cond, std::string_view msg)
{
    std::cout << (cond ? "ok   : " : "FAIL : ") << msg << '\n';
    if (!cond)
    {
        ++s_fail;
    }
}

int m_open(uint32_t fa_id, const flash_area_t **area)
{
    if (area == nullptr)
    {
        return ERR_ARG;
    }
    for (const auto &a : s_areas)
    {
        if (a.fa_id == fa_id)
        {
            *area = &a;
            return ERR_OK;
        }
    }
    return ERR_NOT_SUPPORTED;
}

int m_erase(const flash_area_t *area, uint32_t off, uint32_t len)
{
    if ((area == nullptr) || ((off + len) > area->fa_size))
    {
        return ERR_ARG;
    }
    std::fill_n(s_flash.begin() + area->fa_offset + off, len, 0xFFu);
    return ERR_OK;
}

int m_write(const flash_area_t *area, uint32_t off, const void *buf, uint32_t len)
{
    const auto *p = static_cast<const uint8_t *>(buf);

    if ((area == nullptr) || (p == nullptr) || ((off + len) > area->fa_size))
    {
        return ERR_ARG;
    }
    for (uint32_t i = 0u; i < len; i++)
    {
        s_flash[area->fa_offset + off + i] &= p[i]; /* NOR：只能把 1 写成 0 */
    }
    return ERR_OK;
}

int m_read(const flash_area_t *area, uint32_t off, void *buf, uint32_t len)
{
    auto *p = static_cast<uint8_t *>(buf);

    if ((area == nullptr) || (p == nullptr) || ((off + len) > area->fa_size))
    {
        return ERR_ARG;
    }
    std::copy_n(s_flash.begin() + area->fa_offset + off, len, p);
    return ERR_OK;
}

int m_get_sectors(const flash_area_t *area, uint32_t max_count,
                  flash_sector_t *sectors, uint32_t *count)
{
    if ((area == nullptr) || (sectors == nullptr) || (count == nullptr) || (max_count < 1u))
    {
        return ERR_ARG;
    }
    sectors[0].fs_off = 0u;
    sectors[0].fs_size = area->fa_size;
    *count = 1u;
    return ERR_OK;
}

const flash_ops_t s_ops = { m_open, m_erase, m_write, m_read, m_get_sectors };

/* ============================ 工具 ============================ */

std::vector<uint8_t> area_read(uint32_t fa_id, size_t len)
{
    const flash_area_t *a = nullptr;
    std::vector<uint8_t> out(len);

    if ((m_open(fa_id, &a) != ERR_OK) || (m_read(a, 0u, out.data(), static_cast<uint32_t>(len)) != ERR_OK))
    {
        return {};
    }
    return out;
}

bool area_program(uint32_t fa_id, const std::vector<uint8_t> &data)
{
    const flash_area_t *a = nullptr;

    if (m_open(fa_id, &a) != ERR_OK)
    {
        return false;
    }
    if (m_erase(a, 0u, a->fa_size) != ERR_OK)
    {
        return false;
    }
    return m_write(a, 0u, data.data(), static_cast<uint32_t>(data.size())) == ERR_OK;
}

uint32_t crc32_of(const std::vector<uint8_t> &data)
{
    crc_stream_t s;

    crc_stream_start(&s, CRC_MODEL_INIT, CRC_MODEL_REFIN, CRC_MODEL_REFOUT,
                     CRC_MODEL_XOR_OUT, CRC_MODEL_POLY, CRC_MODEL_WIDTH);
    if (!data.empty())
    {
        crc_stream_feed(&s, data.data(), data.size());
    }
    return crc_stream_finish(&s);
}

/* 按 read.h 的布局打包一份 CRC 模式镜像（等价于 tools/main.py 的产物） */
std::vector<uint8_t> build_image(const std::vector<uint8_t> &payload,
                                 std::string_view version, std::string_view tag, bool is_front)
{
    const uint32_t crc = crc32_of(payload);
    std::vector<uint8_t> img;

    auto put_le32 = [&img](uint32_t v)
    {
        img.push_back(static_cast<uint8_t>(v & 0xFFu));
        img.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
        img.push_back(static_cast<uint8_t>((v >> 16) & 0xFFu));
        img.push_back(static_cast<uint8_t>((v >> 24) & 0xFFu));
    };
    auto put_str = [&img](std::string_view s)
    {
        img.insert(img.end(), s.begin(), s.end());
    };

    if (is_front) /* crc | version | tag | (aux) | payload */
    {
        put_le32(crc);
        put_str(version);
        put_str(tag);
        img.insert(img.end(), payload.begin(), payload.end());
    }
    else /* payload | (aux) | version | tag | crc */
    {
        img.insert(img.end(), payload.begin(), payload.end());
        put_str(version);
        put_str(tag);
        put_le32(crc);
    }

    /* 末尾自描述 meta: magic | mode|0x80(is_front) | version_len | tag_len */
    img.push_back(static_cast<uint8_t>(IMAGE_META_MAGIC));
    img.push_back(static_cast<uint8_t>(static_cast<uint8_t>(IMAGE_CHECK_CRC) |
                                       (is_front ? 0x80u : 0u)));
    img.push_back(static_cast<uint8_t>(version.size()));
    img.push_back(static_cast<uint8_t>(tag.size()));
    return img;
}

std::vector<uint8_t> make_payload(uint8_t seed, size_t len)
{
    std::vector<uint8_t> fw(len);

    for (size_t i = 0u; i < len; i++)
    {
        fw[i] = static_cast<uint8_t>(seed + i);
    }
    return fw;
}

/* 下载钩子：从内存里的镜像流式喂数据（可模拟传输失败 / 中途断电） */
struct stream_src
{
    const std::vector<uint8_t> *img = nullptr;
    size_t pos = 0u;
    bool fail_now = false;
    int fail_after_chunks = -1; /* <0：从不失败；>=0：成功喂完这么多块后失败（模拟中途掉电） */
    int chunks_done = 0;
};

int feed_hook(void *param, uint8_t *buf, uint32_t want, int *out_len)
{
    auto *s = static_cast<stream_src *>(param);

    if (s->fail_now || ((s->fail_after_chunks >= 0) && (s->chunks_done >= s->fail_after_chunks)))
    {
        return ERR_TRANSMIT; /* 模拟传输中断 / 掉电 */
    }
    if ((buf == nullptr) || (out_len == nullptr))
    {
        return ERR_ARG;
    }

    const size_t left = s->img->size() - s->pos;
    const uint32_t n = static_cast<uint32_t>(std::min<size_t>(want, left));

    if (n == 0u)
    {
        return ERR_ARG;
    }
    std::memcpy(buf, s->img->data() + s->pos, n);
    s->pos += n;
    ++s->chunks_done;
    *out_len = static_cast<int>(n);
    return ERR_OK;
}

/* 平台 boot 流程里"跳哪个区"的判定（与 start.c 内部同款，仅用于断言） */
uint32_t boot_target_area(void)
{
    return (ota_current_partition_get() == OTA_STATE_PARTITION_IMAGE_1)
               ? static_cast<uint32_t>(FLASH_AREA_ID_IMAGE_1)
               : static_cast<uint32_t>(FLASH_AREA_ID_IMAGE_0);
}

/* 擦掉状态区 = 模拟"全新设备 / 状态丢失"，用于各阶段互相隔离 */
void reset_state_medium()
{
    const flash_area_t *a = nullptr;

    if (m_open(FLASH_AREA_ID_STATE, &a) == ERR_OK)
    {
        (void)m_erase(a, 0u, a->fa_size);
    }
}

/* 第一条空白记录的位置（用于伪造"状态记录写一半"） */
size_t state_next_free_off()
{
    const flash_area_t *a = nullptr;
    constexpr size_t kRec = OTA_STATE_RECORD_WORDS * sizeof(uint32_t);

    if (m_open(FLASH_AREA_ID_STATE, &a) != ERR_OK)
    {
        return 0u;
    }
    for (size_t off = 0u; (off + kRec) <= a->fa_size; off += kRec)
    {
        uint32_t w[OTA_STATE_RECORD_WORDS] = { 0u, 0u };

        if (m_read(a, static_cast<uint32_t>(off), w, static_cast<uint32_t>(sizeof(w))) != ERR_OK)
        {
            return 0u;
        }
        if ((w[0] == 0xFFFFFFFFu) && (w[1] == 0xFFFFFFFFu))
        {
            return off;
        }
    }
    return a->fa_size;
}

/* 伪造"状态记录写一半掉电"：只写状态字，校验字留 0xFF（该条应整体作废） */
bool torn_state_write(uint32_t state_word)
{
    const flash_area_t *a = nullptr;
    const size_t off = state_next_free_off();
    uint32_t rec[OTA_STATE_RECORD_WORDS] = { 0u, 0u };

    if ((m_open(FLASH_AREA_ID_STATE, &a) != ERR_OK) || (off >= a->fa_size))
    {
        return false;
    }
    ota_state_record_build(rec, state_word);
    return m_write(a, static_cast<uint32_t>(off), &rec[0], sizeof(uint32_t)) == ERR_OK;
}

/* 完整走一轮：开 OTA + 开回滚 + 下载 + 校验激活 */
bool ota_cycle(const std::vector<uint8_t> &img, std::string_view tag)
{
    ota_open();
    ota_rollback_open();

    stream_src s{ &img };
    const bool dl = mini_boot_source_download_stream(feed_hook, &s, static_cast<uint32_t>(img.size())) == ERR_OK;
    const bool act = (dl && (mini_boot_start_ota() == ERR_OK));

    check(dl, std::string(tag) + ": download ok");
    check(act, std::string(tag) + ": activate ok");
    return dl && act;
}

/* 平台侧备用分区检查钩子：按 g_backup_ok 返回 */
int g_backup_ok = 1;

int backup_check_hook(uint32_t backup_partition)
{
    (void)backup_partition;
    return g_backup_ok;
}

/* 模拟一次复位后的上电：开关位归零（RAM 态），再恢复持久位 */
void simulate_reboot(std::string_view tag)
{
    ota_close();
    ota_rollback_close();
    check(mini_boot_state_load() == ERR_OK, tag);
}

} /* namespace */

int main()
{
    constexpr size_t kPayloadLen = 700u; /* > MINI_BOOT_LOAD_MAX(512)，会分多块下载 */

    s_flash.fill(0xFFu);

    /* ---- 0) 平台初始化 ---- */
    check(flash_ops_register(&s_ops) == ERR_OK, "platform: register flash ops");
    check(ota_state_flash_register() == ERR_OK, "platform: register flash state backend");

    /* 出厂已烧 image_0：旧固件 1.0.0 */
    const std::vector<uint8_t> fw_old = make_payload(0x10u, kPayloadLen);
    const std::vector<uint8_t> img_old = build_image(fw_old, "1.0.0", "old", true);
    check(area_program(FLASH_AREA_ID_IMAGE_0, img_old), "factory: image_0 programmed");

    /* ---- 1) 首次上电：无状态记录 → 默认 current=image_0 ---- */
    simulate_reboot("boot#1: state_load ok (no record -> defaults)");
    check(ota_current_partition_get() == OTA_STATE_PARTITION_IMAGE_0, "boot#1: current == image_0");
    check(ota_is_pending() == 0u, "boot#1: pending == 0");
    check(ota_fail_get() == OTA_FAIL_NONE, "boot#1: fail == NONE");
    check(boot_target_area() == static_cast<uint32_t>(FLASH_AREA_ID_IMAGE_0), "boot#1: jumps image_0");

    /* ---- 2) app 启用 OTA + 回滚 ---- */
    ota_open();
    ota_rollback_open();
    check(ota_is_open() == 1u, "app: OTA opened");
    check(ota_is_rollback() == 1u, "app: rollback enabled");
    check(ota_is_double() == 1u, "app: dual-partition capability (compile-time)");

    /* ---- 3) 下载新固件 2.0.0 到非当前分区（image_1） ---- */
    const std::vector<uint8_t> fw_new = make_payload(0x40u, kPayloadLen);
    const std::vector<uint8_t> img_new = build_image(fw_new, "2.0.0", "new", true);
    stream_src src{ &img_new, 0u, false };

    check(mini_boot_source_download_stream(feed_hook, &src, static_cast<uint32_t>(img_new.size())) == ERR_OK,
          "download: stream ok (multi-chunk)");
    check(area_read(FLASH_AREA_ID_IMAGE_1, img_new.size()) == img_new, "download: image_1 bytes match");
    check(area_read(FLASH_AREA_ID_IMAGE_0, img_old.size()) == img_old, "download: image_0 untouched");

    /* ---- 4) 校验 + 激活（置 pending） ---- */
    check(mini_boot_start_ota() == ERR_OK, "activate: start_ota ok");
    check(ota_current_partition_get() == OTA_STATE_PARTITION_IMAGE_1, "activate: current == image_1");
    check(ota_is_pending() == 1u, "activate: pending == 1");
    check(ota_fail_get() == OTA_FAIL_NONE, "activate: fail == NONE");

    /* ---- 5) 复位但 app 没确认 → 回滚 ---- */
    simulate_reboot("reboot#2: state_load ok");
    check(ota_current_partition_get() == OTA_STATE_PARTITION_IMAGE_0, "rollback: current back to image_0");
    check(ota_is_pending() == 0u, "rollback: pending cleared");
    check(ota_fail_get() == OTA_FAIL_VERIFY, "rollback: fail == VERIFY");
    check(boot_target_area() == static_cast<uint32_t>(FLASH_AREA_ID_IMAGE_0), "rollback: jumps image_0");

    /* ---- 6) 再来一轮：app 确认 → 不回滚 ---- */
    ota_open();
    ota_rollback_open();
    stream_src src2{ &img_new, 0u, false };
    check(mini_boot_source_download_stream(feed_hook, &src2, static_cast<uint32_t>(img_new.size())) == ERR_OK,
          "cycle2: download ok");
    check(mini_boot_start_ota() == ERR_OK, "cycle2: activate ok");
    check(ota_is_pending() == 1u, "cycle2: pending == 1");
    check(mini_boot_confirm_ota() == ERR_OK, "cycle2: app confirm ok");
    check(ota_is_pending() == 0u, "cycle2: pending cleared");
    simulate_reboot("reboot#3: state_load ok");
    check(ota_current_partition_get() == OTA_STATE_PARTITION_IMAGE_1, "cycle2: no rollback (stays image_1)");
    check(ota_fail_get() == OTA_FAIL_NONE, "cycle2: fail == NONE");
    check(boot_target_area() == static_cast<uint32_t>(FLASH_AREA_ID_IMAGE_1), "cycle2: jumps image_1");

    /* ---- 7) 坏镜像：校验不过，不激活 ---- */
    ota_open();
    std::vector<uint8_t> img_bad = build_image(make_payload(0x40u, kPayloadLen), "3.0.0", "bad", true);
    img_bad[img_bad.size() - IMAGE_META_LEN - 1u] ^= 0xFFu; /* 破坏 payload 尾字节 */
    stream_src src3{ &img_bad, 0u, false };
    check(mini_boot_source_download_stream(feed_hook, &src3, static_cast<uint32_t>(img_bad.size())) == ERR_OK,
          "bad: download ok");
    check(mini_boot_start_ota() == ERR_CRC_MISMATCH, "bad: verify fails -> ERR_CRC_MISMATCH");
    check(ota_fail_get() == OTA_FAIL_VERIFY, "bad: fail == VERIFY");
    check(ota_current_partition_get() == OTA_STATE_PARTITION_IMAGE_1, "bad: current unchanged");

    /* ---- 8) 传输失败：ERR_TRANSMIT + fail=READ ---- */
    stream_src src4{ &img_new, 0u, true };
    check(mini_boot_source_download_stream(feed_hook, &src4, static_cast<uint32_t>(img_new.size())) == ERR_TRANSMIT,
          "xfer: hook failure -> ERR_TRANSMIT");
    check(ota_fail_get() == OTA_FAIL_READ, "xfer: fail == READ");

    /* ---- 9) 未开 OTA 时拒绝下载 ---- */
    ota_close();
    stream_src src5{ &img_new, 0u, false };
    check(mini_boot_source_download_stream(feed_hook, &src5, static_cast<uint32_t>(img_new.size())) == ERR_OTA_OPEN,
          "closed: download rejected with ERR_OTA_OPEN");

    /* ---- 10) app 侧读状态（不做回滚的刷新） ---- */
    check(mini_boot_state_refresh() == ERR_OK, "refresh: app-side state refresh ok");
    check(ota_fail_get() == OTA_FAIL_READ, "refresh: fail reads back == READ");
    check(ota_current_partition_get() == OTA_STATE_PARTITION_IMAGE_1, "refresh: current reads back == image_1");

    /* ================= 掉电 / 异常场景 ================= */

    /* ---- 11) 状态记录写一半掉电：半写记录作废，旧状态仍生效 ---- */
    reset_state_medium();
    simulate_reboot("p11: fresh boot (no state)");
    check(ota_current_partition_get() == OTA_STATE_PARTITION_IMAGE_0, "p11: default current == image_0");
    (void)ota_cycle(img_new, "p11");
    check(ota_is_pending() == 1u, "p11: pending == 1");
    check(torn_state_write(ota_state_bit_put(0u, OTA_STATE_BIT_CURRENT, 1u)),
          "p11: appended a torn state record (state word only, crc stays 0xFF)");
    simulate_reboot("p11: reboot after torn state write");
    check(ota_current_partition_get() == OTA_STATE_PARTITION_IMAGE_0, "p11: rollback still applied");
    check(ota_is_pending() == 0u, "p11: pending cleared");
    check(ota_fail_get() == OTA_FAIL_VERIFY, "p11: fail == VERIFY");

    /* ---- 12) 下载中途掉电：目标区半写，状态不受影响，仍启动原镜像 ---- */
    reset_state_medium();
    simulate_reboot("p12: fresh boot");
    check(ota_current_partition_get() == OTA_STATE_PARTITION_IMAGE_0, "p12: current == image_0");
    ota_open();
    ota_rollback_open();
    {
        stream_src s{ &img_new, 0u, false, 1 }; /* 成功喂 1 块后"掉电" */
        check(mini_boot_source_download_stream(feed_hook, &s, static_cast<uint32_t>(img_new.size())) == ERR_TRANSMIT,
              "p12: download aborted mid-way -> ERR_TRANSMIT");
    }
    check(ota_fail_get() == OTA_FAIL_READ, "p12: fail == READ");
    simulate_reboot("p12: reboot after aborted download");
    check(ota_current_partition_get() == OTA_STATE_PARTITION_IMAGE_0, "p12: still boots image_0");
    check(ota_is_pending() == 0u, "p12: no pending");
    (void)ota_cycle(img_new, "p12-recover"); /* 重下一次即可（写入前整体擦除） */
    check(ota_is_pending() == 1u, "p12: pending == 1 after recovery");

    /* ---- 13) 回滚目标校验：备用不可用则放弃回滚 ---- */
    reset_state_medium();
    simulate_reboot("p13: fresh boot");
    check(ota_set_backup_check(backup_check_hook) == ERR_OK, "p13: register backup check hook");
    g_backup_ok = 0; /* 假装备用分区已损坏 */
    (void)ota_cycle(img_new, "p13-bad-backup");
    check(ota_is_pending() == 1u, "p13: pending == 1");
    simulate_reboot("p13: reboot (backup unusable)");
    check(ota_current_partition_get() == OTA_STATE_PARTITION_IMAGE_1, "p13: rollback skipped, stays image_1");
    check(ota_is_pending() == 0u, "p13: pending cleared");
    check(ota_fail_get() == OTA_FAIL_VERIFY, "p13: fail == VERIFY");

    /* 对照：备用可用时照常回滚 */
    reset_state_medium();
    g_backup_ok = 1;
    simulate_reboot("p13b: fresh boot");
    (void)ota_cycle(img_new, "p13b-good-backup");
    simulate_reboot("p13b: reboot (backup usable)");
    check(ota_current_partition_get() == OTA_STATE_PARTITION_IMAGE_0, "p13b: rollback applied");
    check(ota_is_pending() == 0u, "p13b: pending cleared");
    check(ota_set_backup_check(nullptr) == ERR_OK, "p13b: unregister backup check");

    if (s_fail != 0)
    {
        std::cout << "\nFLOW TEST: FAIL (" << s_fail << " checks)\n";
        return 1;
    }
    std::cout << "\nFLOW TEST: PASS\n";
    return 0;
}
