#pragma once

#include <QByteArray>
#include <QString>

#include <optional>

// Password-based encryption for CrossTerm backup files.
//
// The password is stretched with Argon2id and the payload is sealed with
// XChaCha20-Poly1305 (Monocypher). The whole header, including the Argon2
// parameters, salt, and nonce, is authenticated, so a wrong password, a
// truncated file, and a tampered file all fail the same way on decrypt.
//
// File layout (integers little-endian):
//   0  magic "CTBACKUP"      8 bytes
//   8  format version        u8   (1)
//   9  KDF id                u8   (1 = Argon2id)
//  10  reserved              u16  (0)
//  12  Argon2 blocks (KiB)   u32
//  16  Argon2 passes         u32
//  20  Argon2 lanes          u32
//  24  salt                  16 bytes
//  40  nonce                 24 bytes
//  64  MAC                   16 bytes
//  80  ciphertext
namespace BackupCrypto {

enum class Error {
    None,
    NotABackup,
    UnsupportedVersion,
    BadParameters,
    WrongPasswordOrDamaged,
    OutOfMemory,
};

struct KdfParams {
    quint32 blocks = 64 * 1024; // 64 MiB
    quint32 passes = 3;
    quint32 lanes = 1;
};

QByteArray encrypt(const QByteArray &plainText, const QString &password, KdfParams params = {});
std::optional<QByteArray> decrypt(const QByteArray &fileData, const QString &password, Error *error = nullptr);
bool looksLikeBackup(const QByteArray &fileData);
QString errorMessage(Error error);

}
