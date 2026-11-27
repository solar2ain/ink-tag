import { compareFirmwareVersions, loadUpdates, validateArtifact, validateFeed, verifyDownload, type FetchLike, type FirmwareTarget, type UpdateChannel } from './updates'
import type { FirmwareBuildEdition } from '../types'
export { compareFirmwareVersions } from './updates'
export interface FirmwareUpdateInfo {
  currentVersion: string; latestVersion: string; releaseTag: string; boardId: string; boardName: string
  artifactName: string; downloadUrl: string; sha256: string; bytes: number
  edition: FirmwareBuildEdition
  buildTarget?: string
  repositoryPath?: string
}
export type FirmwareUpdateResolution =
  | { status: 'available'; latestVersion: string; update: FirmwareUpdateInfo }
  | { status: 'current' | 'not-published'; latestVersion: string; update: null }
export type FirmwareCheckOptions = { channel?: UpdateChannel; target?: FirmwareTarget; releaseVersion?: number; edition?: FirmwareBuildEdition }
// Fixed0.2 release-baseline contracts, keyed by exact executable wire identity.
// No product-name aliases or future/legacy-protocol guesses.
export function v020FirmwareOptions(family: string, version: string, protocol: number | null): FirmwareCheckOptions {
  const layouts: Record<string,string> = {gr5331_zkel:'layout-v1',gr551x_zkel:'layout-v1',gr5513_zkel_internal:'layout-v1',
    om6626_zkcxxb:'layout-v1',tlsr8359_hanshow:'layout-v1',aesl22_nrf:'N1V1',nrf52832_priceetag:'N3V1',da14585_aesl:'DAV1'}
  if(protocol!==16 || !/^0\.2\.(?:0|[1-9]\d*)(?:\.(?:0|[1-9]\d*))?$/.test(version) || !Object.hasOwn(layouts,family)) return {}
  const patch=Number(version.split('.')[2]), release=200+patch
  return {target:{boardId:family,family,protocolVersion:16,layout:layouts[family]!,compatibilityEpoch:1},
    releaseVersion:family==='aesl22_nrf'||family==='nrf52832_priceetag'?65536+release:release}
}
export function resolveFirmwareUpdate(value: unknown, family: string, currentVersion: string, options: FirmwareCheckOptions = {}): FirmwareUpdateResolution {
  const feed = validateFeed(value, options.channel ?? 'stable')
  const absent = { status: 'not-published' as const, latestVersion: currentVersion, update: null }
  if (!feed.releaseReady || !feed.firmwareSelection.published) return absent
  if (!options.target || !Number.isSafeInteger(options.releaseVersion)) throw new Error('设备未提供自动匹配 OTA 所需的完整兼容信息，请选择与设备匹配的本地固件包')
  const target = options.target
  const edition = options.edition ?? 'basic'
  // No aliases, display-name equivalence, MCU-only matching or guessed layout.
  const fields = ['boardId', 'family', 'protocolVersion', 'layout', 'compatibilityEpoch'] as const
  if (target.family !== family || fields.some(key => target[key] === undefined)) return absent
  const match = feed.firmware.filter(item => item.edition === edition && fields.every(key =>
    item.target[key as keyof FirmwareTarget] === target[key as keyof FirmwareTarget]))
    .sort((a, b) => compareFirmwareVersions(b.version, a.version) || b.releaseVersion - a.releaseVersion)[0]
  if (!match) return absent
  const artifact = match.artifacts.find(item => item.type === 'ota')!
  if (compareFirmwareVersions(match.version, currentVersion) <= 0 || match.releaseVersion <= options.releaseVersion!) {
    return { status: 'current', latestVersion: match.version, update: null }
  }
  return { status: 'available', latestVersion: match.version, update: {
    currentVersion, latestVersion: match.version, releaseTag: match.releaseTag, edition,
    boardId: family, boardName: `${family} / board_id ${target.boardId}`,
    artifactName: artifact.name, downloadUrl: artifact.downloadUrl, sha256: artifact.sha256, bytes: artifact.bytes,
    ...(typeof match.buildTarget === 'string' ? { buildTarget: match.buildTarget } : {}),
    ...(artifact.repositoryPath !== undefined ? { repositoryPath: artifact.repositoryPath } : {}),
  } }
}
export async function checkFirmwareUpdate(family: string, version: string, options: FirmwareCheckOptions = {}, fetcher: FetchLike = globalThis.fetch) {
  return resolveFirmwareUpdate(await loadUpdates(options.channel ?? 'stable', fetcher, undefined, options.edition), family, version, options)
}

// Release assets do not provide browser CORS. Use the published Git copy at
// the same immutable release tag; its bytes must still match the approved feed.
export function firmwareBrowserDownloadUrl(update: FirmwareUpdateInfo): string {
  validateArtifact({ type: 'ota', name: update.artifactName, bytes: update.bytes,
    sha256: update.sha256, downloadUrl: update.downloadUrl }, update.releaseTag)
  if (!/^v\d+\.\d+\.\d+(?:\.\d+)?(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?$/.test(update.releaseTag) ||
      !update.buildTarget || !/^[a-z0-9_]+$/.test(update.buildTarget)) {
    throw new Error('发布清单缺少在线载入所需的信息，请选择本地 OTA 固件包')
  }
  const legacyPath = `firmware/${update.buildTarget}/ota/${update.artifactName}`
  const path = update.repositoryPath ?? legacyPath
  if (update.repositoryPath !== undefined) {
    const prefix = `release/firmware/${update.buildTarget}/ota/`
    if (typeof path !== 'string' || !path.startsWith(prefix) ||
        !/^[A-Za-z0-9][A-Za-z0-9._-]{0,175}\.(?:zip|img|bin)$/.test(path.slice(prefix.length)) ||
        /[\r\n]/.test(path)) {
      throw new Error('发布清单中的 OTA 文件路径与板型不匹配')
    }
  }
  return `https://raw.githubusercontent.com/solar2ain/ink-tag/${encodeURIComponent(update.releaseTag)}/${path}`
}

export async function downloadFirmwareFile(update: FirmwareUpdateInfo,
  options: { signal?: AbortSignal; onProgress?: (received: number, total: number) => void; timeoutMs?: number } = {},
  fetcher: FetchLike = globalThis.fetch): Promise<File> {
  const url = firmwareBrowserDownloadUrl(update)
  if (update.bytes > 16 * 1024 * 1024) throw new Error('在线 OTA 包过大，请选择本地固件包')
  const controller = new AbortController()
  const abort = () => controller.abort()
  options.signal?.addEventListener('abort', abort, { once: true })
  if (options.signal?.aborted) controller.abort()
  let timer: ReturnType<typeof setTimeout>
  const timeout = new Promise<never>((_, reject) => {
    timer = setTimeout(() => { reject(new Error('在线固件下载超时，请重试或选择本地固件包')); controller.abort() }, options.timeoutMs ?? 120000)
  })
  const cancelled = new Promise<never>((_, reject) => {
    controller.signal.addEventListener('abort', () => reject(new DOMException('在线固件下载已取消', 'AbortError')), { once: true })
  })
  const download = async () => {
    if (controller.signal.aborted) throw new DOMException('在线固件下载已取消', 'AbortError')
    const response = await fetcher(url, { credentials: 'omit', referrerPolicy: 'no-referrer',
      redirect: 'error', signal: controller.signal })
    if (!response.ok) throw new Error(`在线固件下载失败（HTTP ${response.status}），请重试或选择本地固件包`)
    if (!response.body) throw new Error('浏览器未提供固件下载内容，请选择本地固件包')
    const reader = response.body.getReader()
    const cancelReader = () => { void reader.cancel().catch(() => {}) }
    controller.signal.addEventListener('abort', cancelReader, { once: true })
    const output = new Uint8Array(update.bytes)
    let received = 0
    try {
      while (true) {
        const chunk = await reader.read()
        if (chunk.done) break
        if (controller.signal.aborted) throw new DOMException('在线固件下载已取消', 'AbortError')
        received += chunk.value.byteLength
        if (received > output.byteLength) throw new Error('下载附件大小不匹配')
        output.set(chunk.value, received - chunk.value.byteLength)
        options.onProgress?.(received, output.byteLength)
      }
      if (controller.signal.aborted) throw new DOMException('在线固件下载已取消', 'AbortError')
      if (received !== output.byteLength) throw new Error('下载附件大小不匹配')
      await verifyDownload(output.buffer, { type: 'ota', name: update.artifactName, bytes: update.bytes,
        sha256: update.sha256, downloadUrl: update.downloadUrl })
      return new File([output], update.artifactName, { type: update.artifactName.endsWith('.img') ? 'application/octet-stream' : 'application/zip' })
    } finally {
      controller.signal.removeEventListener('abort', cancelReader)
      if (controller.signal.aborted || received !== output.byteLength) void reader.cancel().catch(() => {})
      reader.releaseLock()
    }
  }
  try { return await Promise.race([download(), timeout, cancelled]) }
  finally { clearTimeout(timer!); options.signal?.removeEventListener('abort', abort) }
}
