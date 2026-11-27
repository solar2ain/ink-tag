import { describe, expect, it, vi } from 'vitest'
import { crc32Bytes } from '../../../packages/inktag-client-core/src/image'
import { encodeNfcTextRecord, parseInkTagNfcD1, writeInkTagNfcType4Command } from '../../../packages/inktag-client-core/src/nfc'
import { encodeNfcImage } from '../protocol/image'
import { encodeLzo1x } from '../protocol/lzo1x'
import { writePhoneCommand } from './browserNdef'
import { isoDepExchange } from './isoDep'
import { decodeSelection, Pn532, pn532Frame, Type2RfTimeout, type SerialPort } from './pn532'
import { imagePackets, useSerialTag } from './serialTag'
import { InkTagType2 } from './type2'

const ok = (...data: number[]) => Uint8Array.of(...data, 0x90, 0)
const uid = Uint8Array.of(4, 0x61, 0x7b, 0x5c, 0x43, 2, 0x69)
const type2 = Uint8Array.of(1, 1, 0, 0x44, 0, uid.length, ...uid)

describe('Web NFC commands', () => {
  it('uses the compatible short NDEF text type', async () => {
    const write = vi.fn().mockResolvedValue(undefined)
    const { signal } = new AbortController()
    await writePhoneCommand({ write }, 'slot-3', signal)
    expect(write).toHaveBeenCalledWith({ records: [{ recordType: 'text', lang: 'en', encoding: 'utf-8', data: 'slot-3' }] },
      { overwrite: true, signal })
    expect(() => writePhoneCommand({ write }, '字'.repeat(19), signal)).toThrow()
    expect(write).toHaveBeenCalledTimes(1)
  })
})

describe('PN532 public transport', () => {
  it('validates firmware, reassembles incoming frames and keeps outgoing frames inside the serial buffer', async () => {
    const received: Uint8Array[] = []
    const signal = new AbortController().signal
    let input!: ReadableStreamDefaultController<Uint8Array>
    let offset = 0
    let wrapped = false
    const port: SerialPort = {
      readable: new ReadableStream({ start(controller) { input = controller } }),
      writable: new WritableStream({ write(bytes: Uint8Array) {
        const oldOffset = offset
        offset = (offset + bytes.length) % 4096
        if (bytes[0] === 0x55) return
        const start = bytes.indexOf(0xff) - 2
        const frame = bytes.subarray(start)
        expect(oldOffset + bytes.length <= 4096 ||
          (start === 4096 - oldOffset && bytes.subarray(0, start).every(byte => byte === 0))).toBe(true)
        wrapped ||= start > 0
        received.push(frame)
        if (frame.length === 6) return
        const code = frame[6]
        const payload = code === 2 ? [0x32, 1, 6, 7] : code === 0x4a ? [...type2] : code === 0x42 ? [0, 0x0a] : []
        const body = Uint8Array.of(0xd5, code + 1, ...payload)
        const tail = -body.reduce((total, value) => total + value, 0) & 255
        input.enqueue(Uint8Array.of(0, 0, 0xff, 0, 0xff, 0, 0, 0))
        input.enqueue(Uint8Array.of(0xff, body.length, -body.length & 255, ...body, tail, 0))
      } }),
      open: vi.fn().mockResolvedValue(undefined),
      close: vi.fn().mockResolvedValue(undefined),
      setSignals: vi.fn().mockResolvedValue(undefined),
    }
    const reader = new Pn532(port, signal)
    const decision = await reader.run(async () => {
      const selected = await reader.select()
      for (let count = 0; count < 20; count++) {
        expect(await reader.isoFrame(new Uint8Array(240), 100)).toEqual(Uint8Array.of(0x0a))
      }
      return selected
    })
    expect(decision).toEqual({ firmware: '1.6', result: { uid, kind: 'type2', ats: new Uint8Array(0) } })
    expect(wrapped).toBe(true)
    expect(port.open).toHaveBeenCalledWith({ baudRate: 115200, bufferSize: 4096 })
    expect(received.at(-1)).toEqual(pn532Frame(Uint8Array.of(0x32, 1, 0)))
    expect(port.close).toHaveBeenCalledOnce()
    expect(decodeSelection(Uint8Array.of(...type2.slice(0, 4), 0x20, uid.length, ...uid, 2, 2)).kind).toBe('type4')
    expect(() => decodeSelection(Uint8Array.of(2, ...type2.slice(1)))).toThrow()
  })

  it('follows the ATS chain and WTX without replaying the application command', async () => {
    const transmit = vi.fn()
      .mockResolvedValueOnce(Uint8Array.of(0xa2))
      .mockResolvedValueOnce(Uint8Array.of(0xf2, 2))
      .mockResolvedValueOnce(Uint8Array.of(3, 0x90, 0))
    const exchange = isoDepExchange(Uint8Array.of(2, 2), transmit)
    await expect(exchange(new Uint8Array(30))).resolves.toEqual(ok())
    expect(transmit.mock.calls.map(([frame]) => [frame[0], frame.length])).toEqual([[0x12, 30], [3, 2], [0xf2, 2]])
    transmit.mockRejectedValueOnce(new Error('lost reply'))
    await expect(exchange(Uint8Array.of(0, 0xd1, 0, 0, 0))).rejects.toThrow('lost reply')
    expect(transmit).toHaveBeenCalledTimes(4)
  })

  it('reselects the same UID once after a Type 2 screen query times out', async () => {
    const run = vi.spyOn(Pn532.prototype, 'run').mockImplementation(async use => ({ result: await use(), firmware: '1.6' }))
    const select = vi.spyOn(Pn532.prototype, 'select').mockResolvedValue({ uid, kind: 'type2', ats: new Uint8Array() })
    const setup = vi.spyOn(Pn532.prototype, 'setup').mockResolvedValue(undefined)
    const reselect = vi.spyOn(Pn532.prototype, 'reselectType2').mockResolvedValue(undefined)
    const timeout = new Error('读取扇区 0 页 04 失败', { cause: new Type2RfTimeout() })
    const open = vi.spyOn(InkTagType2.prototype, 'open').mockRejectedValue(timeout)
    try {
      const port = {} as SerialPort
      await expect(useSerialTag(port, { kind: 'screen' }, uid, new AbortController().signal, vi.fn())).rejects.toBe(timeout)
      expect(reselect).toHaveBeenCalledExactlyOnceWith(uid, 500)
      expect(open).toHaveBeenCalledTimes(2)
    } finally {
      for (const spy of [run, select, setup, reselect, open]) spy.mockRestore()
    }
  })

  it('requires a matching D1 UID and publishes Type 4 NLEN after all chunks', async () => {
    expect(() => parseInkTagNfcD1(ok(0xc0, 4, 73, 75, 84, 49, 0xc1, uid.length, ...uid), uid.slice(1))).toThrow()
    const command = encodeNfcTextRecord('clock#1795276800,0')
    const exchanges: Uint8Array[] = []
    await writeInkTagNfcType4Command(async bytes => {
      exchanges.push(bytes)
      if (bytes[1] === 0xb0) return bytes[3] === 0
        ? ok(0, 15, 0x20, 0, 18, 0, 12)
        : ok(4, 6, 0xe1, 4, 0, 64, 0, 0)
      return ok()
    }, command)
    const updates = exchanges.filter(bytes => bytes[1] === 0xd6)
    expect([...updates[0]]).toEqual([0, 0xd6, 0, 0, 2, 0, 0])
    expect(updates.slice(1, -1).flatMap(bytes => [...bytes.slice(5)])).toEqual([...command])
    expect([...updates.at(-1)!]).toEqual([0, 0xd6, 0, 0, 2, 0, command.length])
  })

  it('packs padded rows and emits D3 with bounded independent LZO blocks', () => {
    const geometry = { width: 397, height: 100, visibleX: 0, visibleY: 0, visibleWidth: 397, visibleHeight: 100, transform: 0, color: 3 }
    const target = { ...geometry, packing: 3, planes: 1, planeBytes: 10000 }
    const data = new Uint8ClampedArray(397 * 100 * 4).fill(255)
    const image = encodeNfcImage({ data, width: 397, height: 100 }, target, target.packing)
    const [plane] = image.transfers
    const packets = imagePackets(target, image, true)
    expect(packets).toHaveLength(5)
    expect([...packets[0]]).toEqual([0xf0, 0xd3, 0, 1, packets[0].length - 5, 0, 0, ...encodeLzo1x(plane.data.slice(0, 2000))])
    expect([...packets[4].slice(0, 7)]).toEqual([0xf0, 0xd3, 0, 1, packets[4].length - 5, 4, 0])
    expect(imagePackets(target, image, false)).toHaveLength(40)
  })
})

describe('InkTag Type 2 volatile NDEF', () => {
  function tag(mode: number, failWrite = false) {
    let sector = 0
    const mirror = new Uint8Array(64)
    // NDEF URI with HTTPS prefix and the maintained short InkTag link.
    const url = new TextEncoder().encode('solar2ain.github.io/ink-tag/?ble=IKT_OM_AA06')
    mirror.set([3, url.length + 5, 0xd1, 1, url.length + 1, 0x55, 4, ...url, 0xfe])
    const writes: Uint8Array[] = []
    const select = vi.fn()
    const link = new InkTagType2(async (frame, passiveAck) => {
      if (passiveAck) {
        sector = frame[0]
        return new Uint8Array()
      }
      if (frame[0] === 0xc2) return Uint8Array.of(0x0a)
      if (frame[0] === 0x60) return Uint8Array.of(0, 4, 4, 5, 2, 1, 0x15, 3)
      if (frame[0] === 0x30 && sector === 3 && frame[1] === 0xf8) {
        return Uint8Array.of(mode, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0)
      }
      if (frame[0] === 0x30 && sector === 0 && frame[1] === 0) {
        return Uint8Array.of(...uid, 0, 0, 0, 0, 0, 0xe1, 0x10, 0xea, 0)
      }
      if (frame[0] === 0x30 && sector === 0) return mirror.slice((frame[1] - 4) * 4, (frame[1] - 4) * 4 + 16)
      if (frame[0] === 0xa2 && sector === 0) {
        writes.push(frame)
        if (failWrite) throw new Error('unknown result')
        mirror.set(frame.subarray(2), (frame[1] - 4) * 4)
        return Uint8Array.of(0x0a)
      }
      throw new Error('Unexpected exchange')
    }, uid, select)
    return { link, writes, select, mirror }
  }

  it('reads the reported name and commits one short NDEF into the live volatile map', async () => {
    const { link, writes, mirror } = tag(3)
    const record = encodeNfcTextRecord('calendar#1795276800,+480')
    await expect(link.readName()).resolves.toBe('IKT_OM_AA06')
    expect(writes).toHaveLength(0)
    await link.writeText(() => record)
    expect([...writes[0]]).toEqual([0xa2, 4, 3, 0, 0xfe, 0])
    expect(writes.map(frame => frame[1])).toEqual([4, 5, 6, 7, 8, 9, 10, 11, 12, 4])
    expect([...mirror.subarray(0, record.length + 3)]).toEqual([3, record.length, ...record, 0xfe])
  })

  it('does not repeat a page write with an unknown result', async () => {
    const { link, writes, select } = tag(3, true)
    await expect(link.writeText(() => encodeNfcTextRecord('clock'))).rejects.toThrow('unknown result')
    expect(writes).toHaveLength(1)
    expect(select).not.toHaveBeenCalled()
  })

  function directTag(d1 = ok(), corrupt?: 'count' | 'digest') {
    const sram = new Uint8Array(64)
    const nonce = new Uint8Array(4)
    let sector = 0
    let compact = false
    let pending = false
    let negotiating = false
    let groupCount = 0
    const groupBytes: number[] = []
    const requests: { kind: number; offset: number; data: Uint8Array }[] = []
    const crc = () => (crc32Bytes(compact ? Uint8Array.of(...nonce, ...sram.subarray(0, 60)) : sram.subarray(0, 60)) ^ 0xffffffff) >>> 0
    const raw = async (frame: Uint8Array, passiveAck = false) => {
      if (passiveAck) {
        sector = frame[0]
        return new Uint8Array()
      }
      const [kind, page] = frame
      if (kind === 0xc2) return Uint8Array.of(0x0a)
      if (kind === 0x60) return Uint8Array.of(0, 4, 4, 5, 2, 1, 0x15, 3)
      if (kind === 0x30 && sector === 0 && !page) return Uint8Array.of(...uid, 0, 0, 0, 0, 0, 0xe1, 0x10, 0xea, 0)
      if (kind === 0x30 && sector === 3 && page === 0xf8) {
        return Uint8Array.of(pending ? 0x40 : 0x41, 0, 0, 0, 0, 0, pending ? 9 : 1, 0, 0, 0, 0, 0, 0, 0, 0, 0)
      }
      expect(sector).toBe(1)
      const from = (page - 0xf0) * 4
      if (kind === 0x30) {
        expect(pending).toBe(true)
        const bytes = sram.slice(from, from + 16)
        if (page === 0xfc) {
          pending = false
          compact ||= negotiating
        }
        return bytes
      }
      expect(kind).toBe(0xa2)
      expect(pending).toBe(false)
      sram.set(frame.subarray(2), from)
      if (page === 0xff) {
        const view = new DataView(sram.buffer)
        const kindAt = compact ? 0 : 5
        const sizeAt = compact ? 1 : 16
        const offsetAt = compact ? 4 : 12
        const dataAt = compact ? 8 : 20
        if (!compact) nonce.set(sram.subarray(8, 12))
        expect(view.getUint32(60, true)).toBe(crc())
        const command = sram[kindAt]
        const offset = view.getUint16(offsetAt, true)
        const total = view.getUint16(offsetAt + 2, true)
        const data = sram.slice(dataAt, dataAt + sram[sizeAt])
        requests.push({ kind: command, offset, data })
        negotiating = command === 6
        if (command === 7) {
          groupBytes.push(...data)
          if (offset + data.length === total) groupCount++
          return Uint8Array.of(0x0a)
        }
        const imageData = command === 1 || command === 5
        const more = imageData && offset + data.length < total
        if (command === 5 && more) return Uint8Array.of(0x0a)
        let replyOffset = more ? offset + data.length : 0
        let reply = !imageData ? data : more ? new Uint8Array() : ok()
        let replyTotal = more ? total : reply.length
        if ((command === 1 && data[0] === 0 && data[1] === 0xd1) || command === 2) {
          replyOffset = command === 2 ? offset : 0
          replyTotal = d1.length
          reply = d1.slice(replyOffset, replyOffset + (compact ? 52 : 40))
        }
        if (command === 8) {
          const digest = groupBytes.reduce((hash, byte) =>
            ((hash ^ BigInt(byte)) * 16777619n) % 0x100000000n, 2166136261n)
          reply = new Uint8Array(6)
          const confirmation = new DataView(reply.buffer)
          confirmation.setUint16(0, groupCount + Number(corrupt === 'count'), true)
          confirmation.setUint32(2, Number(digest) ^ Number(corrupt === 'digest'), true)
          replyTotal = reply.length
          groupCount = 0
          groupBytes.length = 0
        }
        sram[kindAt] = more ? 0x82 : 0x81
        sram[sizeAt] = reply.length
        view.setUint16(offsetAt, replyOffset, true)
        view.setUint16(offsetAt + 2, replyTotal, true)
        sram.fill(0, dataAt, 60)
        sram.set(reply, dataAt)
        view.setUint32(60, crc(), true)
        pending = true
      }
      return Uint8Array.of(0x0a)
    }
    return { raw, requests }
  }

  it.each([
    { width: 250, color: 3, validUid: true },
    { width: 248, color: 3, validUid: true },
    { width: 250, color: 2, validUid: true },
    { width: 250, color: 3, validUid: false },
  ])('checks the live Type 4 target after Type 2 screen recognition ($width, $color, UID $validUid)', async ({ width, color, validUid }) => {
    const otherUid = Uint8Array.of(4, 2, 3, 4, 5, 6, 7)
    const height = 122
    const describeScreen = (tagUid: Uint8Array, w: number, palette: number) => ok(
      0xc0, 4, 73, 75, 84, 49, 0xc1, tagUid.length, ...tagUid,
      0xe0, 15, 0, w, 0, height, 0, 0, 0, 0, 0, w, 0, height, palette, 3, 0,
    )
    const { raw } = directTag(describeScreen(uid, 250, 3))
    const run = vi.spyOn(Pn532.prototype, 'run').mockImplementation(async function (this: Pn532, use) {
      Object.defineProperty(this, 'type2Frame', { value: raw })
      return { result: await use(), firmware: '1.6' }
    })
    const select = vi.spyOn(Pn532.prototype, 'select')
      .mockResolvedValueOnce({ uid, kind: 'type2', ats: new Uint8Array() })
      .mockResolvedValueOnce({ uid: otherUid, kind: 'type4', ats: Uint8Array.of(2, 8) })
    const setup = vi.spyOn(Pn532.prototype, 'setup').mockResolvedValue(undefined)
    const commands: number[] = []
    const apdu: number[] = []
    const isoFrame = vi.spyOn(Pn532.prototype, 'isoFrame').mockImplementation(async frame => {
      const pcb = frame[0]
      const number = pcb & 1
      apdu.push(...frame.subarray(1))
      if (pcb & 0x10) return Uint8Array.of(0xa2 | number)
      const command = apdu[1]
      commands.push(command)
      apdu.length = 0
      const reply = command === 0xd1 ? describeScreen(validUid ? otherUid : uid, width, color) : ok()
      return Uint8Array.of(2 | number, ...reply)
    })
    try {
      const port = {} as SerialPort
      const signal = new AbortController().signal
      const reading = await useSerialTag(port, { kind: 'screen' }, undefined, signal, vi.fn())
      expect(reading).toMatchObject({ tagType: 'Type 2', screen: { width: 250, height: 122, color: 3 } })
      const { screen } = reading
      if (!screen) throw new Error('Expected Type 2 screen parameters')
      const frame = { width: screen.width, height: screen.height, data: new Uint8ClampedArray(screen.width * screen.height * 4).fill(255) }
      const sending = useSerialTag(port, { kind: 'image', frame, color: screen.color }, undefined, signal, vi.fn())
      if (!validUid) {
        await expect(sending).rejects.toThrow('未通过 InkTag 身份核对')
        expect(commands).toEqual([0xd1])
      } else if (width !== frame.width || color !== screen.color) {
        await expect(sending).resolves.toMatchObject({
          screen: { width, height, color },
          outcome: expect.stringContaining('再次点击「NFC 传图」'),
        })
        expect(commands).toEqual([0xd1])
      } else {
        await expect(sending).resolves.toMatchObject({ uid: otherUid, tagType: 'Type 4', outcome: expect.stringContaining('9000') })
        expect(commands[0]).toBe(0xd1)
        expect(commands.slice(1, -1)).toEqual(Array(Math.ceil(Math.ceil(250 / 4) * height / 250)).fill(0xd2))
        expect(commands.at(-1)).toBe(0xd4)
      }
    } finally {
      for (const spy of [run, select, setup, isoFrame]) spy.mockRestore()
    }
  })

  it.each([0, 1, 2, 0x0f])('uses the D1 stream flags %i and confirms the complete image packet', async flags => {
    const compactSupported = Boolean(flags & 2)
    const streamSupported = Boolean(flags & 1)
    const { raw, requests } = directTag()
    const link = new InkTagType2(raw, uid, vi.fn())
    await link.open()
    await link.qualifyWrites(Uint8Array.of(1, flags))
    const image = Uint8Array.of(0xf0, 0xd3, 0, 1, 52, ...new Uint8Array(52).fill(0x5a))
    await expect(link.exchange(image)).resolves.toEqual(ok())
    const dataFrames = requests.filter(request => request.kind === 1 || request.kind === 5)
    const chunkSize = compactSupported ? 52 : 40
    expect(dataFrames.map(({ offset, data }) => [offset, data.length])).toEqual([[0, chunkSize], [chunkSize, 57 - chunkSize]])
    expect(dataFrames.flatMap(({ data }) => [...data])).toEqual([...image])
    expect(dataFrames.map(frame => frame.kind)).toEqual(streamSupported ? [5, 5] : [1, 1])
    expect(requests.filter(request => request.kind === 6).map(({ data }) => [...data])).toEqual(compactSupported ? [[52]] : [])
    expect(requests.filter(request => request.kind === 3).map(({ data }) => data.length)).toEqual(compactSupported ? [16, 32, 40, 52] : [16, 32, 40])
    await expect(link.exchange(Uint8Array.of(0xf0, 0xd4, 0, 0, 0))).resolves.toEqual(ok())
    expect(requests.at(-1)?.kind).toBe(1)
  })

  it.each([
    { flags: 0x07, compress: true },
    { flags: 0x0f, compress: false },
    { flags: 0x03, compress: false },
    { flags: 0x0f, compress: true, corrupt: 'count' as const },
    { flags: 0x0f, compress: true, corrupt: 'digest' as const },
  ])('confirms image groups only after the first block ($flags, LZO $compress, corrupt $corrupt)', async ({ flags, compress, corrupt }) => {
    const width = 100, height = 200, color = 3
    const data = new Uint8ClampedArray(width * height * 4)
    const palette = [[0,0,0,255], [255,255,255,255], [255,0,0,255], [255,255,0,255]]
    let seed = 1
    for (let offset = 0; offset < data.length; offset += 4) {
      seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0
      data.set(palette[seed >>> 30], offset)
    }
    const target = { width, height, visibleX: 0, visibleY: 0, visibleWidth: width, visibleHeight: height,
      color, packing: 3, transform: 0, planeBytes: width * height / 4, planes: 1 }
    const frame = { width, height, data }
    const packets = imagePackets(target, encodeNfcImage(frame, target, 3), compress)
    const firstBlock = compress ? packets.findIndex(packet => packet[3] === 1) + 1 : 1
    if (compress) expect(firstBlock).toBeGreaterThan(1)
    const description = ok(0xc0,4,0x49,0x4b,0x54,0x31, 0xc1,7,...uid,
      0xe0,15,0,width,0,height,0,0,0,0,0,width,0,height,color,3,0,
      0xe2,2,1,flags, 0xd1,7,Number(compress),0,0,0,0,0,0)
    const { raw, requests } = directTag(description, corrupt)
    const run = vi.spyOn(Pn532.prototype, 'run').mockImplementation(async function (this: Pn532, use) {
      Object.defineProperty(this, 'type2Frame', { value: raw })
      return { result: await use(), firmware: '1.6' }
    })
    const select = vi.spyOn(Pn532.prototype, 'select').mockResolvedValue({ uid, kind: 'type2', ats: new Uint8Array() })
    const setup = vi.spyOn(Pn532.prototype, 'setup').mockResolvedValue(undefined)
    vi.useFakeTimers()
    try {
      const progress = vi.fn()
      const sending = useSerialTag({} as SerialPort, { kind: 'image', frame, color }, uid, new AbortController().signal, progress)
      const result = corrupt
        ? expect(sending).rejects.toThrow('图像分组确认不一致')
        : expect(sending).resolves.toMatchObject({ outcome: expect.stringContaining('9000') })
      await vi.runAllTimersAsync()
      await result
      const dataStarts = requests.filter(({ kind, offset }) => (kind === 5 || kind === 7) && offset === 0)
      const refresh = requests.filter(({ kind, data }) => kind === 1 && data[0] === 0xf0 && data[1] === 0xd4)
      const confirmations = requests.filter(({ kind }) => kind === 8)
      const imageProgress = progress.mock.calls.flatMap(([, image]) => image ? [image] : [])
      const submitted = imageProgress.filter(image => image.submitted !== undefined)
      const confirmed = imageProgress.filter(image => image.submitted === undefined).map(image => image.confirmed)
      if (flags & 4) {
        expect(dataStarts.slice(0, firstBlock).map(({ kind }) => kind)).toEqual(Array(firstBlock).fill(5))
        expect(dataStarts.slice(firstBlock).every(({ kind }) => kind === 7)).toBe(true)
        const groupSizes = []
        let count = 0
        for (const { kind, offset } of requests) {
          if (kind === 7 && offset === 0) count++
          if (kind === 8) { groupSizes.push(count); count = 0 }
        }
        expect(groupSizes).toEqual(corrupt ? [Math.min(16, packets.length - firstBlock)]
          : Array.from({ length: Math.ceil((packets.length - firstBlock) / 16) },
            (_, group) => Math.min(16, packets.length - firstBlock - 16 * group)))
        expect(submitted.map(image => image.submitted)).toEqual(
          Array.from({ length: groupSizes.reduce((sum, size) => sum + size, 0) }, (_, index) => firstBlock + index + 1),
        )
        let confirmedBeforeGroup = firstBlock
        let submittedIndex = 0
        for (const size of groupSizes) {
          const pendingGroup = submitted.slice(submittedIndex, submittedIndex + size)
          expect(pendingGroup.map(image => image.confirmed)).toEqual(Array(size).fill(confirmedBeforeGroup))
          submittedIndex += size
          confirmedBeforeGroup += size
        }
      } else {
        expect(dataStarts).toHaveLength(packets.length)
        expect(dataStarts.every(({ kind }) => kind === 5)).toBe(true)
        expect(confirmations).toHaveLength(0)
        expect(submitted).toHaveLength(0)
      }
      expect(refresh).toHaveLength(corrupt ? 0 : 1)
      expect(confirmed.at(-1)).toBe(corrupt ? firstBlock : packets.length)
      if (!corrupt && flags & 4) expect(confirmed[firstBlock + 1]).toBe(firstBlock + Math.min(16, packets.length - firstBlock))
    } finally {
      vi.useRealTimers()
      for (const spy of [run, select, setup]) spy.mockRestore()
    }
  })
})
