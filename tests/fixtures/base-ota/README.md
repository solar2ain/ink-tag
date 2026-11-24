# Synthetic OTA protocol fixtures

`onmicro-crc-only.zip` is a synthetic native-container test vector: its body is
`bytes(range(256)) * 2`, followed by an IKCI descriptor. It is not executable
firmware, is never an update feed or device-test input, and contains no signing
key. It exercises exact format, contexts, integrity and live-policy admission.

`telink-crc-only.zip` uses a synthetic 512-byte body with the native TLNK length,
entry binding and stored big-endian CRC. It has the same offline-only purpose;
it is not executable firmware or a device-test package.

`goodix-crc-only.zip` similarly contains a synthetic GATT-version byte sequence,
test body, IKCI and 48-byte SDK image-info. It exercises Free package loading
and live Recovery admission.
