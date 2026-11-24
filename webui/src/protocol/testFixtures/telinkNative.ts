/** Radio-free transport data, not a publisher-signed image. */
export function telinkNativeTransportFixture() {
  const declaredSize = 0x160
  const firmware = new Uint8Array(declaredSize + 4 + 96)
  const image = new DataView(firmware.buffer)
  image.setUint32(24, declaredSize, true)
  const prefix = declaredSize + 4
  image.setUint32(prefix + 12, 1, true)
  image.setUint32(prefix + 16, 0x53484c54, true)
  image.setUint32(prefix + 20, 0x31564c54, true)
  const entry = new Uint8Array(20)
  entry.set([0x49, 0x41, 0x42, 0x31]) // IAB1, actual compiled entry contract
  const binding = new DataView(entry.buffer)
  ;[0x1000, 0x3f000, 0x40000, 0x11000].forEach((value, i) => binding.setUint32(4 + i * 4, value, true))
  firmware.set(entry, 0x140)
  const compatibility = new Uint8Array(20)
  compatibility.set([0x49, 0x54, 0x43, 0x31])
  const context = new DataView(compatibility.buffer)
  ;[0x3f000, 1, 0x31564c54, 0x53484c54].forEach((value, i) => context.setUint32(4 + i * 4, value, true))
  return { firmware, declaredSize, entry, compatibility }
}
