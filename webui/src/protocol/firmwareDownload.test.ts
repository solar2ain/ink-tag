import { afterEach, expect, it, vi } from 'vitest'
import { downloadFirmwareFile, firmwareBrowserDownloadUrl, type FirmwareUpdateInfo } from './firmwareUpdate'
import type { FetchLike } from './updates'

const info: FirmwareUpdateInfo = {
  edition: 'basic',
  currentVersion: '0.2.0.0', latestVersion: '0.2.1.0', releaseTag: 'v0.2.1-20261107.1',
  boardId: 'nrf52832_priceetag', boardName: 'nRF52832', buildTarget: 'nrf52832_priceetag',
  artifactName: 'InkTag-nrf52832_priceetag-ota.zip', bytes: 3,
  sha256: 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad',
  downloadUrl: 'https://github.com/solar2ain/ink-tag/releases/download/v0.2.1-20261107.1/InkTag-nrf52832_priceetag-ota.zip',
}
afterEach(() => vi.restoreAllMocks())

it('downloads the exact tag Git copy, checks bytes, and returns an ordinary OTA File', async () => {
  const fetcher = vi.fn<FetchLike>(async () => new Response('abc'))
  const progress = vi.fn()
  const file = await downloadFirmwareFile(info, { onProgress: progress }, fetcher)
  expect(fetcher.mock.calls[0][0]).toBe('https://raw.githubusercontent.com/solar2ain/ink-tag/v0.2.1-20261107.1/firmware/nrf52832_priceetag/ota/InkTag-nrf52832_priceetag-ota.zip')
  expect(fetcher.mock.calls[0][1]).toMatchObject({ credentials: 'omit', referrerPolicy: 'no-referrer', redirect: 'error' })
  expect(fetcher.mock.calls[0][1]?.body).toBeUndefined()
  expect(file.name).toBe(info.artifactName)
  expect(await file.text()).toBe('abc')
  expect(progress).toHaveBeenLastCalledWith(3, 3)
})

it.each([
  { buildTarget: undefined }, { buildTarget: '../wrong' }, { artifactName: '../wrong.zip' },
  { releaseTag: '../main' }, { downloadUrl: 'https://example.invalid/firmware.zip' }, { bytes: 16 * 1024 * 1024 + 1 },
])('rejects missing/untrusted metadata before networking: %j', async change => {
  const fetcher = vi.fn<FetchLike>()
  await expect(downloadFirmwareFile({ ...info, ...change }, {}, fetcher)).rejects.toThrow()
  expect(fetcher).not.toHaveBeenCalled()
})

it.each(['ab', 'abcd', 'abd'])('rejects truncated, oversized, or hash-mismatched bytes %s', async contents => {
  await expect(downloadFirmwareFile(info, {}, async () => new Response(contents))).rejects.toThrow(/大小|SHA256/)
})

it('reports HTTP failure without returning a file', async () => {
  await expect(downloadFirmwareFile(info, {}, async () => new Response('missing', { status: 404 }))).rejects.toThrow(/404/)
})

it('bounds an unresponsive fetch even when it ignores AbortSignal', async () => {
  await expect(downloadFirmwareFile(info, { timeoutMs: 5 }, () => new Promise(() => {}))).rejects.toThrow(/超时/)
})

it('cancels a pending stream and rejects without completing verification', async () => {
  const controller = new AbortController()
  const cancelled = vi.fn()
  const fetcher = vi.fn<FetchLike>(async () => new Response(new ReadableStream({
    start(stream) { stream.enqueue(new TextEncoder().encode('a')) }, cancel: cancelled,
  })))
  const result = downloadFirmwareFile(info, { signal: controller.signal,
    onProgress() { controller.abort() } }, fetcher)
  await expect(result).rejects.toMatchObject({ name: 'AbortError' })
  expect(cancelled).toHaveBeenCalledOnce()
})

it('makes no request when already cancelled', async () => {
  const controller = new AbortController(); controller.abort()
  const fetcher = vi.fn<FetchLike>()
  await expect(downloadFirmwareFile(info, { signal: controller.signal }, fetcher)).rejects.toMatchObject({ name: 'AbortError' })
  expect(fetcher).not.toHaveBeenCalled()
})

it('does not accept a moving main-branch URL instead of the release tag', () => {
  expect(firmwareBrowserDownloadUrl(info)).not.toContain('/main/')
})
