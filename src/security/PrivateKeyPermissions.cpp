#include "PrivateKeyPermissions.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#ifdef Q_OS_WIN
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>

#include <vector>
#endif

namespace PrivateKeyPermissions {

QString keyDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/keys");
}

bool isInKeyDirectory(const QString &path)
{
    const QString dir = QDir::cleanPath(QDir(keyDirectory()).absolutePath()) + QLatin1Char('/');
    const QString file = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
#ifdef Q_OS_WIN
    return file.startsWith(dir, Qt::CaseInsensitive);
#else
    return file.startsWith(dir);
#endif
}

#ifdef Q_OS_WIN

namespace {
QString windowsError(DWORD code)
{
    return QStringLiteral("Windows error %1: %2").arg(code).arg(qt_error_string(int(code)).trimmed());
}

void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

// Current user (from the process token), LocalSystem, and BUILTIN\Administrators.
struct Principals {
    std::vector<BYTE> user;
    BYTE system[SECURITY_MAX_SID_SIZE] = {};
    BYTE admins[SECURITY_MAX_SID_SIZE] = {};

    bool isAllowed(PSID sid) const
    {
        return EqualSid(sid, PSID(user.data())) || EqualSid(sid, PSID(system)) || EqualSid(sid, PSID(admins));
    }
};

bool loadPrincipals(Principals &principals, QString *error)
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        setError(error, QStringLiteral("Could not open the process token. ") + windowsError(GetLastError()));
        return false;
    }
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<BYTE> buffer(size);
    const bool gotUser = size > 0 && GetTokenInformation(token, TokenUser, buffer.data(), size, &size);
    const DWORD tokenError = GetLastError();
    CloseHandle(token);
    if (!gotUser) {
        setError(error, QStringLiteral("Could not read the current user's SID. ") + windowsError(tokenError));
        return false;
    }

    PSID userSid = reinterpret_cast<TOKEN_USER *>(buffer.data())->User.Sid;
    const DWORD length = GetLengthSid(userSid);
    principals.user.resize(length);
    CopySid(length, principals.user.data(), userSid);

    DWORD systemSize = sizeof(principals.system);
    DWORD adminsSize = sizeof(principals.admins);
    if (!CreateWellKnownSid(WinLocalSystemSid, nullptr, principals.system, &systemSize)
        || !CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, principals.admins, &adminsSize)) {
        setError(error, QStringLiteral("Could not build the SYSTEM/Administrators SIDs. ") + windowsError(GetLastError()));
        return false;
    }
    return true;
}

QString describeSid(PSID sid)
{
    QString text;
    LPWSTR sidString = nullptr;
    if (ConvertSidToStringSidW(sid, &sidString)) {
        text = QString::fromWCharArray(sidString);
        LocalFree(sidString);
    }
    wchar_t name[256];
    wchar_t domain[256];
    DWORD nameSize = 256;
    DWORD domainSize = 256;
    SID_NAME_USE use;
    if (LookupAccountSidW(nullptr, sid, name, &nameSize, domain, &domainSize, &use)) {
        const QString account = domainSize > 0
            ? QString::fromWCharArray(domain) + QLatin1Char('\\') + QString::fromWCharArray(name)
            : QString::fromWCharArray(name);
        text = QStringLiteral("%1 (%2)").arg(account, text);
    }
    return text;
}

bool applyAcl(const QString &path, bool isDirectory, QString *error)
{
    Principals principals;
    if (!loadPrincipals(principals, error))
        return false;

    // The user needs read/write/delete on keys (CrossTerm writes them, ssh
    // reads them) and full control of the folder. As owner they can always
    // change the ACL again.
    const DWORD inheritance = isDirectory ? SUB_CONTAINERS_AND_OBJECTS_INHERIT : NO_INHERITANCE;
    const DWORD userRights = isDirectory ? FILE_ALL_ACCESS : (FILE_GENERIC_READ | FILE_GENERIC_WRITE | DELETE);
    EXPLICIT_ACCESS_W entries[3] = {};
    const auto fill = [inheritance](EXPLICIT_ACCESS_W &entry, PSID sid, DWORD rights) {
        entry.grfAccessPermissions = rights;
        entry.grfAccessMode = SET_ACCESS;
        entry.grfInheritance = inheritance;
        entry.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        entry.Trustee.TrusteeType = TRUSTEE_IS_UNKNOWN;
        entry.Trustee.ptstrName = reinterpret_cast<LPWSTR>(sid);
    };
    fill(entries[0], PSID(principals.user.data()), userRights);
    fill(entries[1], PSID(principals.system), FILE_ALL_ACCESS);
    fill(entries[2], PSID(principals.admins), FILE_ALL_ACCESS);

    PACL acl = nullptr;
    DWORD result = SetEntriesInAclW(3, entries, nullptr, &acl);
    if (result != ERROR_SUCCESS) {
        setError(error, QStringLiteral("Could not build the access list. ") + windowsError(result));
        return false;
    }

    // Protected DACL: inherited entries are discarded, not merged in.
    std::wstring native = QDir::toNativeSeparators(path).toStdWString();
    result = SetNamedSecurityInfoW(native.data(), SE_FILE_OBJECT,
                                   OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION
                                       | PROTECTED_DACL_SECURITY_INFORMATION,
                                   PSID(principals.user.data()), nullptr, acl, nullptr);
    if (result == ERROR_INVALID_OWNER || result == ERROR_ACCESS_DENIED) {
        // Without the right to take ownership, keep the current owner; isSecure
        // below still requires it to be the user, SYSTEM, or Administrators.
        result = SetNamedSecurityInfoW(native.data(), SE_FILE_OBJECT,
                                       DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                                       nullptr, nullptr, acl, nullptr);
    }
    LocalFree(acl);
    if (result != ERROR_SUCCESS) {
        setError(error, QStringLiteral("Could not apply the access list. ") + windowsError(result));
        return false;
    }

    QString details;
    if (!isSecure(path, &details)) {
        setError(error, details);
        return false;
    }
    return true;
}
}

bool isSecure(const QString &path, QString *details)
{
    Principals principals;
    if (!loadPrincipals(principals, details))
        return false;

    const std::wstring native = QDir::toNativeSeparators(path).toStdWString();
    PSID owner = nullptr;
    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    const DWORD result = GetNamedSecurityInfoW(native.c_str(), SE_FILE_OBJECT,
                                               OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                                               &owner, nullptr, &dacl, nullptr, &descriptor);
    if (result != ERROR_SUCCESS) {
        setError(details, QStringLiteral("Could not read the file's permissions. ") + windowsError(result));
        return false;
    }

    QString problem;
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    GetSecurityDescriptorControl(descriptor, &control, &revision);
    if (!(control & SE_DACL_PROTECTED)) {
        problem = QStringLiteral("Permission inheritance is enabled.");
    } else if (!dacl) {
        problem = QStringLiteral("The file has no access list, so everyone can read it.");
    } else if (!owner || !principals.isAllowed(owner)) {
        problem = QStringLiteral("The file is owned by %1.").arg(owner ? describeSid(owner) : QStringLiteral("nobody"));
    } else {
        bool userCanRead = false;
        for (DWORD i = 0; i < dacl->AceCount && problem.isEmpty(); ++i) {
            void *ace = nullptr;
            if (!GetAce(dacl, i, &ace))
                continue;
            const auto *header = static_cast<ACE_HEADER *>(ace);
            if (header->AceType == ACCESS_DENIED_ACE_TYPE)
                continue;
            if (header->AceType != ACCESS_ALLOWED_ACE_TYPE) {
                problem = QStringLiteral("The access list has an unexpected entry of type %1.").arg(header->AceType);
                break;
            }
            const auto *allowed = static_cast<ACCESS_ALLOWED_ACE *>(ace);
            PSID sid = PSID(&allowed->SidStart);
            if (!principals.isAllowed(sid)) {
                problem = QStringLiteral("%1 has access.").arg(describeSid(sid));
            } else if (EqualSid(sid, PSID(principals.user.data()))
                       && (allowed->Mask & (FILE_READ_DATA | GENERIC_READ | GENERIC_ALL))) {
                userCanRead = true;
            }
        }
        if (problem.isEmpty() && !userCanRead)
            problem = QStringLiteral("The current user cannot read the file.");
    }
    LocalFree(descriptor);

    setError(details, problem);
    return problem.isEmpty();
}

bool secureFile(const QString &path, QString *error)
{
    return applyAcl(path, false, error);
}

bool secureDirectory(const QString &path, QString *error)
{
    return applyAcl(path, true, error);
}

#else // Unix

namespace {
constexpr QFileDevice::Permissions kGroupOrOther =
    QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup
    | QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;

bool applyMode(const QString &path, QFileDevice::Permissions mode, QString *error)
{
    if (QFile::setPermissions(path, mode))
        return true;
    if (error)
        *error = QStringLiteral("Could not change permissions of %1.").arg(path);
    return false;
}
}

bool isSecure(const QString &path, QString *details)
{
    const QFileInfo info(path);
    QString problem;
    if (!info.exists())
        problem = QStringLiteral("The file does not exist.");
    else if (info.permissions() & kGroupOrOther)
        problem = QStringLiteral("Other users can access the file.");
    else if (!(info.permissions() & QFileDevice::ReadOwner))
        problem = QStringLiteral("The owner cannot read the file.");
    if (details)
        *details = problem;
    return problem.isEmpty();
}

bool secureFile(const QString &path, QString *error)
{
    return applyMode(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner, error);
}

bool secureDirectory(const QString &path, QString *error)
{
    return applyMode(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner, error);
}

#endif

}
