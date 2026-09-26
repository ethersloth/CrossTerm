#include "SecretStore.h"

#if defined(Q_OS_WIN)

#include <windows.h>
#include <wincred.h>

namespace {
class WindowsCredentialStore final : public SecretStore
{
public:
    bool isAvailable() override { return true; }

    std::optional<QString> read(const QString &account) override
    {
        const std::wstring target = targetName(account);
        PCREDENTIALW credential = nullptr;
        if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential))
            return std::nullopt;
        const QString secret = QString::fromUtf8(reinterpret_cast<const char *>(credential->CredentialBlob),
                                                 qsizetype(credential->CredentialBlobSize));
        SecureZeroMemory(credential->CredentialBlob, credential->CredentialBlobSize);
        CredFree(credential);
        return secret;
    }

    bool write(const QString &account, const QString &secret) override
    {
        std::wstring target = targetName(account);
        std::wstring user = account.toStdWString();
        QByteArray blob = secret.toUtf8();

        CREDENTIALW credential{};
        credential.Type = CRED_TYPE_GENERIC;
        credential.TargetName = target.data();
        credential.UserName = user.data();
        credential.CredentialBlobSize = DWORD(blob.size());
        credential.CredentialBlob = reinterpret_cast<LPBYTE>(blob.data());
        credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
        const bool ok = CredWriteW(&credential, 0);
        SecureZeroMemory(blob.data(), size_t(blob.size()));
        return ok;
    }

    bool remove(const QString &account) override
    {
        const std::wstring target = targetName(account);
        return CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0) || GetLastError() == ERROR_NOT_FOUND;
    }

    QString name() const override { return QStringLiteral("Windows Credential Manager"); }

private:
    static std::wstring targetName(const QString &account)
    {
        return (QStringLiteral("CrossTerm/") + account).toStdWString();
    }
};
}

std::unique_ptr<SecretStore> SecretStore::createSystemStore()
{
    return std::make_unique<WindowsCredentialStore>();
}

#elif defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QEventLoop>
#include <QTimer>

namespace {
const QString kService = QStringLiteral("org.freedesktop.secrets");
const QString kServicePath = QStringLiteral("/org/freedesktop/secrets");
const QString kServiceIface = QStringLiteral("org.freedesktop.Secret.Service");
const QString kCollectionIface = QStringLiteral("org.freedesktop.Secret.Collection");
const QString kItemIface = QStringLiteral("org.freedesktop.Secret.Item");
const QString kPromptIface = QStringLiteral("org.freedesktop.Secret.Prompt");
const QString kNoPrompt = QStringLiteral("/");

using StringMap = QMap<QString, QString>;

// Secret Service "Secret" struct, signature (oayays).
struct DBusSecret {
    QDBusObjectPath session;
    QByteArray parameters;
    QByteArray value;
    QString contentType;
};
}

Q_DECLARE_METATYPE(DBusSecret)

QDBusArgument &operator<<(QDBusArgument &arg, const DBusSecret &secret)
{
    arg.beginStructure();
    arg << secret.session << secret.parameters << secret.value << secret.contentType;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, DBusSecret &secret)
{
    arg.beginStructure();
    arg >> secret.session >> secret.parameters >> secret.value >> secret.contentType;
    arg.endStructure();
    return arg;
}

namespace {
// Waits for a Secret Service prompt (e.g. "unlock your wallet") to finish.
class PromptWaiter final : public QObject
{
    Q_OBJECT
public:
    bool dismissed = true;
    QEventLoop loop;

public slots:
    void completed(bool wasDismissed, const QDBusVariant &)
    {
        dismissed = wasDismissed;
        loop.quit();
    }
};

class SecretServiceStore final : public SecretStore
{
public:
    SecretServiceStore() : m_bus(QDBusConnection::sessionBus())
    {
        qDBusRegisterMetaType<DBusSecret>();
        qDBusRegisterMetaType<StringMap>();
    }

    bool isAvailable() override { return openSession(); }

    std::optional<QString> read(const QString &account) override
    {
        if (!openSession())
            return std::nullopt;
        const QString item = findItem(account, true);
        if (item.isEmpty())
            return std::nullopt;

        const QDBusMessage reply = call(item, kItemIface, QStringLiteral("GetSecret"),
                                        {QVariant::fromValue(QDBusObjectPath(m_session))});
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
            return std::nullopt;
        DBusSecret secret;
        reply.arguments().first().value<QDBusArgument>() >> secret;
        return QString::fromUtf8(secret.value);
    }

    bool write(const QString &account, const QString &secret) override
    {
        if (!openSession())
            return false;
        const QString collection = defaultCollection();
        if (collection.isEmpty() || !unlock({collection}))
            return false;

        QVariantMap properties;
        properties.insert(QStringLiteral("org.freedesktop.Secret.Item.Label"),
                          QStringLiteral("CrossTerm: %1").arg(account));
        properties.insert(QStringLiteral("org.freedesktop.Secret.Item.Attributes"),
                          QVariant::fromValue(attributes(account)));

        DBusSecret payload{QDBusObjectPath(m_session), QByteArray(), secret.toUtf8(),
                           QStringLiteral("text/plain; charset=utf8")};
        const QDBusMessage reply = call(collection, kCollectionIface, QStringLiteral("CreateItem"),
                                        {properties, QVariant::fromValue(payload), true});
        payload.value.fill('\0');
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() < 2)
            return false;
        const QString prompt = reply.arguments().at(1).value<QDBusObjectPath>().path();
        return prompt == kNoPrompt || runPrompt(prompt);
    }

    bool remove(const QString &account) override
    {
        if (!openSession())
            return false;
        bool ok = true;
        for (const QString &item : searchItems(account, true)) {
            const QDBusMessage reply = call(item, kItemIface, QStringLiteral("Delete"), {});
            if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
                ok = false;
                continue;
            }
            const QString prompt = reply.arguments().first().value<QDBusObjectPath>().path();
            if (prompt != kNoPrompt && !runPrompt(prompt))
                ok = false;
        }
        return ok;
    }

    QString name() const override { return QStringLiteral("the system keychain (Secret Service)"); }

private:
    QDBusMessage call(const QString &path, const QString &iface, const QString &method, const QVariantList &args)
    {
        QDBusMessage message = QDBusMessage::createMethodCall(kService, path, iface, method);
        message.setArguments(args);
        return m_bus.call(message, QDBus::Block, 30000);
    }

    static StringMap attributes(const QString &account)
    {
        return {{QStringLiteral("application"), QStringLiteral("CrossTerm")},
                {QStringLiteral("crossterm-profile"), account}};
    }

    bool openSession()
    {
        if (!m_session.isEmpty())
            return true;
        if (m_sessionFailed || !m_bus.isConnected())
            return false;
        // "plain" is fine here: the session bus is private to this user.
        const QDBusMessage reply = call(kServicePath, kServiceIface, QStringLiteral("OpenSession"),
                                        {QStringLiteral("plain"), QVariant::fromValue(QDBusVariant(QString()))});
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() < 2) {
            m_sessionFailed = true;
            return false;
        }
        m_session = reply.arguments().at(1).value<QDBusObjectPath>().path();
        return !m_session.isEmpty();
    }

    QString defaultCollection()
    {
        const QDBusMessage reply = call(kServicePath, kServiceIface, QStringLiteral("ReadAlias"),
                                        {QStringLiteral("default")});
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
            return QString();
        const QString path = reply.arguments().first().value<QDBusObjectPath>().path();
        return path == kNoPrompt ? QString() : path;
    }

    QStringList searchItems(const QString &account, bool unlockLocked)
    {
        const QDBusMessage reply = call(kServicePath, kServiceIface, QStringLiteral("SearchItems"),
                                        {QVariant::fromValue(attributes(account))});
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() < 2)
            return {};
        QStringList items;
        QStringList locked;
        for (const auto &path : qdbus_cast<QList<QDBusObjectPath>>(reply.arguments().at(0)))
            items.append(path.path());
        for (const auto &path : qdbus_cast<QList<QDBusObjectPath>>(reply.arguments().at(1)))
            locked.append(path.path());
        if (unlockLocked && !locked.isEmpty() && unlock(locked))
            items.append(locked);
        return items;
    }

    QString findItem(const QString &account, bool unlockLocked)
    {
        const QStringList items = searchItems(account, unlockLocked);
        return items.isEmpty() ? QString() : items.first();
    }

    bool unlock(const QStringList &paths)
    {
        QList<QDBusObjectPath> objects;
        for (const auto &path : paths)
            objects.append(QDBusObjectPath(path));
        const QDBusMessage reply = call(kServicePath, kServiceIface, QStringLiteral("Unlock"),
                                        {QVariant::fromValue(objects)});
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() < 2)
            return false;
        const QString prompt = reply.arguments().at(1).value<QDBusObjectPath>().path();
        return prompt == kNoPrompt || runPrompt(prompt);
    }

    bool runPrompt(const QString &promptPath)
    {
        PromptWaiter waiter;
        m_bus.connect(kService, promptPath, kPromptIface, QStringLiteral("Completed"),
                      &waiter, SLOT(completed(bool, QDBusVariant)));
        const QDBusMessage reply = call(promptPath, kPromptIface, QStringLiteral("Prompt"), {QString()});
        if (reply.type() != QDBusMessage::ReplyMessage)
            return false;
        // The user may take a while to type their wallet password.
        QTimer::singleShot(120000, &waiter.loop, &QEventLoop::quit);
        waiter.loop.exec();
        m_bus.disconnect(kService, promptPath, kPromptIface, QStringLiteral("Completed"),
                         &waiter, SLOT(completed(bool, QDBusVariant)));
        return !waiter.dismissed;
    }

    QDBusConnection m_bus;
    QString m_session;
    bool m_sessionFailed = false;
};
}

std::unique_ptr<SecretStore> SecretStore::createSystemStore()
{
    return std::make_unique<SecretServiceStore>();
}

#include "SecretStore.moc"

#else

namespace {
class UnavailableStore final : public SecretStore
{
public:
    bool isAvailable() override { return false; }
    std::optional<QString> read(const QString &) override { return std::nullopt; }
    bool write(const QString &, const QString &) override { return false; }
    bool remove(const QString &) override { return false; }
    QString name() const override { return QStringLiteral("no keychain"); }
};
}

std::unique_ptr<SecretStore> SecretStore::createSystemStore()
{
    return std::make_unique<UnavailableStore>();
}

#endif
