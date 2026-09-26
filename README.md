# AC4 Faction Overhaul — Update Channel

Public update channel for **AC4 Faction Overhaul**.

This repository is used by the launcher to discover launcher and runtime updates.

## Stable manifest

`update.json`

The launcher reads the raw `main/update.json` manifest and updates itself first, then the mod runtime.

## Publishing

Release artifacts are versioned and immutable:

- `launcher/FactionOverhaulLauncher-<version>.zip`
- `runtime/FactionOverhaulRuntime-<version>.zip`

The SHA-256 stored in `update.json` must match the uploaded package.
