import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { inktagClient } from './client'
import { CommandV2 } from './generatedCommandsV2'
import { useDeviceStore } from '../store/device'
import { imageSlotContentTypeIds, type ImageSlotContentType, type ScreenConfig } from '../types'
import { makeScreenConfig } from './screen'

const transport = inktagClient as unknown as {
  requestV2: (opcode: number, payload: Uint8Array) => Promise<unknown>
  sendCanvasV2: (canvas: HTMLCanvasElement, transfers: { step: string; data: Uint8Array }[],
    format: number, screen: ScreenConfig, name: string, save: boolean, type: ImageSlotContentType,
    replace?: number, display?: boolean) => Promise<unknown>
}

describe('image names and types are upload-owned', () => {
  beforeEach(() => {
    useDeviceStore.setState({ capabilities: { ...useDeviceStore.getState().capabilities,
      imageStore: true, imageMetadata: true } })
  })
  afterEach(() => vi.restoreAllMocks())

  it.each(['课程表', '一'.repeat(11) + 'ab', ''])('CREATE carries UTF-8 name without a second mutation: %s', async (name) => {
    const request = vi.spyOn(transport, 'requestV2').mockImplementation(async (opcode, payload) => {
      expect(opcode).toBe(CommandV2.TRANSFER_BEGIN)
      expect(payload[20]).toBe(imageSlotContentTypeIds.schedule)
      expect(payload[38]).toBe(new TextEncoder().encode(name).length)
      expect(new TextDecoder().decode(payload.slice(39))).toBe(name)
      throw new Error('stop before device write')
    })
    const screen = makeScreenConfig({ driver: 0x13, color: 2, transform: 0,
      width: 400, height: 300, visibleX: 0, visibleY: 0, visibleWidth: 400, visibleHeight: 300 })
    await expect(transport.sendCanvasV2({} as HTMLCanvasElement,
      [{ step: 'bw', data: new Uint8Array([1, 2, 3]) }], 1, screen, name, true, 'schedule',
      undefined, false)).rejects.toThrow('stop before device write')
    expect(request).toHaveBeenCalledTimes(1)
  })

  it('has no standalone metadata/alias update API even when installed firmware advertises it', () => {
    expect(inktagClient).not.toHaveProperty('updateStoredImageMetadata')
    expect(inktagClient).not.toHaveProperty('setStoredImageAlias')
    expect(inktagClient).not.toHaveProperty('setResourceAlias')
    expect(inktagClient.readStoredImage).toBeTypeOf('function')
    expect(inktagClient.showStoredImage).toBeTypeOf('function')
  })
})
