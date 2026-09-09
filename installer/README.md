# Windows installer

CMake installs the executable and license notices, then Qt's deployment script
invokes windeployqt for shared libraries, Windows platform, TLS and SQLite plugins.
CPack NSIS produces `release/PathWeave-Setup-x64.exe`. No Python, Node.js, webview,
remote backend or Qt development kit is required on the destination computer.

`scripts/verify-installer.ps1` installs in an isolated workspace directory,
removes development DLL directories from PATH, launches the installed executable,
runs native form/service checks, restarts to verify SQLite persistence, exports
JSON/CSV/Markdown/HTML/PDF, and silently uninstalls. It verifies that the executable
is removed and the isolated career database remains. Verification outputs stay
under `artifacts`; user career data is never used for this test.

Uninstall removes program files and shortcuts. It preserves the user's data.
Use Settings > Delete all data before uninstall if you want those records removed.

The installer is unsigned unless a distributor adds an actual certificate and
signing step. This repository makes no signing claim. Windows SmartScreen may
warn about an unsigned installer. Verify its SHA-256 against SHA256SUMS.txt.
