# AC4 Faction Overhaul — automatic update channel

This repository is the **single source of truth** for AC4 Faction Overhaul launcher/runtime distribution.

## Fully automatic flow

1. The current buildable source is stored as `source/FactionOverhaul-core-source.b64`.
2. `source/source.sha256` verifies the decoded source ZIP.
3. Updating `source/READY` starts the Windows build.
4. GitHub compiles the **Win32 runtime, injector, launcher and self-updater**.
5. A successful build packages both update layers and computes SHA-256 hashes.
6. The workflow writes `update.json` and versioned ZIPs to `releases/`.
7. Installed launchers read `update.json`, update the launcher first, then the runtime.

Stable manifest:

`https://raw.githubusercontent.com/lenaths/FactionOverhaul-Updates/main/update.json`

Users only need to launch `FactionOverhaulLauncher.exe`.

## Repository layout

```text
.github/workflows/build-publish.yml
source/
  FactionOverhaul-core-source.b64
  source.sha256
  README.md
  READY
releases/
  FactionOverhaulLauncher-<version>.zip
  FactionOverhaulRuntime-<version>.zip
  latest.txt
update.json
AUTOMATION.md
```

`update.json` is only replaced after a successful Windows build, so a broken source revision cannot be advertised to installed launchers.
