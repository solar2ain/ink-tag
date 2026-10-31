# Security

Do not publish device secrets, pairing keys, factory MAC-derived credentials, private signing keys, or dumps containing per-device data in an issue.

Until a dedicated security contact is published, report a suspected vulnerability privately to the repository owner through GitHub. Include the affected board, firmware version, reproduction steps, and whether physical access is required.

Only install artifacts downloaded from this repository's GitHub Releases and verified against `SHA256SUMS.txt`.

The default local service listens on loopback. Optional LAN mode remains available over HTTPS with a generated access password; host and browser-origin checks also protect the data API and Find proxy. Localhost-only use does not require this extra login. Uploaded active documents are downloads, not same-origin web pages.

Use the browser's optional device-credential memory only on trusted computers. The saved value is a control credential, not the plaintext password. Pages hosted at different paths on the same GitHub Pages hostname share a browser origin and storage; path names are not a security boundary. Prefer a dedicated hostname or local service when isolation from other applications is needed. A weak device password may permit offline guessing if its verifier is obtained; online device throttling does not prevent that attack.

The distribution alone cannot establish firmware-side authorization, signed-image enforcement or BLE link encryption for a particular device. Client-side checks are not proof of device security. Do not configure a device password over a link whose confidentiality you cannot establish; follow the device's documented pairing/security requirements.
