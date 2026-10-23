#pragma once

class ProfileManager;
class QWidget;

// File > Export Backup / Import Backup: password-protected copy of all
// sessions, settings, and referenced private keys, for moving CrossTerm to
// another computer.
namespace BackupDialogs {

void exportBackup(QWidget *parent, const ProfileManager &profiles);
// Returns true when a backup was applied and the UI should reload.
bool importBackup(QWidget *parent, ProfileManager &profiles);

}
