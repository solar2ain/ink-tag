/** Read-only Nordic dfu-cc.proto metadata. This does not verify a signature or admit an update. */
type Field = { wire: number; value: number | Uint8Array }
function fields(bytes: Uint8Array): Map<number, Field[]> {
  let at = 0
  const result = new Map<number, Field[]>()
  const uint = () => {
    let value = 0, factor = 1
    for (let i = 0; i < 5; i++, factor *= 128) {
      if (at >= bytes.length) throw Error('truncated varint')
      const next = bytes[at++]
      value += (next & 127) * factor
      if (value > 0xffffffff) throw Error('uint32 overflow')
      if (!(next & 128)) return value
    }
    throw Error('invalid varint')
  }
  while (at < bytes.length) {
    const tag = uint(), number = Math.floor(tag / 8), wire = tag & 7
    if (!number) throw Error('invalid field')
    let value: number | Uint8Array
    if (wire === 0) value = uint()
    else if (wire === 2 || wire === 1 || wire === 5) {
      const size = wire === 2 ? uint() : wire === 1 ? 8 : 4
      if (size > bytes.length - at) throw Error('truncated field')
      value = bytes.subarray(at, at + size); at += size
    } else throw Error('unsupported wire type')
    result.set(number, [...(result.get(number) ?? []), { wire, value }])
  }
  return result
}
function one(source: Map<number, Field[]>, number: number, wire: number) {
  const entries = source.get(number)
  if (entries?.length !== 1 || entries[0].wire !== wire) throw Error('missing or ambiguous metadata')
  return entries[0].value
}
function message(source: Map<number, Field[]>, number: number) { return fields(one(source, number, 2) as Uint8Array) }

export function readNordicPackageVersion(initPacket: unknown, applicationBytes: number | undefined) {
  if (!(initPacket instanceof Uint8Array) || initPacket.length > 4096 || !initPacket.length) return undefined
  try {
    const packet = fields(initPacket)
    if (packet.size !== 1 || (!packet.has(1) && !packet.has(2))) return undefined
    let command: Map<number, Field[]>
    if (packet.has(2)) {
      const signed = message(packet, 2)
      if (one(signed, 2, 0) !== 0 || (one(signed, 3, 2) as Uint8Array).length !== 64) return undefined
      command = message(signed, 1)
    } else command = message(packet, 1) // Display metadata only; live Recovery still decides admission.
    if (one(command, 1, 0) !== 1) return undefined
    const init = message(command, 2)
    if (init.has(4) && one(init, 4, 0) !== 0) return undefined
    if (init.has(7) && (applicationBytes === undefined || one(init, 7, 0) !== applicationBytes)) return undefined
    const encodedVersion = one(init, 1, 0) as number
    return { encodedVersion, compatibilityEpoch: Math.floor(encodedVersion / 65536), releaseNumber: encodedVersion % 65536 }
  } catch { return undefined } // optional display metadata never changes transfer/admission
}
