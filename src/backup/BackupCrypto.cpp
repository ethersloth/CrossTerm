#include "BackupCrypto.h"

#include <QRandomGenerator>
#include <QtEndian>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>

extern "C" {
#include "monocypher.h"
}

namespace {
constexpr char kMagic[8] = {'C', 'T', 'B', 'A', 'C', 'K', 'U', 'P'};
constexpr quint8 kFormatVersion = 1;
constexpr quint8 kKdfArgon2id = 1;
constexpr int kSaltSize = 16;
constexpr int kNonceSize = 24;
constexpr int kMacSize = 16;
constexpr int kKeySize = 32;
constexpr int kAdSize = 64; // everything before the MAC
constexpr int kHeaderSize = kAdSize + kMacSize;

// Upper bounds accepted when reading a file, so a crafted header cannot make
// us allocate or spin without limit. Writers use far less (see KdfParams).
constexpr quint32 kMaxBlocks = 1024 * 1024; // 1 GiB
constexpr quint32 kMaxPasses = 16;
constexpr quint32 kMaxLanes = 8;

bool validParams(const BackupCrypto::KdfParams &params)
{
    return params.lanes >= 1 && params.lanes <= kMaxLanes
        && params.passes >= 1 && params.passes <= kMaxPasses
        && params.blocks >= 8 * params.lanes && params.blocks <= kMaxBlocks;
}

void fillRandom(uint8_t *out, size_t size)
{
    // QRandomGenerator::system() is backed by the OS CSPRNG.
    std::array<quint32, 16> words{};
    while (size > 0) {
        QRandomGenerator::system()->fillRange(words.data(), words.size());
        const size_t chunk = std::min(size, sizeof(words));
        std::memcpy(out, words.data(), chunk);
        out += chunk;
        size -= chunk;
    }
    crypto_wipe(words.data(), sizeof(words));
}

bool deriveKey(uint8_t key[kKeySize], const QString &password, const uint8_t salt[kSaltSize],
               const BackupCrypto::KdfParams &params)
{
    // NFC so the same password typed on different platforms yields the same key.
    QByteArray pass = password.normalized(QString::NormalizationForm_C).toUtf8();
    const size_t workSize = size_t(params.blocks) * 1024;
    void *workArea = std::malloc(workSize);
    if (!workArea) {
        crypto_wipe(pass.data(), size_t(pass.size()));
        return false;
    }

    crypto_argon2_config config{};
    config.algorithm = CRYPTO_ARGON2_ID;
    config.nb_blocks = params.blocks;
    config.nb_passes = params.passes;
    config.nb_lanes = params.lanes;

    crypto_argon2_inputs inputs{};
    inputs.pass = reinterpret_cast<const uint8_t *>(pass.constData());
    inputs.pass_size = quint32(pass.size());
    inputs.salt = salt;
    inputs.salt_size = kSaltSize;

    crypto_argon2(key, kKeySize, workArea, config, inputs, crypto_argon2_no_extras);

    crypto_wipe(workArea, workSize);
    std::free(workArea);
    crypto_wipe(pass.data(), size_t(pass.size()));
    return true;
}
}

namespace BackupCrypto {

QByteArray encrypt(const QByteArray &plainText, const QString &password, KdfParams params)
{
    if (!validParams(params))
        return {};

    QByteArray out(kHeaderSize + plainText.size(), Qt::Uninitialized);
    auto *bytes = reinterpret_cast<uint8_t *>(out.data());
    std::memcpy(bytes, kMagic, sizeof(kMagic));
    bytes[8] = kFormatVersion;
    bytes[9] = kKdfArgon2id;
    qToLittleEndian<quint16>(0, bytes + 10);
    qToLittleEndian<quint32>(params.blocks, bytes + 12);
    qToLittleEndian<quint32>(params.passes, bytes + 16);
    qToLittleEndian<quint32>(params.lanes, bytes + 20);
    uint8_t *salt = bytes + 24;
    uint8_t *nonce = bytes + 40;
    uint8_t *mac = bytes + kAdSize;
    fillRandom(salt, kSaltSize);
    fillRandom(nonce, kNonceSize);

    uint8_t key[kKeySize];
    if (!deriveKey(key, password, salt, params))
        return {};

    crypto_aead_lock(bytes + kHeaderSize, mac, key, nonce,
                     bytes, kAdSize,
                     reinterpret_cast<const uint8_t *>(plainText.constData()), size_t(plainText.size()));
    crypto_wipe(key, sizeof(key));
    return out;
}

bool looksLikeBackup(const QByteArray &fileData)
{
    return fileData.size() >= kHeaderSize && std::memcmp(fileData.constData(), kMagic, sizeof(kMagic)) == 0;
}

std::optional<QByteArray> decrypt(const QByteArray &fileData, const QString &password, Error *error)
{
    const auto fail = [error](Error e) -> std::optional<QByteArray> {
        if (error)
            *error = e;
        return std::nullopt;
    };

    if (!looksLikeBackup(fileData))
        return fail(Error::NotABackup);

    const auto *bytes = reinterpret_cast<const uint8_t *>(fileData.constData());
    if (bytes[8] != kFormatVersion || bytes[9] != kKdfArgon2id)
        return fail(Error::UnsupportedVersion);

    KdfParams params;
    params.blocks = qFromLittleEndian<quint32>(bytes + 12);
    params.passes = qFromLittleEndian<quint32>(bytes + 16);
    params.lanes = qFromLittleEndian<quint32>(bytes + 20);
    if (qFromLittleEndian<quint16>(bytes + 10) != 0 || !validParams(params))
        return fail(Error::BadParameters);

    uint8_t key[kKeySize];
    if (!deriveKey(key, password, bytes + 24, params))
        return fail(Error::OutOfMemory);

    const size_t textSize = size_t(fileData.size() - kHeaderSize);
    QByteArray plainText(qsizetype(textSize), Qt::Uninitialized);
    const int status = crypto_aead_unlock(reinterpret_cast<uint8_t *>(plainText.data()),
                                          bytes + kAdSize, key, bytes + 40,
                                          bytes, kAdSize,
                                          bytes + kHeaderSize, textSize);
    crypto_wipe(key, sizeof(key));
    if (status != 0)
        return fail(Error::WrongPasswordOrDamaged);

    if (error)
        *error = Error::None;
    return plainText;
}

QString errorMessage(Error error)
{
    switch (error) {
    case Error::None:
        return QString();
    case Error::NotABackup:
        return QStringLiteral("This file is not a CrossTerm backup.");
    case Error::UnsupportedVersion:
        return QStringLiteral("This backup was made by a newer version of CrossTerm. Update CrossTerm and try again.");
    case Error::BadParameters:
        return QStringLiteral("The backup file's header is invalid. The file may be damaged.");
    case Error::WrongPasswordOrDamaged:
        return QStringLiteral("Wrong password, or the backup file is damaged.");
    case Error::OutOfMemory:
        return QStringLiteral("Not enough memory to unlock this backup.");
    }
    return QString();
}

}
