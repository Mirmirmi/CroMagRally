#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
UPDATER_SRC="${SCRIPT_DIR}/cromag-updater.py"

OS="$(uname -s)"

if [[ "${OS}" == "Darwin" ]]; then
  SUPPORT_DIR="${HOME}/Library/Application Support/CroMagRally"
  UPDATER_DST="${SUPPORT_DIR}/cromag-updater.py"
  LOG_FILE="${HOME}/Library/Logs/CroMagRally-updater.log"
  PLIST_DIR="${HOME}/Library/LaunchAgents"
  PLIST_PATH="${PLIST_DIR}/io.jor.cromagrally.updater.plist"
  LAUNCHER_APP="/Applications/Play Cro-Mag Rally.app"

  mkdir -p "${SUPPORT_DIR}" "${PLIST_DIR}" "$(dirname "${LOG_FILE}")"
  cp "${UPDATER_SRC}" "${UPDATER_DST}"
  chmod 0755 "${UPDATER_DST}"

  # Build a lightweight native .app launcher that runs a pre-launch check then opens Cro-Mag Rally.app
  rm -rf "${LAUNCHER_APP}"
  mkdir -p "${LAUNCHER_APP}/Contents/MacOS" "${LAUNCHER_APP}/Contents/Resources"
  cp "${REPO_ROOT}/packaging/CroMagRally.icns" "${LAUNCHER_APP}/Contents/Resources/CroMagRally.icns"

  cat > "${LAUNCHER_APP}/Contents/Info.plist" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleExecutable</key>
  <string>PlayCroMagRally</string>
  <key>CFBundleIconFile</key>
  <string>CroMagRally.icns</string>
  <key>CFBundleIdentifier</key>
  <string>io.jor.cromagrally.launcher</string>
  <key>CFBundleName</key>
  <string>Play Cro-Mag Rally</string>
  <key>CFBundlePackageType</key>
  <string>APPL</string>
  <key>CFBundleShortVersionString</key>
  <string>1.0</string>
  <key>LSUIElement</key>
  <true/>
</dict>
</plist>
EOF

  cat > "${LAUNCHER_APP}/Contents/MacOS/PlayCroMagRally" <<EOF
#!/usr/bin/env bash
exec /usr/bin/python3 "${UPDATER_DST}" launch --gui "\$@"
EOF
  chmod 0755 "${LAUNCHER_APP}/Contents/MacOS/PlayCroMagRally"

  cat > "${PLIST_PATH}" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key>
  <string>io.jor.cromagrally.updater</string>
  <key>ProgramArguments</key>
  <array>
    <string>/usr/bin/python3</string>
    <string>${UPDATER_DST}</string>
    <string>update</string>
  </array>
  <key>RunAtLoad</key>
  <true/>
  <key>StartInterval</key>
  <integer>1800</integer>
  <key>StandardOutPath</key>
  <string>${LOG_FILE}</string>
  <key>StandardErrorPath</key>
  <string>${LOG_FILE}</string>
</dict>
</plist>
EOF

  UID_NUM="$(id -u)"
  launchctl bootout "gui/${UID_NUM}/io.jor.cromagrally.updater" >/dev/null 2>&1 || true
  launchctl bootstrap "gui/${UID_NUM}" "${PLIST_PATH}"

  /usr/bin/python3 "${UPDATER_DST}" update
  echo "macOS auto-updater installed."
  echo "  Background agent: ${PLIST_PATH}"
  echo "  Launcher app:     ${LAUNCHER_APP}"

elif [[ "${OS}" == "Linux" ]]; then
  TARGET_USER="${1:-${SUDO_USER:-$USER}}"
  TARGET_HOME="$(eval echo "~${TARGET_USER}")"
  OPT_DIR="/opt/cromagrally"
  APP_DIR="${TARGET_HOME}/.local/share/CroMagRally"
  APPIMAGE_PATH="${APP_DIR}/CroMagRally.AppImage"
  DESKTOP_DIR="${TARGET_HOME}/Desktop"
  MENU_DIR="${TARGET_HOME}/.local/share/applications"

  sudo mkdir -p "${OPT_DIR}"
  sudo cp "${UPDATER_SRC}" "${OPT_DIR}/cromag-updater.py"
  sudo chmod 0755 "${OPT_DIR}/cromag-updater.py"
  sudo cp "${REPO_ROOT}/packaging/io.jor.cromagrally.png" "${OPT_DIR}/icon.png"
  sudo chmod 0644 "${OPT_DIR}/icon.png"

  sudo -u "${TARGET_USER}" mkdir -p "${APP_DIR}" "${DESKTOP_DIR}" "${MENU_DIR}"

  # Migrate an existing Desktop AppImage into the managed path if present
  if sudo test -f "${DESKTOP_DIR}/CroMagRally.AppImage" && ! sudo test -f "${APPIMAGE_PATH}"; then
    sudo mv "${DESKTOP_DIR}/CroMagRally.AppImage" "${APPIMAGE_PATH}"
    sudo chown "${TARGET_USER}:${TARGET_USER}" "${APPIMAGE_PATH}"
    sudo chmod 0755 "${APPIMAGE_PATH}"
  fi

  DESKTOP_CONTENT="[Desktop Entry]
Version=1.0
Type=Application
Name=Cro-Mag Rally
Comment=Play Cro-Mag Rally (checks for updates on launch)
Exec=/usr/bin/python3 ${OPT_DIR}/cromag-updater.py launch --gui --install-path ${APPIMAGE_PATH}
Icon=${OPT_DIR}/icon.png
Terminal=false
Categories=Game;ActionGame;
StartupWMClass=CroMagRally
"

  printf '%s' "${DESKTOP_CONTENT}" | sudo -u "${TARGET_USER}" tee "${DESKTOP_DIR}/CroMagRally.desktop" >/dev/null
  sudo chmod 0755 "${DESKTOP_DIR}/CroMagRally.desktop"
  printf '%s' "${DESKTOP_CONTENT}" | sudo -u "${TARGET_USER}" tee "${MENU_DIR}/io.jor.cromagrally.desktop" >/dev/null
  sudo chmod 0644 "${MENU_DIR}/io.jor.cromagrally.desktop"

  sudo tee /etc/systemd/system/cromagrally-updater.service >/dev/null <<EOF
[Unit]
Description=Cro-Mag Rally Auto-Updater
Wants=network-online.target
After=network-online.target

[Service]
Type=oneshot
User=${TARGET_USER}
ExecStart=/usr/bin/python3 ${OPT_DIR}/cromag-updater.py update --install-path ${APPIMAGE_PATH}
EOF

  sudo tee /etc/systemd/system/cromagrally-updater.timer >/dev/null <<EOF
[Unit]
Description=Periodically check for Cro-Mag Rally updates

[Timer]
OnBootSec=2min
OnUnitActiveSec=30min
Persistent=true

[Install]
WantedBy=timers.target
EOF

  sudo systemctl daemon-reload
  sudo systemctl enable --now cromagrally-updater.timer
  sudo -u "${TARGET_USER}" /usr/bin/python3 "${OPT_DIR}/cromag-updater.py" update

  echo "Linux auto-updater installed for user ${TARGET_USER}."
  echo "  Systemd timer: cromagrally-updater.timer"
  echo "  AppImage path: ${APPIMAGE_PATH}"
  echo "  Launcher:      ${DESKTOP_DIR}/CroMagRally.desktop"
else
  echo "Unsupported OS: ${OS}" >&2
  exit 1
fi
