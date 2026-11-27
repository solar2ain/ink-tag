/** Public, anonymous update discovery. Never accepts device/account identifiers. */
import type { FirmwareBuildEdition } from '../types'

export type UpdateChannel = 'stable'
export const UPDATE_ROOT = 'https://solar2ain.github.io/ink-tag/updates/'
export const PRO_FIRMWARE_FEED_URL = `${UPDATE_ROOT}pro/stable.json`
export const UPDATE_SCHEMA = 3
export type Artifact = { type: string; name: string; bytes: number; sha256: string; downloadUrl: string; repositoryPath?: string }
export type Component = { version: string; buildId: string; buildNumber: number; releaseTag: string;
  channel: UpdateChannel; candidateOnly: false; releaseReady: true; published: true; artifacts: Artifact[] }
export type FirmwareTarget = { boardId: number | string; family: string; protocolVersion: number; layout: string; compatibilityEpoch: number }
export type FirmwareRelease = Component & { target: FirmwareTarget; releaseVersion: number; buildTarget?: string; edition?: FirmwareBuildEdition }
export type UpdateFeed = { schemaVersion: 3; channel: UpdateChannel; candidateOnly: false; releaseReady: boolean;
  firmwareSelection: { published: boolean }; components: Record<'webui' | 'cli' | 'localServer', Component | null>; firmware: FirmwareRelease[] }
export type FetchLike = (input: RequestInfo | URL, init?: RequestInit) => Promise<Response>

const safeName = /^[A-Za-z0-9][A-Za-z0-9._-]{0,180}$/
const tagPattern = /^v\d+\.\d+\.\d+(?:\.\d+)?(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?$/
function object(value: unknown): value is Record<string, unknown> { return !!value && typeof value === 'object' && !Array.isArray(value) }
export function compareVersions(left: string, right: string): number {
  const parse = (value: string) => {
    const match = /^(?:v)?(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:-([0-9A-Za-z.-]+))?(?:\+[0-9A-Za-z.-]+)?$/.exec(value)
    if (!match || match.slice(1, 4).some(v => !Number.isSafeInteger(Number(v))) || match[4]?.split('.').some(v => !v || /^0\d+$/.test(v))) throw new Error('更新清单包含无效版本号')
    return { core: match.slice(1, 4).map(Number), pre: match[4]?.split('.') }
  }
  const a = parse(left), b = parse(right)
  for (let i = 0; i < 3; i++) if (a.core[i] !== b.core[i]) return a.core[i] > b.core[i] ? 1 : -1
  if (!a.pre || !b.pre) return a.pre ? -1 : b.pre ? 1 : 0
  for (let i = 0; i < Math.max(a.pre.length, b.pre.length); i++) {
    const x = a.pre[i], y = b.pre[i]
    if (x === y) continue
    if (x === undefined || y === undefined) return x === undefined ? -1 : 1
    const xn = /^\d+$/.test(x), yn = /^\d+$/.test(y)
    if (xn && yn) return BigInt(x) > BigInt(y) ? 1 : -1
    if (xn !== yn) return xn ? -1 : 1
    return x > y ? 1 : -1
  }
  return 0
}

/** Native firmware has four numeric fields; WebUI also accepts a fourth numeric component. */
export function compareFirmwareVersions(left: string, right: string): number {
  const parse = (value: string) => {
    if (!/^(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)(?:\.(?:0|[1-9]\d*))?$/.test(value)) throw new Error('固件版本号无效')
    const fields = value.split('.').map(Number)
    if (fields.some(n => !Number.isSafeInteger(n))) throw new Error('固件版本号无效')
    return [...fields, ...Array(4 - fields.length).fill(0)] as number[]
  }
  const a = parse(left), b = parse(right)
  for (let i = 0; i < 4; i++) if (a[i] !== b[i]) return a[i] > b[i] ? 1 : -1
  return 0
}
export function isFirmwareVersion(value: string): boolean {
  try { compareFirmwareVersions(value, value); return true } catch { return false }
}

export function validateArtifact(value: unknown, tag: string): Artifact {
  if (!object(value) || typeof value.type !== 'string' || typeof value.name !== 'string' || !safeName.test(value.name) ||
      !Number.isSafeInteger(value.bytes) || Number(value.bytes) <= 0 || typeof value.sha256 !== 'string' || !/^[a-f0-9]{64}$/.test(value.sha256) ||
      value.downloadUrl !== `https://github.com/solar2ain/ink-tag/releases/download/${tag}/${value.name}`) throw new Error('更新附件的地址、大小或 SHA256 无效')
  return value as Artifact
}
function component(value: unknown, channel: UpdateChannel, kind?: 'firmware' | 'webui'): Component {
  if (!object(value) || value.candidateOnly !== false || value.releaseReady !== true || value.published !== true || value.channel !== channel ||
      typeof value.releaseTag !== 'string' || !tagPattern.test(value.releaseTag) || typeof value.version !== 'string' ||
      typeof value.buildId !== 'string' || !/^[A-Za-z0-9._:-]{1,128}$/.test(value.buildId) ||
      !Number.isSafeInteger(value.buildNumber) || Number(value.buildNumber) < 0 || !Array.isArray(value.artifacts) || !value.artifacts.length) throw new Error('更新附件尚未正式批准或发布')
  if (kind === 'firmware' || kind === 'webui' && isFirmwareVersion(value.version)) compareFirmwareVersions(value.version, value.version)
  else compareVersions(value.version, value.version)
  const datedRelease = /^v\d+\.\d+\.\d+(?:\.\d+)?-\d{8}\.[1-9]\d*$/.test(value.releaseTag)
  if (channel === 'stable' && (value.version.includes('-') || value.releaseTag.includes('-') && !datedRelease)) throw new Error('正式通道不能包含预览版本')
  value.artifacts.forEach(item => validateArtifact(item, value.releaseTag as string))
  return value as Component
}
export function validateFeed(value: unknown, channel: UpdateChannel): UpdateFeed {
  if (channel !== 'stable') throw new Error('仅支持正式版更新')
  if (!object(value) || value.schemaVersion !== UPDATE_SCHEMA || value.channel !== channel || value.candidateOnly !== false ||
      typeof value.releaseReady !== 'boolean' || !object(value.components) || !object(value.firmwareSelection) ||
      typeof value.firmwareSelection.published !== 'boolean' || !Array.isArray(value.firmware)) throw new Error('更新清单格式或通道无效')
  for (const key of ['webui', 'cli', 'localServer']) {
    const entry = value.components[key]
    if (entry !== null) component(entry, channel, key === 'webui' ? 'webui' : undefined)
  }
  for (const item of value.firmware) {
    component(item, channel, 'firmware')
    const target = item.target
    if (!object(target) || !(Number.isSafeInteger(target.boardId) && Number(target.boardId) >= 0 || typeof target.boardId === 'string' && /^[a-z0-9_]+$/.test(target.boardId)) ||
        typeof target.family !== 'string' || !/^[a-z0-9_]+$/.test(target.family) ||
        !Number.isSafeInteger(target.protocolVersion) || Number(target.protocolVersion) < 0 ||
        typeof target.layout !== 'string' || !/^[a-zA-Z0-9._-]+$/.test(target.layout) ||
        !Number.isSafeInteger(target.compatibilityEpoch) || Number(target.compatibilityEpoch) < 0 ||
        !Number.isSafeInteger(item.releaseVersion) || item.releaseVersion < 0 ||
        !item.artifacts.some((a: Artifact) => a.type === 'ota')) throw new Error('OTA 缺少准确板型或兼容约束')
  }
  if (value.firmwareSelection.published !== (value.firmware.length > 0) || (!value.releaseReady &&
      (value.firmware.length || Object.values(value.components).some(Boolean)))) throw new Error('更新清单的发布状态矛盾')
  return value as UpdateFeed
}

const cache = new Map<string, { expires: number; result: Promise<UpdateFeed> }>()
export async function loadUpdates(channel: UpdateChannel = 'stable', fetcher: FetchLike = globalThis.fetch,
  timeoutMs = 8000, edition: FirmwareBuildEdition = 'basic'): Promise<UpdateFeed> {
  if (channel !== 'stable') throw new Error('仅支持正式版更新')
  const url = edition === 'pro' ? PRO_FIRMWARE_FEED_URL : `${UPDATE_ROOT}${channel}.json`
  const prior = cache.get(url)
  if (fetcher === globalThis.fetch && prior && prior.expires > Date.now()) return prior.result
  const controller = new AbortController()
  let timer: ReturnType<typeof setTimeout>
  const timeout = new Promise<never>((_, reject) => { timer = setTimeout(() => { controller.abort(); reject(new Error('检查更新超时，请稍后重试')) }, timeoutMs) })
  const request = Promise.race([timeout, (async () => {
    const response = await fetcher(url, { headers: { Accept: 'application/json' },
      credentials: 'omit', referrerPolicy: 'no-referrer', cache: 'no-cache', signal: controller.signal })
    if (edition === 'pro' && response.status === 404) {
      return validateFeed({ schemaVersion: UPDATE_SCHEMA, channel, candidateOnly: false, releaseReady: false,
        firmwareSelection: { published: false }, components: { webui: null, cli: null, localServer: null }, firmware: [] }, channel)
    }
    if (!response.ok) throw new Error(`更新检查失败（HTTP ${response.status}）`)
    if (!response.headers.get('content-type')?.includes('application/json')) throw new Error('更新清单响应格式无效')
    const text = await response.text()
    if (text.length > 512 * 1024) throw new Error('更新清单过大')
    return validateFeed(JSON.parse(text), channel)
  })()]).finally(() => clearTimeout(timer))
  if (fetcher === globalThis.fetch) {
    cache.set(url, { expires: Date.now() + 300000, result: request })
    void request.catch(() => { if (cache.get(url)?.result === request) cache.delete(url) })
  }
  return request
}
export function componentUpdate(latest: Component | null, current: { version: string; buildId: string; buildNumber: number }) {
  if (!latest) return 'not-published' as const
  const compare = isFirmwareVersion(latest.version) && isFirmwareVersion(current.version)
    ? compareFirmwareVersions(latest.version, current.version) : compareVersions(latest.version, current.version)
  return compare > 0 || compare === 0 && latest.buildId !== current.buildId && latest.buildNumber > current.buildNumber ? 'available' as const : 'current' as const
}
export async function verifyDownload(bytes: ArrayBuffer, artifact: Artifact) {
  if (bytes.byteLength !== artifact.bytes) throw new Error('下载附件大小不匹配')
  const digest = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', bytes)), b => b.toString(16).padStart(2, '0')).join('')
  if (digest !== artifact.sha256) throw new Error('下载附件 SHA256 不匹配')
  // This is file integrity only. Existing OTA publisher-signature and device
  // attestation checks are still mandatory and are not replaced here.
}
