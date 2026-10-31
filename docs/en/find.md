# Find queries and data migration

Find is experimental and requires a compatible device with Pro activated. Reports depend on nearby devices receiving and uploading broadcasts; real-time locations are not guaranteed.

## Enable Find

After connecting, **Find status** explains whether broadcasting can be enabled. Compatible Free devices are prompted to activate Pro; enabled devices show that broadcasting is on. Unsupported devices, unread status and firmware that needs updating have separate messages.

Online, static and standalone editions can enroll devices and enable or disable broadcasting over Bluetooth. They cannot sign in to Apple or query locations; queries require the local WebUI.

Before enabling Find in a non-local edition, save the device information:

1. Connect the device, open **Find**, and turn on the broadcast switch.
2. First enrollment generates a new identity. For an already enrolled device, select or paste its existing backup to verify its identity.
3. Passphrase encryption is selected by default. Use at least 6 characters, or generate and copy a random 12-character passphrase.
4. Copy the backup string or save the file. Keep the passphrase separately if encrypted.
5. Confirm that you saved the material, then enroll and enable Find, or enable the existing identity.

Import the device information into a local WebUI that supports queries; otherwise you cannot obtain the device's location later. The device cannot recover your query private key. If the original backup is missing, export it from the local WebUI where the device was enrolled.

Encrypted copies appear directly under **Device information saved in this browser**, with actions to copy, save a file or clear an individual record. Clearing removes only this browser's copy; it does not disable broadcasting or delete the local client's device information or history. Save a separate file first. Clearing site data or changing browsers or origins can also remove the copy. Unencrypted material is never automatically saved in browser storage.

The browser stores neither the decryption passphrase nor plaintext private keys. Ciphertext still needs protection: scripts on the same origin can read it, and short passphrases may be guessed offline. Different paths on a hosted site normally share the same origin. Prefer a generated passphrase and store it separately from the backup.

New backups also remember the tag name for identification. Names and fingerprint suffixes are not encrypted; query private keys remain inside the encrypted backup. Older unnamed records show a fingerprint suffix, or the current device name when its identity matches the connected device.

The browser backup list appears as a separate panel only in the hosted or standalone client when saved records exist. It is hidden when empty and in local service mode; hiding it does not delete saved records.

When multiple tags are saved, select one from the dropdown. Only its information is shown, and copy, save and clear act only on the selected tag.

Disabling broadcasting may require the matching device backup and passphrase to verify ownership. It does not delete identities or history.

## Import into the local WebUI

1. Start the local WebUI. Beside **Enrolled tags** on the Find page, choose **Import**.
2. Paste the string or select a file. Enter the passphrase for encrypted files; acknowledge the risk for plaintext files.
3. Decrypt/import. Existing device information and history are preserved and merged.
4. Sign in to Apple, choose a tag and query.

Import does not require reconnecting or reenrolling the device and does not replace its installed identity. Automatic queries are disabled for newly imported identities.

## Export one or multiple devices

Use **Export** beside **Find broadcast** for the connected device. In the local WebUI, **Export tags** beside **Enrolled tags** lets you select multiple devices.

Select the contents independently:

| Contents | Available after import |
| --- | --- |
| Device information, including query private keys | Query new locations after signing in to Apple; history is optional |
| Location history | View recorded locations only; no query private keys and no new queries |
| Both | Continue querying and retain recorded locations |

Single-device export provides a copyable string. Exporting local information produces a downloadable file without displaying a large text block; device selection stays inside the dialog. For a device with extensive history, select only that device in the batch dialog to save a file. Import accepts both files and strings.

All identities belonging to a selected device are included. A history-only import can later be upgraded by importing the matching device information. Apple passwords, verification codes and login sessions are excluded.

Complete or recover pending enrollment before exporting. If the backup exceeds 8 MiB, select fewer devices or export keys and history separately without deleting the original records.

## Encrypted and unencrypted backups

**Encrypt with a passphrase (recommended)** is selected by default. The encrypted string and file use the same format and require the passphrase. It is not saved and cannot be recovered. A generated passphrase is recommended; store it separately.

You may turn encryption off. Export then requires acknowledgement of the privacy warning: someone with unencrypted device information may query its location, and anyone with history can read past locations. Do not share these files publicly or upload them to public repositories, group chats or untrusted storage.

Plaintext exports use JSON files; encrypted exports use .iktfind files. LAN migration still requires HTTPS; HTTP is allowed on loopback addresses. Selecting an unencrypted file does not disable transport protection.

## History and periodic recording

**History** beside a device heading combines tracks from all its identities. **Note** updates the device's identity notes together. The identity count appears beside the device name.

**Start all** enables periodic queries for each device's current identity. **Pause all** stops every identity while preserving history. The local service must remain running with a valid Apple query session.

Historical identities stop automatic queries by default. You can enable one manually using its own recording control; that choice persists. Updating a device's Find identity stops its older identities again and preserves their history.

A wrong passphrase or damaged backup does not change the library. If enrollment is unconfirmed, keep the backup and reconnect to inspect the state instead of repeatedly reenrolling.
