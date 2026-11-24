import { afterEach, describe, expect, it, vi } from 'vitest'
import { inktagClient } from './client'
import { CapabilityV2, CommandV2, TransferDescriptorFlagV2 } from './generatedCommandsV2'
import { encodeImage, type ImageStep, type ImageTransfer } from './image'
import { makeScreenConfig, TransferFormat } from './screen'
import { useDeviceStore } from '../store/device'
import type { ScreenConfig } from '../types'

const transport = inktagClient as unknown as {
  v2Capabilities: number
  v2WritePayload: number
  requestV2: (opcode: number, payload: Uint8Array) => Promise<{ payload: Uint8Array }>
  waitForV2Completion: () => Promise<Uint8Array>
  writeV2TransferChunk: (tx: number, object: number, total: number, offset: number,
    chunk: Uint8Array, decodedLength?: number) => Promise<void>
  sendCanvasV2: (canvas: HTMLCanvasElement, transfers: ImageTransfer[], format: number,
    screen: ScreenConfig, name: string, save: boolean, type: 'image') => Promise<unknown>
  stepLabel: (step: ImageStep, format: number, screen: ScreenConfig) => string
}
const previous = { capabilities: transport.v2Capabilities, chunk: transport.v2WritePayload }
afterEach(() => {
  vi.restoreAllMocks()
  transport.v2Capabilities = previous.capabilities
  transport.v2WritePayload = previous.chunk
})

describe('image transfer presentation does not redefine wire channels', () => {
  for (const [driver, recipe, width, height, color] of [[0x40, 1, 240, 240,10], [0x41, 1, 400, 400,10], [0x41, 2, 720, 480,10],
    [0x30,1,400,300,3],[0x33,1,400,300,3]]) {
    it.each([false, true])(`labels ${driver}/${recipe}/${color} data on raw/RLE=%s`, async (rle) => {
      const label=color===10 ? '六色数据':'四色数据'
      const format=color===10 ? TransferFormat.INDEXED_4BPP:TransferFormat.INDEXED_2BPP
      const screen = makeScreenConfig({ driver, recipe, color, transform: 0,
        width, height, visibleX: 0, visibleY: 0, visibleWidth: width, visibleHeight: height })
      const pixels = new Uint8ClampedArray(width * height * 4).fill(255)
      const palette = [[0, 0, 0], [255, 255, 255], [235, 194, 25], [220, 48, 38],
        ...(color===10 ? [[0, 0, 255], [41, 204, 20]]:[])]
      palette.forEach((rgb, i) => pixels.set([...rgb, 255], i * 4))
      const encoded = encodeImage({ width, height, data: pixels }, screen)
      expect(encoded.format).toBe(format)
      expect(encoded.transfers[0].step).toBe(color===10 ? 'bw':'color')
      expect([...encoded.transfers[0].data.slice(0, color===10 ? 3:1)]).toEqual(color===10 ? [0x01,0x23,0x56]:[0x1b])
      transport.v2Capabilities = rle ? CapabilityV2.TRANSFER_PACKBITS_RLE : 0
      transport.v2WritePayload = 244
      const begin: Uint8Array[] = []
      vi.spyOn(transport, 'requestV2').mockImplementation(async (opcode, payload) => {
        if (opcode === CommandV2.TRANSFER_BEGIN) begin.push(payload.slice())
        return { payload: new Uint8Array(0) }
      })
      vi.spyOn(transport, 'waitForV2Completion').mockResolvedValue(new Uint8Array(0))
      const restored = new Uint8Array(encoded.transfers[0].data.length)
      vi.spyOn(transport, 'writeV2TransferChunk').mockImplementation(async (_tx, _object, _total, offset, chunk, decodedLength) => {
        if (decodedLength === undefined) restored.set(chunk, offset)
        else {
          const decoded: number[] = []
          for (let i = 0; i < chunk.length;) {
            const token = chunk[i++]
            if (token & 0x80) decoded.push(...Array((token & 0x7f) + 3).fill(chunk[i++]))
            else { decoded.push(...chunk.slice(i, i + token + 1)); i += token + 1 }
          }
          expect(decoded).toHaveLength(decodedLength)
          restored.set(decoded, offset)
        }
      })
      const action = vi.spyOn(useDeviceStore.getState(), 'setAction').mockImplementation(() => undefined)
      const log = vi.spyOn(useDeviceStore.getState(), 'addLog').mockImplementation(() => undefined)
      await transport.sendCanvasV2({} as HTMLCanvasElement, encoded.transfers,
        encoded.format, screen, '', false, 'image')
      expect(begin).toHaveLength(1)
      expect(begin[0][1]).toBe(format)
      expect(begin[0][3] & 1).toBe(color===10 ? 1:0) // Native E6 alone uses the black descriptor.
      expect(Boolean(begin[0][3] & TransferDescriptorFlagV2.PACKBITS_RLE)).toBe(rle)
      expect(restored).toEqual(encoded.transfers[0].data)
      const messages = action.mock.calls.map((call) => call[2] || '')
      expect(messages).toContain(`正在传输${label}。`)
      expect(messages.some((message) => message.includes('黑白'))).toBe(false)
      if (!rle) expect(log.mock.calls.some(([message]) => message.startsWith(label+' '))).toBe(true)
    })
  }

  const screen = makeScreenConfig({ driver: 0x40, recipe: 1, color: 10, transform: 0,
    width: 240, height: 240, visibleX: 0, visibleY: 0, visibleWidth: 240, visibleHeight: 240 })
  it.each([
    ['bw', '黑白图层'], ['red', '彩色图层'], ['color', '四色图层'],
    ['gray-msb', '灰阶 RAM1图层'], ['gray-lsb', '灰阶 RAM2图层'],
    ['e6-area0', '六色第一阶段（蓝/白/绿/红）图层'], ['e6-area6', '六色第二阶段（黑/黄）图层'],
  ] as const)('preserves existing %s phase labels', (step, expected) => {
    expect(transport.stepLabel(step, TransferFormat.DUAL_1BPP, screen)).toBe(expected)
  })
  it('requires effective Osprey driver, six-color profile and native indexed format', () => {
    expect(transport.stepLabel('bw', TransferFormat.INDEXED_4BPP, { ...screen, driver: 0x13 })).toBe('黑白图层')
    expect(transport.stepLabel('bw', TransferFormat.INDEXED_4BPP, { ...screen, color: 1 })).toBe('黑白图层')
    expect(transport.stepLabel('bw', TransferFormat.MONO_1BPP, screen)).toBe('黑白图层')
  })
})
