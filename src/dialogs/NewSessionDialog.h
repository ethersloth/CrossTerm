#pragma once

#include <QDialog>
#include <memory>

class ConnectionProfile;
class ProfileManager;
class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QCheckBox;
class QPushButton;
class QStackedWidget;
class QListWidget;
class QFontComboBox;

/**
 * Dialog for creating new sessions and managing connection profiles.
 * Supports Local Shell, SSH, Serial, and Telnet connections.
 */
class NewSessionDialog : public QDialog
{
    Q_OBJECT
public:
    explicit NewSessionDialog(ProfileManager &profileManager, QWidget *parent = nullptr);

    ConnectionProfile createProfile() const;
    bool selectProfile(const QString &profileName);
    static ConnectionProfile showNewSessionDialog(ProfileManager &profileManager, QWidget *parent = nullptr);

private slots:
    void onConnectionTypeChanged(int index);
    void onLoadProfileClicked();
    void onSaveProfileClicked();
    void onProfileSelected(int index);
    void onCategoryChanged(int row);

private:
    void buildUi();
    void buildLocalShellUI();
    void buildSSHUI();
    void buildSerialUI();
    void buildTelnetUI();
    void buildCategoryPages();
    void populateProfileList();
    void loadProfileIntoUI(const ConnectionProfile &profile);

    ProfileManager &m_profileManager;
    QComboBox *m_connectionTypeCombo = nullptr;
    QComboBox *m_profileCombo = nullptr;
    QStackedWidget *m_optionsStack = nullptr;
    QListWidget *m_categoryList = nullptr;
    QLabel *m_sshAuthMethodLabel = nullptr;
    QComboBox *m_sshAuthMethod = nullptr;
    QComboBox *m_terminalTypeCombo = nullptr;
    QSpinBox *m_scrollbackSpin = nullptr;
    QComboBox *m_colorSchemeCombo = nullptr;
    QComboBox *m_backgroundCombo = nullptr;
    QSpinBox *m_transparencySpin = nullptr;
    QFontComboBox *m_fontCombo = nullptr;
    QSpinBox *m_fontSizeSpin = nullptr;

    // Local Shell widgets
    QLineEdit *m_localShellCmd = nullptr;

    // SSH widgets
    QLineEdit *m_sshHost = nullptr;
    QSpinBox *m_sshPort = nullptr;
    QLineEdit *m_sshUsername = nullptr;
    QLineEdit *m_sshPassword = nullptr;
    QLineEdit *m_sshPrivateKey = nullptr;
    QCheckBox *m_sshUseKey = nullptr;

    // Serial widgets
    QLineEdit *m_serialPort = nullptr;
    QSpinBox *m_serialBaud = nullptr;
    QSpinBox *m_serialDataBits = nullptr;
    QComboBox *m_serialParity = nullptr;
    QSpinBox *m_serialStopBits = nullptr;

    // Telnet widgets
    QLineEdit *m_telnetHost = nullptr;
    QSpinBox *m_telnetPort = nullptr;

    // Profile management
    QLineEdit *m_profileName = nullptr;
    QPushButton *m_saveProfileBtn = nullptr;
    QPushButton *m_loadProfileBtn = nullptr;

    // Session logging
    QCheckBox *m_logSessionCheck = nullptr;
    QLineEdit *m_logPathEdit = nullptr;
    QPushButton *m_logBrowseBtn = nullptr;
    QLineEdit *m_downloadDirectoryEdit = nullptr;
    QPushButton *m_downloadDirectoryBrowseBtn = nullptr;
};
