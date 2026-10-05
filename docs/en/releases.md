# Versioning and releases

InkTag firmware, OTA packages, Web UI, and user documentation share one semantic version: `major.minor.patch`.

- `0.x.y`: preview releases whose features and protocol may still change.
- `1.x.y`: stable releases after support-matrix regression and a documented compatibility policy.
- Stable tags use `v<version>`; prereleases use a distinct SemVer tag such as
  `v0.2.0-preview.1`. Each tag maps to exactly one GitHub Release.

Release-repository `main` always points at the latest release snapshot. Immutable
tags and GitHub Releases retain older versions; `main` does not accumulate one
directory per version. A release may contain only the CLI, WebUI, or filtered
firmware. `firmware/manifest.json` is authoritative for the actual contents.

The release process is build once, test those exact bytes, then promote those same bytes. A hardware-tested candidate must not be rebuilt before publishing. Every artifact records its size and SHA-256 digest.

Status meanings:

- **Beta**: core flows are hardware-tested, with explicit remaining release gates.
- **Experimental**: buildable or partially working, without complete hardware qualification.
- **Future**: roadmap entry only; no firmware is shipped.
