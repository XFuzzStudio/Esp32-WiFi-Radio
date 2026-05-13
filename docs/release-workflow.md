# Release Workflow

This repository uses a folder-per-version local workflow and GitHub releases for
published versions.

## Local Version Folders

Keep local working copies under:

```text
Desktop/github/Esp32-WiFi-Radio/
  V1.0/
  V1.1/
  V1.2/
  V2.0/
```

Before starting a new version, copy the newest finished folder to the next
version folder. Small changes increment by `0.1`; large final updates increment
the major version.

Examples:

- Small update: `V1.1` -> `V1.2`
- Large final update: `V1.9` -> `V2.0`

## Required Files And Secrets

Do not publish private credentials, private SD dumps, or local-only binaries.
Use `private_files/` for local copies that must stay off GitHub.

Keep clean publishable templates in the repo. For example, the radio config
template lives at:

```text
sd_card/apps_data/ESP32WiFiRadio/radio.cfg
```

Private values such as `wifi_ssid`, `wifi_password`, paired device data, or
local network notes must stay out of Git.

## Changelog Rules

Update `CHANGELOG.md` for every version before publishing.

Each entry should include:

- Version number and release date.
- User-visible changes.
- Firmware/app changes.
- SD card layout or config changes.
- Breaking changes or migration steps.
- Known issues, if any.

Template:

```markdown
## X.Y - YYYY-MM-DD

- Added ...
- Changed ...
- Fixed ...
- Removed ...
- Migration: ...
```

## Release Checklist

1. Build the affected firmware projects.
2. Test boot, SD mount, Wi-Fi/AP, web UI, and the main changed workflow.
3. Update visible version strings where applicable.
4. Update `CHANGELOG.md`.
5. Update `VERSION_NOTES.md` in the version folder.
6. Verify no private files are staged:

```powershell
git status --short
git diff --check
```

7. Commit and push to GitHub.
8. Create a GitHub release with the same version tag.
9. Attach release binaries only when they are meant to be public.
