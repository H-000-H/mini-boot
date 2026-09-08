/**
 * @copyright: SPDX-License-Identifier: Apache-2.0
 * @file: main.cpp
 * @brief: OTA 镜像读取/校验工具（宿主机编译运行，板上用 image.c + read.c + crc.c）
 *        解析与校验全部走 bootutil 的 image.h/read.h，本文件只负责读文件与打印
 * 编译(仅 CRC): g++ -std=c++17 test/main.cpp bootutil/src/image.c bootutil/src/read.c ^
 *                    bootutil/src/err.c algorithm/src/crc.c ^
 *                    -I bootutil/inc -I algorithm/inc -o test/main.exe
 * 编译(含加密): 加密默认开(boot_config.h 的 IMAGE_CRYPTO_ENABLE=1), 只需把 algorithm/src 下的
 *               .c 与 mbedtls 一起编译链接; 不想编解密则 -DCONFIG_IMAGE_CRYPTO=0
 * 用法: main.exe <image.bin> <plain.bin> [--check CRC|SHA|GCM|CBC|CBC_SHA]
 *                [--key hex] [--version_len N] [--tag_len N] [--front] [--max_diff N]
 *       参数必须与 tools/main.py 打包时一致（version/tag 变长，格式里不记录长度）
 * 退出码: 0 校验通过且 payload 与原始 bin 一致, 1 校验失败或存在差异, 2 用法/环境问题
 */
#include "read.h"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct options
{
    std::string image_path;
    std::string plain_path;
    image_check_t mode = IMAGE_CHECK_CRC;
    std::vector<uint8_t> key;      /* 加密密钥，十六进制解析 */
    std::vector<uint8_t> mac_key;  /* MAC 密钥（CBC_SHA），空则回退用 key */
    size_t version_len = 5;   /* 对应打包默认 version "1.0.0" */
    size_t tag_len = 0;       /* 对应打包默认 tag "" */
    int is_front = 0;
    size_t max_diff = 16;
};

bool is_crypto_mode(image_check_t mode)
{
    return mode == IMAGE_CHECK_GCM || mode == IMAGE_CHECK_CBC ||
           mode == IMAGE_CHECK_CBC_SHA;
}

void print_usage()
{
    std::cout <<
        "用法: main.exe <image.bin> <plain.bin> [选项]\n"
        "  --check CRC|SHA|GCM|CBC|CBC_SHA  打包模式(默认 CRC), 决定 aux 长度与校验方式\n"
        "  --key hex                        解密密钥(16/24/32 字节)，GCM/CBC/CBC_SHA 必填\n"
        "  --mac_key hex                    MAC 密钥(CBC_SHA)，不给则复用 --key\n"
        "  --version_len N                  version 字节数(默认 5, 如 1.0.0)\n"
        "  --tag_len N                      tag 字节数(默认 0)\n"
        "  --front                          元数据在头部(默认在尾部)\n"
        "  --max_diff N                     最多列出多少条差异(默认 16)\n"
        "退出码: 0 校验通过且一致, 1 校验失败或存在差异, 2 用法/环境问题\n";
}

bool parse_mode(const std::string &text, image_check_t &mode)
{
    if (text == "CRC")     { mode = IMAGE_CHECK_CRC;     return true; }
    if (text == "SHA")     { mode = IMAGE_CHECK_SHA;     return true; }
    if (text == "GCM")     { mode = IMAGE_CHECK_GCM;     return true; }
    if (text == "CBC")     { mode = IMAGE_CHECK_CBC;     return true; }
    if (text == "CBC_SHA") { mode = IMAGE_CHECK_CBC_SHA; return true; }
    return false;
}

const char *mode_name(image_check_t mode)
{
    switch (mode)
    {
    case IMAGE_CHECK_CRC:     return "CRC";
    case IMAGE_CHECK_SHA:     return "SHA";
    case IMAGE_CHECK_GCM:     return "GCM";
    case IMAGE_CHECK_CBC:     return "CBC";
    case IMAGE_CHECK_CBC_SHA: return "CBC_SHA";
    default:                  return "?";
    }
}

bool parse_size(const std::string &text, size_t &out)
{
    if (text.empty())
    {
        return false;
    }
    errno = 0;
    char *end = nullptr;
    unsigned long long value = std::strtoull(text.c_str(), &end, 0);
    if (errno != 0 || end != text.c_str() + text.size())
    {
        return false;
    }
    out = static_cast<size_t>(value);
    return true;
}

bool parse_hex_key(const std::string &text, std::vector<uint8_t> &out)
{
    if (text.empty() || (text.size() % 2) != 0)
    {
        return false;
    }
    out.clear();
    for (size_t i = 0; i < text.size(); i += 2)
    {
        std::string byte_text = text.substr(i, 2);
        errno = 0;
        char *end = nullptr;
        unsigned long value = std::strtoul(byte_text.c_str(), &end, 16);
        if (errno != 0 || end != byte_text.c_str() + byte_text.size())
        {
            return false;
        }
        out.push_back(static_cast<uint8_t>(value));
    }
    return true;
}

bool read_file(const std::string &path, std::vector<uint8_t> &out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        std::cerr << "[错误] 无法打开文件: " << path << "\n";
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

std::string as_text(const uint8_t *data, size_t len)
{
    return std::string(reinterpret_cast<const char *>(data), len);
}

std::string hex_byte(uint8_t value)
{
    std::ostringstream os;
    os << "0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(value);
    return os.str();
}

} /* namespace */

int main(int argc, char *argv[])
{
    options opt;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--check")
        {
            if (i + 1 >= argc || !parse_mode(argv[++i], opt.mode))
            {
                std::cerr << "[错误] --check 取值非法\n";
                print_usage();
                return 2;
            }
        }
        else if (arg == "--key")
        {
            if (i + 1 >= argc || !parse_hex_key(argv[++i], opt.key))
            {
                std::cerr << "[错误] --key 必须是偶数长度的十六进制串\n";
                print_usage();
                return 2;
            }
        }
        else if (arg == "--mac_key")
        {
            if (i + 1 >= argc || !parse_hex_key(argv[++i], opt.mac_key))
            {
                std::cerr << "[错误] --mac_key 必须是偶数长度的十六进制串\n";
                print_usage();
                return 2;
            }
        }
        else if (arg == "--version_len" || arg == "--tag_len" || arg == "--max_diff")
        {
            size_t value = 0;
            if (i + 1 >= argc || !parse_size(argv[++i], value))
            {
                std::cerr << "[错误] " << arg << " 取值非法\n";
                print_usage();
                return 2;
            }
            if (arg == "--version_len")     { opt.version_len = value; }
            else if (arg == "--tag_len")    { opt.tag_len = value; }
            else                            { opt.max_diff = value; }
        }
        else if (arg == "--front")
        {
            opt.is_front = 1;
        }
        else if (!arg.empty() && arg[0] == '-')
        {
            std::cerr << "[错误] 未知选项: " << arg << "\n";
            print_usage();
            return 2;
        }
        else if (opt.image_path.empty())
        {
            opt.image_path = arg;
        }
        else if (opt.plain_path.empty())
        {
            opt.plain_path = arg;
        }
        else
        {
            std::cerr << "[错误] 多余的位置参数: " << arg << "\n";
            print_usage();
            return 2;
        }
    }

    if (opt.image_path.empty() || opt.plain_path.empty())
    {
        print_usage();
        return 2;
    }

    std::vector<uint8_t> image_data;
    std::vector<uint8_t> plain_data;
    if (!read_file(opt.image_path, image_data) || !read_file(opt.plain_path, plain_data))
    {
        return 2;
    }

    /* 加密模式需要解密，先备好输出缓冲区（明文不会比密文长） */
    const bool crypto = is_crypto_mode(opt.mode);
    std::vector<uint8_t> plain_out;
    if (crypto)
    {
        plain_out.resize(image_data.size());
    }

    image_read_cfg_t cfg = {};
    cfg.mode = opt.mode;
    cfg.version_len = opt.version_len;
    cfg.tag_len = opt.tag_len;
    cfg.is_front = opt.is_front;
    cfg.key = opt.key.empty() ? nullptr : opt.key.data();
    cfg.key_len = opt.key.size();
    cfg.mac_key = opt.mac_key.empty() ? nullptr : opt.mac_key.data();
    cfg.mac_key_len = opt.mac_key.size();

    image_view_t view = {};
    uint32_t crc_calc = 0;
    size_t plain_len = 0;

    int rc = image_read(image_data.data(), image_data.size(), &cfg,
                        plain_out.empty() ? nullptr : plain_out.data(),
                        plain_out.size(), &view, &plain_len, &crc_calc);
    if (rc != ERR_OK)
    {
        /* 校验类失败说明镜像本身有问题，归为镜像错误；其余归为用法/环境问题 */
        const bool image_bad = (rc == ERR_CRC_MISMATCH || rc == ERR_HASH_MISMATCH ||
                                rc == ERR_AUTH_FAILED || rc == ERR_PADDING ||
                                rc == ERR_DECRYPT_FAILED || rc == ERR_HASH_FAILED);

        std::cerr << "[错误] 镜像读取失败: " << err_str(rc) << " (" << rc << ")\n";
        if (image_bad)
        {
            std::cerr << "       镜像校验未通过：内容可能被篡改, 或打包参数与本命令不一致\n";
        }
        else if (rc == ERR_UNSUPPORTED)
        {
            std::cerr << "       该模式需要 boot_config.h 的 IMAGE_CRYPTO_ENABLE=1 并链接 mbedcrypto\n";
        }
        else
        {
            std::cerr << "       请核对 --check/--key/--version_len/--tag_len/--front 是否与打包时一致\n";
        }
        return image_bad ? 1 : 2;
    }

    size_t overhead = 0;
    (void)image_overhead(opt.mode, opt.version_len, opt.tag_len, &overhead);
    const size_t payload_off = static_cast<size_t>(view.payload - image_data.data());

    std::cout << "Image   : " << opt.image_path << " (" << image_data.size() << " B)\n"
              << "Plain   : " << opt.plain_path << " (" << plain_data.size() << " B)\n"
              << "Mode    : " << mode_name(opt.mode)
              << " (aux " << view.aux_len << " B, overhead " << overhead << " B, "
              << (opt.is_front ? "元数据在前" : "元数据在后") << ")\n"
              << "Version : \"" << as_text(view.version, view.version_len) << "\" ("
              << view.version_len << " B)\n"
              << "Tag     : \"" << as_text(view.tag, view.tag_len) << "\" ("
              << view.tag_len << " B)\n"
              << "Payload : " << view.payload_len << " B @ offset " << payload_off << "\n"
              << "CRC32   : stored 0x" << std::hex << std::setw(8) << std::setfill('0')
              << view.crc_stored << std::dec;
    if (opt.mode == IMAGE_CHECK_CRC)
    {
        std::cout << " / calc 0x" << std::hex << std::setw(8) << std::setfill('0')
                  << crc_calc << std::dec << " -> MATCH";
    }
    else
    {
        std::cout << " (该模式由 " << mode_name(opt.mode) << " 校验, CRC 字段不参与判定)";
    }
    std::cout << "\n"
              << "Verify  : OK (" << mode_name(opt.mode) << " 校验通过)\n";
    if (crypto)
    {
        std::cout << "Decrypt : 明文 " << plain_len << " B (已解密并去填充)\n";
    }

    /* 加密模式比较解密后的明文，明文模式直接比较 payload */
    const uint8_t *cmp_data = crypto ? plain_out.data() : view.payload;
    const size_t cmp_len = crypto ? plain_len : view.payload_len;

    const size_t n = std::min(cmp_len, plain_data.size());
    size_t diff = 0;
    std::vector<size_t> offsets;
    for (size_t i = 0; i < n; ++i)
    {
        if (cmp_data[i] != plain_data[i])
        {
            ++diff;
            if (offsets.size() < opt.max_diff)
            {
                offsets.push_back(i);
            }
        }
    }

    const long long len_diff = static_cast<long long>(cmp_len) -
                               static_cast<long long>(plain_data.size());

    std::cout << "Compare : payload vs plain, 长度差 " << (len_diff >= 0 ? "+" : "")
              << len_diff << " B, 重叠区 " << n << " B, 不同字节 " << diff << " B\n";
    if (!offsets.empty())
    {
        std::cout << "首批差异 (offset: image plain)\n";
        for (size_t off : offsets)
        {
            std::cout << "  " << off << " (0x" << std::hex << off << std::dec << "): "
                      << hex_byte(cmp_data[off]) << " " << hex_byte(plain_data[off]) << "\n";
        }
        if (diff > offsets.size())
        {
            std::cout << "  ... 另有 " << (diff - offsets.size()) << " 处未列出 (--max_diff)\n";
        }
    }

    const bool same = (cmp_len == plain_data.size()) && (diff == 0);
    if (!same && len_diff != 0 && diff == 0)
    {
        std::cout << "提示    : 重叠区完全一致但长度不同, 多出/缺失 " << std::llabs(len_diff) << " B\n";
    }

    std::cout << "RESULT  : " << ((rc == ERR_OK && same) ? "PASS" : "FAIL") << "\n";
    return (rc == ERR_OK && same) ? 0 : 1;
}
