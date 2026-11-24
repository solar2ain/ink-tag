import { NfcCommandV2 } from './generatedCommandsV2'

export const NFC_COMMAND_FORMAT_VERSION = 1
export const NFC_COMMAND_MAX_PAYLOAD_BYTES = 8
export const NFC_COMMAND_REUSABLE_SEQUENCE = 0

export interface NfcCommandRecord {
  sequence: number
  opcode: number
  payload: Uint8Array
}

export interface NfcCapabilities {
  flags: number
  commandMask: number
  maxNdefBytes: number
  maxCommandPayload: number
  maxUidBytes: number
  commandFormatVersion: number
}

export interface NfcStatus {
  flags: number
  lastResult: number
  lastOpcode: number
  lastSequence: number
  uid: Uint8Array
  diagnosticFlags: number
}

export const NFC_NDEF_MAX_BYTES = 192

const noPayloadCommands = new Set<number>([
  NfcCommandV2.PING,
  NfcCommandV2.DEVICE_INFO,
  NfcCommandV2.CALENDAR,
  NfcCommandV2.CLOCK,
  NfcCommandV2.NEXT_IMAGE,
  NfcCommandV2.PREVIOUS_IMAGE,
  NfcCommandV2.PAUSE,
  NfcCommandV2.OPEN_BLE_TRANSFER,
  NfcCommandV2.SLIDESHOW,
  NfcCommandV2.CLEAR,
])

function payloadValid(opcode: number, length: number) {
  if (noPayloadCommands.has(opcode)) return length === 0
  return (opcode === NfcCommandV2.SHOW_SLOT && length === 1) ||
    (opcode === NfcCommandV2.SET_TIME && length === 4)
}

export function nfcCommandCrc32(data: Uint8Array) {
  let crc = 0xffffffff
  for (const byte of data) {
    crc ^= byte
    for (let bit = 0; bit < 8; bit += 1) {
      crc = (crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0)
    }
  }
  return (crc ^ 0xffffffff) >>> 0
}

export function nfcCommandSequenceIsNewer(candidate: number, previous: number) {
  const next = candidate >>> 0
  const last = previous >>> 0
  return next !== 0 && (last === 0 || ((next - last) | 0) > 0)
}

export function encodeNfcCommandText(record: NfcCommandRecord) {
  const sequence = record.sequence >>> 0
  if (record.payload.byteLength > NFC_COMMAND_MAX_PAYLOAD_BYTES ||
      !payloadValid(record.opcode, record.payload.byteLength)) {
    throw new RangeError('NFC 命令的序号、操作或负载无效')
  }
  const hex = (value: number, digits: number) => value.toString(16).toUpperCase().padStart(digits, '0')
  const payload = Array.from(record.payload, (byte) => hex(byte, 2)).join('')
  const prefix = `IKNC1:${hex(sequence, 8)}:${hex(record.opcode, 2)}:${payload}:`
  const crc = nfcCommandCrc32(new TextEncoder().encode(prefix))
  return `${prefix}${hex(crc, 8)}`
}

export function decodeNfcCommandText(text: string): NfcCommandRecord {
  if (text.length < 27 || text.length > 43) throw new Error('NFC 命令长度无效')
  const match = /^IKNC(\d):([0-9A-Fa-f]{8}):([0-9A-Fa-f]{2}):([0-9A-Fa-f]*):([0-9A-Fa-f]{8})$/.exec(text)
  if (!match) throw new Error('NFC 命令格式无效')
  if (match[1] !== String(NFC_COMMAND_FORMAT_VERSION)) throw new Error('NFC 命令版本不受支持')
  if ((match[4].length & 1) !== 0 || match[4].length / 2 > NFC_COMMAND_MAX_PAYLOAD_BYTES) {
    throw new Error('NFC 命令负载过长')
  }
  const checksumPrefix = text.slice(0, -8)
  const expected = Number.parseInt(match[5], 16) >>> 0
  if (nfcCommandCrc32(new TextEncoder().encode(checksumPrefix)) !== expected) {
    throw new Error('NFC 命令 CRC 校验失败')
  }
  const sequence = Number.parseInt(match[2], 16) >>> 0
  const opcode = Number.parseInt(match[3], 16)
  const payload = new Uint8Array(match[4].length / 2)
  for (let i = 0; i < payload.length; i += 1) payload[i] = Number.parseInt(match[4].slice(i * 2, i * 2 + 2), 16)
  if (!payloadValid(opcode, payload.length)) throw new Error('NFC 命令操作或负载无效')
  return { sequence, opcode, payload }
}

export function decodeNfcCapabilities(payload: Uint8Array): NfcCapabilities {
  if (payload.length !== 10 || payload[0] !== 1) throw new Error('NFC 能力响应无效')
  const view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength)
  const capabilities = {
    flags: view.getUint16(1, true),
    commandMask: view.getUint16(3, true),
    maxNdefBytes: view.getUint16(5, true),
    maxCommandPayload: payload[7],
    maxUidBytes: payload[8],
    commandFormatVersion: payload[9],
  }
  if (capabilities.maxNdefBytes > NFC_NDEF_MAX_BYTES ||
      capabilities.maxCommandPayload > NFC_COMMAND_MAX_PAYLOAD_BYTES ||
      capabilities.maxUidBytes > 10 || capabilities.commandFormatVersion !== 1) {
    throw new Error('NFC 能力超出 0.2.0 边界')
  }
  return capabilities
}

export function decodeNfcStatus(payload: Uint8Array): NfcStatus {
  if (payload.length !== 20 || payload[0] !== 1 || payload[8] > 10) {
    throw new Error('NFC 状态响应无效')
  }
  const view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength)
  return {
    flags: payload[1],
    lastResult: payload[2],
    lastOpcode: payload[3],
    lastSequence: view.getUint32(4, true),
    uid: payload.slice(9, 9 + payload[8]),
    diagnosticFlags: payload[19],
  }
}

export function encodeNfcNdef(ndef: Uint8Array) {
  if (ndef.byteLength > NFC_NDEF_MAX_BYTES) throw new RangeError('NDEF 数据不能超过 192 字节')
  const payload = new Uint8Array(3 + ndef.byteLength)
  payload[0] = 1
  new DataView(payload.buffer).setUint16(1, ndef.byteLength, true)
  payload.set(ndef, 3)
  return payload
}

export function decodeNfcNdef(payload: Uint8Array) {
  if (payload.length < 3 || payload[0] !== 1) throw new Error('NDEF 响应无效')
  const length = new DataView(payload.buffer, payload.byteOffset, payload.byteLength).getUint16(1, true)
  if (length > NFC_NDEF_MAX_BYTES || payload.length !== 3 + length) throw new Error('NDEF 响应长度无效')
  return payload.slice(3)
}

export function encodeNfcUriRecord(url: string) {
  const normalized = url.trim()
  const prefix = 'https://'
  if (!normalized.toLocaleLowerCase().startsWith(prefix)) {
    throw new RangeError('NFC 上位机地址必须使用 HTTPS')
  }
  const suffix = new TextEncoder().encode(normalized.slice(prefix.length))
  const payloadLength = 1 + suffix.byteLength
  if (payloadLength > 0xff) throw new RangeError('NFC 上位机地址过长')
  const record = new Uint8Array(5 + suffix.byteLength)
  record.set([0xd1, 0x01, payloadLength, 0x55, 0x04])
  record.set(suffix, 5)
  if (record.byteLength > NFC_NDEF_MAX_BYTES) throw new RangeError('NDEF 数据不能超过 192 字节')
  return record
}
