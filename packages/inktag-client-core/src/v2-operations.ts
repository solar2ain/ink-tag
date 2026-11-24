import {
  CommandV2,
  DisplayModeV2,
  TransferObjectV2,
} from './generated/commands-v2'
import { imageCrc32, type EncodedImage, type ImageStep } from './image'
import { encodeScreenProfileV2, type ScreenProfileLikeV2 } from './screen'

export type V2RequestOptions = {
  transactionId?: number
  timeoutMs?: number
  quiet?: boolean
}

/** Platform-neutral command port. Web Bluetooth and wx BLE each adapt to this interface. */
export interface V2OperationPort {
  allocateTransaction(): number
  request(opcode: number, payload?: Uint8Array, options?: V2RequestOptions): Promise<Uint8Array>
  waitForCompletion(transactionId: number, opcode: number, timeoutMs?: number): Promise<Uint8Array>
  cancelCompletion(transactionId: number, reason: unknown): void
}

export type ImageTransferPhase = 'prepare' | 'transfer' | 'refresh' | 'stored'

export interface V2ImageTransferOptions {
  screen: ScreenProfileLikeV2
  writePayload: number
  store?: boolean
  displayAfterStore?: boolean
  name?: string
  contentType?: number
  /** V2 atomic replacement; never follow commit with a separate delete. */
  replaceSlotId?: number
  onPhase?: (phase: ImageTransferPhase) => void
  onStep?: (step: ImageStep, bytes: number) => void
  onProgress?: (progress: number) => void
  assertActive?: () => void
  yieldControl?: () => Promise<void>
}

export interface DeviceResourceV2 {
  id: number
  contentType: number
  format: number
  segments: number
  compatible: boolean
  name: string
  width: number
  height: number
  bytes: number
  createdAt: number
}

export interface ResourceInfoV2 {
  width: number
  height: number
  bytesPerImage: number
  requiredSectors: number
  totalSectors: number
  freeSectors: number
  largestFreeRun: number
  allocatable: number
  saved: number
  compatible: number
  playable: number
  incompatible: number
  slideshow: boolean
  intervalSeconds: number
}

function encodeUtf8(value: string): Uint8Array {
  const result: number[] = []
  for (const character of value) {
    const codePoint = character.codePointAt(0)!
    if (codePoint <= 0x7f) result.push(codePoint)
    else if (codePoint <= 0x7ff) result.push(0xc0 | (codePoint >> 6), 0x80 | (codePoint & 0x3f))
    else if (codePoint <= 0xffff) {
      result.push(0xe0 | (codePoint >> 12), 0x80 | ((codePoint >> 6) & 0x3f), 0x80 | (codePoint & 0x3f))
    } else {
      result.push(
        0xf0 | (codePoint >> 18),
        0x80 | ((codePoint >> 12) & 0x3f),
        0x80 | ((codePoint >> 6) & 0x3f),
        0x80 | (codePoint & 0x3f),
      )
    }
  }
  return Uint8Array.from(result)
}

function decodeUtf8(bytes: Uint8Array): string {
  let result = ''
  for (let index = 0; index < bytes.length;) {
    const first = bytes[index++]
    let codePoint = first
    let remaining = 0
    if ((first & 0xe0) === 0xc0) { codePoint = first & 0x1f; remaining = 1 }
    else if ((first & 0xf0) === 0xe0) { codePoint = first & 0x0f; remaining = 2 }
    else if ((first & 0xf8) === 0xf0) { codePoint = first & 0x07; remaining = 3 }
    else if (first >= 0x80) { result += '\ufffd'; continue }
    if (index + remaining > bytes.length) { result += '\ufffd'; break }
    let valid = true
    for (let part = 0; part < remaining; part += 1) {
      const continuation = bytes[index++]
      if ((continuation & 0xc0) !== 0x80) { valid = false; break }
      codePoint = (codePoint << 6) | (continuation & 0x3f)
    }
    if (!valid || codePoint > 0x10ffff || (codePoint >= 0xd800 && codePoint <= 0xdfff)) result += '\ufffd'
    else result += String.fromCodePoint(codePoint)
  }
  return result
}

export function sanitizeResourceName(value: string, fallback = 'image'): string {
  return (value || fallback).replace(/[^a-zA-Z0-9_-]/g, '_').slice(0, 19)
}

export function encodeLocalTimeV2(date = new Date(), weekStart = 1): Uint8Array {
  if (!Number.isInteger(weekStart) || weekStart < 0 || weekStart > 6) throw new RangeError('weekStart must be 0..6')
  const payload = new Uint8Array(7)
  const view = new DataView(payload.buffer)
  view.setUint32(0, Math.floor(date.getTime() / 1000), true)
  view.setInt16(4, -date.getTimezoneOffset(), true)
  payload[6] = weekStart
  return payload
}

export function encodeDisplayModeV2(mode: number, extra: Uint8Array = new Uint8Array(0)): Uint8Array {
  const payload = new Uint8Array(1 + extra.length)
  payload[0] = mode
  payload.set(extra, 1)
  return payload
}

export async function runV2LongCommand(
  port: V2OperationPort,
  opcode: number,
  payload: Uint8Array = new Uint8Array(0),
  requestTimeoutMs = 10000,
  completionTimeoutMs = 90000,
): Promise<Uint8Array> {
  const transactionId = port.allocateTransaction()
  const completion = port.waitForCompletion(transactionId, opcode, completionTimeoutMs)
  try {
    await port.request(opcode, payload, { transactionId, timeoutMs: requestTimeoutMs })
    return await completion
  } catch (error) {
    port.cancelCompletion(transactionId, error)
    void completion.catch(() => undefined)
    throw error
  }
}

export function runV2DisplayWork(
  port: V2OperationPort,
  mode: number,
  extra: Uint8Array = new Uint8Array(0),
): Promise<Uint8Array> {
  return runV2LongCommand(port, CommandV2.SET_DISPLAY_MODE, encodeDisplayModeV2(mode, extra))
}

export function encodeImageTransferBeginV2(
  encoded: EncodedImage,
  options: Pick<V2ImageTransferOptions, 'screen' | 'store' | 'name' | 'contentType' | 'replaceSlotId'>,
): Uint8Array {
  const store = options.store === true
  const name = encodeUtf8(sanitizeResourceName(options.name || '', 'image'))
  if (options.replaceSlotId !== undefined && (!store || !Number.isInteger(options.replaceSlotId) || options.replaceSlotId < 0 || options.replaceSlotId >= 255)) throw new Error('Invalid replacement slot')
  const begin = new Uint8Array(store ? 39 + name.length + (options.replaceSlotId === undefined ? 0 : 1) : 20)
  const view = new DataView(begin.buffer)
  begin[0] = store ? TransferObjectV2.STORED_IMAGE : TransferObjectV2.DIRECT_FRAME
  begin[1] = encoded.format
  begin[2] = encoded.transfers.length
  begin[3] = encoded.transfers.reduce((mask, item, index) =>
    mask | ((item.step === 'bw' || item.step === 'gray-msb' || item.step === 'e6-area0') ? 1 << index : 0), 0)
  view.setUint32(4, encoded.transfers.reduce((sum, item) => sum + item.data.length, 0), true)
  view.setUint32(8, imageCrc32(encoded.transfers), true)
  view.setUint32(12, encoded.transfers[0]?.data.length || 0, true)
  view.setUint32(16, encoded.transfers[1]?.data.length || 0, true)
  if (store) {
    begin[20] = options.contentType || 0
    begin.set(encodeScreenProfileV2(options.screen), 21)
    begin[38] = name.length
    begin.set(name, 39)
    if (options.replaceSlotId !== undefined) begin[39 + name.length] = options.replaceSlotId
  }
  return begin
}

export function encodeTransferChunkV2(offset: number, chunk: Uint8Array): Uint8Array {
  const payload = new Uint8Array(4 + chunk.length)
  new DataView(payload.buffer).setUint32(0, offset, true)
  payload.set(chunk, 4)
  return payload
}

export async function transferEncodedImageV2(
  port: V2OperationPort,
  encoded: EncodedImage,
  options: V2ImageTransferOptions,
): Promise<number | null> {
  const store = options.store === true
  const totalSize = encoded.transfers.reduce((sum, item) => sum + item.data.length, 0)
  const transactionId = port.allocateTransaction()
  let transferActive = false
  let completion: Promise<Uint8Array> | null = null
  options.onPhase?.('prepare')
  try {
    const begin = encodeImageTransferBeginV2(encoded, options)
    const beginReply = await port.request(CommandV2.TRANSFER_BEGIN, begin, { transactionId, timeoutMs: 15000 })
    transferActive = true
    const deviceChunk = beginReply.length >= 4
      ? new DataView(beginReply.buffer, beginReply.byteOffset, beginReply.byteLength).getUint16(2, true)
      : options.writePayload
    const chunkSize = Math.max(20, Math.min(options.writePayload, deviceChunk))
    let offset = 0
    options.onPhase?.('transfer')
    for (const transfer of encoded.transfers) {
      options.onStep?.(transfer.step, transfer.data.length)
      for (let localOffset = 0; localOffset < transfer.data.length; localOffset += chunkSize) {
        options.assertActive?.()
        const chunk = transfer.data.subarray(localOffset, Math.min(localOffset + chunkSize, transfer.data.length))
        await port.request(CommandV2.TRANSFER_WRITE, encodeTransferChunkV2(offset, chunk), {
          transactionId,
          timeoutMs: 12000,
          quiet: true,
        })
        offset += chunk.length
        options.onProgress?.(totalSize ? offset / totalSize : 1)
        if ((offset & 0x0fff) < chunk.length) await options.yieldControl?.()
      }
    }
    completion = store ? null : port.waitForCompletion(transactionId, CommandV2.TRANSFER_COMMIT, 90000)
    const commit = await port.request(CommandV2.TRANSFER_COMMIT, new Uint8Array(0), { transactionId, timeoutMs: 15000 })
    transferActive = false
    if (completion) {
      options.onPhase?.('refresh')
      await completion
      return null
    }
    const resourceId = commit[0]
    if (resourceId == null || resourceId === 0xff) throw new Error('设备未返回新图槽编号')
    options.onPhase?.('stored')
    if (options.displayAfterStore !== false) {
      options.onPhase?.('refresh')
      await runV2DisplayWork(port, DisplayModeV2.RESOURCE, Uint8Array.of(resourceId))
    }
    return resourceId
  } catch (error) {
    if (transferActive) {
      await port.request(CommandV2.TRANSFER_ABORT, new Uint8Array(0), {
        transactionId,
        timeoutMs: 5000,
        quiet: true,
      }).catch(() => undefined)
    }
    port.cancelCompletion(transactionId, error)
    if (completion) void completion.catch(() => undefined)
    throw error
  }
}

export function decodeResourceListItemV2(payload: Uint8Array): { done: true } | { done: false; item: DeviceResourceV2 } {
  if (payload.length >= 2 && payload[0] === 1) return { done: true }
  if (payload.length < 19 || payload[0] !== 0 || payload.length !== 19 + payload[6]) {
    throw new Error('设备图槽列表响应无效')
  }
  const view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength)
  return { done: false, item: {
    id: payload[1], contentType: payload[2], format: payload[3], segments: payload[4],
    compatible: payload[5] !== 0, name: decodeUtf8(payload.subarray(19)),
    width: view.getUint16(7, true), height: view.getUint16(9, true),
    bytes: view.getUint32(11, true), createdAt: view.getUint32(15, true),
  } }
}

export async function listResourcesV2(request: (opcode: number, payload?: Uint8Array) => Promise<Uint8Array>): Promise<DeviceResourceV2[]> {
  const resources: DeviceResourceV2[] = []
  let cursor = 0xff
  for (let count = 0; count < 256; count++) {
    const decoded = decodeResourceListItemV2(await request(CommandV2.LIST_RESOURCES, Uint8Array.of(cursor)))
    if (decoded.done) return resources
    resources.push(decoded.item)
    cursor = decoded.item.id
  }
  throw new Error('设备图槽列表超过协议上限')
}

export function decodeResourceInfoV2(payload: Uint8Array): ResourceInfoV2 {
  if (payload.length < 30 || payload[0] !== 1) throw new Error('设备图槽信息响应无效')
  const view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength)
  const requiredSectors = view.getUint16(9, true)
  const largestFreeRun = view.getUint16(15, true)
  return {
    width: view.getUint16(1, true), height: view.getUint16(3, true), bytesPerImage: view.getUint32(5, true),
    requiredSectors, totalSectors: view.getUint16(11, true), freeSectors: view.getUint16(13, true), largestFreeRun,
    allocatable: payload.length >= 32 ? view.getUint16(30, true) : requiredSectors ? Math.floor(largestFreeRun / requiredSectors) : 0,
    saved: view.getUint16(17, true), compatible: view.getUint16(19, true), playable: view.getUint16(21, true),
    incompatible: view.getUint16(23, true), slideshow: payload[25] !== 0, intervalSeconds: view.getUint32(26, true),
  }
}
