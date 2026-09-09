# Contributing

Use C++20 and shared Qt 6 Widgets. Runtime code must remain C++; development tools
may use PowerShell. Run `scripts/build.ps1 -Configuration Debug` before a change is
reviewed. Keep commits small and describe the user-visible change and validation.

Use `.clang-format`. Domain behavior belongs in services, not widget callbacks.
All SQL values must be bound parameters. Dynamic identifiers must come from the
schema registry, never input. Add migrations instead of rewriting an already
released migration. Preserve SQL NULL values and foreign-key integrity.

Network changes must preserve explicit opt-in, source enablement, cancellation,
TLS validation, timeouts, rate limits and redacted errors. Do not introduce
telemetry, remote resume processing, stealth scraping or automatic application
submission. Add offline fixtures for source parsers; keep live API tests opt-in.

Never commit user databases, exports, API credentials, build directories or local
Qt installations. Report security issues privately to the repository maintainer;
do not include private career information in public issues.

Before a release run Debug and Release tests, CPack, and
`scripts/verify-installer.ps1`. Tag with the version from CMake. The GitHub release
job needs repository token permission to create release artifacts.
