import { crc32Bytes } from '../../../packages/inktag-client-core/src/image'
import { INKTAG_NFC_D1, nfcApduData, parseInkTagNfcD1, type NfcExchange } from '../../../packages/inktag-client-core/src/nfc'

const gtVersion = Uint8Array.of(0, 4, 4, 5, 2, 1, 0x15, 3)
const gtMagic = Uint8Array.of(0x49, 0x4b, 0x47, 0x53, 1) // IKGS v1
const dataKind = 1, pullKind = 2, echoKind = 3, replyKind = 0x81, ackKind = 0x82
const streamKind = 5, compactKind = 6
const groupDataKind = 7, groupConfirmKind = 8
const payloadSize = 40
const compactSize = 52
export const TYPE2_IMAGE_GROUP_PACKETS = 16
const empty = new Uint8Array(0)

type GtFrame = { kind: number; sequence: number; nonce: number; offset: number; total: number; data: Uint8Array }
type RawExchange = (command: Uint8Array, passiveAck?: boolean, retryRead?: boolean, waitMs?: number) => Promise<Uint8Array>

const equals = (a: Uint8Array, b: Uint8Array) => a.length === b.length && a.every((byte, i) => byte === b[i])
const delay = (ms: number) => new Promise<void>(resolve => setTimeout(resolve, ms))
const frameCrc = (raw: Uint8Array) => (crc32Bytes(raw.subarray(0, 60)) ^ 0xffffffff) >>> 0

function compactCrc(raw: Uint8Array, nonce: number) {
  const prefix = new Uint8Array(4)
  new DataView(prefix.buffer).setUint32(0, nonce, true)
  return (crc32Bytes(raw.subarray(0, 60), crc32Bytes(prefix)) ^ 0xffffffff) >>> 0
}

function pack({ kind, sequence, nonce, offset, total, data }: GtFrame, compact: boolean) {
  const raw = new Uint8Array(64)
  const view = new DataView(raw.buffer)
  if (compact) {
    raw.set([kind, data.length])
    view.setUint16(2, sequence, true)
    view.setUint16(4, offset, true)
    view.setUint16(6, total, true)
    raw.set(data, 8)
    view.setUint32(60, compactCrc(raw, nonce), true)
    return raw
  }
  raw.set(gtMagic)
  view.setUint8(5, kind)
  view.setUint16(6, sequence, true)
  view.setUint32(8, nonce, true)
  view.setUint16(12, offset, true)
  view.setUint16(14, total, true)
  view.setUint8(16, data.length)
  raw.set(data, 20)
  view.setUint32(60, frameCrc(raw), true)
  return raw
}

function unpack(raw: Uint8Array, compactNonce?: number): GtFrame | undefined {
  if (raw.length !== 64) return
  const view = new DataView(raw.buffer, raw.byteOffset, raw.byteLength)
  if (compactNonce !== undefined) {
    const n = raw[1]
    if (n > compactSize || raw.subarray(8 + n, 60).some(Boolean) ||
        view.getUint32(60, true) !== compactCrc(raw, compactNonce)) return
    return {
      kind: raw[0], sequence: view.getUint16(2, true), nonce: compactNonce,
      offset: view.getUint16(4, true), total: view.getUint16(6, true), data: raw.subarray(8, 8 + n),
    }
  }
  const n = raw[16]
  if (!equals(raw.subarray(0, 5), gtMagic) || n > payloadSize ||
      raw.subarray(17, 20).some(Boolean) || raw.subarray(20 + n, 60).some(Boolean)) return
  if (view.getUint32(60, true) !== frameCrc(raw)) return
  return {
    kind: raw[5], sequence: view.getUint16(6, true), nonce: view.getUint32(8, true),
    offset: view.getUint16(12, true), total: view.getUint16(14, true), data: raw.subarray(20, 20 + n),
  }
}

function inkTagUri(raw: Uint8Array): string | undefined {
  const size = raw[1]
  if (raw[0] !== 3 || size < 6 || size > 61 || raw[size + 2] !== 0xfe || raw[2] !== 0xd1 ||
      raw[3] !== 1 || raw[4] + 4 !== size || raw[5] !== 0x55) return
  const prefix = ['', 'http://www.', 'https://www.', 'http://', 'https://'][raw[6]]
  if (prefix === undefined) return
  try {
    const link = new URL(prefix + new TextDecoder('utf-8', { fatal: true }).decode(raw.subarray(7, size + 2)))
    const query = new URLSearchParams(link.hash.includes('?') ? link.hash.split('?')[1] : link.search)
    const name = query.get('ble')
    if (/^https?:$/.test(link.protocol) && name?.startsWith('IKT_')) return name
  } catch { /* Unsupported URI record. */ }
}

const mode = (status: Uint8Array) => status[0] & 0x43
const mirrorReady = (status: Uint8Array) => mode(status) === 3 && status[2] === 1 && (status[6] & 0x59) === 1
const hexByte = (value: number) => value.toString(16).padStart(2, '0').toUpperCase()
const sessionState = (status: Uint8Array) => `NC=${hexByte(status[0])}，NS=${hexByte(status[6])}，镜像块=${hexByte(status[2])}`
function queryReply(raw: Uint8Array) {
  const reply = unpack(raw)
  return !!reply && reply.nonce !== 0 && reply.kind === replyKind &&
    (reply.total === 75 || reply.total === 79) && reply.offset % payloadSize === 0 &&
    reply.offset < reply.total && reply.data.length === Math.min(payloadSize, reply.total - reply.offset)
}

/** InkTag's qualified GT map. The only write targets are the two volatile SRAM windows. */
export class InkTagType2 {
  private sector = 0
  private mirrorHead?: Uint8Array
  private readonly nonce = 1 + Math.floor(Math.random() * 0xffffffff)
  private sequence = 0
  private lastRxReady = 0
  private previousSram?: Uint8Array
  private compact = false
  private streamed = false
  private grouped = false

  constructor(private readonly raw: RawExchange, private readonly uid: Uint8Array,
    private readonly reselectTag: () => Promise<void>) {}

  private async exchangeRf(command: Uint8Array, action: string, passiveAck = false, retryRead = false) {
    // Use the CLI's RF timings during the live image stream.
    try { return await this.raw(command, passiveAck, retryRead, this.streamed ? (passiveAck ? 1.5 : 100) : undefined) }
    catch (error) {
      const detail = error instanceof Error ? error.message : '读卡器 NFC 通信失败。'
      throw new Error(`${action}失败。${detail}`, { cause: error })
    }
  }

  private async selectSector(sector: number) {
    if (this.sector === sector) return
    const action = `切换到扇区 ${sector}`
    const ack = await this.exchangeRf(Uint8Array.of(0xc2, 0xff), action)
    if (ack.length !== 1 || ack[0] !== 0x0a) throw new Error('Type 2 NFC 不支持所需的扇区')
    // PN532 reports a passive ACK as a short RF timeout.
    const reply = await this.exchangeRf(Uint8Array.of(sector, 0, 0, 0), action, true)
    if (reply.length) throw new Error('Type 2 NFC 扇区选择失败')
    this.sector = sector
  }

  private async read(page: number) {
    for (let attempt = 0; ; attempt++) {
      const value = await this.exchangeRf(Uint8Array.of(0x30, page), `读取扇区 ${this.sector} 页 ${hexByte(page)}`, false, this.sector === 0)
      if (value.length === 16) return value
      if (this.sector !== 0 || value.length !== 1 || value[0] !== 3 || attempt === 3) {
        throw new Error(`Type 2 NFC 读取失败（扇区 ${this.sector}，页 ${hexByte(page)}），请保持贴近`)
      }
      await delay(10)
    }
  }

  private async session() {
    await this.selectSector(3)
    // Android's presence checks and the GT direction change can briefly drop
    // the field bit. Recheck only the status register, never an NFC write.
    let status = await this.read(0xf8)
    for (let attempt = 0; ; attempt++) {
      if (status[6] & 1) return status
      if (attempt === 2) break
      await delay(25)
      status = await this.read(0xf8)
    }
    throw new Error(`Type 2 NFC 标签连接已断开（${sessionState(status)}），请保持标签贴近读卡器。`)
  }

  private async waitFor(nc: number, ready: number, timeout = 4500) {
    const until = Date.now() + timeout
    let status: Uint8Array
    do {
      status = await this.session()
      if (nc === 3) {
        if (mode(status) !== 3 || status[2] !== 1) {
          throw new Error(`Type 2 NFC 易失镜像已关闭（${sessionState(status)}）。`)
        }
        if (mirrorReady(status)) return
      } else if (mode(status) === nc && (status[6] & 0x18) === ready) return
      await delay(12)
    } while (Date.now() < until)
    throw new Error(`等待 Type 2 NFC 通道就绪超时（期望 NC=${hexByte(nc)}，${sessionState(status)}）。`)
  }

  private async readWindow(firstPage: number) {
    const bytes = new Uint8Array(64)
    for (let i = 0; i < 64; i += 16) bytes.set(await this.read(firstPage + i / 4), i)
    return bytes
  }

  private async verifyIdentity() {
    await this.selectSector(0)
    const header = await this.read(0)
    if (!equals(header.subarray(0, 7), this.uid) || header[12] !== 0xe1 || header[13] >>> 4 !== 1 || header[14] < 8) {
      throw new Error('贴近的 Type 2 标签未通过 InkTag 身份核对')
    }
  }

  private async verifyVersion() {
    await this.selectSector(0)
    if (!equals(await this.raw(Uint8Array.of(0x60)), gtVersion)) {
      throw new Error('当前 Type 2 标签不提供受支持的 InkTag NFC 通道')
    }
  }

  async readName() {
    await this.verifyIdentity()
    return inkTagUri(await this.readWindow(4))
  }

  async open() {
    if (this.uid.length !== 7 || this.uid[0] !== 4) {
      throw new Error('当前 Type 2 标签不提供受支持的 InkTag NFC 通道')
    }
    const until = Date.now() + 4000
    do {
      const status = await this.session()
      if (mode(status) === 0x41) {
        await this.verifyIdentity()
        await this.verifyVersion()
        await this.waitFor(0x41, 0)
        return
      }
      if (mirrorReady(status)) {
        await this.verifyIdentity()
        const prefix = await this.readWindow(4)
        if (inkTagUri(prefix) || queryReply(prefix)) {
          await this.verifyVersion()
          this.mirrorHead = prefix.slice(0, 4)
          return
        }
      }
      await delay(80)
    } while (Date.now() < until)
    throw new Error('标签未提供已识别的 InkTag Type 2 易失通道')
  }

  async writeText(makeRecord: () => Uint8Array) {
    await this.open()
    if (!this.mirrorHead) {
      parseInkTagNfcD1(await this.exchange(INKTAG_NFC_D1), this.uid)
      await this.qualifyWrites()
      const result = nfcApduData(await this.exchange(makeRecord()))
      if (result.length) throw new Error('Type 2 命令应答格式无效')
      return
    }
    await this.waitFor(3, 0, 600)
    await this.verifyIdentity()
    const previous = await this.readWindow(4)
    if (!inkTagUri(previous)) throw new Error('标签没有可用的 InkTag NFC 设备链接')
    const record = makeRecord()
    const frame = new Uint8Array(Math.ceil((record.length + 3) / 4) * 4)
    frame.set([3, record.length])
    frame.set(record, 2)
    frame[record.length + 2] = 0xfe
    // Terminate parsing while staging, then publish the length last.
    await this.writeWindow(frame, previous.subarray(0, 4), Uint8Array.of(3, 0, 0xfe, 0))
  }

  private async writePage(page: number, data: Uint8Array, expected?: Uint8Array) {
    const command = Uint8Array.of(0xa2, page, ...data)
    const action = `写入扇区 ${this.sector} 页 ${hexByte(page)}`
    for (let attempt = 0; ; attempt++) {
      const reply = await this.exchangeRf(command, action)
      if (reply.length === 1 && reply[0] === 0x0a) return
      if (!expected || reply.length !== 1 || reply[0] !== 3 || attempt === 3) {
        throw new Error(`Type 2 NFC 页 ${hexByte(page)} 写入未确认，请查看设备状态`)
      }
      // A definite GT NAK can deselect the tag. Retry only if the same UID,
      // volatile map, and acknowledged bytes are still present.
      await this.reselectTag()
      this.sector = 0
      await this.waitFor(3, 0, 600)
      await this.verifyIdentity()
      for (let offset = 0; offset < expected.length; offset += 16) {
        const prefix = expected.subarray(offset, offset + 16)
        if (!equals((await this.read(4 + offset / 4)).subarray(0, prefix.length), prefix)) {
          throw new Error('Type 2 NFC 暂存数据已变化，写入已停止')
        }
      }
    }
  }

  private async writeWindow(raw: Uint8Array, previousHead: Uint8Array, marker: Uint8Array) {
    const staged = raw.slice()
    staged.set(marker)
    await this.writePage(4, marker, previousHead)
    for (let i = 4; i < raw.length; i += 4) await this.writePage(4 + i / 4, raw.subarray(i, i + 4), staged.subarray(0, i))
    await this.writePage(4, raw.subarray(0, 4), staged)
  }

  private async writeMirror(raw: Uint8Array, previousHead: Uint8Array) {
    await this.waitFor(3, 0, 600)
    await this.verifyIdentity()
    if (!equals((await this.read(4)).subarray(0, 4), previousHead)) throw new Error('Type 2 NFC 镜像内容已变化')
    await this.writeWindow(raw, previousHead, Uint8Array.of(0xfe, 0, 0, 0))
  }

  private nextFrame(kind: number, offset: number, total: number, data: Uint8Array) {
    if (this.sequence >= 65535) throw new Error('Type 2 NFC 会话已达到传输上限')
    const sequence = this.sequence++
    const raw = pack({ kind, sequence, nonce: this.nonce, offset, total, data }, this.compact)
    return { sequence, raw }
  }

  private async writeMailbox(raw: Uint8Array) {
    const guard = 120 - (Date.now() - this.lastRxReady)
    if (guard > 0) await delay(guard)
    // Check live ownership before each frame, including after a fragment
    // that stays in RX and does not publish a response.
    await this.waitFor(0x41, 0)
    if (!this.compact && !this.streamed) await this.verifyIdentity()
    await this.selectSector(1)
    for (let i = 0; i < 64; i += 4) {
      const page = raw.subarray(i, i + 4)
      if (i === 60 || !this.previousSram || !equals(page, this.previousSram.subarray(i, i + 4))) {
        await this.writePage(0xf0 + i / 4, page)
      }
    }
    this.previousSram = raw
  }

  private async roundTrip(kind: number, offset: number, total: number, data: Uint8Array = empty) {
    const { sequence, raw } = this.nextFrame(kind, offset, total, data)
    const matches = (frame?: GtFrame) => frame?.nonce === this.nonce && frame.sequence === sequence
    if (this.mirrorHead) {
      if (kind === dataKind && !equals(data, INKTAG_NFC_D1)) throw new Error('Type 2 图传通道尚未准备好')
      await this.writeMirror(raw, this.mirrorHead)
      const until = Date.now() + 4500
      do {
        await delay(80)
        const status = await this.session()
        if (mode(status) === 0x40) {
          this.mirrorHead = undefined // First ECHO may hand off to the direct mailbox.
          break
        }
        if (mode(status) !== 3 || status[2] !== 1) throw new Error('Type 2 NFC 易失镜像已关闭')
        if (kind === echoKind || !mirrorReady(status)) continue
        await this.selectSector(0)
        const bytes = await this.readWindow(4)
        const frame = unpack(bytes)
        if (frame && matches(frame) && frame.kind === replyKind && frame.offset === offset) {
          this.mirrorHead = bytes.slice(0, 4)
          return frame
        }
      } while (Date.now() < until)
      if (this.mirrorHead) throw new Error('Type 2 NFC 镜像响应超时，当前固件可能不支持识屏或传图')
    } else {
      await this.writeMailbox(raw)
      if (!this.streamed) await delay(20)
    }
    await this.waitFor(0x40, 8)
    await this.selectSector(1)
    // Consume with four short READs, matching the PN532 path. A failed final
    // page can mean the reply was consumed, so this window is never replayed.
    this.previousSram = await this.readWindow(0xf0)
    const frame = unpack(this.previousSram, this.compact ? this.nonce : undefined)
    if (!frame || !matches(frame)) throw new Error('Type 2 NFC 回包校验失败')
    if (!this.compact && !this.streamed) await delay(20)
    await this.waitFor(0x41, 0)
    this.lastRxReady = Date.now()
    return frame
  }

  readonly exchange: NfcExchange = async command => {
    const size = this.compact ? compactSize : payloadSize
    const kind = this.streamed && command[0] === 0xf0 && (command[1] === 0xd2 || command[1] === 0xd3) ? streamKind : dataKind
    let reply: GtFrame | undefined
    for (let at = 0; at < command.length; at += size) {
      const chunk = command.subarray(at, at + size)
      const end = at + chunk.length
      if (kind === streamKind && end < command.length) {
        await this.writeMailbox(this.nextFrame(kind, at, command.length, chunk).raw)
        continue
      }
      reply = await this.roundTrip(kind, at, command.length, chunk)
      if (end < command.length &&
          (reply.kind !== ackKind || reply.offset !== end || reply.total !== command.length || reply.data.length)) {
        throw new Error('Type 2 NFC 数据分片未确认')
      }
    }
    if (!reply || reply.kind !== replyKind || reply.offset || reply.total < 1 || reply.total > 80) {
      throw new Error('Type 2 NFC 回包格式错误')
    }
    const result = new Uint8Array(reply.total)
    for (let at = 0;;) {
      if (reply.kind !== replyKind || reply.offset !== at || reply.total !== result.length ||
          reply.data.length !== Math.min(size, result.length - at)) throw new Error('Type 2 NFC 回包分片格式错误')
      result.set(reply.data, at)
      at += reply.data.length
      if (at === result.length) return result
      reply = await this.roundTrip(pullKind, at, result.length)
    }
  }

  get supportsImageGroups() {
    return this.grouped
  }

  async exchangeImageGroup(commands: readonly Uint8Array[], onPacketSent?: (count: number) => void) {
    if (!this.grouped || commands.length < 1 || commands.length > TYPE2_IMAGE_GROUP_PACKETS ||
        commands.some(command => command.length < 5 || command.length !== command[4] + 5 ||
          command[0] !== 0xf0 || (command[1] !== 0xd2 && command[1] !== 0xd3))) {
      throw new Error('无效的 Type 2 NFC 图像数据组')
    }
    let digest = 2166136261
    for (const [index, command] of commands.entries()) {
      for (let offset = 0; offset < command.length; offset += compactSize) {
        const chunk = command.subarray(offset, offset + compactSize)
        await this.writeMailbox(this.nextFrame(groupDataKind, offset, command.length, chunk).raw)
      }
      for (const byte of command) digest = Math.imul(digest ^ byte, 16777619)
      onPacketSent?.(index + 1)
    }
    const expected = new Uint8Array(6)
    const view = new DataView(expected.buffer)
    view.setUint16(0, commands.length, true)
    view.setUint32(2, digest, true)
    const reply = await this.roundTrip(groupConfirmKind, 0, 0)
    if (reply.kind !== replyKind || reply.offset !== 0 || reply.total !== expected.length || !equals(reply.data, expected)) {
      throw new Error('Type 2 NFC 图像分组确认不一致')
    }
  }

  async qualifyWrites(capability?: Uint8Array) {
    for (const size of [16, 32, 40]) await this.echo(size)
    if (capability?.length !== 2 || capability[0] !== 1) return
    if (capability[1] & 2) {
      const choice = Uint8Array.of(compactSize)
      const reply = await this.roundTrip(compactKind, 0, 1, choice)
      if (reply.kind !== replyKind || reply.offset || reply.total !== 1 || !equals(reply.data, choice)) {
        throw new Error('Type 2 NFC 紧凑传输协商失败')
      }
      this.compact = true
      await this.echo(compactSize)
    }
    this.streamed = Boolean(capability[1] & 1)
    this.grouped = this.compact && this.streamed && Boolean(capability[1] & 4)
  }

  private async echo(size: number) {
    const data = Uint8Array.from({ length: size }, (_, i) => (i * 37 + size) & 255)
    const reply = await this.roundTrip(echoKind, 0, size, data).catch(error => {
      const detail = error instanceof Error ? error.message : '读卡器 NFC 通信失败。'
      throw new Error(`${size} 字节回显未完成。${detail}`)
    })
    if (reply.kind !== replyKind || reply.offset || reply.total !== size || !equals(reply.data, data)) {
      throw new Error(`${size} 字节回显校验失败。`)
    }
  }
}
