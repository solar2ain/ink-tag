import { type ImageGeometry } from './image'

export type NfcExchange = (apdu: Uint8Array) => Promise<Uint8Array>
export const INKTAG_NFC_D1 = Uint8Array.of(0x00, 0xd1, 0, 0, 0)

export function nfcApduData(reply: Uint8Array) {
  if (reply.length < 2 || reply[reply.length - 2] !== 0x90 || reply[reply.length - 1] !== 0) {
    const status = Array.from(reply.slice(-2), byte => byte.toString(16).padStart(2, '0')).join('').toUpperCase()
    throw new Error(status ? `NFC 设备拒绝请求（${status}）` : 'NFC 回包为空')
  }
  return reply.subarray(0, reply.length - 2)
}

export function parseInkTagNfcD1(reply: Uint8Array, uid: Uint8Array) {
  const data = nfcApduData(reply)
  const fields = new Map<number, Uint8Array>()
  for (let offset = 0; offset < data.length;) {
    const tag = data[offset++], size = data[offset++]
    if (size === undefined || offset + size > data.length || fields.has(tag)) throw new Error('NFC D1 描述无效')
    fields.set(tag, data.subarray(offset, offset + size))
    offset += size
  }
  const app = fields.get(0xc0), tagUid = fields.get(0xc1)
  if (app?.length !== 4 || !app.every((byte, index) => byte === 'IKT1'.charCodeAt(index)) ||
      !uid.length || tagUid?.length !== uid.length || !tagUid.every((byte, index) => byte === uid[index])) {
    throw new Error('贴近的标签未通过 InkTag 身份核对')
  }
  return fields
}

export interface NfcImageTarget extends ImageGeometry {
  packing: number
  planes: number
  planeBytes: number
}

export function decodeNfcImageTarget(fields: ReadonlyMap<number, Uint8Array>): NfcImageTarget {
  const metadata = fields.get(0xe0)
  if (metadata?.length !== 15) throw new Error('设备未提供 NFC 图传屏幕参数')
  const view = new DataView(metadata.buffer, metadata.byteOffset, metadata.byteLength)
  const [width, height, visibleX, visibleY, visibleWidth, visibleHeight] = Array.from({ length: 6 }, (_, index) => view.getUint16(index * 2))
  const [color, packing, transform] = metadata.subarray(12)
  if (width < 8 || width > 2048 || height < 8 || height > 2048 ||
      visibleWidth < 8 || visibleHeight < 8 || visibleX + visibleWidth > width || visibleY + visibleHeight > height ||
      transform > 15 || ![1, 2, 3, 4, 5, 6, 7, 8, 10].includes(color) ||
      packing < 1 || packing > 6 || (packing === 6 && color !== 10)) {
    throw new Error('设备返回的 NFC 图传屏幕参数无效')
  }
  const pixelsPerByte = [0, 8, 8, 4, 8, 2, 4][packing]
  return { width, height, visibleX, visibleY, visibleWidth, visibleHeight, color, packing, transform,
    planes: [2, 4, 6].includes(packing) ? 2 : 1, planeBytes: Math.ceil(width / pixelsPerByte) * height }
}

export function encodeNfcTextRecord(text: string) {
  // WeChat does not consistently provide the browser TextEncoder.
  const escaped = encodeURIComponent(text)
  const bytes = Uint8Array.from(escaped.match(/%[0-9A-F]{2}|./g) ?? [],
    value => value.length === 1 ? value.charCodeAt(0) : Number.parseInt(value.slice(1), 16))
  if (!bytes.length || bytes.length > 54 || /[\u0000-\u001f]/.test(text)) {
    throw new Error('请输入 1–54 字节的 NFC 命令文本')
  }
  return Uint8Array.of(0xd1, 1, bytes.length + 3, 0x54, 2, 0x65, 0x6e, ...bytes)
}

export function nfcPhoneCommand(name: string, time: Date, withTimezone: boolean) {
  if (name !== 'time' && name !== 'calendar' && name !== 'clock') return name
  const offset = -time.getTimezoneOffset()
  return `${name}#${Math.floor(time.getTime() / 1000)}${withTimezone ? `,${offset > 0 ? '+' : ''}${offset}` : ''}`
}

/** Match the InkTag tool's Type 4 transient NDEF transaction. Publish NLEN last. */
export async function writeInkTagNfcType4Command(exchange: NfcExchange, record: Uint8Array) {
  const apdu = async (...bytes: number[]) => nfcApduData(await exchange(Uint8Array.from(bytes)))
  await apdu(0, 0xa4, 4, 0, 7, 0xd2, 0x76, 0, 0, 0x85, 1, 1, 0)
  await apdu(0, 0xa4, 0, 0x0c, 2, 0xe1, 3)
  const header = await apdu(0, 0xb0, 0, 0, 7)
  if (header.length !== 7) throw new Error('设备的 NFC 能力描述不完整')
  const length = header[0] * 256 + header[1]
  const maxRead = Math.min(18, header[3] * 256 + header[4])
  const maxWrite = Math.min(24, header[5] * 256 + header[6])
  if (length < 15 || length > 256 || header[2] >>> 4 !== 2 || !maxRead || maxWrite < 2) {
    throw new Error('设备的 Type 4 NFC 命令入口不兼容')
  }
  const cc = new Uint8Array(length)
  cc.set(header)
  for (let offset = header.length; offset < length;) {
    const count = Math.min(maxRead, length - offset)
    const part = await apdu(0, 0xb0, offset >> 8, offset & 0xff, count)
    if (part.length !== count) throw new Error('设备的 NFC 能力描述不完整')
    cc.set(part, offset)
    offset += part.length
  }
  for (let offset = header.length; offset < length;) {
    const tag = cc[offset++]
    if (tag === 0) continue
    if (tag === 0xfe) break
    const size = cc[offset++]
    if (size === undefined || offset + size > length) break
    if (tag === 4 && size === 6) {
      const [fileHigh, fileLow, capacityHigh, capacityLow, readAccess, writeAccess] = cc.subarray(offset, offset + size)
      if (readAccess !== 0 || writeAccess !== 0 || capacityHigh * 256 + capacityLow < record.length + 2) {
        throw new Error('设备的 NFC 命令入口不可写或容量不足')
      }
      await apdu(0, 0xa4, 0, 0x0c, 2, fileHigh, fileLow)
      await apdu(0, 0xd6, 0, 0, 2, 0, 0)
      for (let index = 0; index < record.length; index += maxWrite) {
        const part = record.subarray(index, index + maxWrite)
        await apdu(0, 0xd6, 0, index + 2, part.length, ...part)
      }
      try {
        await apdu(0, 0xd6, 0, 0, 2, 0, record.length)
      } catch (error) {
        throw new Error(`未确认命令是否已写入，请查看设备状态。${(error as Error).message}`)
      }
      return
    }
    offset += size
  }
  throw new Error('未发现可写的 InkTag NFC 命令入口')
}
