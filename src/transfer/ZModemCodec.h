#pragma once

#include <QByteArray>
#include <QtGlobal>
#include <cstdint>

enum class ZModemFrameType : uint8_t {
    ZrqInit = 0,
    ZrInit = 1,
    ZsInit = 2,
    ZAck = 3,
    ZFile = 4,
    ZSkip = 5,
    ZNak = 6,
    ZAbort = 7,
    ZFin = 8,
    ZRpos = 9,
    ZData = 10,
    ZEof = 11,
    ZFerr = 12,
    ZCrc = 13,
    ZChallenge = 14,
    ZCompl = 15,
    ZCan = 16,
    ZFreeCnt = 17,
    ZCommand = 18,
    ZStderr = 19,
};

enum class ZModemFrameEnd : uint8_t {
    CrcE = 'h',
    CrcG = 'i',
    CrcQ = 'j',
    CrcW = 'k',
};

class ZModemCodec final
{
public:
    static constexpr uint8_t ZPAD = '*';
    static constexpr uint8_t ZDLE = 0x18;
    static constexpr uint8_t ZBIN = 'A';
    static constexpr uint8_t ZHEX = 'B';
    static constexpr uint8_t ZBIN32 = 'C';
    static constexpr uint8_t XON = 0x11;
    static constexpr uint8_t XOFF = 0x13;

    static constexpr uint8_t CANFDX = 0x01;
    static constexpr uint8_t CANOVIO = 0x02;
    static constexpr uint8_t CANFC32 = 0x20;
    static constexpr uint8_t ESCCTL = 0x40;

    static QByteArray hexHeader(ZModemFrameType type, const QByteArray &header);
    static QByteArray binaryHeader16(ZModemFrameType type, const QByteArray &header);
    static QByteArray binaryHeader32(ZModemFrameType type, const QByteArray &header);
    static QByteArray dataSubpacket(const QByteArray &data, ZModemFrameEnd end,
                                    bool crc32, bool escapeAllControl = false,
                                    bool turbo = false);
    static QByteArray encodeEscaped(const QByteArray &data, bool escapeAllControl = false,
                                    bool turbo = false, uint8_t previous = 0);

    static uint16_t updateCrc16(uint16_t crc, uint8_t byte);
    static uint32_t updateCrc32(uint32_t crc, uint8_t byte);
    static uint16_t crc16(const QByteArray &data);
    static uint32_t crc32(const QByteArray &data);

private:
    static bool needsEscape(uint8_t byte, uint8_t previous, bool escapeAllControl, bool turbo);
};
