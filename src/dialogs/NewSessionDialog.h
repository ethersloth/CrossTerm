#pragma once

#include <QDialog>
#include <memory>

class ConnectionProfile;
class ProfileManager;
class QComboBox;
class QLineEdit;
class QSpinBox;
class QCheckBox;
class QPushButton;
class QStackedWidget;

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

private:
    void buildUi();
    void buildLocalShellUI();
    void buildSSHUI();
    void buildSerialUI();
    void buildTelnetUI();
    void populateProfileList();
    void loadProfileIntoUI(const ConnectionProfile &profile);

    ProfileManager &m_profileManager;
    QComboBox *m_connectionTypeCombo = nullptr;
    QComboBox *m_profileCombo = nullptr;
    QStackedWidget *m_optionsStack = nullptr;

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
};
