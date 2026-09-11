# PathWeave 1.1.0 — verification record

Verified on Windows 11 x64 and macOS 15 arm64 CI, 2026-09-11. Native C++20 / shared Qt 6.8.3 Widgets.

| Check | Result |
| --- | --- |
| MSVC Debug configure/build | PASS |
| Debug Qt Test cases | 62 passed, 0 failed |
| MSVC Release configure/build | PASS |
| Release Qt Test cases | 62 passed, 0 failed |
| Shared Qt deployment + CPack NSIS | PASS |
| Silent installation | Exit 0 |
| Installed app native workflow smoke | Exit 0 |
| Separate-process restart/persistence | Exit 0 |
| CV generation, review, version, PDF attachment | PASS |
| PDF / HTML / TXT CV exports | PASS |
| Password-encrypted backup roundtrip | PASS |
| JSON roundtrip / CSV / Markdown / HTML / PDF reports | PASS |
| Uninstall | Exit 0; executable removed; user data preserved |
| Portable runtime deployment | PASS; application version 1.1.0 |
| Windows binary metadata | File 1.1.0.0 / product 1.1.0 |
| Authenticode | NotSigned |
| macOS debug/release tests + DragNDrop packaging | PASS (GitHub Actions run 34623708803) |

The two suites contain 38 and 24 Qt cases, respectively, including four combined
init/cleanup cases. Both configurations run the same suites. Network tests use
asynchronous fixtures for timeout, cancellation, 429 backoff, cache expiry,
same-origin redirect following, redirect limits and robots rules.

The CV suite covers entered-only facts, explicit source selection, missing
requirements, HTML escaping, safe photo loading, searchable PDF output, review
invalidation, attachment/status separation, draft autosave and persistence.
Additional tests cover v1-to-v2 migration, strong requisition identities, partial
updates, skill links, bilingual word boundaries, saved-search execution, schedules,
bounded explicit feedback, attachment integrity, encrypted backup tampering,
non-demo report cohorts, ICS timestamps and permanent deletion.

## Local artifacts

Repository: `C:/Users/ANHKHOI/Documents/ChatGPT/Path_Weave`

Installer (Windows): `C:/Users/ANHKHOI/Documents/ChatGPT/Path_Weave/release/PathWeave-Setup-x64.exe`

Installer (macOS, CI artifact): `C:/Users/ANHKHOI/Documents/ChatGPT/Path_Weave/release/PathWeave-Setup-macOS-arm64.dmg`

Windows size: **36,180,669 bytes**

Windows SHA-256: `7bd7539837bab65a2bac4681064c9ea504cc408e45052dd2deac136598060c20`

macOS size: **24,014,404 bytes**

macOS SHA-256: `5a7a5bdff54ff1ae21f745aec2c233d2d58ace6286d3224df2bb6423da38f630`

Portable application: `C:/Users/ANHKHOI/Documents/ChatGPT/Path_Weave/release/portable/PathWeave.exe`

- `build/Debug/test-results.xml`, `enhancement-results.xml`
- `build/Release/test-results.xml`, `enhancement-results.xml`
- `artifacts/installer-verification.json`
- `artifacts/installed-checks/smoke-verification.json`, `restart-verification.json`
- `artifacts/installed-checks/cv.pdf`, `cv.html`, `cv.txt`
- `artifacts/installed-checks/backup.json`, `backup.pwbackup`, `applications.csv`
- `artifacts/installed-checks/installed-cv-editor.png`
- `docs/screenshots/cv-editor.png`
- `release/build-manifest.json`, `release/SHA256SUMS.txt`

The installed application ran with PATH restricted to Windows system directories,
so Qt DLLs came from the installed package. The installer includes app-local MSVC
runtime DLLs. The isolated test installation was removed after verification; the
portable folder remains available.

An independent PDF reader verified the final Release example as one A4 page with
searchable Vietnamese text and one embedded synthetic test image. The installed
smoke PDF also has searchable text; that particular fixture omits the optional
photo. The rendered example was visually inspected for margins and typography.

## Scope and limits

These are automated tests of real Qt widgets, forms and service workflows, not
comprehensive human UI or assistive-technology certification. The installed smoke
completes onboarding, creates work records, converts evidence, configures the demo
source, filters employment types separately from remote mode, saves a job, creates
an application/follow-up/interview, generates and attaches a reviewed CV, exports
and restores data, and restarts in a separate process to check persistence.

Installation was verified on this Windows host, not a fresh matrix of Windows VMs.
Windows EFS enablement was not exercised on the host; it remains dependent on the
Windows edition, NTFS support and policy. Adzuna credentials were not supplied.
Provider parsing/configuration has fixture coverage; this release does not claim
live validation of every external provider. The earlier 1.0.0 Remotive probe is
historical evidence, not a new 1.1.0 provider validation.

The GitHub Actions workflow reproduces build/test/package/install checks. Its
actual run status must be checked on GitHub; local success does not establish CI
success. The hash above identifies the locally verified installer; a separate CI
build can produce a different installer hash.

No signing certificate or SmartScreen reputation is claimed. CV generation ranks
and assembles local facts; it cannot guarantee a best CV or hiring success. See
README Known limitations and CV_AND_DISCOVERY.md for remaining product constraints.
