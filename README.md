# AC4 Faction Overhaul — automatic update channel

This repository is the **single source of truth** for AC4 Faction Overhaul launcher/runtime distribution.

## Fully automatic flow

1. The current source bundle is stored under `source/parts/`.
2. Updating `source/READY` starts the Windows GitHub Actions build.
3. GitHub compiles the **Win32 runtime, injector, launcher and self-updater**.
4. The workflow packages both update layers and computes SHA-256 hashes.
5. It writes `update.json` and commits versioned ZIPs to `releases/`.
6. Installed launchers read the raw `main/update.json`, update the launcher first, then the mod runtime.

Stable manifest:

`https://raw.githubusercontent.com/lenaths/FactionOverhaul-Updates/main/update.json`

Users only need to launch `FactionOverhaulLauncher.exe`.

## Repository layout

```text
.github/workflows/build-publish.yml
source/
  parts/
  source.sha256
  READY
releases/
  FactionOverhaulLauncher-<version>.zip
  FactionOverhaulRuntime-<version>.zip
  latest.txt
update.json
```

Generated release files are only updated after a successful Windows build.
