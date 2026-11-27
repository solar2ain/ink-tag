import { isMobileDevice } from '../layout'

export interface SerialPort {
  readonly connected?: boolean
  readonly readable: ReadableStream<Uint8Array> | null
  readonly writable: WritableStream<Uint8Array> | null
  getInfo?(): { usbVendorId?: number; usbProductId?: number }
  open(options: { baudRate: number; bufferSize: number }): Promise<void>
  setSignals(options: { dataTerminalReady: boolean; requestToSend: boolean }): Promise<void>
  close(): Promise<void>
}

export interface WebSerial {
  requestPort(): Promise<SerialPort>
  getPorts?(): Promise<SerialPort[]>
}

export function webSerial() {
  if (!window.isSecureContext || window.top !== window || isMobileDevice(navigator)) return undefined
  return (navigator as Navigator & { serial?: WebSerial }).serial
}

const SERIAL_BUFFER_SIZE = 4096
const ACK = Uint8Array.of(0, 0, 0xff, 0, 0xff, 0)
const delay = (ms: number) => new Promise<void>(resolve => setTimeout(resolve, ms))
class SerialReplyTimeout extends Error {
  constructor() { super('PN532 串口响应超时') }
}

export class Type2RfTimeout extends Error {
  constructor() { super('Type 2 射频通信失败（1）') }
}

const sum = (bytes: Uint8Array) => bytes.reduce((total, byte) => (total + byte) & 255, 0)

export function pn532Frame(command: Uint8Array) {
  const size = command.length + 1
  // The PCR532 HSU path uses normal UART frames.
  if (size > 255) throw new Error('PN532 串口帧过长')
  const body = Uint8Array.of(0xd4, ...command)
  return Uint8Array.of(0, 0, 0xff, size, -size & 255, ...body, -sum(body) & 255, 0)
}

export interface NfcSelection {
  uid: Uint8Array
  kind: 'type2' | 'type4'
  ats: Uint8Array
}

export function decodeSelection(bytes: Uint8Array): NfcSelection {
  const length = bytes[5]
  if (bytes[0] !== 1 || bytes[1] !== 1 || ![4, 7, 10].includes(length) || bytes.length < 6 + length) {
    throw new Error('读卡器没有返回唯一且有效的标签 UID')
  }
  const uid = bytes.slice(6, 6 + length)
  const ats = bytes.slice(6 + length)
  if (bytes[4] & 0x20) return { uid, kind: 'type4', ats }
  if (bytes[4] === 0 && uid.length === 7 && uid[0] === 4 && !ats.length) return { uid, kind: 'type2', ats }
  throw new Error('当前标签不支持 InkTag Type 2 或 Type 4 通道')
}

function crcA(data: Uint8Array) {
  let crc = 0x6363
  for (const byte of data) {
    let value = (byte ^ crc) & 255
    value = (value ^ (value << 4)) & 255
    crc = (crc >>> 8) ^ (value << 8) ^ (value << 3) ^ (value >>> 4)
  }
  return Uint8Array.of(crc & 255, crc >>> 8)
}

export class Pn532 {
  private buffer: number[] = []
  private timeoutCode = -1
  private firmwareVerified = false
  private pending = false
  private writeOffset = 0
  private incoming?: Promise<ReadableStreamReadResult<Uint8Array>>
  private reader?: ReadableStreamDefaultReader<Uint8Array>
  private writer?: WritableStreamDefaultWriter<Uint8Array>

  constructor(private readonly port: SerialPort, private readonly signal: AbortSignal) {}

  async run<T>(use: () => Promise<T>) {
    this.signal.throwIfAborted()
    await this.port.open({ baudRate: 115200, bufferSize: SERIAL_BUFFER_SIZE })
    try {
      this.reader = this.port.readable?.getReader()
      this.writer = this.port.writable?.getWriter()
      if (!this.reader || !this.writer) throw new Error('无法打开读卡器的串口数据流')
      await this.port.setSignals({ dataTerminalReady: false, requestToSend: false })
      await this.write(Uint8Array.of(0x55, 0x55, 0, 0, 0))
      await delay(500)
      await this.command(0x14, [1, 0x14, 1])
      const firmware = await this.command(2, [])
      if (firmware.length !== 4 || firmware[0] !== 0x32 || !(firmware[3] & 1)) {
        throw new Error('此串口没有通过 PN532 HSU 固件识别')
      }
      this.firmwareVerified = true
      await this.command(0x32, [1, 0])
      await this.command(0x12, [0x24])
      await this.command(0x32, [5, 0xff, 1, 0]) // Finite activation
      await this.command(0x12, [0x14]) // AutoRATS
      const result = await use()
      await this.fieldOff()
      return { result, firmware: `${firmware[1]}.${firmware[2]}` }
    } finally {
      try {
        if (this.firmwareVerified) await this.fieldOff()
      } catch { /* Preserve the original operation outcome. */ }
      await this.reader?.cancel().catch(() => undefined)
      this.reader?.releaseLock()
      this.writer?.releaseLock()
      await this.port.close().catch(() => undefined)
    }
  }

  private async write(frame: Uint8Array) {
    const remaining = SERIAL_BUFFER_SIZE - this.writeOffset
    // Chromium can split a write at the Web Serial pipe boundary. Put only
    // idle HSU preamble zeros in that tail so the next frame stays contiguous.
    const padding = frame.length > remaining ? remaining : 0
    const bytes = padding ? Uint8Array.of(...new Uint8Array(padding), ...frame) : frame
    await this.writer!.write(bytes)
    this.writeOffset = (this.writeOffset + bytes.length) % SERIAL_BUFFER_SIZE
  }

  private async fieldOff() {
    if (this.pending) {
      // ACK aborts the outstanding PN532 command. Never repeat the RF/APDU.
      await this.write(ACK)
      await delay(10)
    }
    await this.command(0x32, [1, 0], 1400, false)
    this.firmwareVerified = false
  }

  private async readFrame(deadline: number, abortable: boolean): Promise<number[]> {
    for (;;) {
      while (this.buffer.length >= 3 && (this.buffer[0] !== 0 || this.buffer[1] !== 0 || this.buffer[2] !== 0xff)) {
        this.buffer.shift()
      }
      if (this.buffer.length >= 6) {
        const [, , , len, check, next] = this.buffer
        if (len === 0 && check === 255 && next === 0) {
          this.buffer.splice(0, 6)
          return []
        }
        if (!len || ((len + check) & 255)) throw new Error('PN532 帧头长度或校验无效')
        if (this.buffer.length >= len + 7) {
          const frame = this.buffer.splice(0, len + 7)
          const body = frame.slice(5, -2)
          if (frame.at(-1) !== 0 || ((sum(Uint8Array.from(body)) + frame[len + 5]) & 255)) {
            throw new Error('PN532 串口校验失败')
          }
          return body
        }
      }
      const remaining = deadline - performance.now()
      if (remaining <= 0) throw new SerialReplyTimeout()
      const { value, done } = await this.readChunk(remaining, abortable)
      if (done) throw new Error('PN532 串口已断开')
      if (value) for (const byte of value) this.buffer.push(byte)
    }
  }

  private async readChunk(ms: number, abortable: boolean) {
    // Keep an outstanding stream read for RF-off cleanup after host timeout.
    const reader = this.reader!
    let timer: ReturnType<typeof setTimeout> | undefined
    let stopped: (() => void) | undefined
    this.incoming ??= reader.read()
    try {
      const chunk = await Promise.race([
        this.incoming,
        new Promise<never>((_, reject) => {
          timer = setTimeout(() => reject(new SerialReplyTimeout()), ms)
          if (abortable) {
            stopped = () => reject(this.signal.reason)
            this.signal.addEventListener('abort', stopped, { once: true })
            if (this.signal.aborted) stopped()
          }
        }),
      ])
      this.incoming = undefined
      return chunk
    } finally {
      clearTimeout(timer)
      if (stopped) this.signal.removeEventListener('abort', stopped)
    }
  }

  private async command(code: number, args: number[] | Uint8Array, ms = 1800, abortable = true) {
    if (abortable) this.signal.throwIfAborted()
    this.pending = true
    await this.write(pn532Frame(Uint8Array.of(code, ...args)))
    const deadline = performance.now() + ms
    let ack = await this.readFrame(deadline, abortable)
    // RF-off cleanup can see a response from the command being aborted.
    while (!abortable && ack.length) ack = await this.readFrame(deadline, false)
    if (ack.length) throw new Error('PN532 未确认串口命令')
    let response = await this.readFrame(deadline, abortable)
    while (!abortable && (response[0] !== 0xd5 || response[1] !== code + 1)) {
      response = await this.readFrame(deadline, false)
    }
    this.pending = false
    if (response[0] !== 0xd5 || response[1] !== code + 1) throw new Error('PN532 串口响应不匹配')
    return Uint8Array.from(response.slice(2))
  }

  async select() {
    const deadline = Date.now() + 30_000
    do {
      const result = await this.command(0x4a, [1, 0])
      if (result.length !== 1 || result[0] !== 0) return decodeSelection(result)
      await delay(250)
    } while (Date.now() < deadline)
    throw new Error('等待 InkTag 超时，请将 PN532 读卡器放到设备上')
  }

  async reselectType2(uid: Uint8Array, fieldOffMs = 160) {
    await this.command(0x32, [1, 0])
    await delay(fieldOffMs)
    const selected = await this.select()
    if (selected.kind !== 'type2' || !selected.uid.every((byte, index) => byte === uid[index])) {
      throw new Error('再次寻卡时 UID 已变化')
    }
    await this.setup('type2')
  }

  async setup(kind: NfcSelection['kind']) {
    const mode = kind === 'type4' ? 0x80 : 0
    await this.command(8, [0x63, 2, mode, 0x63, 3, mode, 0x63, 0x3d, 0])
    const registers = await this.command(6, [0x63, 2, 0x63, 3, 0x63, 0x3d])
    if (registers.length !== 3 || registers[0] !== mode || registers[1] !== mode || (registers[2] & 0x77) !== 0) {
      throw new Error('PN532 射频帧设置未通过读回核验')
    }
  }

  private async rf(frame: Uint8Array, wait: number) {
    // RFConfiguration item 2 uses finite powers of two in 0.1 ms units.
    const code = Math.min(16, Math.max(1, Math.ceil(Math.log2(wait / 0.1)) + 1))
    if (this.timeoutCode !== code) {
      await this.command(0x32, [2, 0, 0x0b, code])
      this.timeoutCode = code
    }
    const result = await this.command(0x42, frame, 0.1 * 2 ** (code - 1) + 800)
    if (!result.length) throw new Error('PN532 射频响应为空')
    return result
  }

  async isoFrame(frame: Uint8Array, wait: number) {
    const reply = await this.rf(frame, wait)
    if (reply[0]) throw new Error(`Type 4 射频通信失败（${reply[0].toString(16)}）`)
    return reply.subarray(1)
  }

  readonly type2Frame = async (frame: Uint8Array, passiveAck = false, retryRead = false, waitMs = 6) => {
    const request = Uint8Array.of(...frame, ...crcA(frame))
    // The default uses one finite timer for the silent SECTOR SELECT and
    // ordinary page operations. Image streaming can supply the CLI timing.
    let reply: Uint8Array
    try {
      reply = await this.rf(request, waitMs)
    } catch (error) {
      if (!retryRead || frame[0] !== 0x30 || !(error instanceof SerialReplyTimeout)) throw error
      await this.write(ACK)
      await delay(100)
      this.buffer.length = 0
      // Discard an old response before retrying this non-consuming read.
      try { await this.readChunk(20, true) } catch (drainError) {
        if (!(drainError instanceof SerialReplyTimeout)) throw drainError
      }
      reply = await this.rf(request, waitMs)
    }
    if (passiveAck && reply.length === 1 && reply[0] === 1) return new Uint8Array(0)
    if (reply[0] === 1) throw new Type2RfTimeout()
    if (reply[0]) throw new Error(`Type 2 射频通信失败（${reply[0].toString(16)}）`)
    const bytes = reply.subarray(1)
    if (bytes.length === 1) return bytes // Four-bit RF ACK/NAK.
    const payload = bytes.subarray(0, -2)
    const crc = crcA(payload)
    if (bytes.length < 3 || crc[0] !== bytes.at(-2) || crc[1] !== bytes.at(-1)) throw new Error('Type 2 射频响应 CRC 错误')
    return payload
  }
}
