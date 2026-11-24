import { readNordicPackageVersion } from '../protocol/nordicPackageMetadata'
import { INKTAG_FIRMWARE_VERSION } from '../generated/version'
import type { DeviceCapabilities } from '../types'
import type { ParsedOtaPackage } from '../protocol/ota'
import { mcuDisplayName } from '../protocol/boards'

/** Display parsed metadata only; never infer admission, signature or transfer state. */
export function otaPlatformLabel(type: DeviceCapabilities['ota'], boardId: string) {
  if (type === 'goodix') return /^GR\d/i.test(mcuDisplayName(boardId)) ? mcuDisplayName(boardId) : 'GR 系列'
  if (type === 'onmicro') return 'OM6626'
  if (type === 'telink') return 'TLSR8359'
  if (type === 'dialog') return 'DA14585'
  if (type === 'nordic') {
    const mcu = mcuDisplayName(boardId)
    return /^nRF528(?:11|32)$/i.test(mcu) ? mcu.replace(/^nrf/i, 'nRF') : 'Nordic'
  }
  return '未识别平台'
}

const nonempty = (value: unknown) => typeof value === 'string' && value.trim() ? value.trim() : undefined
const size = (value: unknown) => typeof value === 'number' && Number.isSafeInteger(value) && value > 0 ? value : undefined
const displayVersion = (value: string | undefined) => value && /^\d+\.\d+\.\d+$/.test(value) ? `${value}.0` : value

export function otaPackagePresentation(pkg: ParsedOtaPackage, boardId: string) {
  const payload = pkg.payload ?? {}
  const appBytes = pkg.type === 'goodix' || pkg.type === 'onmicro' ? size(payload.application?.size)
    : pkg.type === 'nordic' ? size(payload.firmware?.length)
      : pkg.type === 'telink' ? size(payload.declaredSize) : size(payload.codeSize)
  const nordic = pkg.type === 'nordic' ? readNordicPackageVersion(payload.initPacket, appBytes) : undefined
  const knownNordicRelease = nordic && nordic.compatibilityEpoch > 0 && nordic.releaseNumber === INKTAG_FIRMWARE_VERSION.otaVersion ? INKTAG_FIRMWARE_VERSION.version : undefined
  const release = pkg.type === 'dialog' ? nonempty(payload.version)
    : pkg.type === 'nordic' ? knownNordicRelease : nonempty(payload.manifest?.release)
  const releaseNumber = nordic ? String(nordic.encodedVersion) : pkg.type !== 'nordic' && pkg.type !== 'dialog'
    && typeof payload.manifest?.releaseVersion === 'number'
    && Number.isSafeInteger(payload.manifest.releaseVersion) && payload.manifest.releaseVersion >= 0
    ? String(payload.manifest.releaseVersion) : undefined
  return {
    platform: otaPlatformLabel(pkg.type, boardId),
    fileName: nonempty(payload.fileName) ?? '未提供文件名',
    appBytes,
    applicationSize: appBytes === undefined ? '未提供' : `${appBytes.toLocaleString('zh-CN')} B`,
    versionLabel: !release && releaseNumber ? '版本编号' : '版本',
    version: displayVersion(release) ?? releaseNumber ?? '未提供',
    signatureLabel: payload.verified?.publisherVerified === false ? '未签名 · 需允许未签名更新的开源 Recovery'
      : payload.verified?.publisherVerified === true ? '官方签名已验证' : undefined,
    technicalSummary: nordic ? `${pkg.summary} · 包内版本编号 ${nordic.encodedVersion} · 兼容纪元 ${nordic.compatibilityEpoch}` : pkg.summary,
  }
}
