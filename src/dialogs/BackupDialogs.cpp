#include "BackupDialogs.h"
#include "../backup/BackupArchive.h"
#include "../backup/BackupCrypto.h"
#include "../profiles/ProfileManager.h"
#include "../security/PrivateKeyPermissions.h"

#include <QApplication>
#include <QButtonGroup>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QSysInfo>
#include <QVBoxLayout>

namespace {
constexpr int kMinPasswordLength = 8;
constexpr qint64 kMaxBackupFileSize = 256 * 1024 * 1024;
const QString kFileFilter = QStringLiteral("CrossTerm backup (*.ctbackup);;All files (*)");

// Argon2 takes a moment; show a busy cursor while it runs.
class BusyCursor
{
public:
    BusyCursor() { QApplication::setOverrideCursor(Qt::WaitCursor); }
    ~BusyCursor() { QApplication::restoreOverrideCursor(); }
    BusyCursor(const BusyCursor &) = delete;
    BusyCursor &operator=(const BusyCursor &) = delete;
};

QString plural(int count, const QString &singular, const QString &pluralForm)
{
    return QStringLiteral("%1 %2").arg(count).arg(count == 1 ? singular : pluralForm);
}

QSettings appSettings()
{
    return QSettings(QStringLiteral("CrossTerm"), QStringLiteral("CrossTerm"));
}

// Asks for a new backup password twice. Returns an empty string on cancel.
QString promptNewPassword(QWidget *parent)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("Export Backup"));
    dialog.setMinimumWidth(460);

    auto *layout = new QVBoxLayout(&dialog);
    auto *intro = new QLabel(QStringLiteral(
        "The backup contains your sessions, saved passwords, private keys, and settings. "
        "Choose a password to encrypt it.<br><br>"
        "<b>Keep this password safe.</b> Without it the backup cannot be opened, and it cannot be recovered."),
        &dialog);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto *form = new QFormLayout();
    auto *password = new QLineEdit(&dialog);
    password->setEchoMode(QLineEdit::Password);
    auto *confirm = new QLineEdit(&dialog);
    confirm->setEchoMode(QLineEdit::Password);
    form->addRow(QStringLiteral("Password:"), password);
    form->addRow(QStringLiteral("Confirm password:"), confirm);
    layout->addLayout(form);

    auto *hint = new QLabel(&dialog);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Choose Location..."));
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    const auto validate = [=] {
        QString message;
        if (password->text().size() < kMinPasswordLength)
            message = QStringLiteral("Use at least %1 characters.").arg(kMinPasswordLength);
        else if (confirm->text() != password->text())
            message = QStringLiteral("The passwords do not match.");
        hint->setText(message);
        buttons->button(QDialogButtonBox::Ok)->setEnabled(message.isEmpty());
    };
    QObject::connect(password, &QLineEdit::textChanged, &dialog, validate);
    QObject::connect(confirm, &QLineEdit::textChanged, &dialog, validate);
    validate();

    if (dialog.exec() != QDialog::Accepted)
        return QString();
    return password->text();
}
}

namespace BackupDialogs {

void exportBackup(QWidget *parent, const ProfileManager &profiles)
{
    const QString password = promptNewPassword(parent);
    if (password.isEmpty())
        return;

    const QString defaultName = QStringLiteral("CrossTerm-backup-%1-%2.ctbackup")
                                    .arg(QSysInfo::machineHostName(),
                                         QDate::currentDate().toString(QStringLiteral("yyyyMMdd")));
    const QString startDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QString path = QFileDialog::getSaveFileName(parent, QStringLiteral("Save Backup"),
                                                QDir(startDir).filePath(defaultName), kFileFilter);
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().isEmpty())
        path += QStringLiteral(".ctbackup");

    const QSettings settings = appSettings();
    const BackupArchive::Contents contents = BackupArchive::collect(profiles, settings);
    QByteArray sealed;
    {
        BusyCursor busy;
        sealed = BackupCrypto::encrypt(BackupArchive::serialize(contents), password);
    }

    QSaveFile file(path);
    if (sealed.isEmpty() || !file.open(QIODevice::WriteOnly) || file.write(sealed) != sealed.size() || !file.commit()) {
        QMessageBox::critical(parent, QStringLiteral("Export Backup"),
                              QStringLiteral("Could not write the backup to %1.").arg(QDir::toNativeSeparators(path)));
        return;
    }
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);

    QString message = QStringLiteral("Backup saved to %1.\n\nIt contains %2, %3, %4, %5, and your global settings.")
                          .arg(QDir::toNativeSeparators(path),
                               plural(contents.profiles.size(), QStringLiteral("session"), QStringLiteral("sessions")),
                               plural(contents.folders.size(), QStringLiteral("folder"), QStringLiteral("folders")),
                               plural(contents.keyFiles.size(), QStringLiteral("private key"), QStringLiteral("private keys")),
                               plural(BackupArchive::knownHostCount(contents.knownHosts), QStringLiteral("known host"),
                                      QStringLiteral("known hosts")));
    if (!contents.missingKeys.isEmpty()) {
        message += QStringLiteral("\n\nThese key files could not be read and were not included:\n%1")
                       .arg(contents.missingKeys.join(QLatin1Char('\n')));
        QMessageBox::warning(parent, QStringLiteral("Export Backup"), message);
        return;
    }
    QMessageBox::information(parent, QStringLiteral("Export Backup"), message);
}

bool importBackup(QWidget *parent, ProfileManager &profiles)
{
    const QString startDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString path = QFileDialog::getOpenFileName(parent, QStringLiteral("Open Backup"), startDir, kFileFilter);
    if (path.isEmpty())
        return false;

    QFile file(path);
    if (file.size() > kMaxBackupFileSize || !file.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(parent, QStringLiteral("Import Backup"),
                              QStringLiteral("Could not read %1.").arg(QDir::toNativeSeparators(path)));
        return false;
    }
    const QByteArray sealed = file.readAll();
    if (!BackupCrypto::looksLikeBackup(sealed)) {
        QMessageBox::critical(parent, QStringLiteral("Import Backup"),
                              BackupCrypto::errorMessage(BackupCrypto::Error::NotABackup));
        return false;
    }

    std::optional<QByteArray> plain;
    QString prompt = QStringLiteral("Password for %1:").arg(QFileInfo(path).fileName());
    while (!plain) {
        bool ok = false;
        const QString password = QInputDialog::getText(parent, QStringLiteral("Import Backup"), prompt,
                                                       QLineEdit::Password, QString(), &ok);
        if (!ok)
            return false;

        BackupCrypto::Error error = BackupCrypto::Error::None;
        {
            BusyCursor busy;
            plain = BackupCrypto::decrypt(sealed, password, &error);
        }
        if (plain)
            break;
        if (error != BackupCrypto::Error::WrongPasswordOrDamaged) {
            QMessageBox::critical(parent, QStringLiteral("Import Backup"), BackupCrypto::errorMessage(error));
            return false;
        }
        prompt = BackupCrypto::errorMessage(error) + QStringLiteral("\n\nTry again:");
    }

    QString parseError;
    const auto contents = BackupArchive::deserialize(*plain, &parseError);
    if (!contents) {
        QMessageBox::critical(parent, QStringLiteral("Import Backup"), parseError);
        return false;
    }

    // Summary and merge/replace choice.
    QSet<QString> incoming;
    for (const auto &profile : contents->profiles)
        incoming.insert(profile.name());
    int wouldRemove = 0;
    for (const auto &name : profiles.profileNames()) {
        if (!incoming.contains(name))
            ++wouldRemove;
    }

    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("Import Backup"));
    dialog.setMinimumWidth(500);
    auto *layout = new QVBoxLayout(&dialog);

    const QString created = QLocale().toString(contents->createdAt.toLocalTime(), QLocale::LongFormat);
    auto *summary = new QLabel(QStringLiteral(
        "<b>Backup from %1</b> (%2)<br>Created %3 with CrossTerm %4<br><br>"
        "Contains %5, %6, %7, %8, and %9.")
        .arg(contents->sourceHost.toHtmlEscaped(), contents->sourcePlatform.toHtmlEscaped(), created,
             contents->appVersion.toHtmlEscaped(),
             plural(contents->profiles.size(), QStringLiteral("session"), QStringLiteral("sessions")),
             plural(contents->folders.size(), QStringLiteral("folder"), QStringLiteral("folders")),
             plural(contents->keyFiles.size(), QStringLiteral("private key"), QStringLiteral("private keys")),
             plural(BackupArchive::knownHostCount(contents->knownHosts), QStringLiteral("known host"),
                    QStringLiteral("known hosts")),
             plural(contents->settings.size(), QStringLiteral("setting"), QStringLiteral("settings"))),
        &dialog);
    summary->setWordWrap(true);
    layout->addWidget(summary);

    auto *merge = new QRadioButton(QStringLiteral("Merge: add these sessions and update ones with the same name; "
                                                  "keep everything else"),
                                   &dialog);
    auto *replace = new QRadioButton(
        wouldRemove > 0
            ? QStringLiteral("Replace: make this computer match the backup (removes %1 not in the backup)")
                  .arg(plural(wouldRemove, QStringLiteral("session"), QStringLiteral("sessions")))
            : QStringLiteral("Replace: make this computer match the backup"),
        &dialog);
    merge->setChecked(true);
    auto *modeGroup = new QButtonGroup(&dialog);
    modeGroup->addButton(merge);
    modeGroup->addButton(replace);
    layout->addWidget(merge);
    layout->addWidget(replace);

    auto *note = new QLabel(QStringLiteral(
        "Private keys are restored to CrossTerm's own key folder, readable only by you. "
        "Known hosts are added to your SSH known_hosts file; existing entries are kept. "
        "Folder paths that don't exist on this computer fall back to its defaults. "
        "A copy of your current sessions is saved first."),
        &dialog);
    note->setWordWrap(true);
    layout->addWidget(note);

    if (!contents->missingKeys.isEmpty()) {
        auto *missing = new QLabel(QStringLiteral("Not in this backup (unreadable when it was made):\n%1")
                                       .arg(contents->missingKeys.join(QLatin1Char('\n'))),
                                   &dialog);
        missing->setWordWrap(true);
        layout->addWidget(missing);
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Import"));
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return false;

    const auto mode = replace->isChecked() ? BackupArchive::ImportMode::Replace : BackupArchive::ImportMode::Merge;
    const QString keyDirectory = PrivateKeyPermissions::keyDirectory();
    QSettings settings = appSettings();
    const BackupArchive::ImportResult result = BackupArchive::apply(*contents, profiles, settings, mode, keyDirectory);

    if (!result.error.isEmpty()) {
        QMessageBox::critical(parent, QStringLiteral("Import Backup"), result.error);
        // Profiles or settings may have been partly applied if saving failed.
        return result.added + result.updated + result.removed + result.settingsApplied > 0;
    }

    QString message = QStringLiteral("Imported %1 new and %2 updated sessions")
                          .arg(result.added).arg(result.updated);
    if (result.removed > 0)
        message += QStringLiteral(", removed %1").arg(result.removed);
    message += QStringLiteral(", restored %1, added %2, and applied %3.")
                   .arg(plural(result.keysRestored, QStringLiteral("private key"), QStringLiteral("private keys")),
                        plural(result.knownHostsAdded, QStringLiteral("known host"), QStringLiteral("known hosts")),
                        plural(result.settingsApplied, QStringLiteral("setting"), QStringLiteral("settings")));
    if (result.pathsReset > 0) {
        message += QStringLiteral("\n\n%1 from the other computer didn't exist here, so this computer's defaults are used instead.")
                       .arg(plural(result.pathsReset, QStringLiteral("folder path"), QStringLiteral("folder paths")));
    }
    if (!profiles.secretStorageWarning().isEmpty())
        message += QStringLiteral("\n\n") + profiles.secretStorageWarning();
    if (!result.safetyCopyPath.isEmpty()) {
        message += QStringLiteral("\n\nYour previous sessions were saved to:\n%1")
                       .arg(QDir::toNativeSeparators(result.safetyCopyPath));
    }
    QMessageBox::information(parent, QStringLiteral("Import Backup"), message);
    return true;
}

}
