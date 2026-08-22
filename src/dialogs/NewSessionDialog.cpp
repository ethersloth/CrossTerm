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
#include <QListWidget>
#include <QFontComboBox>
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
    setWindowTitle(QStringLiteral("Session Options"));
    resize(920, 640);
    setMinimumWidth(760);
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

    profile.setProperty(QStringLiteral("terminal_type"), m_terminalTypeCombo ? m_terminalTypeCombo->currentText() : QStringLiteral("xterm"));
    profile.setProperty(QStringLiteral("scrollback_lines"), m_scrollbackSpin ? QString::number(m_scrollbackSpin->value()) : QStringLiteral("10000"));
    profile.setProperty(QStringLiteral("font_family"), m_fontCombo ? m_fontCombo->currentFont().family() : QStringLiteral("Consolas"));
    profile.setProperty(QStringLiteral("font_size"), m_fontSizeSpin ? QString::number(m_fontSizeSpin->value()) : QStringLiteral("12"));
    profile.setProperty(QStringLiteral("color_scheme"), m_colorSchemeCombo ? m_colorSchemeCombo->currentText() : QStringLiteral("Standard"));
    profile.setProperty(QStringLiteral("background"), m_backgroundCombo ? m_backgroundCombo->currentText() : QStringLiteral("Black"));
    profile.setProperty(QStringLiteral("transparency"), m_transparencySpin ? QString::number(m_transparencySpin->value()) : QStringLiteral("255"));

    switch (type) {
    case ConnectionProfile::ConnectionType::LocalShell:
        // Local shell typically doesn't need saved settings
        break;

    case ConnectionProfile::ConnectionType::SSH:
        profile.setSshHost(m_sshHost->text());
        profile.setSshPort(m_sshPort->value());
        profile.setSshUsername(m_sshUsername->text());
        profile.setProperty(QStringLiteral("ssh_auth_method"), m_sshAuthMethod ? m_sshAuthMethod->currentText() : QStringLiteral("Password"));
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
    profile.setDownloadDirectory(m_downloadDirectoryEdit->text().trimmed());

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
    auto *outerLayout = new QHBoxLayout(this);
    outerLayout->setContentsMargins(12, 12, 12, 12);
    outerLayout->setSpacing(10);

    m_categoryList = new QListWidget(this);
    m_categoryList->setFixedWidth(220);
    m_categoryList->setAlternatingRowColors(false);
    m_categoryList->addItem(QStringLiteral("Connection"));
    m_categoryList->addItem(QStringLiteral("Port Forwarding"));
    m_categoryList->addItem(QStringLiteral("Terminal"));
    m_categoryList->addItem(QStringLiteral("Appearance"));
    m_categoryList->addItem(QStringLiteral("Advanced"));
    m_categoryList->setCurrentRow(0);
    outerLayout->addWidget(m_categoryList);

    auto *rightPanel = new QWidget(this);
    auto *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(8);

    m_optionsStack = new QStackedWidget(this);
    buildCategoryPages();
    rightLayout->addWidget(m_optionsStack, 1);

    auto *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();

    auto *cancelBtn = new QPushButton(QStringLiteral("Cancel"), this);
    auto *connectBtn = new QPushButton(QStringLiteral("OK"), this);
    connectBtn->setDefault(true);
    buttonLayout->addWidget(connectBtn);
    buttonLayout->addWidget(cancelBtn);
    rightLayout->addLayout(buttonLayout);

    outerLayout->addWidget(rightPanel, 1);

    connect(m_categoryList, &QListWidget::currentRowChanged, this, &NewSessionDialog::onCategoryChanged);
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
    m_scrollbackSpin->setValue(settings.value(QStringLiteral("global/scrollbackLines"), 10000).toInt());
    onConnectionTypeChanged(m_connectionTypeCombo->currentIndex());
}

void NewSessionDialog::buildCategoryPages()
{
    auto *connectionPage = new QWidget(this);
    auto *connectionLayout = new QFormLayout(connectionPage);
    connectionLayout->setContentsMargins(0, 0, 0, 0);

    m_profileCombo = new QComboBox(this);
    connectionLayout->addRow(QStringLiteral("Profile:"), m_profileCombo);

    m_profileName = new QLineEdit(this);
    m_profileName->setPlaceholderText(QStringLiteral("Session name"));
    connectionLayout->addRow(QStringLiteral("Name:"), m_profileName);

    auto *profileActionLayout = new QHBoxLayout();
    m_loadProfileBtn = new QPushButton(QStringLiteral("Load"), this);
    m_saveProfileBtn = new QPushButton(QStringLiteral("Save"), this);
    profileActionLayout->addWidget(m_loadProfileBtn);
    profileActionLayout->addWidget(m_saveProfileBtn);
    connectionLayout->addRow(QStringLiteral(""), profileActionLayout);

    m_connectionTypeCombo = new QComboBox(this);
    m_connectionTypeCombo->addItem(QStringLiteral("Local Shell"));
    m_connectionTypeCombo->addItem(QStringLiteral("SSH"));
    m_connectionTypeCombo->addItem(QStringLiteral("Serial"));
    m_connectionTypeCombo->addItem(QStringLiteral("Telnet"));
    connectionLayout->addRow(QStringLiteral("Connection Type:"), m_connectionTypeCombo);

    m_sshAuthMethodLabel = new QLabel(QStringLiteral("Preferred auth method:"), this);
    m_sshAuthMethod = new QComboBox(this);
    m_sshAuthMethod->addItem(QStringLiteral("Password"));
    m_sshAuthMethod->addItem(QStringLiteral("Public Key"));
    m_sshAuthMethod->addItem(QStringLiteral("Agent"));
    connectionLayout->addRow(m_sshAuthMethodLabel, m_sshAuthMethod);

    auto *hostLayout = new QHBoxLayout();
    m_sshHost = new QLineEdit(this);
    m_sshHost->setPlaceholderText(QStringLiteral("example.com"));
    hostLayout->addWidget(m_sshHost, 1);
    connectionLayout->addRow(QStringLiteral("Host:"), hostLayout);

    m_sshPort = new QSpinBox(this);
    m_sshPort->setMinimum(1);
    m_sshPort->setMaximum(65535);
    m_sshPort->setValue(22);
    connectionLayout->addRow(QStringLiteral("Port:"), m_sshPort);

    m_sshUsername = new QLineEdit(this);
    m_sshUsername->setPlaceholderText(QStringLiteral("username"));
    connectionLayout->addRow(QStringLiteral("Username:"), m_sshUsername);

    m_sshPassword = new QLineEdit(this);
    m_sshPassword->setEchoMode(QLineEdit::Password);
    m_sshPassword->setPlaceholderText(QStringLiteral("password"));
    connectionLayout->addRow(QStringLiteral("Password:"), m_sshPassword);

    m_sshUseKey = new QCheckBox(QStringLiteral("Use private key"), this);
    connectionLayout->addRow(QStringLiteral("Authentication:"), m_sshUseKey);

    m_sshPrivateKey = new QLineEdit(this);
    m_sshPrivateKey->setPlaceholderText(QStringLiteral("~/.ssh/id_rsa"));
    auto *keyRow = new QWidget(this);
    auto *keyLayout = new QHBoxLayout(keyRow);
    keyLayout->setContentsMargins(0, 0, 0, 0);
    auto *browsePrivateKeyBtn = new QPushButton(QStringLiteral("Browse..."), this);
    keyLayout->addWidget(m_sshPrivateKey, 1);
    keyLayout->addWidget(browsePrivateKeyBtn);
    connectionLayout->addRow(QStringLiteral("Private Key:"), keyRow);

    connect(browsePrivateKeyBtn, &QPushButton::clicked, this, [this]() {
        QString startPath = m_sshPrivateKey->text().trimmed();
        if (startPath.isEmpty())
            startPath = QDir::homePath() + QStringLiteral("/.ssh");
        const QString filePath = QFileDialog::getOpenFileName(
            this,
            QStringLiteral("Select SSH Private Key"),
            startPath,
            QStringLiteral("All Files (*)"));
        if (!filePath.isEmpty())
            m_sshPrivateKey->setText(filePath);
    });

    auto *loggingGroup = new QGroupBox(QStringLiteral("Session Logging"), this);
    auto *loggingLayout = new QFormLayout(loggingGroup);
    m_logSessionCheck = new QCheckBox(QStringLiteral("Log this session to a file"), this);
    loggingLayout->addRow(m_logSessionCheck);

    auto *logPathRow = new QWidget(this);
    auto *logPathBox = new QHBoxLayout(logPathRow);
    logPathBox->setContentsMargins(0, 0, 0, 0);
    logPathBox->setSpacing(6);
    m_logPathEdit = new QLineEdit(this);
    m_logBrowseBtn = new QPushButton(QStringLiteral("Browse..."), this);
    logPathBox->addWidget(m_logPathEdit, 1);
    logPathBox->addWidget(m_logBrowseBtn);
    loggingLayout->addRow(QStringLiteral("Log File:"), logPathRow);
    connectionLayout->addRow(loggingGroup);

    auto *downloadRow = new QWidget(this);
    auto *downloadLayout = new QHBoxLayout(downloadRow);
    downloadLayout->setContentsMargins(0, 0, 0, 0);
    m_downloadDirectoryEdit = new QLineEdit(this);
    m_downloadDirectoryBrowseBtn = new QPushButton(QStringLiteral("Browse..."), this);
    downloadLayout->addWidget(m_downloadDirectoryEdit, 1);
    downloadLayout->addWidget(m_downloadDirectoryBrowseBtn);
    connectionLayout->addRow(QStringLiteral("ZModem download folder:"), downloadRow);
    connect(m_downloadDirectoryBrowseBtn, &QPushButton::clicked, this, [this]() {
        QString startPath = m_downloadDirectoryEdit->text().trimmed();
        if (startPath.isEmpty())
            startPath = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        const QString directory = QFileDialog::getExistingDirectory(
            this, QStringLiteral("Select ZModem Download Folder"), startPath);
        if (!directory.isEmpty())
            m_downloadDirectoryEdit->setText(directory);
    });
    m_optionsStack->addWidget(connectionPage);

    auto *forwardPage = new QWidget(this);
    auto *forwardLayout = new QFormLayout(forwardPage);
    forwardLayout->setContentsMargins(0, 0, 0, 0);
    forwardLayout->addRow(QStringLiteral("Local forwarding:"), new QLineEdit(QStringLiteral("127.0.0.1:8080 -> localhost:80"), this));
    forwardLayout->addRow(QStringLiteral("Remote forwarding:"), new QLineEdit(QStringLiteral("127.0.0.1:2222 -> localhost:22"), this));
    forwardLayout->addRow(QStringLiteral("X11 forwarding:"), new QCheckBox(QStringLiteral("Enable X11 forwarding"), this));
    m_optionsStack->addWidget(forwardPage);

    auto *terminalPage = new QWidget(this);
    auto *terminalLayout = new QFormLayout(terminalPage);
    terminalLayout->setContentsMargins(0, 0, 0, 0);
    m_terminalTypeCombo = new QComboBox(this);
    m_terminalTypeCombo->addItem(QStringLiteral("xterm"));
    m_terminalTypeCombo->addItem(QStringLiteral("xterm-256color"));
    m_terminalTypeCombo->addItem(QStringLiteral("vt100"));
    m_terminalTypeCombo->addItem(QStringLiteral("screen"));
    terminalLayout->addRow(QStringLiteral("Terminal type:"), m_terminalTypeCombo);
    m_scrollbackSpin = new QSpinBox(this);
    m_scrollbackSpin->setRange(100, 100000);
    m_scrollbackSpin->setValue(10000);
    terminalLayout->addRow(QStringLiteral("Scrollback lines:"), m_scrollbackSpin);
    terminalLayout->addRow(QStringLiteral("Remote terminal bell:"), new QCheckBox(QStringLiteral("Enable bell"), this));
    m_optionsStack->addWidget(terminalPage);

    auto *appearancePage = new QWidget(this);
    auto *appearanceLayout = new QFormLayout(appearancePage);
    appearanceLayout->setContentsMargins(0, 0, 0, 0);
    m_colorSchemeCombo = new QComboBox(this);
    m_colorSchemeCombo->addItem(QStringLiteral("Standard"));
    m_colorSchemeCombo->addItem(QStringLiteral("Dark"));
    m_colorSchemeCombo->addItem(QStringLiteral("Solarized Dark"));
    m_colorSchemeCombo->addItem(QStringLiteral("Solarized Light"));
    appearanceLayout->addRow(QStringLiteral("Color scheme:"), m_colorSchemeCombo);
    m_backgroundCombo = new QComboBox(this);
    m_backgroundCombo->addItem(QStringLiteral("Black"));
    m_backgroundCombo->addItem(QStringLiteral("Dark Gray"));
    m_backgroundCombo->addItem(QStringLiteral("Custom"));
    appearanceLayout->addRow(QStringLiteral("Background:"), m_backgroundCombo);
    m_transparencySpin = new QSpinBox(this);
    m_transparencySpin->setRange(0, 255);
    m_transparencySpin->setValue(255);
    appearanceLayout->addRow(QStringLiteral("Transparency:"), m_transparencySpin);
    m_fontCombo = new QFontComboBox(this);
    m_fontCombo->setCurrentFont(QFont(QStringLiteral("Consolas")));
    appearanceLayout->addRow(QStringLiteral("Font:"), m_fontCombo);
    m_fontSizeSpin = new QSpinBox(this);
    m_fontSizeSpin->setRange(6, 64);
    m_fontSizeSpin->setValue(12);
    appearanceLayout->addRow(QStringLiteral("Font size:"), m_fontSizeSpin);
    m_optionsStack->addWidget(appearancePage);

    auto *advancedPage = new QWidget(this);
    auto *advancedLayout = new QFormLayout(advancedPage);
    advancedLayout->setContentsMargins(0, 0, 0, 0);
    advancedLayout->addRow(QStringLiteral("Keep alive:"), new QCheckBox(QStringLiteral("Enable keepalive"), this));
    advancedLayout->addRow(QStringLiteral("Compression:"), new QCheckBox(QStringLiteral("Enable compression"), this));
    advancedLayout->addRow(QStringLiteral("Custom command:"), new QLineEdit(this));
    m_optionsStack->addWidget(advancedPage);
}

void NewSessionDialog::onCategoryChanged(int row)
{
    if (!m_optionsStack || row < 0)
        return;

    const int index = qMin(row, m_optionsStack->count() - 1);
    m_optionsStack->setCurrentIndex(index);
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
    const bool isSsh = index == static_cast<int>(ConnectionProfile::ConnectionType::SSH);
    m_sshAuthMethodLabel->setVisible(isSsh);
    m_sshAuthMethod->setVisible(isSsh);
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

    if (m_terminalTypeCombo) {
        const QString terminalType = profile.terminalType();
        const int terminalIndex = m_terminalTypeCombo->findText(terminalType);
        if (terminalIndex >= 0)
            m_terminalTypeCombo->setCurrentIndex(terminalIndex);
    }
    if (m_downloadDirectoryEdit)
        m_downloadDirectoryEdit->setText(profile.downloadDirectory());
    if (m_scrollbackSpin)
        m_scrollbackSpin->setValue(profile.property(QStringLiteral("scrollback_lines"), QStringLiteral("10000")).toInt());
    if (m_fontCombo) {
        const QString fontFamily = profile.fontFamily();
        const int fontIndex = m_fontCombo->findText(fontFamily);
        if (fontIndex >= 0)
            m_fontCombo->setCurrentIndex(fontIndex);
        else
            m_fontCombo->setCurrentFont(QFont(fontFamily));
    }
    if (m_fontSizeSpin)
        m_fontSizeSpin->setValue(profile.fontSize());
    if (m_colorSchemeCombo) {
        const QString colorScheme = profile.property(QStringLiteral("color_scheme"), QStringLiteral("Standard"));
        const int idx = m_colorSchemeCombo->findText(colorScheme);
        if (idx >= 0)
            m_colorSchemeCombo->setCurrentIndex(idx);
    }
    if (m_backgroundCombo) {
        const QString background = profile.property(QStringLiteral("background"), QStringLiteral("Black"));
        const int idx = m_backgroundCombo->findText(background);
        if (idx >= 0)
            m_backgroundCombo->setCurrentIndex(idx);
    }
    if (m_transparencySpin)
        m_transparencySpin->setValue(profile.property(QStringLiteral("transparency"), QStringLiteral("255")).toInt());

    switch (profile.type()) {
    case ConnectionProfile::ConnectionType::LocalShell:
        // Nothing to load
        break;

    case ConnectionProfile::ConnectionType::SSH:
        m_sshHost->setText(profile.sshHost());
        m_sshPort->setValue(profile.sshPort());
        m_sshUsername->setText(profile.sshUsername());
        if (m_sshAuthMethod) {
            const QString authMethod = profile.property(QStringLiteral("ssh_auth_method"), QStringLiteral("Password"));
            const int authIndex = m_sshAuthMethod->findText(authMethod);
            if (authIndex >= 0)
                m_sshAuthMethod->setCurrentIndex(authIndex);
        }
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
