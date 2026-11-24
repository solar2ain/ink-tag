# DA14585 OTA admission

DA keeps the 64-byte native pQ header, DAV1 layout, epoch admission, CRC,
single-slot transport and 96-byte App trailer budget. CRC and unsigned hashes
detect corruption; they do not authenticate a publisher.

## Recovery policy

Read SUOTA VERSION on the same Recovery connection before the first transfer
command. The exact nine-byte value is `0d 49 4b 01 01 53 50 01 PP`.
The original five bytes retain native version 13 and safe-resume schema1/bit1.
`53 50` is the policy marker, the following byte is schema1, and `PP` is
`00` (legacy optional), `01` (legacy required), or `02` (CRC-only).
The new exact value is `0d494b010153500102`. Unknown, truncated, duplicate or
conflicting responses do not authorize unsigned OTA; neither a software version
nor a package manifest establishes live device policy. The attribute is read-only
on the same identified Recovery connection.

New source builds have only CRC-only Recovery1.0.1; the publisher verifier,
public key, ECC/SHA2 SDK dependency and policy switches are removed. Legacy
optional/required meanings remain unchanged on already-installed Recovery:
optional still verifies any present publisher signature; required still rejects
unsigned images. App OTA cannot replace Recovery.

## Image formats

The little-endian native header contains payload size at4, CRC32 at8 and
four-component ASCII version at12 (16-byte field). Its24-byte preflight record
at40 is six uint32 values: `IKP1` (`0x31504b49`), raw App size, epoch, board
`DAXB` (`0x42584144`), layout `DAV1` (`0x31564144`) and verifier identity.
Payload is raw App followed by a96-byte trailer. Its32-byte prefix is
`<IBBHIIIIII`: magic, schema1, algorithm, trailer size96, raw size, epoch,
board, layout, verifier and release version (`major*10000+minor*100+patch`).
Header and trailer contexts must match.

| Format | Magic | Algorithm | Verifier | Final64 bytes |
| --- | --- | --- | --- | --- |
| Publisher signed | IKS1 /0x31534b49 |1 | DAK1 /0x314b4144 | P-256 R/S,32B each, big endian, low-S |
| Explicitly unsigned | IKU1 /0x31554b49 |0 | DAU1 /0x31554144 | SHA256(raw+prefix),32B; then32 zero bytes |

New CRC-only Recovery checks native stored length/CRC, duplicate format/target/
epoch fields and safe metadata transactions, without publisher authentication.
IKS1 signature bytes are opaque in this role. IKU1 retains its existing trailer
and zero reserved bytes; the host packager still creates its SHA-256 data digest
using the standard library. No extra IKCI is added because DA already has a
complete native pQ payload length/CRC record, used by cold boot and resume.

`build-support/build/package_ota.py` creates the unsigned format using Python's standard
library. It does not use keys, the SDK, BLE or a programmer. Raw maximum95648B,
payload maximum95744B and complete OTA maximum95808B apply to both formats.
