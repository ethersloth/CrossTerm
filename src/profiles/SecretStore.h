#pragma once

#include <QString>

#include <memory>
#include <optional>

// Where saved session passwords live instead of the profiles file.
// Accounts are profile names. Implementations must not throw.
class SecretStore
{
public:
    virtual ~SecretStore() = default;

    // False when no keychain can be reached; callers then keep passwords in
    // the profiles file as before.
    virtual bool isAvailable() = 0;
    virtual std::optional<QString> read(const QString &account) = 0;
    virtual bool write(const QString &account, const QString &secret) = 0;
    virtual bool remove(const QString &account) = 0;
    // Human-readable name for messages, e.g. "Windows Credential Manager".
    virtual QString name() const = 0;

    // The OS keychain: Secret Service (GNOME Keyring, KWallet) on Linux,
    // Credential Manager on Windows. Never null; may report unavailable.
    static std::unique_ptr<SecretStore> createSystemStore();
};
