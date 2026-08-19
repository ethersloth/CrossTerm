#include "NewSessionDialog.h"
#include "../profiles/ConnectionProfile.h"
#include "../profiles/ProfileManager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QComboBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QGroupBox>
#include <QMessageBox>
#include <QFileDialog>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

NewSessionDialog::NewSessionDialog(ProfileManager &profileManager, QWidget *parent)
    : QDialog(parent), m_profileManager(profileManager)
{
    setWindowTitle(QStringLiteral("New Session"));
    setMinimumWidth(500);
    buildUi();
    populateProfileList();
}

ConnectionProfile NewSessionDialog::createProfile() const
{
    int typeIndex = m_connectionTypeCombo->currentIndex();
    auto type = static_cast<ConnectionProfile::ConnectionType>(typeIndex);
    
    ConnectionProfile profile(m_profileName->text().isEmpty() ? 
                             QStringLiteral("New Session") : m_profileName->text(), 
                             type);

    switch (type) {
    case ConnectionProfile::ConnectionType::LocalShell:
        // Local shell typically doesn't need saved settings
        break;

    case ConnectionProfile::ConnectionType::SSH:
        profile.setSshHost(m_sshHost->text());
        profile.setSshPort(m_sshPort->value());
        profile.setSshUsername(m_sshUsername->text());
        if (!m_sshUseKey->isChecked()) {
            profile.setSshPassword(m_sshPassword->text());
        } else {
            profile.setSshPrivateKey(m_sshPrivateKey->text());
        }
        break;

    case ConnectionProfile::ConnectionType::Serial:
        profile.setSerialPort(m_serialPort->text());
        profile.setSerialBaud(m_serialBaud->value());
        profile.setSerialDataBits(m_serialDataBits->value());
        break;

    case ConnectionProfile::ConnectionType::Telnet:
        profile.setTelnetHost(m_telnetHost->text());
        profile.setTelnetPort(m_telnetPort->value());
        break;
    }

    profile.setProperty(QStringLiteral("session_log_enabled"),
                        m_logSessionCheck->isChecked() ? QStringLiteral("1") : QStringLiteral("0"));
    profile.setProperty(QStringLiteral("session_log_path"), m_logPathEdit->text().trimmed());

    return profile;
}

ConnectionProfile NewSessionDialog::showNewSessionDialog(ProfileManager &profileManager, QWidget *parent)
{
    NewSessionDialog dialog(profileManager, parent);
    if (dialog.exec() == QDialog::Accepted) {
        return dialog.createProfile();
    }
    return ConnectionProfile();
}

bool NewSessionDialog::selectProfile(const QString &profileName)
{
    const int index = m_profileCombo->findText(profileName);
    if (index <= 0)
        return false;

    m_profileCombo->setCurrentIndex(index);
    return true;
}

void NewSessionDialog::buildUi()
{
    auto *mainLayout = new QVBoxLayout(this);

    // Profile section
    auto *profileGroup = new QGroupBox(QStringLiteral("Profile Management"), this);
    auto *profileLayout = new QFormLayout(profileGroup);

    m_profileCombo = new QComboBox(this);
    profileLayout->addRow(QStringLiteral("Load Profile:"), m_profileCombo);

    m_profileName = new QLineEdit(this);
    m_profileName->setPlaceholderText(QStringLiteral("Name for saving this profile"));
    profileLayout->addRow(QStringLiteral("Profile Name:"), m_profileName);

    auto *profileBtnLayout = new QHBoxLayout();
    m_loadProfileBtn = new QPushButton(QStringLiteral("Load"), this);
    m_saveProfileBtn = new QPushButton(QStringLiteral("Save"), this);
    profileBtnLayout->addWidget(m_loadProfileBtn);
    profileBtnLayout->addWidget(m_saveProfileBtn);
    profileLayout->addRow(profileBtnLayout);

    mainLayout->addWidget(profileGroup);

    // Connection type
    auto *typeLayout = new QFormLayout();
    m_connectionTypeCombo = new QComboBox(this);
    m_connectionTypeCombo->addItem(QStringLiteral("Local Shell"));
    m_connectionTypeCombo->addItem(QStringLiteral("SSH"));
    m_connectionTypeCombo->addItem(QStringLiteral("Serial"));
    m_connectionTypeCombo->addItem(QStringLiteral("Telnet"));
    typeLayout->addRow(QStringLiteral("Connection Type:"), m_connectionTypeCombo);
    mainLayout->addLayout(typeLayout);

    // Stacked widget for connection options
    m_optionsStack = new QStackedWidget(this);

    buildLocalShellUI();
    buildSSHUI();
    buildSerialUI();
    buildTelnetUI();

    mainLayout->addWidget(m_optionsStack);

    // Session logging options
    auto *loggingGroup = new QGroupBox(QStringLiteral("Session Logging"), this);
    auto *loggingLayout = new QFormLayout(loggingGroup);

    m_logSessionCheck = new QCheckBox(QStringLiteral("Log this session to a file"), this);
    loggingLayout->addRow(m_logSessionCheck);

    auto *logPathRow = new QWidget(this);
    auto *logPathLayout = new QHBoxLayout(logPathRow);
    logPathLayout->setContentsMargins(0, 0, 0, 0);
    logPathLayout->setSpacing(6);

    m_logPathEdit = new QLineEdit(this);
    m_logPathEdit->setPlaceholderText(QStringLiteral("Optional: explicit log file path"));
    m_logBrowseBtn = new QPushButton(QStringLiteral("Browse..."), this);
    logPathLayout->addWidget(m_logPathEdit, 1);
    logPathLayout->addWidget(m_logBrowseBtn);
    loggingLayout->addRow(QStringLiteral("Log File:"), logPathRow);

    mainLayout->addWidget(loggingGroup);

    // Buttons
    auto *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    
    auto *cancelBtn = new QPushButton(QStringLiteral("Cancel"), this);
    auto *connectBtn = new QPushButton(QStringLiteral("Connect"), this);
    connectBtn->setDefault(true);
    
    buttonLayout->addWidget(cancelBtn);
    buttonLayout->addWidget(connectBtn);
    mainLayout->addLayout(buttonLayout);

    connect(m_connectionTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &NewSessionDialog::onConnectionTypeChanged);
    connect(m_loadProfileBtn, &QPushButton::clicked, this, &NewSessionDialog::onLoadProfileClicked);
    connect(m_saveProfileBtn, &QPushButton::clicked, this, &NewSessionDialog::onSaveProfileClicked);
    connect(m_profileCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &NewSessionDialog::onProfileSelected);
    connect(m_logBrowseBtn, &QPushButton::clicked, this, [this]() {
        QString startPath = m_logPathEdit->text().trimmed();
        if (startPath.isEmpty()) {
            QSettings settings(QStringLiteral("CrossTerm"), QStringLiteral("CrossTerm"));
            startPath = settings.value(QStringLiteral("global/logDirectory"),
                                       QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/logs")).toString();
        } else {
            QFileInfo info(startPath);
            if (info.exists() && info.isFile())
                startPath = info.absolutePath();
        }

        const QString filePath = QFileDialog::getSaveFileName(
            this,
            QStringLiteral("Select Session Log File"),
            startPath,
            QStringLiteral("Text Files (*.txt *.log);;All Files (*)"));

        if (!filePath.isEmpty())
            m_logPathEdit->setText(filePath);
    });
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(connectBtn, &QPushButton::clicked, this, &QDialog::accept);

    QSettings settings(QStringLiteral("CrossTerm"), QStringLiteral("CrossTerm"));
    const bool defaultLogEnabled = settings.value(QStringLiteral("global/loggingEnabledByDefault"), false).toBool();
    const QString defaultLogDir = settings.value(QStringLiteral("global/logDirectory"),
                                                 QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/logs")).toString();
    m_logSessionCheck->setChecked(defaultLogEnabled);
    m_logPathEdit->setText(defaultLogDir);
}

void NewSessionDialog::buildLocalShellUI()
{
    auto *widget = new QWidget(this);
    auto *layout = new QFormLayout(widget);

    m_localShellCmd = new QLineEdit(this);
    m_localShellCmd->setPlaceholderText(QStringLiteral("/bin/bash"));
    layout->addRow(QStringLiteral("Command:"), m_localShellCmd);

    m_optionsStack->addWidget(widget);
}

void NewSessionDialog::buildSSHUI()
{
    auto *widget = new QWidget(this);
    auto *layout = new QFormLayout(widget);

    m_sshHost = new QLineEdit(this);
    m_sshHost->setPlaceholderText(QStringLiteral("example.com"));
    layout->addRow(QStringLiteral("Host:"), m_sshHost);

    m_sshPort = new QSpinBox(this);
    m_sshPort->setMinimum(1);
    m_sshPort->setMaximum(65535);
    m_sshPort->setValue(22);
    layout->addRow(QStringLiteral("Port:"), m_sshPort);

    m_sshUsername = new QLineEdit(this);
    m_sshUsername->setPlaceholderText(QStringLiteral("user"));
    layout->addRow(QStringLiteral("Username:"), m_sshUsername);

    m_sshUseKey = new QCheckBox(QStringLiteral("Use Private Key"), this);
    layout->addRow(m_sshUseKey);

    m_sshPassword = new QLineEdit(this);
    m_sshPassword->setEchoMode(QLineEdit::Password);
    m_sshPassword->setPlaceholderText(QStringLiteral("password (if not using key)"));
    layout->addRow(QStringLiteral("Password:"), m_sshPassword);

    m_sshPrivateKey = new QLineEdit(this);
    m_sshPrivateKey->setPlaceholderText(QStringLiteral("~/.ssh/id_rsa"));
    auto *keyPathRow = new QWidget(this);
    auto *keyPathLayout = new QHBoxLayout(keyPathRow);
    keyPathLayout->setContentsMargins(0, 0, 0, 0);
    keyPathLayout->setSpacing(6);

    auto *browsePrivateKeyBtn = new QPushButton(QStringLiteral("Browse..."), this);
    keyPathLayout->addWidget(m_sshPrivateKey, 1);
    keyPathLayout->addWidget(browsePrivateKeyBtn);
    layout->addRow(QStringLiteral("Private Key:"), keyPathRow);

    connect(browsePrivateKeyBtn, &QPushButton::clicked, this, [this]() {
        QString startPath = m_sshPrivateKey->text().trimmed();
        if (startPath.isEmpty()) {
            startPath = QDir::homePath() + QStringLiteral("/.ssh");
        } else {
            QFileInfo info(startPath);
            if (info.exists() && info.isFile()) {
                startPath = info.absolutePath();
            }
        }

        const QString filePath = QFileDialog::getOpenFileName(
            this,
            QStringLiteral("Select SSH Private Key"),
            startPath,
            QStringLiteral("All Files (*)"));

        if (!filePath.isEmpty()) {
            m_sshPrivateKey->setText(filePath);
            m_sshUseKey->setChecked(true);
        }
    });

    m_optionsStack->addWidget(widget);
}

void NewSessionDialog::buildSerialUI()
{
    auto *widget = new QWidget(this);
    auto *layout = new QFormLayout(widget);

    m_serialPort = new QLineEdit(this);
    m_serialPort->setPlaceholderText(QStringLiteral("/dev/ttyUSB0 or COM3"));
    layout->addRow(QStringLiteral("Port:"), m_serialPort);

    m_serialBaud = new QSpinBox(this);
    m_serialBaud->setMinimum(300);
    m_serialBaud->setMaximum(921600);
    m_serialBaud->setValue(9600);
    layout->addRow(QStringLiteral("Baud Rate:"), m_serialBaud);

    m_serialDataBits = new QSpinBox(this);
    m_serialDataBits->setMinimum(5);
    m_serialDataBits->setMaximum(8);
    m_serialDataBits->setValue(8);
    layout->addRow(QStringLiteral("Data Bits:"), m_serialDataBits);

    m_serialParity = new QComboBox(this);
    m_serialParity->addItem(QStringLiteral("None"));
    m_serialParity->addItem(QStringLiteral("Even"));
    m_serialParity->addItem(QStringLiteral("Odd"));
    layout->addRow(QStringLiteral("Parity:"), m_serialParity);

    m_serialStopBits = new QSpinBox(this);
    m_serialStopBits->setMinimum(1);
    m_serialStopBits->setMaximum(2);
    m_serialStopBits->setValue(1);
    layout->addRow(QStringLiteral("Stop Bits:"), m_serialStopBits);

    m_optionsStack->addWidget(widget);
}

void NewSessionDialog::buildTelnetUI()
{
    auto *widget = new QWidget(this);
    auto *layout = new QFormLayout(widget);

    m_telnetHost = new QLineEdit(this);
    m_telnetHost->setPlaceholderText(QStringLiteral("example.com"));
    layout->addRow(QStringLiteral("Host:"), m_telnetHost);

    m_telnetPort = new QSpinBox(this);
    m_telnetPort->setMinimum(1);
    m_telnetPort->setMaximum(65535);
    m_telnetPort->setValue(23);
    layout->addRow(QStringLiteral("Port:"), m_telnetPort);

    m_optionsStack->addWidget(widget);
}

void NewSessionDialog::populateProfileList()
{
    m_profileCombo->clear();
    m_profileCombo->addItem(QStringLiteral("-- No Profile --"));
    for (const auto &name : m_profileManager.profileNames()) {
        m_profileCombo->addItem(name);
    }
}

void NewSessionDialog::onConnectionTypeChanged(int index)
{
    m_optionsStack->setCurrentIndex(index);
}

void NewSessionDialog::onLoadProfileClicked()
{
    int index = m_profileCombo->currentIndex();
    if (index <= 0)
        return;

    QString profileName = m_profileCombo->currentText();
    auto profile = m_profileManager.profile(profileName);
    loadProfileIntoUI(profile);
}

void NewSessionDialog::onSaveProfileClicked()
{
    if (m_profileName->text().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Error"), 
                            QStringLiteral("Please enter a profile name"));
        return;
    }

    auto profile = createProfile();
    m_profileManager.updateProfile(profile);
    
    if (!m_profileManager.saveProfiles()) {
        QMessageBox::critical(this, QStringLiteral("Error"), 
                             QStringLiteral("Failed to save profiles"));
        return;
    }

    populateProfileList();
    QMessageBox::information(this, QStringLiteral("Success"), 
                            QStringLiteral("Profile saved successfully"));
}

void NewSessionDialog::onProfileSelected(int index)
{
    if (index <= 0)
        return;

    QString profileName = m_profileCombo->currentText();
    auto profile = m_profileManager.profile(profileName);
    loadProfileIntoUI(profile);
}

void NewSessionDialog::loadProfileIntoUI(const ConnectionProfile &profile)
{
    m_connectionTypeCombo->setCurrentIndex(static_cast<int>(profile.type()));
    m_profileName->setText(profile.name());

    switch (profile.type()) {
    case ConnectionProfile::ConnectionType::LocalShell:
        // Nothing to load
        break;

    case ConnectionProfile::ConnectionType::SSH:
        m_sshHost->setText(profile.sshHost());
        m_sshPort->setValue(profile.sshPort());
        m_sshUsername->setText(profile.sshUsername());
        if (!profile.sshPrivateKey().isEmpty()) {
            m_sshUseKey->setChecked(true);
            m_sshPrivateKey->setText(profile.sshPrivateKey());
        } else {
            m_sshUseKey->setChecked(false);
            m_sshPassword->setText(profile.sshPassword());
        }
        break;

    case ConnectionProfile::ConnectionType::Serial:
        m_serialPort->setText(profile.serialPort());
        m_serialBaud->setValue(profile.serialBaud());
        m_serialDataBits->setValue(profile.serialDataBits());
        break;

    case ConnectionProfile::ConnectionType::Telnet:
        m_telnetHost->setText(profile.telnetHost());
        m_telnetPort->setValue(profile.telnetPort());
        break;
    }

    const QString logEnabled = profile.property(QStringLiteral("session_log_enabled"), QStringLiteral("0"));
    m_logSessionCheck->setChecked(logEnabled == QStringLiteral("1") || logEnabled.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0);
    m_logPathEdit->setText(profile.property(QStringLiteral("session_log_path")));
}
