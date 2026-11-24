// @vitest-environment jsdom
import { afterEach, beforeEach, expect, it, vi } from 'vitest'
import { downloadBlob, downloadCanvasPng } from './download'

beforeEach(() => {
  vi.useFakeTimers()
  vi.stubGlobal('URL', { createObjectURL: vi.fn(() => 'blob:png-test'), revokeObjectURL: vi.fn() })
})
afterEach(() => { vi.runOnlyPendingTimers(); vi.restoreAllMocks(); vi.unstubAllGlobals(); vi.useRealTimers() })

it('downloads a PNG blob through a connected anchor and releases the URL after consumption', async () => {
  const canvas = document.createElement('canvas'), blob = new Blob(['png'], { type: 'image/png' })
  vi.spyOn(canvas, 'toBlob').mockImplementation(callback => callback(blob))
  const click = vi.spyOn(HTMLAnchorElement.prototype, 'click').mockImplementation(function(this: HTMLAnchorElement) {
    expect(this.isConnected).toBe(true)
    expect(this.href).toBe('blob:png-test'); expect(this.download).toBe('我的_页面.png')
  })
  await downloadCanvasPng(canvas, '我的/页面.png')
  expect(canvas.toBlob).toHaveBeenCalledWith(expect.any(Function), 'image/png')
  expect(URL.createObjectURL).toHaveBeenCalledWith(blob); expect(click).toHaveBeenCalledOnce()
  expect(document.querySelector('a[download]')).toBeNull()
  vi.advanceTimersByTime(1000); expect(URL.revokeObjectURL).not.toHaveBeenCalled()
  vi.advanceTimersByTime(59_000); expect(URL.revokeObjectURL).toHaveBeenCalledWith('blob:png-test')
})

it('reports PNG encoding failure without triggering a download', async () => {
  const canvas = document.createElement('canvas')
  vi.spyOn(canvas, 'toBlob').mockImplementation(callback => callback(null))
  await expect(downloadCanvasPng(canvas, 'test.png')).rejects.toThrow('无法生成 PNG')
  expect(URL.createObjectURL).not.toHaveBeenCalled()
})

it('cleans up even when the browser rejects the download click', () => {
  vi.spyOn(HTMLAnchorElement.prototype, 'click').mockImplementation(() => { throw new Error('download blocked') })
  expect(() => downloadBlob(new Blob(['{}']), 'test.json')).toThrow('download blocked')
  expect(document.querySelector('a[download]')).toBeNull()
  vi.runOnlyPendingTimers(); expect(URL.revokeObjectURL).toHaveBeenCalledWith('blob:png-test')
})
