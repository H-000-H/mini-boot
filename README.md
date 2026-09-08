# mini-boot
- 兼容mini-tree的简单ota和bootloar项目

## 工具路径
- 打包工具: tools/main.py
- 查看工具: tools\check_bin_file.py 或者 test\main.cpp

## 使用方法

### 打包 (py)

```bash
# CRC 校验(默认): payload + version + crc
python tools/main.py app.bin image.bin --version 1.0.0 --tag h-000-h

# SHA-256 校验
python tools/main.py app.bin image.bin --check SHA

# AES-GCM 加密(--key 必填, 16/24/32 字节 hex)
python tools/main.py app.bin image.bin --check GCM --key 00112233445566778899aabbccddeeff

# AES-CBC + HMAC-SHA-256(--mac_key 不给则复用 --key)
python tools/main.py app.bin image.bin --check CBC_SHA \
    --key 00112233445566778899aabbccddeeff \
    --mac_key ffeeddccbbaa99887766554433221100

# 元数据放文件头部(默认在尾部)
python tools/main.py app.bin image.bin --is_front
```

可选参数: `--version`(默认 1.0.0) `--tag`(默认空) `--is_front`
`--crc_init --crc_refin --crc_refout --crc_xor_out --crc_poly`(改 CRC 模型, 默认值即标准 CRC-32)

### 查看 / 对比 (py)

```bash
python tools/check_bin_file.py dump image.bin --length 64      # 看前 64 字节
python tools/check_bin_file.py dump image.bin --offset -64     # 看末尾 64 字节(负数从尾部倒数)

python tools/check_bin_file.py diff image.bin app.bin          # 逐字节对比, 生成 diff_report.txt
python tools/check_bin_file.py diff a.bin b.bin ./diff_out     # 额外导出差异片段到目录

# 解析镜像后再对比(自动剥掉 version/tag/crc/aux, 加密模式先解密)
python tools/check_bin_file.py image_diff image.bin app.bin --plain_b
python tools/check_bin_file.py image_diff a.bin b.bin --check GCM --key 00112233445566778899aabbccddeeff
```

`diff` / `image_diff` 完全一致返回 0, 存在差异返回 1, 可直接用于脚本判断。

### 查看 / 校验 (cpp)

PC 端工具, 用 bootutil 的 image.c(解析) + read.c(校验解密) + err.c(错误码), 与板上同一套代码。

```powershell
cmake -S test -B test/build -G Ninja
cmake --build test/build
cd test
.\build\pc_check.exe image.bin app.bin
```

输出示例:

```
Mode    : CRC (aux 0 B, overhead 9 B, 元数据在后)
Version : "1.0.0" (5 B)
Payload : 22592 B @ offset 0
CRC32   : stored 0xbf7e1c67 / calc 0xbf7e1c67 -> MATCH
Verify  : OK (CRC 校验通过)
Compare : payload vs plain, 长度差 +0 B, 重叠区 22592 B, 不同字节 0 B
RESULT  : PASS
```

常用选项:

| 选项 | 说明 |
|---|---|
| `--check CRC\|SHA\|GCM\|CBC\|CBC_SHA` | 打包模式, 默认 CRC |
| `--key hex` | 解密密钥, GCM/CBC/CBC_SHA 必填 |
| `--mac_key hex` | MAC 密钥(CBC_SHA), 不给则复用 `--key` |
| `--version_len N` | version 字节数, 默认 5(对应 "1.0.0") |
| `--tag_len N` | tag 字节数, 默认 0 |
| `--front` | 元数据在头部(默认在尾部) |
| `--max_diff N` | 最多列出多少条差异, 默认 16 |

退出码: `0` 校验通过且一致, `1` 校验失败或存在差异, `2` 用法/环境问题。

需要 SHA/GCM/CBC 解密时(会一并编译 mbedtls):

```powershell
cmake -S test -B test/build -G Ninja -DTOOL_ENABLE_CRYPTO=ON
cmake --build test/build
```

## 镜像布局

```
元数据在后(默认):  payload | aux | version | tag | crc32(4B)
元数据在前(--is_front): crc32(4B) | version | tag | aux | payload

aux: CRC=空  SHA=sha256(32B)  GCM=nonce(12B)+tag(16B)  CBC=iv(16B)  CBC_SHA=iv(16B)+hmac(32B)
```

- `payload`: 原始固件, 或加密后的密文(加密模式下 crc 对密文计算)
- `version` / `tag`: 变长字符串, **格式内不记录长度**, 解析时必须由调用方给出
- CBC_SHA 的 HMAC 覆盖 `iv || 密文`

## 打包与校验的参数必须一致

| 打包端 (tools/main.py) | 校验端 |
|---|---|
| `--check` | `--check` / `image_read_cfg_t.mode` |
| `--version` 的字节数 | `--version_len` / `cfg.version_len` |
| `--tag` 的字节数 | `--tag_len` / `cfg.tag_len` |
| `--is_front` | `--front` / `cfg.is_front` |
| `--crc_init --crc_refin --crc_refout --crc_xor_out --crc_poly` | `boot_config.h` 的 `CRC_MODEL_*` |

板上改 CRC 模型只需改 `bootutil/inc/boot_config.h`, 不用动 CMake:

```c
#define CRC_MODEL_POLY      0x04C11DB7u   /* 改成与打包一致的值 */
```

## 常见问题

**1. `ModuleNotFoundError: No module named 'm_crc.image_crc'`**
脚本按脚本所在目录导入 `tools/m_crc` 包, 从任意目录运行 `python tools/main.py` 都可以, 不需要设 `PYTHONPATH`。

**2. 校验不过 (crc mismatch / sha256 mismatch / authentication failed)**
先核对上表的参数是否一致, 尤其是 `--version_len`、`--tag_len`——version 写成 `1.0.10`(6 字节) 却按 5 解析会整段错位。

**3. 编译报 `undefined reference to sha256 / aes_decrypt_gcm`**
开了 `IMAGE_CRYPTO_ENABLE`(默认 1) 却没有把 `algorithm/src` 下的 `aes.c`、`hmac.c`、`sha.c` 和 mbedtls 一起编译链接。用 `test/CMakeLists.txt` 构建即可自动处理; 关掉加密则只依赖 `crc.c`。

**4. mbedtls 报 `void*` 隐式转换 / `jump to label`**
mbedtls 只能用 C 编译器编译, 不要混进 g++ 命令。`test/CMakeLists.txt` 里 `project(pc_check C CXX)`, `.c` 自动走 C 编译器。
