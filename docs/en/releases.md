# Versions and checksums

Firmware and the Web console may update independently. Check each component's version in the manifest rather than assuming all files share one version.

Verify downloads against `SHA256SUMS.txt`: Windows uses `Get-FileHash -Algorithm SHA256 FILE`, Linux uses `sha256sum FILE`, and macOS uses `shasum -a 256 FILE`. Do not flash incomplete, modified or mismatching files.

`firmware/manifest.json` lists exact public board IDs, included files, sizes, hashes and system support. complete-flashing images are PCB-specific; shared OTA bytes do not make complete-flashing images interchangeable. Preview candidates are for inspection, not formal release or production approval. A board without an included artifact cannot be installed from this package.
