#pragma once

#include <QString>

// Locks down private key files so OpenSSH will use them.
//
// Unix: mode 0600 for files, 0700 for directories.
//
// Windows: owner = current user, inheritance disabled, and a DACL that only
// grants the current user, SYSTEM, and Administrators - the principals
// Win32-OpenSSH accepts (and what its own ssh-keygen produces). The SIDs come
// from the process token, so domain accounts and localized group names work.
// The whole DACL is replaced, so stray entries such as inherited group
// grants are dropped. The integrity label lives in the SACL and is left alone.
//
// Do not use QFile permission bits for key files: on Windows Qt maps them to
// an unprotected DACL whose "group" is the first token group (often the
// Mandatory Label SID), which OpenSSH rejects.
namespace PrivateKeyPermissions {

bool secureFile(const QString &path, QString *error = nullptr);
// Protects a directory the same way; new files created inside inherit it.
bool secureDirectory(const QString &path, QString *error = nullptr);

// True when only the principals above can access the path, inheritance is
// off (Windows), and the current user can read it. `details` explains a false.
bool isSecure(const QString &path, QString *details = nullptr);

// CrossTerm's folder for keys restored from backups.
QString keyDirectory();
bool isInKeyDirectory(const QString &path);

}
