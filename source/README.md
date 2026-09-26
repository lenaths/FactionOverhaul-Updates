# Build source payload

The current buildable Faction Overhaul source is stored as a base64-encoded ZIP:

- `FactionOverhaul-core-source.b64`
- SHA-256 of the decoded ZIP is stored in `source.sha256`
- `READY` is changed last to trigger the Windows build/publish workflow.

Current versions:

- Launcher: **1.2.0**
- Runtime/mod: **0.10.0**

Third-party libraries are restored by GitHub Actions at pinned versions and are not duplicated in this source payload.
