# 自包装 crc库
def _reflect(x: int, width: int) -> int:
    """位反转：将x的低width位按位反转"""
    result = 0
    for _ in range(width):
        result = (result << 1) | (x & 1)
        x >>= 1
    return result


def crc_generic(
        data: bytes,
        init: int,
        refin: bool,
        refout: bool,
        xor_out: int,
        poly: int,
        width: int = 32
) -> int:
    """
    :param data: 输入字节数据
    :param init: 寄存器初始值
    :param refin: 输入字节是否按位反转
    :param refout: 最终结果是否按位反转
    :param xor_out: 最终异或输出值
    :param poly: 生成多项式
    :param width: CRC位宽
    :return: CRC计算结果
    """
    mask = (1 << width) - 1
    if refin:
        reg = _reflect(init, width) & mask
        poly = _reflect(poly, width)
        if width >= 8:
            for byte in data:
                reg ^= byte
                for _ in range(8):
                    reg = ((reg >> 1) ^ poly) if reg & 1 else (reg >> 1)
                    reg &= mask
        else:
            # 窄宽度逐位输入，避免高位丢弃
            for byte in data:
                for i in range(8):
                    reg ^= (byte >> i) & 1
                    reg = ((reg >> 1) ^ poly) if reg & 1 else (reg >> 1)
                    reg &= mask
    else:
        reg = init & mask
        top_bit = 1 << (width - 1)
        if width >= 8:
            for byte in data:
                reg ^= byte << (width - 8)
                for _ in range(8):
                    reg = ((reg << 1) ^ poly) if reg & top_bit else (reg << 1)
                    reg &= mask
        else:
            # 窄宽度逐位输入，避免负移位
            for byte in data:
                for i in range(7, -1, -1):
                    reg ^= ((byte >> i) & 1) << (width - 1)
                    reg = ((reg << 1) ^ poly) if reg & top_bit else (reg << 1)
                    reg &= mask

    # 仅当输入输出反射状态不一致时，才需要最终反转
    if refin != refout:
        reg = _reflect(reg, width)
    return (reg ^ xor_out) & mask


def crc32(data: bytes) -> int:
    """标准CRC-32（以太网/zip通用）"""
    return crc_generic(
        data,
        init=0xFFFFFFFF,
        refin=True,
        refout=True,
        xor_out=0xFFFFFFFF,
        poly=0x04C11DB7,
        width=32
    )


def crc32_mpeg2(data: bytes) -> int:
    """CRC-32/MPEG-2（视频编码）"""
    return crc_generic(
        data,
        init=0xFFFFFFFF,
        refin=False,
        refout=False,
        xor_out=0x00000000,
        poly=0x04C11DB7,
        width=32
    )


def crc16_modbus(data: bytes) -> int:
    """CRC-16/MODBUS"""
    return crc_generic(
        data,
        init=0xFFFF,
        refin=True,
        refout=True,
        xor_out=0x0000,
        poly=0x8005,
        width=16
    )


def crc16_ccitt(data: bytes) -> int:
    """CRC-16/CCITT-FALSE（多数通信协议默）"""
    return crc_generic(
        data,
        init=0xFFFF,
        refin=False,
        refout=False,
        xor_out=0x0000,
        poly=0x1021,
        width=16
    )


def crc8_maxim(data: bytes) -> int:
    """CRC-8/MAXIM（单总线、小型外设）"""
    return crc_generic(
        data,
        init=0x00,
        refin=True,
        refout=True,
        xor_out=0x00,
        poly=0x31,
        width=8
    )


# -------------------------- 自测验证 --------------------------
if __name__ == "__main__":
    test_data = b"123456789"
    assert crc32(test_data) == 0xCBF43926, "CRC32校验失败"
    assert crc32_mpeg2(test_data) == 0x0376E6E7, "CRC32-MPEG2校验失败"
    assert crc16_modbus(test_data) == 0x4B37, "CRC16-MODBUS校验失败"
    assert crc16_ccitt(test_data) == 0x29B1, "CRC16-CCITT校验失败"
    assert crc8_maxim(test_data) == 0xA1, "CRC8-MAXIM校验失败"
    print("所有标准CRC模型验证通过")
