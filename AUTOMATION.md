# Automatic publishing contract

Repository: `lenaths/FactionOverhaul-Updates`

Manifest consumed by installed launchers:

`https://raw.githubusercontent.com/lenaths/FactionOverhaul-Updates/main/update.json`

For each new Faction Overhaul update:

1. update the source bundle and `source.sha256`;
2. bump `version.json` inside the source bundle;
3. update `source/READY` last;
4. GitHub Actions builds on `windows-latest` for **Win32**;
5. only a successful build may replace `update.json`;
6. generated launcher/runtime packages are stored under `releases/`.

This keeps launcher self-updates and runtime updates on the same automatic channel.
