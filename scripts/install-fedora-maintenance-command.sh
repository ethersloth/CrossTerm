#!/usr/bin/env bash
set -euo pipefail

readonly WRAPPER_PATH="/usr/local/sbin/crossterm-update"
readonly SUDOERS_PATH="/etc/sudoers.d/crossterm-maintenance"

if [[ ${EUID} -ne 0 ]]; then
    echo "Run this installer with sudo:" >&2
    echo "  sudo $0 [username]" >&2
    exit 1
fi

target_user=${1:-${SUDO_USER:-}}
if [[ -z ${target_user} || ${target_user} == root ]]; then
    echo "Specify the non-root SSH username: sudo $0 <username>" >&2
    exit 1
fi

if ! getent passwd "${target_user}" >/dev/null; then
    echo "User '${target_user}' does not exist." >&2
    exit 1
fi

if ! command -v dnf >/dev/null || ! command -v systemctl >/dev/null; then
    echo "This installer requires Fedora with dnf and systemctl." >&2
    exit 1
fi

install -d -o root -g root -m 0755 /usr/local/sbin
install -d -o root -g root -m 0750 /etc/sudoers.d

wrapper_temp=$(mktemp)
sudoers_temp=$(mktemp)
trap 'rm -f "${wrapper_temp}" "${sudoers_temp}"' EXIT

cat >"${wrapper_temp}" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
/usr/bin/dnf -y upgrade --refresh
/usr/bin/systemctl reboot
EOF

printf '%s ALL=(root) NOPASSWD: %s\n' "${target_user}" "${WRAPPER_PATH}" >"${sudoers_temp}"

if ! visudo -cf "${sudoers_temp}" >/dev/null; then
    echo "Generated sudoers rule failed validation; no changes were installed." >&2
    exit 1
fi

install -o root -g root -m 0755 "${wrapper_temp}" "${WRAPPER_PATH}"
install -o root -g root -m 0440 "${sudoers_temp}" "${SUDOERS_PATH}"
visudo -cf "${SUDOERS_PATH}"

echo
echo "Fedora maintenance command installed for ${target_user}."
echo "Save this command in the matching CrossTerm profile:"
echo "  sudo -n ${WRAPPER_PATH}"
echo
echo "Running it will update the host, reboot it, and close the SSH session."