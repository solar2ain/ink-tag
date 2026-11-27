import { canonicalBoardId, firmwareFamilyProfile, resolveBoardLedCapability, resolveBoardProfile } from './boards'
import { decodeAdvertisingConfig, encodeAdvertisingConfig, type AdvertisingConfig } from './advertising'
import { decodeCapabilitiesV2 } from '../../../packages/inktag-client-core/src/decoders'
import { encodeReleaseRequestFlags } from './generatedReleaseV1'
import { decodePinReport, encodePinBlock, pinSetRequest, pinRoles, validatePinUpdate, PinWriteUnconfirmedError, type PinConfig } from './pinConfig'
import { auxPins, decodeAuxIoConfiguration, encodeAuxIoConfiguration, firstAuxIoSettings, type AuxIoConfiguration } from './auxIo'
import { decodeScreenConfig, encodeScreenConfig, screenConfigsEqual, screenRequiresPro, validateScreenConfig, ScreenWriteUnconfirmedError, TransferFormat } from './screen'
import { crc32Bytes, decodeStoredImage, encodeImage, imageCrc32, type ImageStep, type ImageTransfer } from './image'
import { telemetryFromMessage } from './telemetry'
import { statusTemperatureTelemetry } from './temperatureSources'
import {
  decodeButtonConfig, decodeDeviceSettingsCapabilities, decodeLedSettingsPayload,
  decodeReportedButtonSettings,
  decodeReportedDeviceLabel, decodeReportedLedSettings, decodeReportedNfcSettings, decodeReportedNfcUrl,
  calendarDateToTargetDay, decodeDisplaySettings, emptyDeviceSettingsCapabilities, encodeBluetoothName, encodeDeviceLabel,
  encodeDisplaySettings, encodeInputAction, encodeLedSettings,
  inputActionSupported,
} from './deviceSettings'
import {
  ActivationSchemeV2, BusyPhaseV2, CapabilityV2, CapabilityExtensionV2, CommandV2, DisplayActionV2, DisplayModeV2, EventV2,
  NfcCapabilityV2, PROTOCOL_V2_VERSION, RequestFlagV2, ResourceClearScopeV2, SettingsSectionV2, TestOperationV2,
  SleepWakeFlagV2, StatusV2, SystemActionV2, TransferDescriptorFlagV2, TransferObjectV2,
} from './generatedCommandsV2'
import { buildPackBitsBlocks, packBitsIsWorthwhile, subsequentSegmentStarts, type PackBitsBlock } from './packbits'
import { buildLzo1xBlocks } from './lzo1x'
import {
  activationResultLabels, encodeActivationInstall, emptyFirmwareActivation,
  parseActivationRequest, parseActivationStatus,
} from './activation'
import { useDeviceStore } from '../store/device'
import { db, type DevicePreference } from '../data/db'
import { getDevicePreference, rememberDeviceEdition, updateDevicePreference } from '../data/devicePreferences'
import { buildInkTagDeviceUrl, packQrValue, createQrMatrix, packQrMatrix } from '../qr/qr'
import { buildPasswordQrLink, createPasswordProof, decodeDevicePasswordStatus, derivePasswordChangeKey, derivePasswordCredential, encodePasswordBundle, RESET_SETTINGS_PROOF_DATA, type DevicePasswordStatus } from './devicePassword'
import { getPasswordCredential, savePasswordCredential, takePasswordQrCredential } from '../data/passwordCredentials'
import { findClient } from '../find/client'
import { ownerActionHash, parseOwnerChallenge, proveOwnerWithProCode, proveOwnerWithFindK } from './ownerAuth'
import { GUI_ASSET_ABSOLUTE_MAX_BYTES, type GuiAssetInfo, type GuiAssetPackage } from './guiAssets'
import { parseFontInfo, parseFontPackage, admitFontPackage } from './fontRoles'
import { imageSlotContentTypeIds, type ButtonActionSettings, type ButtonBehaviorSettings, type ButtonGesture, type DeviceAction, type DeviceCapabilities, type ImageSlotContentType, type ImageStoreInfo, type ImageStoreItem, type LedSettings, type NfcTapSettings, type ScreenConfig } from '../types'
import { encodeUploadImageName } from './imageNames'
import { resourceReadBudget, retryResourceRead, type ResourceReadBudget } from './resourceRead'
import { resourceInfoFailureMessage } from '../image/storageAvailability'
import { formatImageRate } from '../image/transferRate'
import { acquireImageLink, type ImageLinkLease } from './imageLink'
import { ImageTransferTiming, decodeTlsrNotifyCounters, imageDocumentVisibility, observeImageTiming, type ImageTimingSink } from './imageTransferTiming'
import { buildBluetoothRequestOptions, type BluetoothDiscoveryOptions } from './bluetoothDiscovery'
import { parseRuntimeBuildInfo } from './runtimeBuild'
import { BluetoothConnectionTimeoutError, withConnectionTimeout } from './connectionTimeout'
import { isBluetoothDeviceOutOfRange, waitForBluetoothRetry, waitForDeviceAdvertisement } from './bluetoothReconnect'
import { encodeTimeSetTest, encodeTimeShiftTest, type TimeTestDisplayMode } from './timeTest'
import { decodeTestOperations } from './testOperations'
import { decodeNfcCapabilities, decodeNfcNdef, decodeNfcStatus, encodeNfcNdef, encodeNfcUriRecord } from './nfcCommand'
import { buildCrashAlert, decodeDeviceHealth, suppressBrownoutAlert } from './health'
import { clockDiagnostic } from './clockDiagnostic'
import {
  decodeFindCapabilities, decodeFindConfig, decodeFindOwnerRotationChallenge,
  decodeFindOwnerRotationProof, decodeFindPublicOnlyStagePayload, decodeFindPublicOnlyReplacementStagePayload, decodeFindPublicOnlyOwnerStatus, encodeFindConfig,
  decodeFindOwnerUnregisterPayload, supportsFindOwnerUnregister,
  type FindOwnerRotationProof,
} from './find'
import { constrainedImageStoreInfo, findVisibleImageRecord, visibleImageRecords } from '../access/imageQuota'

const SERVICE_UUID = '62750001-d828-918d-fb46-b6c11c675aec'
const COMMAND_UUID = '62750002-d828-918d-fb46-b6c11c675aec'
const VERSION_UUID = '62750003-d828-918d-fb46-b6c11c675aec'
const OPTIONAL_SERVICES = [
  SERVICE_UUID,
  '0000fe59-0000-1000-8000-00805f9b34fb',
  'a6ed0401-d344-460a-8075-b9e8ec90d71b',
  '000001bf-5f00-0000-0000-4f4d2d4f5441',
  '0000fef5-0000-1000-8000-00805f9b34fb',
  '00010203-0405-0607-0809-0a0b0c0d1912',
]
const sleep = (milliseconds: number) => new Promise((resolve) => setTimeout(resolve, milliseconds))
const CONNECTION_TIMEOUT_MS = 15000
// WinRT can expose a connected GATT server several seconds before Chromium's
// service cache is ready.  Five seconds caused us to tear down a healthy link
// and turn one slow discovery into multiple full reconnects.  Keep this
// bounded, allowing ten seconds for service discovery before a fresh attempt.
const GATT_SERVICE_DISCOVERY_TIMEOUT_MS = 10000
const GATT_BOOTSTRAP_RETRY_DELAYS_MS = [700, 1300] as const
const KNOWN_DEVICE_RETRY_DELAYS_MS = [1000, 2000, 4000, 6000] as const
const KNOWN_DEVICE_ADVERTISEMENT_TIMEOUT_MS = 15000
const ONMICRO_OTA_SERVICE_UUID = '000001bf-5f00-0000-0000-4f4d2d4f5441'
const DIALOG_OTA_SERVICE_UUID = '0000fef5-0000-1000-8000-00805f9b34fb'
const TELINK_OTA_SERVICE_UUID = '00010203-0405-0607-0809-0a0b0c0d1912'
const GOODIX_OTA_SERVICE_UUID = 'a6ed0401-d344-460a-8075-b9e8ec90d71b'

type FixedRecoveryType = 'onmicro' | 'dialog' | 'telink' | 'goodix' | 'nordic'
type ExpectedFixedRecovery = { type: FixedRecoveryType; deviceId: string; name: string; legacyName: string }

function fixedRecoveryFromName(name: string): { type: FixedRecoveryType; name: string } | null {
  const match = /^(IKT|InkTag)_(OM|DA|TL|GR|NRF)_([0-9A-F]{4})_OTA$/i.exec(name.trim())
  if (!match) return null
  const type = match[2].toUpperCase() === 'OM' ? 'onmicro'
    : match[2].toUpperCase() === 'GR' ? 'goodix'
    : match[2].toUpperCase() === 'NRF' ? 'nordic'
    : match[2].toUpperCase() === 'DA' ? 'dialog'
      : 'telink'
  return { type, name: `${match[1].toUpperCase() === 'IKT' ? 'IKT' : 'InkTag'}_${match[2].toUpperCase()}_${match[3].toUpperCase()}_OTA` }
}

function rememberedOnMicroRecovery(device: BluetoothDevice, preference?: DevicePreference): ExpectedFixedRecovery | null {
  // Windows can retain the App name on this exact browser grant even after a
  // page reload. A previously read App identity permits only the corresponding
  // read-only fixed Recovery probe; the live profile must still pass below.
  if (!preference || preference.id !== device.id || preference.reconnectHidden ||
      !Number.isFinite(preference.lastConnectedAt) || !(preference.lastConnectedAt! > 0)) return null
  const board = preference.lastConnectedBoardId || ''
  const ota = firmwareFamilyProfile(board)?.ota ?? resolveBoardProfile(board)?.capabilities.ota
  if (ota !== 'onmicro') return null
  const match = /^(?:IKT|InkTag)_OM_([0-9A-F]{4})$/i.exec(preference.lastConnectedName || '')
  if (!match || (device.name !== preference.lastConnectedName && !fixedRecoveryFromName(device.name || ''))) return null
  const id4 = match[1].toUpperCase()
  return { type: 'onmicro', deviceId: device.id, name: `IKT_OM_${id4}_OTA`, legacyName: `InkTag_OM_${id4}_OTA` }
}

function fixedRecoveryService(type: FixedRecoveryType) {
  return type === 'onmicro' ? ONMICRO_OTA_SERVICE_UUID
    : type === 'goodix' ? GOODIX_OTA_SERVICE_UUID
    : type === 'dialog' ? DIALOG_OTA_SERVICE_UUID
    : type === 'nordic' ? '0000fe59-0000-1000-8000-00805f9b34fb'
      : TELINK_OTA_SERVICE_UUID
}

function fixedRecoveryLabel(type: FixedRecoveryType) {
  return type === 'onmicro' ? 'OM6626' : type === 'dialog' ? 'DA14585' : type === 'goodix' ? 'GR' : type === 'nordic' ? 'nRF' : 'TLSR8359'
}

type StoreReply = { ok: boolean; id?: number; error?: string }
type ScreenReply = { ok: true; screen: ScreenConfig } | { ok: false; error: string }
type V2Reply = { payload: Uint8Array; transactionId: number }
export type RuntimeImageInfo = { version: number; appBytes: number; checksum: number }
type ApplicationReturnIdentity = { boardId: string; bleAddress: string; activationId: string | null; image: RuntimeImageInfo | null }
type V2Pending = {
  opcode: number
  startedAt: number
  quiet: boolean
  resolve: (reply: V2Reply) => void
  reject: (error: Error) => void
  timer: number
  imageTiming?: ImageTimingSink
}

export function displayWorkDetailFromLog(action: DeviceAction, message: string): string | null {
  if (action === 'idle' || action === 'ota' || action === 'slideshow') return null
  // Firmware diagnostics append controller, BUSY and pin fields after the
  // stage tag. Match the first token so `gui:refresh drv=...` changes the UI
  // phase just like the compact legacy `gui:refresh` message.
  const tag = message.split(' ', 1)[0]
  if (/^gui:(start|black|red|draw|area0|area6)$/.test(tag)) {
    return action === 'device-info' ? '设备正在绘制设备信息页布局。'
      : action === 'calendar' ? '设备正在绘制日历布局。'
        : action === 'clock' ? '设备正在绘制时钟布局。'
          : '设备正在绘制布局。'
  }
  if (tag === 'gui:refresh') return '布局绘制完成，设备正在刷新墨水屏。'
  if (tag === 'clear:start' || tag === 'clear:init') return '设备正在清空并刷新墨水屏。'
  if (message === 'picture:refresh' || message.startsWith('img:show:refresh')) {
    return '图片数据已准备完成，设备正在刷新墨水屏。'
  }
  if (message.startsWith('img:show:start')) return '设备正在读取图片并写入屏幕缓冲区。'
  return null
}

export function isTransientGattBootstrapError(error: unknown): boolean {
  if (error instanceof BluetoothConnectionTimeoutError) {
    return error.phase === '建立 GATT 连接' || error.phase === '读取 InkTag 服务'
  }
  if (error instanceof DOMException && error.name === 'NetworkError') return true
  const message = String(error)
  return /GATT Server is disconnected|Cannot retrieve services/i.test(message)
}
type V2Completion = {
  opcode: number
  resolve: (payload: Uint8Array) => void
  reject: (error: Error) => void
  timer: number
}

const v2StatusLabels: Record<number, string> = {
  [StatusV2.UNKNOWN_COMMAND]: '当前设备不支持这个功能',
  [StatusV2.UNSUPPORTED]: '当前设备不支持这个功能',
  [StatusV2.INVALID_REQUEST]: '请求参数无效',
  [StatusV2.INVALID_STATE]: '设备当前状态不允许此操作',
  [StatusV2.BUSY]: '设备正忙',
  [StatusV2.TIMEOUT]: '设备操作超时',
  [StatusV2.NOT_FOUND]: '未找到目标资源',
  [StatusV2.NO_SPACE]: '设备存储空间不足',
  [StatusV2.CHECKSUM_ERROR]: '数据校验失败',
  [StatusV2.INCOMPATIBLE]: '屏幕或资源参数不兼容',
  [StatusV2.IO_ERROR]: '设备读写失败',
  [StatusV2.PERSISTENCE_ERROR]: '设备持久化失败',
  [StatusV2.DENIED]: '设备拒绝了操作',
  [StatusV2.LOCKED]: '设备功能已锁定',
  [StatusV2.CANCELLED]: '操作已取消',
  [StatusV2.INTERNAL_ERROR]: '设备内部错误',
  [StatusV2.RETRY_REQUIRED]: '需要重新发送',
  [StatusV2.LOW_POWER]: '设备电量过低，已拒绝当前受保护的写入或更新操作',
}

export class ProtocolV2Error extends Error {
  constructor(readonly status: number, message = v2StatusLabels[status] || `设备返回错误 0x${status.toString(16).padStart(2, '0')}`) {
    super(message)
    this.name = 'ProtocolV2Error'
  }
}

export function isOptionalFindProbeUnavailable(error: unknown) {
  const unavailableStatuses: readonly number[] = [
    StatusV2.UNKNOWN_COMMAND,
    StatusV2.UNSUPPORTED,
    StatusV2.INVALID_STATE,
    StatusV2.DENIED,
    StatusV2.LOCKED,
  ]
  return error instanceof ProtocolV2Error && unavailableStatuses.includes(error.status)
}

const displayBusyDetail = '设备正在执行之前的屏幕任务。本次操作没有排队；可以保持当前任务，或安全取消后只重试最新操作。'

type PendingDisplayRetry = {
  action: DeviceAction
  run: () => Promise<unknown>
}

let pendingDisplayRetry: PendingDisplayRetry | null = null

function retainDisplayBusyConflict(action: DeviceAction, error: unknown, run: () => Promise<unknown>) {
  const busy = (error instanceof ProtocolV2Error && error.status === StatusV2.BUSY) ||
    String(error).includes('epd:busy') || String(error).includes('设备正忙')
  if (busy) {
    pendingDisplayRetry = { action, run }
    useDeviceStore.getState().setAction(action, null, displayBusyDetail)
  } else {
    pendingDisplayRetry = null
  }
  return busy
}

function retainDisplayFailure(action: DeviceAction, error: unknown, run: () => Promise<unknown>) {
  if (retainDisplayBusyConflict(action, error, run)) return
  const detail = error instanceof Error ? error.message : String(error)
  useDeviceStore.getState().failAction(action, detail, run)
}

function fieldsFrom(message: string) {
  return Object.fromEntries([...message.matchAll(/([a-zA-Z_]+)=([^\s]+)/g)].map((match) => [match[1], match[2]]))
}

function numberField(fields: Record<string, string>, key: string) {
  return Number(fields[key] || 0)
}

function toImageStoreInfo(fields: Record<string, string>): ImageStoreInfo {
  const size = (fields.size || '0x0').split('x').map(Number)
  const compatible = numberField(fields, 'compatible')
  return {
    width: size[0] || 0, height: size[1] || 0, bytesPerImage: numberField(fields, 'bytes'),
    requiredSectors: numberField(fields, 'need'), totalSectors: numberField(fields, 'total'),
    freeSectors: numberField(fields, 'free'), largestFreeRun: numberField(fields, 'largest'),
    fit: numberField(fields, 'fit'), contiguousFit: numberField(fields, 'contiguous'),
    saved: numberField(fields, 'saved'), compatible,
    playable: fields.playable === undefined ? compatible : numberField(fields, 'playable'),
    incompatible: numberField(fields, 'incompatible'), slideshow: fields.slideshow === '1',
    intervalSeconds: numberField(fields, 'interval'),
  }
}

function toImageStoreItem(fields: Record<string, string>): ImageStoreItem {
  const size = (fields.size || '0x0').split('x').map(Number)
  const contentTypes: ImageSlotContentType[] = ['image', 'schedule', 'wifi', 'todo', 'business', 'page']
  return {
    id: numberField(fields, 'id'), name: fields.name || 'image', width: size[0] || 0,
    height: size[1] || 0, bytes: numberField(fields, 'bytes'), format: numberField(fields, 'fmt'),
    segments: numberField(fields, 'seg'), createdAt: numberField(fields, 'created'),
    compatible: fields.compatible === '1',
    contentType: contentTypes[numberField(fields, 'type')] || 'image',
  }
}

export class InkTagClient {
  private device: BluetoothDevice | null = null
  private lastConnectedDevice: BluetoothDevice | null = null
  private server: BluetoothRemoteGATTServer | null = null
  private characteristic: BluetoothRemoteGATTCharacteristic | null = null
  private textDecoder = new TextDecoder()
  private writeQueue: Promise<void> = Promise.resolve()
  private imageMtu = 22
  private imageReadyResolver: ((ready: boolean) => void) | null = null
  private storeResolver: ((reply: StoreReply) => void) | null = null
  private stopResolver: ((stopped: boolean) => void) | null = null
  private workResolver: ((reply: StoreReply) => void) | null = null
  private workAction: 'device-info' | 'clear' | 'calendar' | 'clock' | 'picture' | 'slideshow-starting' | null = null
  private nfcConfigResolver: ((reply: StoreReply) => void) | null = null
  private settingsResolver: ((reply: StoreReply) => void) | null = null
  private deviceInfoQrResolver: ((reply: StoreReply) => void) | null = null
  private screenResolver: ((reply: ScreenReply) => void) | null = null
  private expectedScreen: ScreenConfig | null = null
  private runtimeImageInfo: RuntimeImageInfo | null = null
  private transferGeneration = 0
  private connectionGeneration = 0
  private resourceReadAbort = new AbortController()
  private resourceReadSequence = 0
  private connectionAttempt: Promise<void> | null = null
  private interactiveConnectionAbort: AbortController | null = null
  private disconnectRequested = false
  private otaRecoveryType: DeviceCapabilities['ota'] | null = null
  private dialogSafeResume = false
  private nordicSafeResume = false
  private verifiedRecoveryName = ''
  private dialogOriginalImage: { deviceId: string; image: RuntimeImageInfo } | null = null
  private expectedFixedRecovery: ExpectedFixedRecovery | null = null
  private recoveryHandoffDevice: BluetoothDevice | null = null
  private protocolV2 = false
  private v2Transaction = 0
  private v2WritePayload = 20
  private v2Capabilities = 0
  private v2CapabilityExtensions = 0
  private v2WeekStart = 1
  private detailsIncomplete = false
  private v2Pending = new Map<number, V2Pending>()
  private v2RequestQueue: Promise<void> = Promise.resolve()
  private v2Completions = new Map<number, V2Completion>()
  passwordStatus: DevicePasswordStatus | null = null
  private passwordAttemptAfter = 0
  private passwordOperation = false
  private fontOperation = false
  private passwordDetailsGeneration = -1
  private passwordBootstrap: Promise<void> = Promise.resolve()
  private rejectedPasswordKeys = new Map<string, string>()
  private activePasswordCredential: Uint8Array | null = null

  private assertPasswordConnection(generation: number) {
    if (generation !== this.connectionGeneration || this.disconnectRequested) {
      throw new Error('连接已改变，请重新验证设备密码。')
    }
  }
  private rememberPasswordCredential(id: string, key: Uint8Array | null, remember = false) {
    this.activePasswordCredential = key ? Uint8Array.from(key) : null
    if (!savePasswordCredential(id, remember ? key : null)) {
      useDeviceStore.getState().addLog(remember ? '本机无法保存控制凭据，下次连接需重新输入密码。' : '无法清除本机已保存的控制凭据，请检查浏览器存储权限。', 'warning')
    }
  }

  passwordSupported() { return Boolean(this.v2Capabilities & CapabilityV2.DEVICE_PASSWORD) }
  private passwordNotify(prompt = false) {
    if (typeof window.dispatchEvent === 'function') window.dispatchEvent(new CustomEvent('inktag-password-state', { detail: { prompt } }))
  }
  private passwordId() {
    const id = useDeviceStore.getState().activation.activationId ?? ''
    if (!/^[0-9a-fA-F]{16}$/.test(id) || /^0+$/.test(id)) throw new Error('无法读取设备标识。')
    return id.toUpperCase()
  }
  async readPasswordStatus() {
    const generation = this.connectionGeneration
    const status = this.passwordSupported() ? decodeDevicePasswordStatus((await this.requestV2(CommandV2.GET_DEVICE_PASSWORD_STATUS)).payload) : null
    this.assertPasswordConnection(generation)
    this.passwordStatus = status
    this.passwordAttemptAfter = Math.max(this.passwordAttemptAfter, Date.now() + (this.passwordStatus?.retryAfterMs ?? 0))
    this.passwordNotify()
    return this.passwordStatus
  }
  private async provePassword(key: Uint8Array, purpose = 0, bundle?: Uint8Array) {
    const generation = this.connectionGeneration
    const id = this.passwordId()
    if (Date.now() < this.passwordAttemptAfter) throw new Error('操作过于频繁，请稍后再试。')
    this.passwordAttemptAfter = Date.now() + 1000
    if (purpose === 0 && this.passwordStatus) this.passwordStatus = { ...this.passwordStatus, authorized: false }
    const challenge = (await this.requestV2(CommandV2.GET_DEVICE_PASSWORD_CHALLENGE, new Uint8Array([purpose]))).payload
    if (challenge.length !== 22 || challenge[1] !== purpose) throw new Error('设备密码挑战不匹配。')
    this.assertPasswordConnection(generation)
    try {
      const proof = await createPasswordProof(id, key, challenge, bundle)
      this.assertPasswordConnection(generation)
      const reply = await this.requestV2(CommandV2.AUTHORIZE_DEVICE_PASSWORD, proof)
      this.assertPasswordConnection(generation)
      this.passwordStatus = decodeDevicePasswordStatus(reply.payload)
      if (!this.passwordStatus.authorized) throw new Error('密码验证未通过。')
      this.passwordNotify()
    } catch (error) {
      if (generation !== this.connectionGeneration || this.disconnectRequested) throw error
      if (purpose === 0 && this.passwordStatus) this.passwordStatus = { ...this.passwordStatus, authorized: false }
      // This catch covers AUTH only. A definite rejection may refresh the
      // device-wide cooldown; transport errors never cause a second proof.
      if (error instanceof ProtocolV2Error && error.status === StatusV2.DENIED) {
        try { await this.readPasswordStatus() } catch { /* Preserve the original failure. */ }
        if (purpose === 0 && this.passwordStatus) this.passwordStatus = { ...this.passwordStatus, authorized: false }
      }
      throw error
    }
  }
  async authenticateDevicePassword(password?: string, credential?: Uint8Array, remember = true) {
    if (this.passwordOperation) throw new Error('密码操作正在进行。')
    this.passwordOperation = true
    const generation = this.connectionGeneration
    try {
      const status = await this.readPasswordStatus()
      if (!status?.enabled) return
      const id = this.passwordId()
      // A retained physical link is not proof that this browser/client owns V.
      this.passwordStatus = { ...status, authorized: false }
      const key = credential ?? await derivePasswordCredential(id, password ?? '')
      this.assertPasswordConnection(generation)
      await this.provePassword(key)
      this.assertPasswordConnection(generation)
      this.rememberPasswordCredential(id, key, remember)
      this.rejectedPasswordKeys.delete(id)
      useDeviceStore.getState().setConnection('connected')
    } finally { this.passwordOperation = false }
    this.assertPasswordConnection(generation)
    await this.finishPasswordConnection().catch((error) => {
      useDeviceStore.getState().addLog(`设备详情读取未完成，基础连接仍可用：${error instanceof Error ? error.message : String(error)}`, 'warning')
    })
  }
  async showPasswordDeviceInfo() { await this.requestV2(CommandV2.SHOW_PASSWORD_DEVICE_INFO) }
  async waitForPasswordAuthorization() {
    await this.passwordBootstrap
    if (this.passwordSupported() && (!this.passwordStatus || (this.passwordStatus.enabled && !this.passwordStatus.authorized))) {
      this.passwordNotify(true)
      throw new Error('请先验证设备密码。')
    }
  }
  private automaticClockSyncSuppression = 0

  async withoutAutomaticClockSync<T>(operation: () => Promise<T>): Promise<T> {
    this.automaticClockSyncSuppression++
    try { return await operation() }
    finally { this.automaticClockSyncSuppression-- }
  }

  private async finishPasswordConnection() {
    if (this.passwordStatus?.enabled && !this.passwordStatus.authorized) return
    const generation = this.connectionGeneration
    if (this.passwordDetailsGeneration === generation) return
    this.passwordDetailsGeneration = generation
    if (useDeviceStore.getState().action !== 'ota' && this.automaticClockSyncSuppression === 0) {
      void this.syncTime().catch(() => useDeviceStore.getState().addLog('后台校时未完成，连接仍可用。', 'warning'))
    }
    await this.initializeV2Details(this.v2Capabilities)
    if (generation === this.connectionGeneration) await this.getDisplaySettings().catch(() => { /* Unknown visibility defaults to showing the mark. */ })
  }
  async configureDevicePassword(newPassword: string | null, oldPassword?: string) {
    if (this.passwordOperation) throw new Error('密码操作正在进行。')
    this.passwordOperation = true
    const generation = this.connectionGeneration
    try {
      const status = await this.readPasswordStatus()
      if (!status) throw new Error('当前固件不支持设备密码。')
      if (status.enabled && !oldPassword) throw new Error('请输入原密码。')
      if (status.enabled && !status.authorized) {
        const oldCredential = await derivePasswordCredential(this.passwordId(), oldPassword!)
        this.assertPasswordConnection(generation)
        await this.provePassword(oldCredential)
        await sleep(1050)
      }
      const id = this.passwordId()
      const key = newPassword === null ? null : await derivePasswordCredential(id, newPassword)
      const changeKey = newPassword === null ? null : await derivePasswordChangeKey(id, newPassword)
      const name = useDeviceStore.getState().deviceName
      const qr = packQrMatrix(createQrMatrix(key ? buildPasswordQrLink(name, id, key) : buildInkTagDeviceUrl(name), 33, 'L'))
      const revision = status.enabled ? status.revision + 1 : 1
      const bundle = encodePasswordBundle(key, qr.size, qr.data, changeKey, revision)
      this.assertPasswordConnection(generation)
      if (status.enabled) {
        if (Date.now() < this.passwordAttemptAfter) await sleep(this.passwordAttemptAfter - Date.now())
        const oldChangeKey = await derivePasswordChangeKey(id, oldPassword!)
        this.assertPasswordConnection(generation)
        await this.provePassword(oldChangeKey, key ? 1 : 2, bundle)
      }
      this.assertPasswordConnection(generation)
      const begin = new Uint8Array(20)
      const view = new DataView(begin.buffer)
      begin.set([TransferObjectV2.DEVICE_PASSWORD_BUNDLE, 0, 1, 0])
      view.setUint32(4, bundle.length, true)
      view.setUint32(8, (crc32Bytes(bundle) ^ 0xffffffff) >>> 0, true)
      view.setUint32(12, bundle.length, true)
      const transaction = this.nextV2Transaction()
      let active = false
      let commitSent = false
      try {
        const reply = (await this.requestV2(CommandV2.TRANSFER_BEGIN, begin, RequestFlagV2.NONE, 15000, transaction)).payload
        active = true
        const chunk = Math.min(this.v2WritePayload, reply.length >= 4 ? new DataView(reply.buffer, reply.byteOffset, reply.byteLength).getUint16(2, true) : this.v2WritePayload)
        if (chunk < 1) throw new Error('设备数据帧大小无效。')
        for (let offset = 0; offset < bundle.length; offset += chunk) {
          this.assertPasswordConnection(generation)
          await this.writeV2TransferChunk(transaction, TransferObjectV2.DEVICE_PASSWORD_BUNDLE, bundle.length, offset, bundle.subarray(offset, offset + chunk))
        }
        commitSent = true
        this.assertPasswordConnection(generation)
        await this.requestV2(CommandV2.TRANSFER_COMMIT, new Uint8Array(), RequestFlagV2.NONE, 15000, transaction)
        active = false
        const confirmed = await this.readPasswordStatus()
        if (!confirmed || confirmed.enabled !== Boolean(key) || (key && confirmed.revision !== revision)) throw new Error('密码保存尚未确认，请使用新密码重新连接验证。')
        if (key) {
          if (Date.now() < this.passwordAttemptAfter) await sleep(this.passwordAttemptAfter - Date.now())
          await this.provePassword(key)
        }
        this.assertPasswordConnection(generation)
        this.rememberPasswordCredential(id, key)
        this.rejectedPasswordKeys.delete(id)
      } catch (error) {
        if (active && !commitSent && generation === this.connectionGeneration && !this.disconnectRequested) await this.requestV2(CommandV2.TRANSFER_ABORT, new Uint8Array(), RequestFlagV2.NONE, 5000, transaction).catch(() => undefined)
        throw error
      }
    } finally { this.passwordOperation = false; this.passwordNotify() }
  }

  async connect(discovery: BluetoothDiscoveryOptions = {}) {
    const store = useDeviceStore.getState()
    if (store.action === 'ota') {
      store.addLog('固件升级正在进行，请等待升级与应用验身完成后再连接设备。', 'warning')
      return
    }
    if (!navigator.bluetooth) {
      store.setConnection('unsupported')
      return
    }
    let selected = false
    try {
      store.setConnection('selecting')
      const device = await navigator.bluetooth.requestDevice(buildBluetoothRequestOptions(OPTIONAL_SERVICES, discovery))
      selected = true
      await this.connectInteractiveDevice(device)
    } catch (error) {
      if (error instanceof DOMException && error.name === 'AbortError' && !this.disconnectRequested) return
      if (this.disconnectRequested) {
        store.setConnection('idle')
        return
      }
      const cancelled = !selected && error instanceof DOMException && error.name === 'NotFoundError'
      store.setConnection(cancelled ? 'idle' : 'error')
      store.addLog(cancelled ? '已取消设备选择' : `连接失败：${String(error)}`, cancelled ? 'info' : 'error')
    }
  }

  async selectRecoveryForOta(handoff: { deviceId: string; recoveryName: string }) {
    const store = useDeviceStore.getState()
    const expected = this.expectedFixedRecovery
    if (store.action !== 'idle' || this.device || !expected ||
        expected.deviceId !== handoff.deviceId || expected.name !== handoff.recoveryName) {
      throw new Error('原设备的 Recovery 交接已变化，请重新确认设备')
    }
    if (!navigator.bluetooth) throw new Error('当前浏览器不支持蓝牙扫描')
    const generation = this.connectionGeneration
    store.setAction('ota', 0, '正在选择 Recovery 设备')
    try {
      // Nordic advertisements can be incomplete in WinRT. The grant and
      // live role checks still apply whichever chooser filter is used.
      const device = await navigator.bluetooth.requestDevice(expected.type === 'nordic'
        ? { acceptAllDevices: true, optionalServices: OPTIONAL_SERVICES }
        : { filters: [{ name: expected.name }, { name: expected.legacyName }], optionalServices: OPTIONAL_SERVICES })
      if (device.id !== expected.deviceId || generation !== this.connectionGeneration ||
          this.expectedFixedRecovery !== expected) throw new Error('所选设备与原设备不一致')
      await this.connectInteractiveDevice(device, false, true)
      if (this.device !== device || this.otaRecoveryType !== expected.type) {
        throw new Error('未读到原设备的 Recovery 状态')
      }
    } finally { useDeviceStore.getState().setAction('idle') }
  }

  async getKnownDevices() {
    if (!navigator.bluetooth) return []
    const bluetooth = navigator.bluetooth as Bluetooth & { getDevices?: () => Promise<BluetoothDevice[]> }
    return bluetooth.getDevices ? bluetooth.getDevices() : []
  }

  getLastConnectedDevice() {
    return this.lastConnectedDevice
  }

  canRestoreKnownDevices() {
    if (!navigator.bluetooth) return false
    const bluetooth = navigator.bluetooth as Bluetooth & { getDevices?: () => Promise<BluetoothDevice[]> }
    return typeof bluetooth.getDevices === 'function'
  }

  async connectKnownDevice(device: BluetoothDevice) {
    const store = useDeviceStore.getState()
    if (store.action === 'ota') {
      store.addLog('固件升级正在进行，请等待升级与应用验身完成后再连接设备。', 'warning')
      return
    }
    try {
      await this.connectInteractiveDevice(device, true)
    } catch (error) {
      if (error instanceof DOMException && error.name === 'AbortError' && !this.disconnectRequested) return
      if (this.disconnectRequested) {
        store.setConnection('idle')
        return
      }
      store.setConnection('error')
      const staleGrant = isBluetoothDeviceOutOfRange(error) || (error instanceof BluetoothConnectionTimeoutError
        && (error.phase === '建立 GATT 连接' || error.phase === '读取 InkTag 服务'))
      store.addLog(staleGrant
        ? `缓存设备 ${device.name || device.id} 多次连接未成功；浏览器记住设备不代表它当前在线，请检查广播及其他连接占用，或重新选择附近设备（${String(error)}）`
        : `连接失败：${String(error)}`, 'error')
    }
  }

  async reconnectDevice(device: BluetoothDevice, timeoutMs = CONNECTION_TIMEOUT_MS, requireRecovery = false) {
    await this.connectDevice(device, timeoutMs, false, requireRecovery)
  }

  private async connectInteractiveDevice(device: BluetoothDevice, remembered = false, requireRecovery = false) {
    this.interactiveConnectionAbort?.abort()
    const connection = new AbortController()
    this.interactiveConnectionAbort = connection
    this.disconnectRequested = false
    const retryDelays = remembered ? KNOWN_DEVICE_RETRY_DELAYS_MS : GATT_BOOTSTRAP_RETRY_DELAYS_MS
    let rediscoveryAttempted = false
    try {
      for (let attempt = 0; ; attempt++) {
        if (connection.signal.aborted) throw new DOMException('蓝牙连接已取消', 'AbortError')
        try {
          await this.connectDevice(device, CONNECTION_TIMEOUT_MS, true, requireRecovery)
          if (connection.signal.aborted) throw new DOMException('蓝牙连接已取消', 'AbortError')
          return
        } catch (error) {
          const retryDelay = retryDelays[attempt]
          if (connection.signal.aborted) throw new DOMException('蓝牙连接已取消', 'AbortError')
          if (this.disconnectRequested || retryDelay == null || !isTransientGattBootstrapError(error)) throw error
          const store = useDeviceStore.getState()
          store.setConnection('connecting')
          if (remembered && !rediscoveryAttempted && isBluetoothDeviceOutOfRange(error)) {
            rediscoveryAttempted = true
            store.addLog('浏览器尚未发现缓存设备，等待其广播（最多 15 秒）')
            try {
              const seen = await waitForDeviceAdvertisement(device, KNOWN_DEVICE_ADVERTISEMENT_TIMEOUT_MS, connection.signal)
              store.addLog(seen ? '已重新发现设备广播，准备重连' : '未获得新广播或浏览器不支持监听，继续有界重试', seen ? 'info' : 'warning')
            } catch (scanError) {
              if (connection.signal.aborted) throw scanError
              store.addLog(`广播监听暂不可用，继续有界重试：${String(scanError)}`, 'warning')
            }
          }
          store.addLog(`第 ${attempt + 1}/${retryDelays.length + 1} 次 GATT 建连未就绪，${retryDelay / 1000} 秒后自动重试`, 'warning')
          await waitForBluetoothRetry(retryDelay, connection.signal)
        }
      }
    } finally {
      connection.abort()
      if (this.interactiveConnectionAbort === connection) this.interactiveConnectionAbort = null
    }
  }

  private async connectDevice(device: BluetoothDevice, timeoutMs = CONNECTION_TIMEOUT_MS, allowCachedTelinkName = false, requireRecovery = false) {
    // A timed-out GR native promise can outlive the UI action. Do not let an
    // ordinary reconnect bypass the adapter's exact-grant quarantine. This
    // synchronous check does not connect, discover or touch other grants.
    if (typeof window !== 'undefined') window.GoodixOta?.assertDeviceAvailable?.(device)
    if (typeof window !== 'undefined') window.TelinkOta?.assertDeviceAvailable?.(device)
    if (this.connectionAttempt) return this.connectionAttempt
    const generation = ++this.connectionGeneration
    const attempt = this.connectDeviceOnce(device, generation, timeoutMs, allowCachedTelinkName, requireRecovery)
    this.connectionAttempt = attempt
    try {
      await attempt
    } finally {
      if (this.connectionAttempt === attempt) this.connectionAttempt = null
    }
  }

  private async connectDeviceOnce(device: BluetoothDevice, generation: number, timeoutMs: number, allowCachedTelinkName: boolean, requireRecovery: boolean) {
    if (typeof window !== 'undefined') window.TelinkOta?.cancelRecoveryDiscovery?.(device)
    const store = useDeviceStore.getState()
    const deadline = Date.now() + timeoutMs
    const assertAttempt = () => {
      if (generation !== this.connectionGeneration || this.disconnectRequested) {
        throw new DOMException('蓝牙连接已取消', 'AbortError')
      }
    }
    const connectionStep = async <T>(operation: Promise<T>, phase: string, phaseTimeoutMs?: number) => {
      const startedAt = performance.now()
      const remaining = phaseTimeoutMs ?? (deadline - Date.now())
      try {
        const value = await withConnectionTimeout(operation, remaining, phase, () => {
          if (generation === this.connectionGeneration && device.gatt?.connected) device.gatt.disconnect()
        })
        assertAttempt()
        store.addLog(`连接阶段完成：${phase}（${Math.max(0, Math.round(performance.now() - startedAt))} ms）`)
        return value
      } catch (error) {
        const cancelled = generation !== this.connectionGeneration || this.disconnectRequested
        store.addLog(
          `连接阶段${cancelled ? '已取消' : '失败'}：${phase}（${Math.max(0, Math.round(performance.now() - startedAt))} ms）`,
          cancelled ? 'info' : 'error',
        )
        throw error
      }
    }
    this.resetImageTransport()
    this.disconnectRequested = false
    this.resourceReadAbort.abort()
    this.resourceReadAbort = new AbortController()
    this.otaRecoveryType = null
    this.device = device
    this.dialogSafeResume = false
    this.nordicSafeResume = false
    this.verifiedRecoveryName = ''
    try {
      const preference = await connectionStep(db.devicePreferences.get(device.id), '读取设备缓存')
      const bleAddress = /^[0-9a-f]{2}(?::[0-9a-f]{2}){5}$/i.test(device.id) ? device.id.toUpperCase() : ''
      store.setIdentity({
        deviceId: device.id,
        bleAddress,
        deviceName: device.name || 'InkTag',
        localAlias: preference?.localAlias || '',
      })
      store.setConnection('connecting')
      store.addLog(`连接 ${device.name || device.id}`)
      device.removeEventListener('gattserverdisconnected', this.onDisconnected)
      device.addEventListener('gattserverdisconnected', this.onDisconnected)
      if (!device.gatt) throw new Error('设备没有可用的 GATT 服务')
      const gattConnection = device.gatt.connect()
      // Web Bluetooth cannot cancel a pending connect().  WinRT may resolve it
      // after our timeout or after the user pressed Disconnect.  Close that
      // late server immediately or the peripheral remains non-connectable even
      // though the UI has already abandoned this generation.
      void gattConnection.then((lateServer) => {
        const reusedByNewerAttempt = generation !== this.connectionGeneration &&
          this.device === device && !this.disconnectRequested
        if ((generation !== this.connectionGeneration || this.disconnectRequested) &&
            !reusedByNewerAttempt && lateServer.connected) lateServer.disconnect()
      }).catch(() => undefined)
      const server = await connectionStep(gattConnection, '建立 GATT 连接')
      this.server = server
      // A browser may keep BluetoothDevice.name cached across an App -> Recovery
      // reboot. Probe the product service first, then authorize Recovery by its
      // stable name or by the same previously verified browser device grant.
      let service: BluetoothRemoteGATTService | null = null
      let applicationServiceError: unknown
      try {
        service = await connectionStep(
          server.getPrimaryService(SERVICE_UUID), '读取 InkTag 服务',
          Math.min(GATT_SERVICE_DISCOVERY_TIMEOUT_MS, Math.max(1, deadline - Date.now())),
        )
      } catch (error) {
        // Only a successful service lookup reporting absence proves that this
        // is not an App. A cached _OTA name cannot turn a transport failure,
        // permission refusal or timeout into evidence of a Recovery role.
        if (!(error instanceof DOMException) || error.name !== 'NotFoundError') throw error
        assertAttempt()
        applicationServiceError = error
      }
      if (!service) {
        const name = device.name || ''
        const namedRecovery = fixedRecoveryFromName(name)
        const expected = this.expectedFixedRecovery ?? rememberedOnMicroRecovery(device, preference)
        const cachedSameDevice = expected?.deviceId === device.id
        let recovery = namedRecovery ?? (cachedSameDevice ? expected : null)
        // TLSR retains its browser grant across modes. A cached factory App name
        // selects a read-only probe on this same GATT link, even on a fresh origin.
        // The live Recovery profile below still determines the role.
        if (allowCachedTelinkName && !recovery && !expected && /^(?:IKT|InkTag)_TL_[0-9A-F]{4}$/i.test(name)) {
          recovery = fixedRecoveryFromName(`${name}_OTA`)
        }
        if (!recovery) throw applicationServiceError
        if (expected && (!cachedSameDevice || (namedRecovery &&
          namedRecovery.name !== expected.name && namedRecovery.name !== expected.legacyName))) {
          throw new Error(`Recovery 身份不匹配；期望 ${expected.name}，实际 ${device.name || device.id}`)
        }
        const label = fixedRecoveryLabel(recovery.type)
        await connectionStep(server.getPrimaryService(fixedRecoveryService(recovery.type)), `读取 ${label} Recovery 服务`)
        if (recovery.type === 'goodix') {
          const ota = await connectionStep(import('./ota'), '加载 Recovery 验证器')
          await connectionStep(ota.verifyGoodixRecovery(server), '确认 GR 固定 Recovery 角色')
        } else if (recovery.type === 'telink') {
          const ota = await connectionStep(import('./ota'), '加载 Recovery 验证器')
          await connectionStep(ota.verifyTelinkRecovery(server), '确认 TLSR 固定 Recovery 角色')
        } else if (recovery.type === 'onmicro') {
          const ota = await connectionStep(import('./ota'), '加载 Recovery 验证器')
          await connectionStep(ota.verifyOnMicroRecovery(server), '确认 OM 固定 Recovery 角色')
        } else if (recovery.type === 'nordic') {
          const service = await connectionStep(server.getPrimaryService(fixedRecoveryService('nordic')), '确认 nRF DFU 服务')
          await connectionStep(service.getCharacteristic('8ec90001-f315-4f60-9fb8-838830daea50'), '确认 nRF DFU 控制通道')
          await connectionStep(service.getCharacteristic('8ec90002-f315-4f60-9fb8-838830daea50'), '确认 nRF DFU 数据通道')
          const ota = await connectionStep(import('./ota'), '加载 Recovery 验证器')
          const proof = await connectionStep(ota.verifyNordicRecovery(server), '读取 nRF Recovery 退出能力')
          this.nordicSafeResume = proof.safeResume
        } else if (recovery.type === 'dialog') {
          const ota = await connectionStep(import('./ota'), '加载 Recovery 验证器')
          const proof = await connectionStep(ota.verifyDialogRecovery(server), '读取 DA Recovery 退出能力')
          this.dialogSafeResume = proof.safeResume
        }
        this.verifiedRecoveryName = recovery.name
        this.otaRecoveryType = recovery.type
        this.protocolV2 = false
        this.characteristic = null
        this.runtimeImageInfo = null
        store.resetDevice(store.action === 'ota')
        store.setCapabilities({ ota: recovery.type })
        store.setIdentity({ deviceId: device.id, bleAddress,
          deviceName: device.name || recovery.name, localAlias: preference?.localAlias || '',
          firmwareVersion: 'Recovery', protocolVersion: null })
        this.lastConnectedDevice = device
        store.setConnection('connected')
        store.addLog(`已连接 ${label} 固定 Recovery，可重新发送完整应用`, 'success')
        return
      }
      if (requireRecovery) throw new Error('设备已返回应用，请重新确认升级')
      this.expectedFixedRecovery = null
      this.recoveryHandoffDevice = null
      this.characteristic = await connectionStep(service.getCharacteristic(COMMAND_UUID), '读取命令通道')
      const versionCharacteristic = await connectionStep(service.getCharacteristic(VERSION_UUID), '读取版本通道')
      const version = await connectionStep(versionCharacteristic.readValue(), '读取固件版本')
      if (version.byteLength < 4) {
        throw new Error('固件版本响应不完整；请先使用当前 InkTag 固件进行完整刷机')
      }
      const protocolByte = version.getUint8(0)
      if (protocolByte !== PROTOCOL_V2_VERSION) {
        throw new Error(`不支持的旧版固件协议 0x${protocolByte.toString(16)}；请先使用当前 InkTag 固件进行完整刷机`)
      }
      this.protocolV2 = true
      store.setIdentity({
        protocolVersion: protocolByte,
        firmwareVersion: Array.from({ length: version.byteLength >= 5 ? 4 : 3 }, (_, i) => version.getUint8(i + 1)).join('.'),
      })
      await connectionStep(this.characteristic.startNotifications(), '开启设备通知')
      this.characteristic.addEventListener('characteristicvaluechanged', this.onNotification)
      store.addLog('设备已连接，通知已开启', 'success')
      await connectionStep(this.initializeV2Core(), '读取 v1 基本信息', 20000)
      this.lastConnectedDevice = device
      store.setConnection(this.passwordSupported() ? 'connecting' : 'connected')
      store.addLog('设备基本信息已读取，正在完成连接', 'success')
      this.passwordBootstrap = (async () => {
        if (this.passwordSupported()) {
          await this.readActivationStatus(false)
          this.assertPasswordConnection(generation)
          const status = await this.readPasswordStatus()
          this.assertPasswordConnection(generation)
          if (status?.enabled) {
            this.passwordStatus = { ...status, authorized: false }
            const key = takePasswordQrCredential(this.passwordId()) ?? getPasswordCredential(this.passwordId())
            if (!key) { this.passwordNotify(true); return }
            if (status.retryAfterMs > 5000) { this.passwordNotify(true); return }
            if (status.retryAfterMs) await sleep(status.retryAfterMs + 50)
            this.assertPasswordConnection(generation)
            const signature = Array.from(key).join(',')
            if (this.rejectedPasswordKeys.get(this.passwordId()) === signature) { this.passwordNotify(true); return }
            try { await this.authenticateDevicePassword(undefined, key) }
            catch (error) {
              if (generation === this.connectionGeneration && !this.disconnectRequested) this.rejectedPasswordKeys.set(this.passwordId(), signature)
              throw error
            }
            return
          }
        }
        this.assertPasswordConnection(generation)
        store.setConnection('connected')
        await this.finishPasswordConnection()
      })().then(() => {
        if (generation !== this.connectionGeneration || this.disconnectRequested) return
        store.addLog(this.passwordStatus?.enabled && !this.passwordStatus.authorized ? '设备等待密码验证'
          : this.detailsIncomplete ? '设备已连接，部分信息读取失败，请查看日志' : '连接详情已就绪', this.detailsIncomplete ? 'warning' : 'success')
      }).catch((error) => {
        if (generation !== this.connectionGeneration || this.disconnectRequested) return
        this.passwordNotify(Boolean(this.passwordStatus?.enabled && !this.passwordStatus.authorized))
        store.addLog(`连接详情暂不可用${this.passwordStatus?.enabled && !this.passwordStatus.authorized ? '，请验证设备密码' : '，基础连接仍可用'}：${error instanceof Error ? error.message : String(error)}`, 'warning')
      })
    } catch (error) {
      // A cancelled attempt must not remove the new attempt's listener or
      // disconnect its link when the browser reuses the same device grant.
      if (generation === this.connectionGeneration || this.device !== device) {
        device.removeEventListener('gattserverdisconnected', this.onDisconnected)
        if (device.gatt?.connected) device.gatt.disconnect()
      }
      if (generation === this.connectionGeneration) {
        this.characteristic?.removeEventListener('characteristicvaluechanged', this.onNotification)
        this.characteristic = null
        this.server = null
        this.device = null
        this.resetImageTransport()
        const connection = useDeviceStore.getState().connection
        if (connection === 'connecting' || connection === 'connected') {
          store.resetDevice(store.action === 'ota')
        }
      }
      throw error
    }
  }

  disconnect() {
    this.disconnectSession(false)
  }

  private disconnectSession(preserveHandoff: boolean) {
    this.disconnectRequested = true
    this.interactiveConnectionAbort?.abort()
    this.resourceReadAbort.abort()
    if (!preserveHandoff) {
      this.expectedFixedRecovery = null
      this.recoveryHandoffDevice = null
    }
    this.connectionGeneration++
    this.connectionAttempt = null
    const device = this.device
    this.characteristic?.removeEventListener('characteristicvaluechanged', this.onNotification)
    this.resetImageTransport()
    if (device?.gatt?.connected) {
      useDeviceStore.getState().addLog('正在断开设备，等待浏览器释放 GATT')
      device.gatt.disconnect()
      // Chromium normally dispatches gattserverdisconnected synchronously.
      // Some WinRT paths only flip `connected`; finish locally in that case,
      // but never claim disconnected while the browser still owns the link.
      if (!device.gatt.connected) this.finishDisconnectedDevice(device)
      else void this.finishDisconnectWhenReleased(device, this.connectionGeneration)
    } else {
      this.finishDisconnectedDevice(device ?? undefined)
    }
  }

  async disconnectAndWait(settleMs = 300) {
    await this.disconnectSessionAndWait(settleMs, false)
  }

  async releaseForRecovery(expected: { deviceId: string; recoveryName: string }, settleMs = 900) {
    const sameHandoff = () => {
      const current = this.getExpectedRecoveryIdentity()
      return current?.deviceId === expected.deviceId && current.recoveryName === expected.recoveryName
    }
    if (!sameHandoff() || (this.device && this.device.id !== expected.deviceId)) {
      throw new Error('Recovery 交接身份已变化，已停止释放连接')
    }
    await this.disconnectSessionAndWait(settleMs, true)
    if (!sameHandoff()) throw new Error('Recovery 交接已取消；未继续读取或传输')
  }

  private async disconnectSessionAndWait(settleMs: number, preserveHandoff: boolean) {
    const device = this.device
    this.disconnectSession(preserveHandoff)
    if (device?.gatt?.connected) {
      for (let poll = 0; poll < 40 && device.gatt.connected; poll++) await sleep(50)
      if (device.gatt.connected) throw new Error('浏览器尚未释放当前蓝牙连接，请稍后重试')
    }
    // WinRT releases a GATT session asynchronously even after `connected`
    // flips to false. Allow its native session to settle before opening
    // another chooser or connection on the same adapter.
    if (settleMs > 0) await sleep(settleMs)
  }

  private async finishDisconnectWhenReleased(device: BluetoothDevice, generation: number) {
    for (let poll = 0; poll < 40; poll++) {
      await sleep(50)
      if (generation !== this.connectionGeneration) return
      if (!device.gatt?.connected) {
        this.finishDisconnectedDevice(device)
        return
      }
    }
    useDeviceStore.getState().addLog('浏览器仍在释放 GATT 连接，请稍候后再重连', 'warning')
  }

  getGattServer() { return this.server }
  getDevice() { return this.device }
  getConnectionGeneration() { return this.connectionGeneration }

  // Transfer ownership of a live, already verified GR Recovery link. This is
  // not a disconnect or a role change; its caller must release the adapter's
  // opaque session. Ordinary App AUTH/notifications are never detached here.
  takeGoodixRecoveryForOta(expectedDeviceId: string) {
    const device = this.device
    const proof = fixedRecoveryFromName(this.verifiedRecoveryName)
    if (!device || device.id !== expectedDeviceId || !this.server?.connected ||
        this.otaRecoveryType !== 'goodix' || proof?.type !== 'goodix' ||
        this.protocolV2 || this.characteristic) {
      throw new Error('请先连接并核对同一台 GR Recovery')
    }
    const previous = this.expectedFixedRecovery
    if (previous && (previous.deviceId !== device.id ||
        (previous.name !== proof.name && previous.legacyName !== proof.name))) {
      throw new Error('Recovery 交接身份已变化，已停止传输')
    }
    this.expectedFixedRecovery = { type: 'goodix', deviceId: device.id,
      name: proof.name, legacyName: proof.name.replace(/^IKT_/, 'InkTag_') }
    this.recoveryHandoffDevice = device
    this.resourceReadAbort.abort()
    device.removeEventListener('gattserverdisconnected', this.onDisconnected)
    this.connectionGeneration++
    this.connectionAttempt = null
    this.server = null
    this.device = null
    this.otaRecoveryType = null
    this.resetImageTransport()
    useDeviceStore.getState().resetDevice(true)
    return this.getExpectedRecoveryIdentity()!
  }
  // Give the TLSR adapter sole ownership of this already verified live link.
  // Do not disconnect/re-discover it, or retain an ordinary-client callback.
  takeTelinkRecoveryForOta(expectedDeviceId: string) {
    const device = this.device
    const proof = fixedRecoveryFromName(this.verifiedRecoveryName)
    if (!device || device.id !== expectedDeviceId || !this.server?.connected ||
        this.otaRecoveryType !== 'telink' || proof?.type !== 'telink' ||
        this.protocolV2 || this.characteristic) throw new Error('请先核对同一台 TLSR Recovery')
    const previous = this.expectedFixedRecovery
    if (previous && (previous.deviceId !== device.id ||
        (previous.name !== proof.name && previous.legacyName !== proof.name))) {
      throw new Error('TLSR Recovery 交接身份已变化')
    }
    this.expectedFixedRecovery = { type: 'telink', deviceId: device.id,
      name: proof.name, legacyName: proof.name.replace(/^IKT_/, 'InkTag_') }
    this.resourceReadAbort.abort()
    device.removeEventListener('gattserverdisconnected', this.onDisconnected)
    this.connectionGeneration++
    this.connectionAttempt = null
    this.server = null
    this.device = null
    this.otaRecoveryType = null
    this.resetImageTransport()
    useDeviceStore.getState().resetDevice(true)
    return this.getExpectedRecoveryIdentity()!
  }

  getOtaRecoveryType() { return this.otaRecoveryType }
  getRecoverySafeResumeSupported() {
    const type = this.getOtaRecoveryType()
    return ['goodix', 'telink', 'onmicro'].includes(type || '') ||
      (type === 'dialog' && this.dialogSafeResume)
      || (type === 'nordic' && this.nordicSafeResume)
  }
  getVerifiedRecoveryName() { return this.verifiedRecoveryName }
  getExpectedRecoveryIdentity() {
    const expected = this.expectedFixedRecovery
    return expected ? { deviceId: expected.deviceId, recoveryName: expected.name } : null
  }
  getRecoveryHandoffDevice() {
    return this.recoveryHandoffDevice?.id === this.expectedFixedRecovery?.deviceId
      ? this.recoveryHandoffDevice : null
  }
  getRecoveryOriginalImage() {
    const baseline = this.dialogOriginalImage
    return baseline && baseline.deviceId === this.device?.id ? baseline.image : null
  }
  getRuntimeImageInfo() { return this.runtimeImageInfo }

  async queryV2ApplicationIdentity() {
    if (!this.protocolV2) throw new Error('设备未启用当前 v1 协议')
    const payload = (await this.requestV2(CommandV2.GET_DEVICE_INFO)).payload
    if (payload.length < 23 || payload[0] !== 1) throw new Error('设备 v1 身份响应无效')
    const view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength)
    return { size: view.getUint32(15, true), checksum: view.getUint32(19, true) }
  }

  async queryRuntimeBuild(timeout = 6500) {
    if (!this.protocolV2) throw new Error('设备未启用当前 v1 协议，无法读取 Recovery 状态')
    const reply = await this.requestV2(CommandV2.GET_DEVICE_INFO, new Uint8Array(0), RequestFlagV2.NONE, timeout)
    return parseRuntimeBuildInfo(reply.payload)
  }

  private resetImageTransport() {
    this.rejectV2Transactions('蓝牙传输已重置')
    this.protocolV2 = false
    this.passwordStatus = null
    this.activePasswordCredential = null
    this.passwordDetailsGeneration = -1
    this.passwordNotify()
    this.runtimeImageInfo = null
    this.v2Transaction = 0
    this.v2WritePayload = 20
    this.v2Capabilities = 0
    this.v2CapabilityExtensions = 0
    this.v2WeekStart = 1
    this.writeQueue = Promise.resolve()
    this.v2RequestQueue = Promise.resolve()
    this.imageMtu = 22
    this.imageReadyResolver?.(false)
    this.imageReadyResolver = null
    this.storeResolver?.({ ok: false, error: 'transport-reset' })
    this.storeResolver = null
    this.stopResolver?.(false)
    this.stopResolver = null
    this.workResolver?.({ ok: false, error: 'transport-reset' })
    this.workResolver = null
    this.workAction = null
    this.nfcConfigResolver?.({ ok: false, error: 'transport-reset' })
    this.nfcConfigResolver = null
    this.settingsResolver?.({ ok: false, error: 'transport-reset' })
    this.settingsResolver = null
    this.deviceInfoQrResolver?.({ ok: false, error: 'transport-reset' })
    this.deviceInfoQrResolver = null
    this.screenResolver?.({ ok: false, error: 'transport-reset' })
    this.screenResolver = null
    this.expectedScreen = null
  }

  private async writePacket(packet: Uint8Array<ArrayBufferLike>, withResponse = true, quiet = false, current?: () => boolean, imageTiming?: ImageTimingSink) {
    const generation = this.connectionGeneration, characteristic = this.characteristic
    const device = this.device, server = this.server
    const queuedAt = imageTiming ? performance.now() : 0
    const run = async () => {
      if (!characteristic) throw new Error('设备未连接')
      if (generation !== this.connectionGeneration || characteristic !== this.characteristic ||
          device !== this.device || server !== this.server || this.disconnectRequested || (current && !current())) {
        throw new Error('设备连接已改变或事务已变化，未发送过期数据')
      }
      const store = useDeviceStore.getState()
      const wirePacket = new Uint8Array(packet.byteLength)
      wirePacket.set(packet)
      if (!quiet) {
        store.addLog(Array.from(wirePacket, (byte) => byte.toString(16).padStart(2, '0')).join(''), 'tx')
      }
      const writeAt = imageTiming ? performance.now() : 0
      if (imageTiming) observeImageTiming(imageTiming, 'writeQueue', writeAt - queuedAt)
      try {
        if (withResponse) await characteristic.writeValueWithResponse(wirePacket)
        else await characteristic.writeValueWithoutResponse(wirePacket)
      } finally {
        if (imageTiming) observeImageTiming(imageTiming, 'attWrite', performance.now() - writeAt)
      }
    }
    const result = this.writeQueue.then(run, run)
    this.writeQueue = result.catch(() => undefined)
    return result
  }

  private nextV2Transaction() {
    for (let attempt = 0; attempt < 255; attempt += 1) {
      this.v2Transaction = this.v2Transaction >= 255 ? 1 : this.v2Transaction + 1
      if (!this.v2Pending.has(this.v2Transaction) && !this.v2Completions.has(this.v2Transaction)) {
        return this.v2Transaction
      }
    }
    throw new Error('没有可用的 v1 事务号')
  }

  private requestV2(
    opcode: number,
    payload: Uint8Array<ArrayBufferLike> = new Uint8Array(0),
    flags: number = RequestFlagV2.NONE,
    timeout = 10000,
    transactionId?: number,
    quiet = false,
    withResponse = true,
    imageTiming?: ImageTimingSink,
  ) {
    const generation = this.connectionGeneration, captured = Uint8Array.from(payload)
    const queuedAt = imageTiming ? performance.now() : 0
    const run = () => {
      if (generation !== this.connectionGeneration) throw new Error('设备连接已变化，未发送排队请求')
      if (imageTiming) observeImageTiming(imageTiming, 'requestQueue', performance.now() - queuedAt)
      return this.requestV2OnWire(opcode, captured, flags, timeout, transactionId, quiet, withResponse, imageTiming)
    }
    // Serialize through the matched product reply, not just the native write.
    // Connection state remains available while background time sync runs; the
    // same firmware mailbox must not receive details/settings ahead of its ACK.
    const reply = this.v2RequestQueue.then(run, run)
    this.v2RequestQueue = reply.then(() => undefined, () => undefined)
    return reply
  }

  private requestV2OnWire(
    opcode: number,
    payload: Uint8Array<ArrayBufferLike> = new Uint8Array(0),
    flags: number = RequestFlagV2.NONE,
    timeout = 10000,
    transactionId = this.nextV2Transaction(),
    quiet = false,
    withResponse = true,
    imageTiming?: ImageTimingSink,
  ) {
    if (!this.protocolV2) return Promise.reject(new Error('设备未启用当前 v1 协议'))
    if (this.passwordSupported() && (!this.passwordStatus || (this.passwordStatus.enabled && !this.passwordStatus.authorized)) && ![
      CommandV2.HELLO, CommandV2.GET_CAPABILITIES, CommandV2.GET_DEVICE_INFO,
      CommandV2.GET_STATUS, CommandV2.GET_TIME, CommandV2.GET_SCREEN_PROFILE,
      CommandV2.GET_ACTIVATION_STATUS, CommandV2.GET_SECURITY_INFO,
      CommandV2.GET_DEVICE_PASSWORD_STATUS, CommandV2.GET_DEVICE_PASSWORD_CHALLENGE,
      CommandV2.AUTHORIZE_DEVICE_PASSWORD, CommandV2.SHOW_PASSWORD_DEVICE_INFO,
    ].some((allowed) => allowed === opcode)) return Promise.reject(new Error('请先验证设备密码。'))
    if (this.v2Pending.has(transactionId)) return Promise.reject(new Error('v1 事务号正在使用'))
    const packet = new Uint8Array(3 + payload.length)
    packet[0] = opcode
    packet[1] = transactionId
    packet[2] = encodeReleaseRequestFlags(flags)
    packet.set(payload, 3)
    const startedAt = performance.now()
    const generation = this.connectionGeneration
    return new Promise<V2Reply>((resolve, reject) => {
      if (!quiet) {
        useDeviceStore.getState().addLog(
          `v1 请求：op=0x${opcode.toString(16).padStart(2, '0')} tx=${transactionId} flags=0x${packet[2].toString(16).padStart(2, '0')} payload=${payload.length} B`,
          'tx',
        )
      }
      const request: V2Pending = { opcode, startedAt, quiet, resolve, reject, timer: 0, imageTiming }
      request.timer = window.setTimeout(() => {
        if (this.v2Pending.get(transactionId) !== request) return
        this.v2Pending.delete(transactionId)
        useDeviceStore.getState().addLog(
          `v1 响应超时：op=0x${opcode.toString(16).padStart(2, '0')} tx=${transactionId}（${Math.max(0, Math.round(performance.now() - startedAt))} ms）`,
          'error',
        )
        reject(new ProtocolV2Error(StatusV2.TIMEOUT, `等待命令 0x${opcode.toString(16).padStart(2, '0')} 响应超时`))
      }, timeout)
      this.v2Pending.set(transactionId, request)
      // requestV2 owns the transaction-level log above. Suppress the generic
      // raw write log so a visible TX line always has a directly comparable
      // V2 response/timeout line. High-volume transfer chunks stay aggregated.
      void this.writePacket(packet, withResponse, true,
        () => generation === this.connectionGeneration && this.v2Pending.get(transactionId) === request, imageTiming).catch((error) => {
        if (generation !== this.connectionGeneration) return
        const pending = this.v2Pending.get(transactionId)
        if (pending !== request) return
        window.clearTimeout(pending.timer)
        this.v2Pending.delete(transactionId)
        useDeviceStore.getState().addLog(
          `v1 发送失败：op=0x${opcode.toString(16).padStart(2, '0')} tx=${transactionId}（${Math.max(0, Math.round(performance.now() - startedAt))} ms）`,
          'error',
        )
        reject(error instanceof Error ? error : new Error(String(error)))
      })
    })
  }

  async readClockPartialRefreshCapability() {
    const generation = this.connectionGeneration
    useDeviceStore.getState().setCapabilities({ clockPartialRefresh: false })
    if (!this.protocolV2) return
    try {
      const caps = decodeCapabilitiesV2((await this.requestV2(CommandV2.GET_CAPABILITIES)).payload)
      if (generation === this.connectionGeneration && caps) {
        useDeviceStore.getState().setCapabilities({ clockPartialRefresh: (caps.mask & CapabilityV2.CLOCK_PARTIAL_REFRESH) !== 0 })
      }
    } catch { /* An unavailable runtime declaration keeps the ordinary confirmation. */ }
  }

  private async initializeV2Core() {
    const store = useDeviceStore.getState()
    let helloReply: V2Reply
    try {
      helloReply = await this.requestV2(CommandV2.HELLO, new Uint8Array(0), RequestFlagV2.NONE, 12000)
    } catch (error) {
      if (!(error instanceof ProtocolV2Error) || error.status !== StatusV2.TIMEOUT) throw error
      store.addLog('设备可能正在完成屏幕刷新，正在重试握手', 'warning')
      await sleep(300)
      helloReply = await this.requestV2(CommandV2.HELLO, new Uint8Array(0), RequestFlagV2.NONE, 12000)
    }
    const hello = helloReply.payload
    if (hello.length < 6 || hello[0] !== 1 || hello[1] !== PROTOCOL_V2_VERSION) {
      throw new Error('设备 v1 握手响应无效')
    }
    const helloView = new DataView(hello.buffer, hello.byteOffset, hello.byteLength)
    this.imageMtu = helloView.getUint16(2, true)
    this.v2WritePayload = Math.max(20, helloView.getUint16(4, true))

    return this.readV2CoreDetails()
  }

  private async readV2CoreDetails() {
    const store = useDeviceStore.getState()
    const generation = this.connectionGeneration
    const read = async (opcode: number) => {
      if (generation !== this.connectionGeneration || this.disconnectRequested) throw new Error('设备基础信息读取期间连接已改变')
      const reply = await this.requestV2(opcode)
      if (generation !== this.connectionGeneration || this.disconnectRequested) throw new Error('设备基础信息读取期间连接已改变')
      return reply.payload
    }
    const capabilities = await read(CommandV2.GET_CAPABILITIES)
    if (capabilities.length < 17 || capabilities[0] !== 1) throw new Error('设备 V2 能力响应无效')
    const capabilityView = new DataView(capabilities.buffer, capabilities.byteOffset, capabilities.byteLength)
    const capabilityMask = capabilityView.getUint32(1, true)
    this.v2Capabilities = capabilityMask
    this.v2CapabilityExtensions = capabilities.length >= 28 ? capabilityView.getUint32(24, true) : 0
    const modeMask = capabilityView.getUint16(5, true)
    const displayActionMask = capabilityView.getUint16(7, true)
    const nameMaxBytes = capabilities[13]
    const labelMaxBytes = capabilities[14]
    const settingsSchema = capabilities.length >= 24 ? capabilities[17] : 0
    const nfcActionMask = settingsSchema === 1 ? capabilityView.getUint16(18, true) : 0
    const buttonActionMask = settingsSchema === 1 ? capabilityView.getUint16(20, true) : 0
    const buttonGestureMask = settingsSchema === 1 ? capabilities[22] : 0
    const settingsFlags = settingsSchema === 1 ? capabilities[23] : 0

    const deviceInfo = await read(CommandV2.GET_DEVICE_INFO)
    if (deviceInfo.length < 23 || deviceInfo[0] !== 1) throw new Error('设备 v1 身份响应无效')
    const boardLength = deviceInfo[1]
    const nameLength = deviceInfo[2]
    const labelLength = deviceInfo[3]
    const stringsEnd = 23 + boardLength + nameLength + labelLength
    if (stringsEnd > deviceInfo.length) throw new Error('设备 v1 身份长度无效')
    const deviceInfoView = new DataView(deviceInfo.buffer, deviceInfo.byteOffset, deviceInfo.byteLength)
    this.runtimeImageInfo = {
      version: deviceInfoView.getUint32(11, true),
      appBytes: deviceInfoView.getUint32(15, true),
      checksum: deviceInfoView.getUint32(19, true),
    }
    const boardId = canonicalBoardId(this.textDecoder.decode(deviceInfo.subarray(23, 23 + boardLength)))
    const deviceName = this.textDecoder.decode(deviceInfo.subarray(23 + boardLength, 23 + boardLength + nameLength))
    const localAlias = this.textDecoder.decode(deviceInfo.subarray(23 + boardLength + nameLength, stringsEnd))
    const bleAddress = deviceInfo[4]
      ? Array.from(deviceInfo.subarray(5, 11), (byte) => byte.toString(16).padStart(2, '0')).join(':').toUpperCase()
      : ''
    const firmwareBuildEdition = (capabilityMask & CapabilityV2.ACTIVATION) !== 0 ? 'pro'
      : boardId === 'aesl22_nrf' || boardId === 'da14585_aesl' ? 'basic' : null
    store.setIdentity({ boardId, deviceName, localAlias, firmwareBuildEdition, ...(bleAddress ? { bleAddress } : {}) })

    const board = resolveBoardProfile(boardId)
    if (board) store.setCapabilities(board.capabilities)
    store.setCapabilities({
      imageStore: (capabilityMask & CapabilityV2.RESOURCES) !== 0,
      imageStoreSectors: capabilityView.getUint16(15, true),
      imageMetadata: (capabilityMask & CapabilityV2.IMAGE_METADATA) !== 0,
      imageReadingOrientation: (this.v2CapabilityExtensions & CapabilityExtensionV2.IMAGE_READING_ORIENTATION) !== 0,
      nfc: (capabilityMask & CapabilityV2.NFC) !== 0 ? 'tap' : 'none',
      button: (capabilityMask & CapabilityV2.INPUT) !== 0 && Boolean(board?.capabilities.button),
      auxReserved: (capabilityMask & CapabilityV2.IO) !== 0 && Boolean(board?.capabilities.auxReserved),
      led: resolveBoardLedCapability(
        boardId,
        (capabilityMask & CapabilityV2.INDICATOR) !== 0,
      ),
      ota: (capabilityMask & CapabilityV2.OTA_RECOVERY) !== 0
        ? (firmwareFamilyProfile(boardId)?.ota || board?.capabilities.ota || 'goodix') : 'none',
      health: (capabilityMask & CapabilityV2.HEALTH) !== 0,
      clockPartialRefresh: (capabilityMask & CapabilityV2.CLOCK_PARTIAL_REFRESH) !== 0,
    })
    store.setSettingsCapabilities({
      ...emptyDeviceSettingsCapabilities,
      protocolVersion: 1,
      bluetoothName: (capabilityMask & CapabilityV2.DEVICE_NAME) !== 0,
      bluetoothNameMaxBytes: nameMaxBytes,
      deviceLabel: (capabilityMask & CapabilityV2.DEVICE_LABEL) !== 0,
      deviceLabelMaxBytes: labelMaxBytes,
      nfcActionMask,
      buttonActionMask,
      buttonGestureMask,
      displayActionMask,
      nativeDeviceInfo: (modeMask & (1 << DisplayModeV2.DEVICE_INFO)) !== 0,
      deviceInfoQr: (capabilityMask & CapabilityV2.DEVICE_INFO_QR) !== 0,
      guiAssetUpdate: (capabilityMask & CapabilityV2.GUI_ASSET_UPDATE) !== 0,
      deviceInfoLayoutMask: (modeMask & (1 << DisplayModeV2.DEVICE_INFO)) !== 0 ? 0x07 : 0,
      // INDICATOR is the V2 contract for both the status-light runtime and its
      // GET/SET configuration commands.  The optional settings-schema flag is
      // retained for older development firmware, but a current 17-byte
      // capability reply must not leave a working indicator UI disabled.
      statusLedSettings: (capabilityMask & CapabilityV2.INDICATOR) !== 0 ||
        (settingsFlags & 0x01) !== 0,
      customButtonGpio: (capabilityMask & CapabilityV2.IO) !== 0,
      reservedIo: (capabilityMask & CapabilityV2.IO) !== 0,
      runtimePinConfig: (capabilityMask & CapabilityV2.PIN_CONFIG) !== 0,
      lifecycle: (capabilityMask & CapabilityV2.LIFECYCLE) !== 0,
      experimentalTimedSleep: (capabilityMask & CapabilityV2.EXPERIMENTAL_TIMED_SLEEP) !== 0,
      experimentalFind: (capabilityMask & (CapabilityV2.FIND | CapabilityV2.EXPERIMENTAL_FIND)) !== 0,
    })

    return capabilityMask
  }

  async refreshDeviceInfo(): Promise<boolean> {
    const store = useDeviceStore.getState()
    if (!this.protocolV2 || store.connection !== 'connected') throw new Error('请先连接设备')
    if (store.action !== 'idle' && store.action !== 'slideshow') throw new Error('设备正在执行任务，请稍后刷新配置')
    const generation = this.connectionGeneration
    const deviceId = store.deviceId
    const assertConnection = () => {
      const current = useDeviceStore.getState()
      if (generation !== this.connectionGeneration || this.disconnectRequested ||
          current.connection !== 'connected' || current.deviceId !== deviceId)
        throw new Error('配置读取期间连接已改变')
    }
    const capabilities = await this.readV2CoreDetails()
    assertConnection()
    await this.initializeV2Details(capabilities)
    assertConnection()
    return !this.detailsIncomplete
  }

  private async initializeV2Details(capabilityMask: number) {
    const store = useDeviceStore.getState()
    this.detailsIncomplete = false
    const generation = this.connectionGeneration
    const assertConnection = () => {
      if (generation !== this.connectionGeneration || this.disconnectRequested) throw new Error('设备详情读取期间连接已改变')
    }
    const read = async (opcode: number) => {
      assertConnection()
      const reply = await this.requestV2(opcode)
      assertConnection()
      return reply.payload
    }
    const warn = (label: string, error: unknown) => {
      assertConnection()
      this.detailsIncomplete = true
      store.addLog(`${label}，基础连接仍可用：${error instanceof Error ? error.message : String(error)}`, 'warning')
    }

    if ((capabilityMask & (CapabilityV2.FIND | CapabilityV2.EXPERIMENTAL_FIND)) !== 0) {
      try {
        const details = await this.readFindDetails(read)
        assertConnection()
        useDeviceStore.setState(details)
      } catch (error) {
        assertConnection()
        store.setFindCapabilities(null)
        store.setFindConfig(null)
        // Strict decoding still rejects obsolete/malformed Find. It must not
        // prevent unrelated screen, pin, status or OTA functionality.
        warn('Find 扩展信息不可用', error)
      }
    } else {
      store.setFindCapabilities(null)
      store.setFindConfig(null)
    }

    if ((capabilityMask & CapabilityV2.INPUT) !== 0) {
      try {
        const input = decodeButtonConfig(await read(CommandV2.GET_INPUT_CONFIG))
        if (!input) throw new Error('设备 V2 按键配置响应无效')
        store.setSettingsCapabilities({
          ...useDeviceStore.getState().settingsCapabilities,
          buttonGestureMask: input.gestureMask,
          buttonActionMask: input.actionMask,
          nfcActionMask: input.nfcActionMask,
          nfcNdef: false,
        })
        store.setReportedButtonActions(input.settings)
        if (input.nfcSettings) store.setReportedNfcTap(input.nfcSettings)
      } catch (error) {
        assertConnection()
        // A bad optional input block must not suppress independent screen reads.
        // Unknown actions remain unavailable instead of acquiring default bindings.
        store.setSettingsCapabilities({
          ...useDeviceStore.getState().settingsCapabilities,
          buttonGestureMask: 0, buttonActionMask: 0, nfcActionMask: 0, nfcNdef: false,
        })
        if (error instanceof ProtocolV2Error && error.status === StatusV2.UNSUPPORTED) {
          store.addLog('设备不支持按键扩展配置，已跳过', 'info')
        } else {
          warn('按键扩展信息不可用，已跳过', error)
        }
      }
    }
    if ((capabilityMask & (CapabilityV2.IO | CapabilityV2.PIN_CONFIG)) !== 0) {
      try { await this.getAuxIoConfiguration() }
      catch (error) { assertConnection(); store.setReportedAuxIoConfiguration(null); warn('扩展 GPIO 读回失败', error) }
    }

    try {
      const screenBytes = await read(CommandV2.GET_SCREEN_PROFILE)
      const screen = decodeScreenConfig(screenBytes)
      if (!screen) throw new Error('设备屏幕配置无效')
      store.setScreen(screen)
      store.addLog(`屏幕配置 ${screen.width}×${screen.height}`, 'rx')
    } catch (error) {
      // Like the CLI, keep signed OTA accessible when GET_SCREEN_PROFILE is
      // unavailable. Never substitute a guessed screen for failed readback.
      assertConnection()
      useDeviceStore.setState({ screen: null })
      warn('屏幕配置读取失败', error)
    }
    if ((capabilityMask & CapabilityV2.PIN_CONFIG) !== 0) {
      try { await this.getRuntimePinConfig() }
      catch (error) { assertConnection(); store.setReportedPinConfig(null); warn('引脚读回失败', error) }
    }
    try {
      const deviceTime = await read(CommandV2.GET_TIME)
      if (deviceTime.length !== 7 || deviceTime[6] > 6) throw new Error('设备时间响应无效')
      this.v2WeekStart = deviceTime[6]
    } catch (error) { warn('时间读取失败', error) }
    const healthSupported = (capabilityMask & CapabilityV2.HEALTH) !== 0
    if (healthSupported) {
      try { await this.readHealth() }
      catch (error) {
        warn('健康状态读取失败，已跳过', error)
      }
    }
    try { await this.requestTelemetry() }
    catch (error) { warn('设备状态读取失败', error) }
    if ((capabilityMask & CapabilityV2.ACTIVATION) !== 0) {
      try { await this.readActivationStatus(false) }
      catch (error) { warn('激活状态读取失败', error) }
    } else {
      store.setActivation(emptyFirmwareActivation)
      if (!store.firmwareBuildEdition && firmwareFamilyProfile(store.boardId)) {
        // Some Pro builds suppress the capability when activation is unavailable.
        // The read-only status distinguishes the BASIC encoder from the Pro scheme.
        try {
          const { scheme } = parseActivationStatus(await read(CommandV2.GET_ACTIVATION_STATUS))
          if (scheme === ActivationSchemeV2.NONE) store.setIdentity({ firmwareBuildEdition: 'basic' })
          if (scheme === ActivationSchemeV2.ECDSA_P256_IKAC1_V1) store.setIdentity({ firmwareBuildEdition: 'pro' })
        } catch {
          assertConnection()
        }
      }
      if (store.deviceId) await rememberDeviceEdition(store.deviceId, 'base')
    }
    if ((capabilityMask & CapabilityV2.RESOURCES) !== 0) {
      try { await this.listStoredImages() }
      catch (error) { warn('图槽读取失败', error) }
    }
    store.addLog(`v1 协议已就绪，最大数据帧 ${this.imageMtu} bytes`, 'success')
  }

  async readActivationStatus(refreshResources = true) {
    const status = parseActivationStatus((await this.requestV2(CommandV2.GET_ACTIVATION_STATUS)).payload)
    const store = useDeviceStore.getState()
    store.setActivation(status)
    if (store.deviceId) await rememberDeviceEdition(store.deviceId, status.edition)
    if (refreshResources && store.capabilities.imageStore) {
      await this.listStoredImages()
    }
    return status
  }

  async readActivationRequest(force = false) {
    return parseActivationRequest((await this.requestV2(
      CommandV2.GET_ACTIVATION_REQUEST,
      new Uint8Array(0),
      force ? RequestFlagV2.FORCE : RequestFlagV2.NONE,
    )).payload)
  }

  async installActivationCode(code: string, restore = false) {
    const payload = encodeActivationInstall(code, restore)
    if (restore) await this.readActivationRequest(false)
    const status = parseActivationStatus((await this.requestV2(
      CommandV2.SET_ACTIVATION_CODE,
      payload,
    )).payload)
    const store = useDeviceStore.getState()
    store.setActivation(status)
    if (store.deviceId) await rememberDeviceEdition(store.deviceId, status.edition)
    if (status.lastResult !== 0) {
      throw new Error(activationResultLabels[status.lastResult ?? 0] || '设备拒绝了激活码。')
    }
    if (status.phase !== 'active') throw new Error('设备未确认高级功能授权。')
    if (store.capabilities.imageStore) {
      await this.listStoredImages()
    }
    return status
  }

  private applyV2Status(payload: Uint8Array) {
    if (payload.length < 16 || payload[0] !== 1) return
    const view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength)
    const flags = payload[2]
    const voltageMv = view.getUint16(8, true)
    const telemetry = statusTemperatureTelemetry(payload, useDeviceStore.getState().boardId)
    useDeviceStore.getState().setTelemetry({
      voltage: (flags & 0x04) !== 0 && voltageMv >= 1500 && voltageMv <= 6000
        ? voltageMv / 1000
        : null,
      ...telemetry,
    })
    if (canonicalBoardId(useDeviceStore.getState().boardId) === 'tlsr8359_hanshow') {
      const counters = decodeTlsrNotifyCounters(payload)
      if (counters) useDeviceStore.getState().addLog(`TLSR 通知队列快照：lastStatus=${counters.lastStatus} retry=${counters.retries} drop=${counters.drops} pending=${counters.pending}`)
    }
    if ((flags & 0x02) !== 0 && useDeviceStore.getState().action === 'idle') {
      useDeviceStore.getState().setAction('slideshow')
    }
  }

  private async refreshTelemetryAfterDisplay(generation: number) {
    if (!this.protocolV2 || useDeviceStore.getState().connection !== 'connected') return
    try {
      // Read the cached sample once; never start another refresh or sample the panel.
      const { payload } = await this.requestV2(CommandV2.GET_STATUS, new Uint8Array(0), RequestFlagV2.NONE, 3000)
      if (generation !== this.connectionGeneration || useDeviceStore.getState().connection !== 'connected') return
      this.applyV2Status(payload)
    } catch {
      if (generation === this.connectionGeneration && useDeviceStore.getState().connection === 'connected')
        useDeviceStore.getState().addLog('刷屏已完成，但状态读回失败；温度将在下次读取时更新。', 'warning')
    }
  }

  private requestResourceSnapshot(opcode: number, payload: Uint8Array, budget: ResourceReadBudget) {
    return retryResourceRead(opcode,
      (timeout) => this.requestV2(opcode, payload, RequestFlagV2.NONE, timeout),
      (error) => error instanceof ProtocolV2Error && error.status === StatusV2.BUSY,
      this.resourceReadAbort.signal, budget)
  }

  private async readV2ResourceInfo(budget: ResourceReadBudget) {
    let payload: Uint8Array
    try {
      payload = (await this.requestResourceSnapshot(CommandV2.GET_RESOURCE_INFO, new Uint8Array(0), budget)).payload
    } catch (error) {
      const message = error instanceof ProtocolV2Error ? resourceInfoFailureMessage(error.status) : undefined
      if (message && error instanceof ProtocolV2Error) throw new ProtocolV2Error(error.status, message)
      throw error
    }
    if (payload.length < 30 || payload[0] !== 1) throw new Error('设备图槽信息响应无效')
    const view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength)
    const requiredSectors = view.getUint16(9, true)
    const freeSectors = view.getUint16(13, true)
    const largestFreeRun = view.getUint16(15, true)
    const allocatable = payload.length >= 32
      ? view.getUint16(30, true)
      : requiredSectors ? Math.floor(largestFreeRun / requiredSectors) : 0
    if (payload.length === 33) throw new Error('设备固定图槽信息响应不完整')
    const fixedSlotCount = payload.length >= 34 ? view.getUint16(32, true) : 0
    if (fixedSlotCount > 255 || (fixedSlotCount !== 0 && (!requiredSectors
      || fixedSlotCount * requiredSectors > view.getUint16(11, true)
      || view.getUint16(17, true) > fixedSlotCount))) throw new Error('设备固定图槽容量无效')
    const info: ImageStoreInfo = {
      width: view.getUint16(1, true),
      height: view.getUint16(3, true),
      bytesPerImage: view.getUint32(5, true),
      requiredSectors,
      totalSectors: view.getUint16(11, true),
      freeSectors,
      largestFreeRun,
      fit: requiredSectors ? Math.floor(freeSectors / requiredSectors) : 0,
      contiguousFit: allocatable,
      // Fixed slots have independently reported physical geometry. The wire
      // allocatable count can already be base-clamped; keep it authoritative
      // for writes, and use physical free slots only for the Pro estimate.
      physicalContiguousFit: fixedSlotCount && requiredSectors
        ? Math.min(fixedSlotCount, Math.floor(freeSectors / requiredSectors)) : allocatable,
      fixedSlotCount: fixedSlotCount || undefined,
      saved: view.getUint16(17, true),
      compatible: view.getUint16(19, true),
      playable: view.getUint16(21, true),
      incompatible: view.getUint16(23, true),
      slideshow: payload[25] !== 0,
      intervalSeconds: view.getUint32(26, true),
    }
    return info
  }

  private async refreshV2ResourceInfo() {
    const info = await this.readV2ResourceInfo(resourceReadBudget())
    const store = useDeviceStore.getState()
    store.setImageStoreInfo(constrainedImageStoreInfo(info, null, store.activation, store.screen))
  }

  private waitForV2Completion(transactionId: number, opcode: number, timeout = 90000) {
    const previous = this.v2Completions.get(transactionId)
    if (previous) {
      window.clearTimeout(previous.timer)
      previous.reject(new Error('v1 完成事件等待已被替换'))
    }
    return new Promise<Uint8Array>((resolve, reject) => {
      const timer = window.setTimeout(() => {
        this.v2Completions.delete(transactionId)
        reject(new ProtocolV2Error(StatusV2.TIMEOUT, '等待设备完成屏幕操作超时'))
      }, timeout)
      this.v2Completions.set(transactionId, { opcode, resolve, reject, timer })
    })
  }

  private async runV2DisplayWork(mode: number, extra = new Uint8Array(0), afterTimeSync = false) {
    const generation = this.connectionGeneration
    const workGeneration = this.transferGeneration
    const server = this.server
    const device = this.device
    const deadline = performance.now() + 90000
    const remaining = () => {
      if (generation !== this.connectionGeneration || workGeneration !== this.transferGeneration || server !== this.server || device !== this.device || !server?.connected)
        throw new Error('等待校时后刷屏期间设备连接已改变')
      const budget = deadline - performance.now()
      if (budget <= 0) throw new Error('校时已完成，但等待屏幕空闲或刷新超时；未再发送操作')
      return budget
    }
    const payload = new Uint8Array(1 + extra.length)
    payload[0] = mode
    payload.set(extra, 1)
    // Only this caller's acknowledged SET_TIME followed by a not-queued
    // display admission BUSY may wait for idle and make one fresh attempt.
    // Accepted completion errors and missing ACKs never replay the write.
    for (let attempt = 0; attempt < 2; attempt += 1) {
      const transactionId = this.nextV2Transaction()
      const completion = this.waitForV2Completion(transactionId, CommandV2.SET_DISPLAY_MODE, afterTimeSync ? remaining() : 90000)
      void completion.catch(() => undefined)
      let admitted = false
      try {
        await this.requestV2(CommandV2.SET_DISPLAY_MODE, payload, RequestFlagV2.NONE, afterTimeSync ? Math.min(10000, remaining()) : 10000, transactionId)
        admitted = true
        return await completion
      } catch (error) {
        if (!afterTimeSync || attempt !== 0 || admitted || !(error instanceof ProtocolV2Error) || error.status !== StatusV2.BUSY)
          throw error
      } finally {
        const pendingCompletion = this.v2Completions.get(transactionId)
        if (pendingCompletion) {
          window.clearTimeout(pendingCompletion.timer)
          this.v2Completions.delete(transactionId)
          pendingCompletion.reject(new Error('本次显示等待已结束'))
        }
      }
      useDeviceStore.getState().addLog('校时已确认；等待自动屏幕任务结束后，只重新提交一次本次刷新。', 'info')
      while (true) {
        const status = (await this.requestV2(CommandV2.GET_STATUS, new Uint8Array(0), RequestFlagV2.NONE, Math.min(3000, remaining()))).payload
        remaining()
        if (status.length < 16 || status[0] !== 1) throw new Error('等待刷屏时设备状态响应无效；未再发送操作')
        if ((status[2] & 0x11) === 0) break
        await sleep(Math.min(100, remaining()))
      }
    }
    throw new Error('校时后屏幕刷新未获准')
  }

  private assertTestConnection(generation: number) {
    if (generation !== this.connectionGeneration || this.disconnectRequested) {
      throw new Error('设备连接已改变，请重新读取测试项目')
    }
  }

  async listTests(): Promise<number[]> {
    if (!this.protocolV2) throw new Error('设备未启用当前 v1 协议')
    if (!(this.v2Capabilities & CapabilityV2.TEST)) return []
    const generation = this.connectionGeneration
    const response = await this.requestV2(CommandV2.LIST_TESTS)
    this.assertTestConnection(generation)
    return decodeTestOperations(response.payload)
  }

  private async requireTest(operation: number) {
    const generation = this.connectionGeneration
    const operations = await this.listTests()
    this.assertTestConnection(generation)
    if (!operations.includes(operation)) throw new Error('当前固件未提供这项测试，请更新固件后重试')
    return generation
  }

  private async runV2TestWork(payload: Uint8Array<ArrayBufferLike>) {
    const transactionId = this.nextV2Transaction()
    const completion = this.waitForV2Completion(transactionId, CommandV2.RUN_TEST)
    try {
      const response = await this.requestV2(
        CommandV2.RUN_TEST,
        payload,
        RequestFlagV2.NONE,
        10000,
        transactionId,
      )
      await completion
      return response.payload
    } catch (error) {
      const pendingCompletion = this.v2Completions.get(transactionId)
      if (pendingCompletion) {
        window.clearTimeout(pendingCompletion.timer)
        this.v2Completions.delete(transactionId)
        pendingCompletion.reject(error instanceof Error ? error : new Error(String(error)))
      }
      void completion.catch(() => undefined)
      throw error
    }
  }

  async testTimeShift(deltaSeconds: number, mode: TimeTestDisplayMode) {
    if (!this.protocolV2) throw new Error('当前固件不支持 V2 时间测试')
    this.assertTestConnection(await this.requireTest(TestOperationV2.TIME_SHIFT))
    const store = useDeviceStore.getState()
    const action = mode === DisplayModeV2.CALENDAR ? 'calendar' : 'clock'
    store.setAction(action, null, `设备正在调整测试时间，随后绘制${action === 'calendar' ? '日历' : '时钟'}布局。`)
    try {
      return await this.runV2TestWork(encodeTimeShiftTest(deltaSeconds, mode))
    } catch (error) {
      retainDisplayFailure(action, error, () => this.testTimeShift(deltaSeconds, mode))
      throw error
    }
  }

  async testTimeSet(value: Date, mode: TimeTestDisplayMode) {
    if (!this.protocolV2) throw new Error('当前固件不支持 V2 时间测试')
    this.assertTestConnection(await this.requireTest(TestOperationV2.TIME_SET))
    const store = useDeviceStore.getState()
    const action = mode === DisplayModeV2.CALENDAR ? 'calendar' : 'clock'
    store.setAction(action, null, `设备正在设置测试时间，随后绘制${action === 'calendar' ? '日历' : '时钟'}布局。`)
    try {
      return await this.runV2TestWork(encodeTimeSetTest(value, mode, this.v2WeekStart))
    } catch (error) {
      retainDisplayFailure(action, error, () => this.testTimeSet(new Date(value.getTime()), mode))
      throw error
    }
  }

  private rejectV2Transactions(reason: string) {
    for (const [transactionId, pending] of this.v2Pending) {
      window.clearTimeout(pending.timer)
      pending.reject(new Error(reason))
      this.v2Pending.delete(transactionId)
    }
    for (const [transactionId, completion] of this.v2Completions) {
      window.clearTimeout(completion.timer)
      completion.reject(new Error(reason))
      this.v2Completions.delete(transactionId)
    }
  }

  private async waitForTimeWriteReady(timeout = 90000) {
    const generation = this.connectionGeneration, workGeneration = this.transferGeneration
    const device = this.device, server = this.server
    const deadline = performance.now() + timeout
    const remaining = () => {
      if (generation !== this.connectionGeneration || workGeneration !== this.transferGeneration || device !== this.device || server !== this.server || this.disconnectRequested)
        throw new Error('校时前连接或任务已改变，未发送校时')
      const budget = deadline - performance.now()
      if (budget <= 0) throw new Error('等待屏幕空闲超时，未发送校时')
      return budget
    }
    while (true) {
      let status: Uint8Array | undefined
      try {
        status = (await this.requestV2(CommandV2.GET_STATUS, new Uint8Array(0), RequestFlagV2.NONE, Math.min(3000, remaining()))).payload
      } catch (error) {
        if (!(error instanceof ProtocolV2Error) || (error.status !== StatusV2.TIMEOUT && error.status !== StatusV2.BUSY)) throw error
      }
      remaining()
      if (status) {
        if (status.length < 16 || status[0] !== 1) throw new Error('设备空闲状态无效，未发送校时')
        if ((status[2] & 0x11) === 0) return
      }
      await sleep(Math.min(250, remaining()))
    }
  }

  async syncTime(mode?: 1 | 2) {
    await this.waitForTimeWriteReady()
    const now = Math.floor(Date.now() / 1000)
    const timezoneMinutes = -new Date().getTimezoneOffset()
    const payload = new Uint8Array(7)
    const view = new DataView(payload.buffer)
    view.setUint32(0, now, true)
    view.setInt16(4, timezoneMinutes, true)
    payload[6] = this.v2WeekStart
    await this.requestV2(CommandV2.SET_TIME, payload)
    if (mode) {
      const displayMode = mode === 1 ? DisplayModeV2.CALENDAR : DisplayModeV2.CLOCK
      await this.runV2DisplayWork(displayMode, new Uint8Array(0), true)
    }
    return
  }

  async syncTimeAndVerify() {
    const device = this.device, generation = this.connectionGeneration
    await this.waitForPasswordAuthorization()
    const before = Date.now(), timezone = -new Date(before).getTimezoneOffset(), weekStart = this.v2WeekStart
    if (!device || this.device !== device || generation !== this.connectionGeneration) throw new Error('校时前设备连接已变化')
    await this.syncTime()
    const payload = (await this.requestV2(CommandV2.GET_TIME)).payload
    if (this.device !== device || generation !== this.connectionGeneration) throw new Error('校时读回期间设备连接已变化')
    if (payload.length !== 7) throw new Error('校时已应答，但时间读回格式无效；未重复校时')
    const view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength)
    const utc = view.getUint32(0, true)
    if (view.getInt16(4, true) !== timezone || payload[6] !== weekStart ||
        utc < Math.floor(before / 1000) || utc > Math.floor(Date.now() / 1000) + 2) {
      throw new Error('校时已应答，但时间读回不匹配；未重复校时')
    }
  }

  private applicationReturnIdentity(): ApplicationReturnIdentity {
    const state = useDeviceStore.getState()
    return { boardId: state.boardId, bleAddress: state.bleAddress,
      activationId: state.activation.activationId ?? null, image: this.runtimeImageInfo ? { ...this.runtimeImageInfo } : null }
  }

  verifyReturnedApplication(device: BluetoothDevice, expected?: ApplicationReturnIdentity) {
    const state = useDeviceStore.getState(), current = this.applicationReturnIdentity()
    if (this.device !== device || state.deviceId !== device.id || !this.server?.connected ||
        !this.protocolV2 || this.otaRecoveryType !== null || !current.boardId ||
        !current.image?.appBytes || !Number.isInteger(current.image.checksum)) {
      throw new Error('原应用身份或角色尚未核验；未发送校时')
    }
    if (expected && ((expected.boardId && canonicalBoardId(expected.boardId) !== canonicalBoardId(current.boardId)) ||
        (expected.bleAddress && expected.bleAddress.toUpperCase() !== current.bleAddress.toUpperCase()) ||
        (expected.activationId && expected.activationId !== current.activationId) ||
        (expected.image && (expected.image.version !== current.image.version || expected.image.appBytes !== current.image.appBytes || expected.image.checksum !== current.image.checksum)))) {
      throw new Error('重连的设备或应用与重启前不一致；未发送校时')
    }
  }

  async requestTelemetry() {
    const generation = this.connectionGeneration
    const payload = (await this.requestV2(CommandV2.GET_STATUS)).payload
    if (generation !== this.connectionGeneration) throw new Error('设备状态读取期间连接已改变')
    if (payload.length < 16 || payload[0] !== 1) throw new Error('设备状态响应无效')
    this.applyV2Status(payload)
    return
  }

  async readClockDiagnostic() {
    const generation = this.connectionGeneration
    const beforeMs = Date.now()
    const started = performance.now()
    const payload = (await this.requestV2(CommandV2.GET_TIME)).payload
    const roundTripMs = performance.now() - started
    const afterMs = Date.now()
    if (generation !== this.connectionGeneration) throw new Error('时间采样期间设备连接已改变')
    return clockDiagnostic(payload, beforeMs, afterMs, roundTripMs)
  }

  async readDiagnostics() {
    const generation = this.connectionGeneration
    const payload = (await this.requestV2(CommandV2.GET_STATUS)).payload
    if (generation !== this.connectionGeneration) throw new Error('诊断采样期间设备连接已改变')
    if (payload.length < 16 || payload[0] !== 1) throw new Error('设备诊断响应无效')
    this.applyV2Status(payload)
    useDeviceStore.getState().addLog(`运行诊断：mode=${payload[1]} flags=0x${payload[2].toString(16).padStart(2, '0')} progress=${payload[3]} status=${Array.from(payload, (byte) => byte.toString(16).padStart(2, '0')).join('')}`)
    return { mode: payload[1], flags: payload[2], progress: payload[3], rawHex: Array.from(payload, (byte) => byte.toString(16).padStart(2, '0')).join('') }
  }

  async getDisplaySettings() {
    const generation = this.connectionGeneration
    if (!this.protocolV2) {
      return {
        version: 0, supported: false, complete: false, weekStart: this.v2WeekStart as 0 | 1,
        bootRefreshEnabled: false, brandVisibilitySupported: false, brandVisible: true,
        countdownTargetDay: 0, countdownName: '', countdownDate: '',
      }
    }
    const response = (await this.requestV2(
      CommandV2.GET_SETTINGS,
      new Uint8Array([SettingsSectionV2.DISPLAY]),
    )).payload
    const settings = decodeDisplaySettings(response)
    if (!settings) throw new Error('设备显示设置响应无效')
    if (generation !== this.connectionGeneration) throw new Error('读取显示设置期间设备连接已改变')
    this.v2WeekStart = settings.weekStart
    useDeviceStore.setState({ brandVisible: settings.brandVisibilitySupported ? settings.brandVisible : true })
    return settings
  }

  private async writeDisplaySettings(settings: Parameters<typeof encodeDisplaySettings>[0]) {
    const generation = this.connectionGeneration
    const response = (await this.requestV2(
      CommandV2.SET_SETTINGS,
      encodeDisplaySettings(settings),
    )).payload
    const confirmed = decodeDisplaySettings(response)
    if (!confirmed?.complete) throw new Error('设备没有确认完整显示设置，请先更新固件')
    if (generation !== this.connectionGeneration) throw new Error('保存显示设置期间设备连接已改变')
    this.v2WeekStart = confirmed.weekStart
    useDeviceStore.setState({ brandVisible: confirmed.brandVisibilitySupported ? confirmed.brandVisible : true })
    return confirmed
  }

  async setDisplayConfiguration(weekStart: 0 | 1, countdownName: string, countdownDate: string) {
    const current = await this.getDisplaySettings()
    if (!current.complete) throw new Error('当前固件不支持完整的日历与时间设置，请先更新固件')
    const targetDay = countdownDate ? calendarDateToTargetDay(countdownDate) : 0
    if (countdownDate && targetDay === 0) throw new RangeError('目标日期无效')
    return this.writeDisplaySettings({
      ...current, weekStart, countdownName, countdownTargetDay: targetDay,
    })
  }

  async setBootRefresh(enabled: boolean) {
    const current = await this.getDisplaySettings()
    if (!current.complete) throw new Error('当前固件不支持完整显示设置，请先更新固件')
    return (await this.writeDisplaySettings({ ...current, bootRefreshEnabled: enabled })).bootRefreshEnabled
  }

  async setBrandVisible(visible: boolean) {
    const current = await this.getDisplaySettings()
    if (!current.brandVisibilitySupported) throw new Error('当前固件不支持配置等级标识')
    if (!current.complete) throw new Error('当前固件不支持完整显示设置，请先更新固件')
    return (await this.writeDisplaySettings({ ...current, brandVisible: visible })).brandVisible
  }

  async requestNfcStatus(includeUserData = false) {
    const deviceId = useDeviceStore.getState().deviceId
    useDeviceStore.getState().setNfcCapabilities(null)
    const capabilities = decodeNfcCapabilities(
      (await this.requestV2(CommandV2.GET_NFC_CAPABILITIES)).payload,
    )
    const current = useDeviceStore.getState()
    if (current.deviceId !== deviceId || current.connection !== 'connected') return
    // Command support is a complete independent response. A later optional
    // tag-status failure must not hide the successfully reported command list.
    current.setNfcCapabilities(capabilities)
    let status
    try {
      status = decodeNfcStatus((await this.requestV2(CommandV2.GET_NFC_STATUS)).payload)
    } finally {
      const latest = useDeviceStore.getState()
      if ((latest.deviceId !== deviceId || latest.connection !== 'connected') && latest.nfcCapabilities === capabilities) {
        latest.setNfcCapabilities(null)
      }
    }
    const present = (status.flags & 0x01) !== 0
    const uid = Array.from(status.uid, (byte) => byte.toString(16).padStart(2, '0')).join('').toUpperCase()
    const store = useDeviceStore.getState()
    if (store.deviceId !== deviceId || store.connection !== 'connected') return
    store.setCapabilities({ nfc: present ? 'hardware' : 'tap' })
    store.addLog(
      `nfc:status present=${present ? 1 : 0} uid=${uid || '-'} flags=0x${capabilities.flags.toString(16)} result=${status.lastResult} opcode=${status.lastOpcode} sequence=${status.lastSequence} diag=0x${status.diagnosticFlags.toString(16).padStart(2, '0')}`,
      'rx',
    )
    if (includeUserData) {
      if ((capabilities.flags & 0x10) === 0) throw new Error('设备没有开放 NFC NDEF 读取')
      const ndef = decodeNfcNdef((await this.requestV2(CommandV2.GET_NFC_NDEF)).payload)
      store.addLog(`nfc:ndef=${Array.from(ndef, (byte) => byte.toString(16).padStart(2, '0')).join('').toUpperCase()}`, 'rx')
    }
    return
  }

  private async readFindDetails(read: (opcode: number) => Promise<Uint8Array>) {
    const capabilities = decodeFindCapabilities(await read(CommandV2.GET_FIND_CAPABILITIES))
    const config = decodeFindConfig(await read(CommandV2.GET_FIND_CONFIG))
    return { findCapabilities: capabilities, findConfig: config }
  }

  async refreshFindDetails() {
    const generation = this.connectionGeneration
    await this.waitForPasswordAuthorization()
    const assertConnection = () => {
      if (generation !== this.connectionGeneration || this.disconnectRequested) throw new Error('读取 Find 信息期间连接已改变')
    }
    const details = await this.readFindDetails(async (opcode) => {
      assertConnection()
      const reply = await this.requestV2(opcode)
      assertConnection()
      return reply.payload
    })
    assertConnection()
    return details
  }

  async readFindConfig() {
    const config = decodeFindConfig(
      (await this.requestV2(CommandV2.GET_FIND_CONFIG)).payload,
    )
    useDeviceStore.getState().setFindConfig(config)
    return config
  }

  async configureFind(enabled: boolean, localOwnerKey?: Uint8Array) {
    if (enabled && !useDeviceStore.getState().findCapabilities?.publicOnlyP224) {
      throw new Error('当前设备需要更新固件后才能使用 Find v2。')
    }
    const request = encodeFindConfig(enabled)
    const flags = enabled ? RequestFlagV2.CONFIRM : RequestFlagV2.NONE
    const current = await this.readFindConfig()
    if (current.enabled && !enabled) {
      if (localOwnerKey) await this.authorizeOwnerAction(CommandV2.SET_FIND_CONFIG, request, flags, undefined, localOwnerKey)
      else await this.authorizeOwnerAction(CommandV2.SET_FIND_CONFIG, request, flags)
    }
    const config = decodeFindConfig((await this.requestV2(
      CommandV2.SET_FIND_CONFIG,
      request,
      flags,
    )).payload)
    useDeviceStore.getState().setFindConfig(config)
    return config
  }

  async testGhostClean(cycles: number) {
    if (!this.protocolV2) throw new Error('当前固件不支持 V2 残影清洁测试')
    if (!Number.isInteger(cycles) || cycles < 1 || cycles > 10) {
      throw new RangeError('残影清洁循环次数必须为 1–10')
    }
    const store = useDeviceStore.getState()
    if ((store.settingsCapabilities.displayActionMask & (1 << DisplayActionV2.GHOST_CLEAN)) === 0) {
      throw new Error('当前固件未提供残影清洁测试')
    }
    store.setAction('clear', null, `设备正在执行 ${cycles} 次完整黑白残影清洁循环。`)
    const transactionId = this.nextV2Transaction()
    const completion = this.waitForV2Completion(transactionId, CommandV2.DISPLAY_ACTION, 300000)
    try {
      await this.requestV2(
        CommandV2.DISPLAY_ACTION,
        new Uint8Array([DisplayActionV2.GHOST_CLEAN, cycles]),
        RequestFlagV2.CONFIRM,
        10000,
        transactionId,
      )
      return await completion
    } catch (error) {
      const pendingCompletion = this.v2Completions.get(transactionId)
      if (pendingCompletion) {
        window.clearTimeout(pendingCompletion.timer)
        this.v2Completions.delete(transactionId)
        pendingCompletion.reject(error instanceof Error ? error : new Error(String(error)))
      }
      void completion.catch(() => undefined)
      retainDisplayFailure('clear', error, () => this.testGhostClean(cycles))
      throw error
    }
  }

  async abortFindOwnerRotation() {
    await this.requestV2(
      CommandV2.ABORT_FIND_OWNER_ROTATION,
      new Uint8Array(0),
      RequestFlagV2.NONE,
    )
  }

  async beginPublicOnlyFindOwner() {
    const capabilities = useDeviceStore.getState().findCapabilities
    if (!capabilities?.publicOnlyP224) throw new Error('当前设备不支持新版 Find 身份')
    return decodeFindOwnerRotationChallenge((await this.requestV2(
      CommandV2.BEGIN_FIND_OWNER_ROTATION,
      new Uint8Array(0),
      RequestFlagV2.CONFIRM,
    )).payload)
  }

  async readPublicOnlyFindOwnerStatus() {
    return decodeFindPublicOnlyOwnerStatus((await this.requestV2(
      CommandV2.GET_FIND_OWNER_STATUS,
      new Uint8Array(0),
      RequestFlagV2.NONE,
    )).payload)
  }

  async unregisterFindOwner(deviceId: string, fingerprint: string) {
    if (!/^activation:(?!0{16}$)[0-9a-f]{16}$/i.test(deviceId) || !/^(?!0{32}$)[0-9a-f]{32}$/i.test(fingerprint)) {
      throw new Error('缺少可核对的标签标识或 Find 身份指纹。')
    }
    const connected = useDeviceStore.getState()
    const generation = this.connectionGeneration
    const matches = () => {
      const current = useDeviceStore.getState()
      return generation === this.connectionGeneration && !this.disconnectRequested &&
        current.connection === 'connected' && current.deviceId === connected.deviceId &&
        deviceId.toLowerCase() === `activation:${current.activation.activationId}`.toLowerCase()
    }
    const assertTarget = () => {
      if (!matches()) throw new Error('请连接这份 Find 资料对应的标签。')
    }
    assertTarget()
    if (!supportsFindOwnerUnregister(connected.findCapabilities)) throw new Error('当前固件不支持解除 Find 登记。')
    const owner = await this.readPublicOnlyFindOwnerStatus()
    assertTarget()
    if (owner.identityPresent) {
      if (owner.fingerprintHex.toLowerCase() !== fingerprint.toLowerCase()) throw new Error('标签当前登记的 Find 身份与本机所选资料不匹配。')
      const { nonceHex } = await this.beginPublicOnlyFindOwner()
      try {
        assertTarget()
        const proof = await findClient.provePublicOnlyUnregister(fingerprint, nonceHex, deviceId)
        const payload = decodeFindOwnerUnregisterPayload(proof.payloadHex, nonceHex, fingerprint)
        assertTarget()
        const response = await this.requestV2(CommandV2.FIND_ACTION, payload, RequestFlagV2.CONFIRM)
        assertTarget()
        if (response.payload.length || (await this.readPublicOnlyFindOwnerStatus()).identityPresent) {
          throw new Error('无法确认设备已解除 Find 登记。')
        }
      } catch (error) {
        if (matches()) await this.abortFindOwnerRotation().catch(() => undefined)
        throw error
      }
    }
    assertTarget()
    const config = await this.readFindConfig()
    assertTarget()
    if (config.identityPresent || config.enabled) throw new Error('设备仍有 Find 登记或广播未关闭。')
  }

  async stagePublicOnlyFindOwner(payloadHex: string, nonceHex: string) {
    await this.requestV2(
      CommandV2.STAGE_FIND_OWNER_ROTATION,
      decodeFindPublicOnlyStagePayload(payloadHex, nonceHex),
      RequestFlagV2.NONE,
    )
  }

  async stagePublicOnlyFindOwnerReplacement(payloadHex: string, nonceHex: string, currentFingerprintHex: string) {
    const capabilities = useDeviceStore.getState().findCapabilities
    if (!capabilities?.publicOnlyOwnerReplacement) throw new Error('当前固件不支持更新新版 Find 身份')
    await this.requestV2(
      CommandV2.STAGE_FIND_OWNER_ROTATION,
      decodeFindPublicOnlyReplacementStagePayload(payloadHex, nonceHex, currentFingerprintHex),
      RequestFlagV2.NONE,
    )
  }

  async commitPublicOnlyFindOwner(): Promise<FindOwnerRotationProof> {
    const proof = decodeFindOwnerRotationProof((await this.requestV2(
      CommandV2.COMMIT_FIND_OWNER_ROTATION,
      new Uint8Array(0),
      RequestFlagV2.CONFIRM,
      20000,
    )).payload)
    if (!proof.fingerprintHex) throw new Error('设备未返回新版 Find 身份指纹')
    await this.readFindConfig()
    return proof
  }

  async locateDevice() {
    await this.requestV2(
      CommandV2.RUN_TEST,
      new Uint8Array([TestOperationV2.DEVICE_LOCATE, 1]),
    )
  }

  async stopLocateDevice() {
    await this.requestV2(
      CommandV2.RUN_TEST,
      new Uint8Array([TestOperationV2.DEVICE_LOCATE, 0]),
    )
  }

  async testLed(colorMask: number) {
    const mask = Math.trunc(colorMask)
    if (mask < 0 || mask > 0x07) throw new RangeError('状态灯颜色必须在 0–7 之间')
    this.assertTestConnection(await this.requireTest(TestOperationV2.LED_CHANNEL))
    await this.requestV2(
      CommandV2.RUN_TEST,
      new Uint8Array([TestOperationV2.LED_CHANNEL, mask]),
    )
  }

  async readHealth(recordIndex = 0) {
    if (!useDeviceStore.getState().capabilities.health) return null
    const payload = (await this.requestV2(CommandV2.GET_HEALTH,
      new Uint8Array([recordIndex]))).payload
    const health = decodeDeviceHealth(payload)
    if (!health) throw new Error('设备健康状态响应无效')
    const store = useDeviceStore.getState()
    const previousHealth = store.health
    store.setHealth(health)
    if (health.voltageValid) store.setTelemetry({ voltage: health.voltageMv / 1000 })
    if (suppressBrownoutAlert(health) && store.crashAlert?.reason === '低电压复位') {
      store.setCrashAlert(null)
    }
    const deviceId = store.deviceId
    if (deviceId) {
      const current = store.crashAlert
      if (!current || current.totalCrashes !== health.totalCrashes) {
        try {
          const preference = await getDevicePreference(deviceId)
          if (preference.lastAcknowledgedTotalCrashes !== undefined &&
              health.totalCrashes < preference.lastAcknowledgedTotalCrashes) {
            await updateDevicePreference(deviceId, { lastAcknowledgedTotalCrashes: health.totalCrashes })
          } else {
            // The first read is history, even with an older saved browser count.
            const baseline = previousHealth
              ? Math.max(preference.lastAcknowledgedTotalCrashes ?? 0, previousHealth.totalCrashes)
              : undefined
            const alert = buildCrashAlert(health, baseline)
            if (alert) {
              if (alert.newCrashes !== null) store.setCrashAlert(alert)
              const count = alert.newCrashes === null
                ? `读取到异常复位历史记录，累计 ${alert.totalCrashes} 次`
                : `检测到 ${alert.newCrashes} 次新增异常复位`
              store.addLog(`${count}：${alert.reason}，${alert.operation} / ${alert.stage}`, 'warning')
            }
          }
        } catch (error) {
          store.addLog(`异常复位记录读取失败，已跳过提醒：${error instanceof Error ? error.message : String(error)}`, 'warning')
        }
      }
    }
    return health
  }

  async simulateButton(gesture: ButtonGesture) {
    this.assertTestConnection(await this.requireTest(TestOperationV2.INPUT_SIMULATE))
    const ids: Record<ButtonGesture, number> = { single: 1, double: 2, long: 3 }
    await this.requestV2(CommandV2.RUN_TEST, new Uint8Array([TestOperationV2.INPUT_SIMULATE, ids[gesture]]))
    return
  }

  async setBluetoothName(name: string) {
    const state = useDeviceStore.getState()
    const capabilities = state.settingsCapabilities
    if (!capabilities.bluetoothName) throw new Error('当前固件不支持修改蓝牙名称')
    const nameFrame = encodeBluetoothName(name, capabilities.bluetoothNameMaxBytes)
    const encodedName = nameFrame.subarray(2)
    // Keep the unchanged, device-reported legacy UTF-8 label byte-for-byte.
    // ASCII validation applies to the new field, not a readback carried by V2.
    const encodedLabel = new TextEncoder().encode(state.localAlias)
    if (encodedLabel.length > capabilities.deviceLabelMaxBytes) throw new Error('设备返回的设备名超出长度限制')
    const payload = new Uint8Array(4 + encodedName.length + encodedLabel.length)
    payload.set([SettingsSectionV2.IDENTITY, 1, encodedName.length, encodedLabel.length])
    payload.set(encodedName, 4)
    payload.set(encodedLabel, 4 + encodedName.length)
    await this.requestV2(CommandV2.SET_SETTINGS, payload)
    useDeviceStore.getState().setIdentity({ deviceName: name.trim() })
    return
  }

  async setDeviceLabel(label: string) {
    const state = useDeviceStore.getState()
    const capabilities = state.settingsCapabilities
    if (!capabilities.deviceLabel) throw new Error('当前固件不支持保存设备名')
    const labelFrame = encodeDeviceLabel(label, capabilities.deviceLabelMaxBytes)
    // A reported GAP name is not permission to write it back. base/read-only
    // firmware requires NameLen=0 while saving the independent label field.
    const encodedName = capabilities.bluetoothName ? new TextEncoder().encode(state.deviceName) : new Uint8Array()
    if (encodedName.length > capabilities.bluetoothNameMaxBytes) throw new Error('设备返回的蓝牙名超出长度限制')
    const encodedLabel = labelFrame.subarray(2)
    const payload = new Uint8Array(4 + encodedName.length + encodedLabel.length)
    payload.set([SettingsSectionV2.IDENTITY, 1, encodedName.length, encodedLabel.length])
    payload.set(encodedName, 4)
    payload.set(encodedLabel, 4 + encodedName.length)
    await this.requestV2(CommandV2.SET_SETTINGS, payload)
    useDeviceStore.getState().setIdentity({ localAlias: label.trim() })
    return
  }

  async setButtonActions(settings: ButtonBehaviorSettings) {
    const capabilities = useDeviceStore.getState().settingsCapabilities
    const gestures: ButtonGesture[] = ['single', 'double', 'long']
    for (let index = 0; index < gestures.length; index++) {
      const gesture = gestures[index]
      if ((capabilities.buttonGestureMask & (1 << index)) === 0) throw new Error(`当前固件不支持${gesture === 'single' ? '单击' : gesture === 'double' ? '双击' : '长按'}动作`)
      if (!capabilities.buttonActionMask || !inputActionSupported(capabilities.buttonActionMask, settings[gesture].action)) {
        throw new Error('当前固件不支持所选按键动作')
      }
    }
    for (const gesture of gestures) await this.setInputAction(gesture, settings[gesture])
    useDeviceStore.getState().setReportedButtonActions(settings)
  }

  async getRuntimePinConfig() {
    if (!this.protocolV2 || !useDeviceStore.getState().settingsCapabilities.runtimePinConfig) throw new Error('当前固件未声明运行时引脚配置能力')
    const generation = this.connectionGeneration
    const payload = (await this.requestV2(CommandV2.GET_IO_CONFIG, new Uint8Array([2]))).payload
    if (generation !== this.connectionGeneration) throw new Error('连接已变化，未采用其他设备的引脚配置')
    const report = decodePinReport(payload)
    useDeviceStore.getState().setReportedPinConfig(report)
    return report
  }

  async setRuntimePinConfig(config: PinConfig | null) {
    if (useDeviceStore.getState().ioWriteUnconfirmed) throw new PinWriteUnconfirmedError()
    const payload = pinSetRequest(config)
    const generation = this.connectionGeneration
    const current = await this.getRuntimePinConfig()
    if (!current.editableRoles) throw new Error('当前设备引脚仅支持只读，不能修改或恢复默认值')
    if (!current.defaultValid) throw new Error('设备默认引脚无效，拒绝修改未知接线状态')
    if (config) {
      validatePinUpdate(config, current)
      const caps = useDeviceStore.getState().settingsCapabilities
      if (caps.runtimePinConfig || caps.customButtonGpio) {
        const aux = await this.getAuxIoConfiguration()
        const pins = pinRoles.slice(0, 10).map(role => config.pins[role]).filter(pin => pin !== 255)
        if (auxPins(aux).some(pin => pins.includes(pin))) throw new Error('屏幕/LED 接线与当前按键/蜂鸣器 GPIO 冲突')
      }
    }
    if (generation !== this.connectionGeneration) throw new Error('连接已变化，未发送引脚写入')
    try {
      const response = (await this.requestV2(CommandV2.SET_IO_CONFIG, payload)).payload
      if (generation !== this.connectionGeneration) throw new PinWriteUnconfirmedError()
      const report = decodePinReport(response)
      const expected = config ?? report.default
      if (!expected) throw new PinWriteUnconfirmedError()
      const expectedBytes = encodePinBlock(expected)
      if (!encodePinBlock(report.effective).every((value, index) => value === expectedBytes[index])) throw new PinWriteUnconfirmedError()
      useDeviceStore.getState().setReportedPinConfig(report)
      return report
    } catch (error) {
      if (error instanceof ProtocolV2Error && ![StatusV2.TIMEOUT, StatusV2.PERSISTENCE_ERROR, StatusV2.IO_ERROR, StatusV2.INTERNAL_ERROR].some(status => status === error.status)) throw error
      if (generation === this.connectionGeneration) useDeviceStore.setState({ ioWriteUnconfirmed: true })
      throw new PinWriteUnconfirmedError()
    }
  }


  async getAuxIoConfiguration(): Promise<AuxIoConfiguration> {
    const caps = useDeviceStore.getState().settingsCapabilities
    if (!this.protocolV2 || (!caps.runtimePinConfig && !caps.customButtonGpio)) throw new Error('当前固件未声明 GPIO 读回能力')
    const generation = this.connectionGeneration
    const payload = (await this.requestV2(CommandV2.GET_IO_CONFIG, new Uint8Array([3]))).payload
    const config = decodeAuxIoConfiguration(payload)
    if (generation !== this.connectionGeneration) throw new Error('设备已变化，未采用其他设备的 GPIO')
    useDeviceStore.getState().setReportedAuxIoConfiguration(config)
    useDeviceStore.getState().setReportedAuxIo(firstAuxIoSettings(config))
    return config
  }

  async setAuxIoConfiguration(config: AuxIoConfiguration): Promise<AuxIoConfiguration> {
    const writeCaps = useDeviceStore.getState().settingsCapabilities
    if (!this.protocolV2 || (!writeCaps.customButtonGpio)) throw new Error('当前固件未开放扩展 GPIO 写入')
    if (useDeviceStore.getState().ioWriteUnconfirmed) throw new PinWriteUnconfirmedError()
    const payload = encodeAuxIoConfiguration(config)
    const generation = this.connectionGeneration
    const current = await this.getAuxIoConfiguration()
    payload.set(current.reserved, 7)
    if (config.version !== current.version) throw new Error('设备 GPIO 协议已变化，请重新连接；未发送写入')
    const caps = useDeviceStore.getState().settingsCapabilities
    if (config.buttons.some(button => button.gpio !== 255) && !caps.customButtonGpio) throw new Error('当前固件不支持按键 GPIO')
    if (caps.runtimePinConfig) {
      const report = await this.getRuntimePinConfig()
      if (!report.defaultValid) throw new Error('默认接线无效，拒绝修改扩展 GPIO')
      const pins = pinRoles.slice(0, 10).map(role => report.effective.pins[role]).filter(pin => pin !== 255)
      if (auxPins(config).some(pin => pins.includes(pin))) throw new Error('按键与屏幕/LED 引脚冲突')
    }
    if (generation !== this.connectionGeneration) throw new Error('设备已变化，未发送 GPIO 写入')
    try {
      const response = (await this.requestV2(CommandV2.SET_IO_CONFIG, payload)).payload
      const applied = decodeAuxIoConfiguration(response)
      if (generation !== this.connectionGeneration || !encodeAuxIoConfiguration(applied).every((value, index) => value === payload[index]) || response.length !== payload.length) throw new PinWriteUnconfirmedError()
      useDeviceStore.getState().setReportedAuxIoConfiguration(applied)
      useDeviceStore.getState().setReportedAuxIo(firstAuxIoSettings(applied))
      return applied
    } catch (error) {
      if (error instanceof ProtocolV2Error && ![StatusV2.TIMEOUT, StatusV2.PERSISTENCE_ERROR, StatusV2.IO_ERROR, StatusV2.INTERNAL_ERROR].some(status => status === error.status)) throw error
      if (generation === this.connectionGeneration) useDeviceStore.setState({ ioWriteUnconfirmed: true })
      throw new PinWriteUnconfirmedError()
    }
  }


  async getAdvertisingConfig(): Promise<AdvertisingConfig | null> {
    const generation = this.connectionGeneration
    try {
      const reply = await this.requestV2(CommandV2.GET_ADVERTISING_CONFIG)
      if (generation !== this.connectionGeneration) throw new Error('连接已改变，请重新读取广播设置')
      return decodeAdvertisingConfig(reply.payload)
    } catch (error) {
      if (error instanceof ProtocolV2Error && error.status === StatusV2.UNSUPPORTED) return null
      throw error
    }
  }

  async setAdvertisingConfig(config: AdvertisingConfig): Promise<AdvertisingConfig> {
    const wire = encodeAdvertisingConfig(config)
    const generation = this.connectionGeneration
    const reply = await this.requestV2(CommandV2.SET_ADVERTISING_CONFIG, wire)
    if (generation !== this.connectionGeneration) throw new Error('连接已改变，请重新读取广播设置，不要重复写入')
    const acknowledged = decodeAdvertisingConfig(reply.payload)
    if (!reply.payload.every((value, index) => value === wire[index])) throw new Error('设备未确认所选广播设置，请读取后再试')
    const effective = await this.getAdvertisingConfig()
    if (!effective || effective.mode !== acknowledged.mode || effective.timeoutSeconds !== acknowledged.timeoutSeconds) throw new Error('广播设置回读不一致，请读取后再试')
    return effective
  }

  async setLedSettings(settings: LedSettings) {
    const capabilities = useDeviceStore.getState().settingsCapabilities
    if (!capabilities.statusLedSettings) throw new Error('当前固件不支持状态灯策略配置')
    const payload = (await this.requestV2(CommandV2.SET_INDICATOR_CONFIG, encodeLedSettings(settings))).payload
    const reported = decodeLedSettingsPayload(payload)
    if (!reported) throw new Error('设备 V2 状态灯配置响应无效')
    useDeviceStore.getState().setReportedLed(reported)
    return
  }

  private async setInputAction(source: 'nfc' | ButtonGesture, settings: NfcTapSettings | ButtonActionSettings) {
    const encoded = encodeInputAction(source, settings)
    const response = (await this.requestV2(CommandV2.SET_INPUT_CONFIG, encoded)).payload
    const applied = decodeButtonConfig(response)
    if (!applied || (source === 'nfc' && !applied.nfcSettings)) throw new Error('设备没有确认输入行为配置')
    if (source === 'nfc' && (applied.nfcSettings!.action !== settings.action ||
        applied.nfcSettings!.slotId !== encoded[3])) throw new Error('设备未应用所选 NFC 动作，请重新读取配置')
    useDeviceStore.getState().setReportedButtonActions(applied.settings)
    if (applied.nfcSettings) useDeviceStore.getState().setReportedNfcTap(applied.nfcSettings)
    return
  }

  async runNativeTemplate(action: 'calendar' | 'clock') {
    const store = useDeviceStore.getState()
    store.setAction(action, null, action === 'calendar' ? '设备正在准备绘制日历布局。' : '设备正在准备绘制时钟布局。')
    try {
      await this.syncTime(action === 'calendar' ? 1 : 2)
    } catch (error) {
      store.addLog(`${action} 模板同步失败：${String(error)}`, 'error')
      retainDisplayFailure(action, error, () => this.runNativeTemplate(action))
      throw error
    }
  }

  async showNativeDeviceInfo() {
    const store = useDeviceStore.getState()
    if (!store.settingsCapabilities.nativeDeviceInfo) {
      throw new Error('当前固件不支持原生设备信息页')
    }
    store.setAction('device-info', null, '设备正在准备绘制设备信息页布局。')
    try {
      await this.runV2DisplayWork(DisplayModeV2.DEVICE_INFO)
      return
    } catch (error) {
      retainDisplayFailure('device-info', error, () => this.showNativeDeviceInfo())
      throw error
    }
  }

  async writeDeviceInfoQr(slot: 0, value: string) {
    const store = useDeviceStore.getState()
    if (!store.settingsCapabilities.deviceInfoQr) throw new Error('当前固件不支持设备信息二维码')
    const qr = packQrValue(value)
    const crc = (crc32Bytes(qr.data) ^ 0xffffffff) >>> 0
    const begin = new Uint8Array(20)
    const view = new DataView(begin.buffer)
    begin[0] = TransferObjectV2.DEVICE_INFO_QR
    begin[1] = qr.size
    begin[2] = 1
    begin[3] = slot
    view.setUint32(4, qr.data.byteLength, true)
    view.setUint32(8, crc, true)
    view.setUint32(12, qr.data.byteLength, true)
    const transactionId = this.nextV2Transaction()
    let transferActive = false
    try {
      const reply = (await this.requestV2(
        CommandV2.TRANSFER_BEGIN, begin, RequestFlagV2.NONE, 15000, transactionId,
      )).payload
      transferActive = true
      const deviceChunk = reply.length >= 4
        ? new DataView(reply.buffer, reply.byteOffset, reply.byteLength).getUint16(2, true)
        : this.v2WritePayload
      const chunkSize = Math.max(20, Math.min(this.v2WritePayload, deviceChunk))
      for (let offset = 0; offset < qr.data.byteLength; offset += chunkSize) {
        const chunk = qr.data.subarray(offset, Math.min(offset + chunkSize, qr.data.byteLength))
        await this.writeV2TransferChunk(
          transactionId, TransferObjectV2.DEVICE_INFO_QR,
          qr.data.byteLength, offset, chunk,
        )
      }
      await this.requestV2(
        CommandV2.TRANSFER_COMMIT, new Uint8Array(0), RequestFlagV2.NONE, 15000, transactionId,
      )
      transferActive = false
      return
    } catch (error) {
      if (transferActive) {
        await this.requestV2(
          CommandV2.TRANSFER_ABORT, new Uint8Array(0), RequestFlagV2.NONE, 5000, transactionId,
        ).catch(() => undefined)
      }
      throw error
    }
  }

  async writeNfcWebLink(value: string) {
    const url = new URL(value)
    const fragment = new URLSearchParams(url.hash.replace(/^#\/?\??/, ''))
    if (url.searchParams.has('auth') || fragment.has('auth')) {
      throw new Error('控制凭据只能写入设备二维码，不能写入 NFC。')
    }
    let capabilities
    try {
      capabilities = decodeNfcCapabilities(
        (await this.requestV2(CommandV2.GET_NFC_CAPABILITIES)).payload,
      )
    } catch (error) {
      if (
        error instanceof ProtocolV2Error
        && (error.status === StatusV2.UNKNOWN_COMMAND || error.status === StatusV2.UNSUPPORTED)
      ) {
        return { status: 'unsupported' as const, value: value.trim() }
      }
      throw error
    }
    if ((capabilities.flags & NfcCapabilityV2.NDEF_WRITE) === 0) {
      return { status: 'unsupported' as const, value: value.trim() }
    }
    const ndef = encodeNfcUriRecord(value)
    await this.requestV2(CommandV2.SET_NFC_NDEF, encodeNfcNdef(ndef))
    return { status: 'written' as const, value: value.trim(), bytes: ndef.byteLength }
  }

  async syncManagedDeviceLinks(bluetoothName: string, syncQr = true) {
    const state = useDeviceStore.getState()
    const value = buildInkTagDeviceUrl(bluetoothName)
    const failures: string[] = []
    let qr: unknown = { status: 'not-requested' }
    let nfc: unknown = { status: 'not-present' }
    if (syncQr) {
      try {
        let qrValue = value
        if (this.passwordStatus?.enabled) {
          if (!this.passwordStatus.authorized) throw new Error('请先验证设备密码。')
          const id = this.passwordId()
          const key = this.activePasswordCredential ?? getPasswordCredential(id)
          if (!key) throw new Error('请重新输入设备密码以更新二维码。')
          qrValue = buildPasswordQrLink(bluetoothName, id, key)
        }
        qr = await this.writeDeviceInfoQr(0, qrValue)
      } catch (error) {
        failures.push(`设备二维码：${error instanceof Error ? error.message : String(error)}`)
      }
    }
    if (state.capabilities.nfc !== 'none' || state.settingsCapabilities.nfcNdef) {
      try {
        const result = await this.writeNfcWebLink(value)
        nfc = result
        if (result.status !== 'written') {
          failures.push('NFC 链接：当前设备不支持更新，请使用支持此功能的固件后重试。')
        }
      } catch (error) {
        failures.push(`NFC 链接：${error instanceof Error ? error.message : String(error)}`)
      }
    }
    if (failures.length) {
      throw new Error(`蓝牙名已修改，但设备链接尚未完全同步：${failures.join('；')}`)
    }
    return { value, qr, nfc }
  }

  async readGuiAssetInfo(): Promise<GuiAssetInfo> {
    // A fixed base package may expose this read without online update support.
    return parseFontInfo((await this.requestV2(CommandV2.GET_GUI_ASSET_INFO)).payload)
  }

  async writeGuiAssetPackage(packageValue: GuiAssetPackage | Uint8Array,
                             onProgress?: (percent: number) => void,
                             options?: { confirmSingleUser?: boolean }) {
    const store = useDeviceStore.getState(), generation = this.connectionGeneration, deviceId = store.deviceId
    if (this.fontOperation || store.action !== 'idle') throw new Error('设备正在执行其他操作，请等待完成后再更新字库。')
    this.fontOperation = true
    store.setAction('font-update', 0, '正在替换用户字库，请保持连接。')
    const guard = () => {
      if (generation !== this.connectionGeneration || deviceId !== useDeviceStore.getState().deviceId || this.disconnectRequested)
        throw new Error('设备连接已变化，字库更新已停止；请重新读取当前设备状态。')
    }
    try {
      return await this.performGuiAssetPackage(packageValue, percent => {
        guard(); useDeviceStore.getState().setAction('font-update', percent / 100, '正在替换用户字库，请保持连接。'); onProgress?.(percent)
      }, options, guard)
    } finally {
      this.fontOperation = false
      const current = useDeviceStore.getState()
      if (generation === this.connectionGeneration && deviceId === current.deviceId && current.action === 'font-update') current.setAction('idle')
    }
  }

  private async performGuiAssetPackage(packageValue: GuiAssetPackage | Uint8Array,
                             onProgress: ((percent: number) => void) | undefined,
                             options: { confirmSingleUser?: boolean } | undefined, guard: () => void) {
    if (!useDeviceStore.getState().settingsCapabilities.guiAssetUpdate) {
      throw new Error('当前固件不支持字体资源更新')
    }
    const activation = useDeviceStore.getState().activation
    if (activation.phase !== 'active' || activation.edition !== 'advanced') {
      throw new Error('自定义字库更新需要 Pro 激活')
    }
    const bytes = packageValue instanceof Uint8Array ? packageValue : packageValue.bytes
    parseFontPackage(bytes, GUI_ASSET_ABSOLUTE_MAX_BYTES)
    const info = await this.readGuiAssetInfo()
    guard()
    const pkg = admitFontPackage(bytes, info)
    if (!options?.confirmSingleUser) {
      throw new Error('单用户字库更新失败可能丢失原自定义字体，请先确认')
    }
    const begin = new Uint8Array(20)
    const view = new DataView(begin.buffer)
    begin[0] = TransferObjectV2.GUI_ASSET_PACKAGE
    begin[1] = pkg.schema
    begin[2] = 1
    begin[3] = 0
    view.setUint32(4, pkg.bytes.byteLength, true)
    view.setUint32(8, (crc32Bytes(pkg.bytes) ^ 0xffffffff) >>> 0, true)
    view.setUint32(12, pkg.bytes.byteLength, true)
    const transactionId = this.nextV2Transaction()
    let transferActive = false
    try {
      const reply = (await this.requestV2(
        CommandV2.TRANSFER_BEGIN, begin, RequestFlagV2.NONE, 15000, transactionId,
      )).payload
      guard()
      transferActive = true
      const deviceChunk = reply.length >= 4
        ? new DataView(reply.buffer, reply.byteOffset, reply.byteLength).getUint16(2, true)
        : this.v2WritePayload
      const chunkSize = Math.max(20, Math.min(this.v2WritePayload, deviceChunk))
      for (let offset = 0; offset < pkg.bytes.byteLength; offset += chunkSize) {
        guard()
        const chunk = pkg.bytes.subarray(offset, Math.min(offset + chunkSize, pkg.bytes.byteLength))
        await this.writeV2TransferChunk(
          transactionId, TransferObjectV2.GUI_ASSET_PACKAGE,
          pkg.bytes.byteLength, offset, chunk,
        )
        guard()
        onProgress?.(Math.round(Math.min(offset + chunk.byteLength, pkg.bytes.byteLength) * 100 / pkg.bytes.byteLength))
      }
      transferActive = false // Unknown COMMIT ACK is not an abort/replay instruction.
      guard()
      await this.requestV2(
        CommandV2.TRANSFER_COMMIT, new Uint8Array(0), RequestFlagV2.NONE, 30000, transactionId,
      )
      guard()
      transferActive = false
      const committed = await this.readGuiAssetInfo()
      guard()
      if (committed.version !== info.version || committed.maximumSize !== info.maximumSize ||
          committed.activeBank === null || committed.generation !== pkg.generation ||
          committed.activeSize !== pkg.totalSize || committed.activeCrc32 !== pkg.packageCrc32 ||
          committed.schema !== pkg.schema ||
          (['storageMode','supportedSchemas','roleVersion','flags'] as const).some((key) => committed[key] !== info[key])) {
        throw new Error('字体提交后的只读核验未通过，不要重放上传')
      }
      return committed
    } catch (error) {
      if (transferActive) {
        try {
          guard()
          await this.requestV2(CommandV2.TRANSFER_ABORT, new Uint8Array(0), RequestFlagV2.NONE, 5000, transactionId)
        } catch { /* Never send cleanup to a replacement connection. */ }
      }
      throw error
    }
  }

  async runAction(action: Exclude<DeviceAction, 'idle' | 'device-info' | 'picture' | 'font-update' | 'slideshow-starting' | 'slideshow' | 'ota'>) {
    const store = useDeviceStore.getState()
    const details = {
      clear: '设备正在清屏，这一步需要等待墨水屏刷新。',
      calendar: '设备正在准备绘制日历布局。',
      clock: '设备正在准备绘制时钟布局。',
    }
    store.setAction(action, null, details[action])
    try {
      if (action === 'clear') await this.runV2DisplayWork(DisplayModeV2.CLEAR, new Uint8Array([1]))
      if (action === 'calendar') await this.syncTime(1)
      if (action === 'clock') await this.syncTime(2)
      return
    } catch (error) {
      store.addLog(`${action} 失败：${String(error)}`, 'error')
      retainDisplayFailure(action, error, () => this.runAction(action))
      throw error
    }
  }

  async stop() {
    // Display CANCEL is not a DFU abort. OTA owns its transport and cleanup.
    if (useDeviceStore.getState().action === 'ota') return
    try {
      await this.stopDeviceWork()
    } finally {
      useDeviceStore.getState().setAction('idle')
    }
  }

  keepCurrentDisplayWork() {
    pendingDisplayRetry = null
    useDeviceStore.getState().setAction('idle')
  }

  async replaceBusyDisplayWork() {
    const pending = pendingDisplayRetry
    if (!pending) {
      useDeviceStore.getState().setAction('idle')
      return
    }
    pendingDisplayRetry = null
    try {
      await this.stopDeviceWork()
      if (this.protocolV2) await this.waitForV2DisplayIdle()
    } catch (error) {
      pendingDisplayRetry = pending
      useDeviceStore.getState().setAction(pending.action, null, displayBusyDetail)
      throw error
    }
    useDeviceStore.getState().setAction('idle')
    void pending.run().catch((error) => {
      useDeviceStore.getState().addLog(
        `替换后的屏幕任务失败：${error instanceof Error ? error.message : String(error)}`,
        'error',
      )
    })
  }

  private async stopDeviceWork() {
    this.transferGeneration++
    await this.requestV2(CommandV2.CANCEL)
    return
  }

  private async waitForV2DisplayIdle(timeout = 25000) {
    const deadline = Date.now() + timeout
    while (Date.now() < deadline) {
      const remaining = Math.max(1000, deadline - Date.now())
      const payload = (await this.requestV2(
        CommandV2.GET_STATUS,
        new Uint8Array(0),
        RequestFlagV2.NONE,
        remaining,
      )).payload
      if (payload.length >= 3 && (payload[2] & 0x11) === 0) return
      await sleep(100)
    }
    throw new Error('设备取消后未在限定时间内回到空闲状态')
  }

  private async prepareSystemAction() {
    if (!this.protocolV2) throw new Error('当前设备未提供 0.2.0 系统操作协议')
    await this.stopDeviceWork()
    await this.waitForV2DisplayIdle()
  }

  supportsLifecycleV2() {
    const mask = CapabilityV2.LIFECYCLE | CapabilityV2.LIFECYCLE_V2
    return this.protocolV2 && (this.v2Capabilities & mask) === mask
  }

  private async authorizeOwnerAction(opcode: number, request: Uint8Array, flags: number, proCode?: string, localFindKey?: Uint8Array) {
    if ((this.v2Capabilities & CapabilityV2.OWNER_AUTHORIZATION) === 0) return
    const deviceIdHex = useDeviceStore.getState().activation.activationId ?? ''
    if (!/^[0-9a-fA-F]{16}$/.test(deviceIdHex) || /^0+$/.test(deviceIdHex)) {
      throw new Error('设备未返回可验证的所有者身份。')
    }
    const deviceId = Uint8Array.from(deviceIdHex.match(/../g)!, (pair) => Number.parseInt(pair, 16))
    const nonce = parseOwnerChallenge((await this.requestV2(CommandV2.GET_OWNER_CHALLENGE)).payload)
    const digest = await ownerActionHash(opcode, flags, request)
    let proof: Uint8Array | undefined
    if (localFindKey) proof = await proveOwnerWithFindK(localFindKey, deviceId, nonce, opcode, flags, request)
    if (!proof && (this.v2Capabilities & (CapabilityV2.FIND | CapabilityV2.EXPERIMENTAL_FIND)) !== 0) {
      try {
        const owner = await this.readPublicOnlyFindOwnerStatus()
        if (owner.identityPresent) {
          const result = await findClient.proveOwnerAction(
            owner.fingerprintHex, deviceIdHex, Array.from(nonce, (byte) => byte.toString(16).padStart(2, '0')).join(''),
            Array.from(digest, (byte) => byte.toString(16).padStart(2, '0')).join(''),
          )
          if (!/^[0-9a-fA-F]{100}$/.test(result.proofHex)) throw new Error('本机服务返回的所有者证明无效。')
          proof = Uint8Array.from(result.proofHex.match(/../g)!, (pair) => Number.parseInt(pair, 16))
          if (proof[0] !== 1 || proof[1] !== 2 ||
              !digest.every((byte, index) => byte === proof![index + 2])) {
            throw new Error('本机服务返回的所有者证明与操作不匹配。')
          }
        }
      } catch (error) {
        if (!proCode) throw new Error(`无法使用本机 Find 身份授权：${error instanceof Error ? error.message : String(error)}。可输入当前已安装的 Pro 激活码重试。`)
      }
    }
    if (!proof && proCode) {
      proof = await proveOwnerWithProCode(proCode, deviceId, nonce, opcode, flags, request)
    }
    if (!proof) throw new Error('此操作需要本机 Find 所有者身份或当前已安装的 Pro 激活码。')
    await this.requestV2(CommandV2.AUTHORIZE_OWNER_ACTION, proof)
  }

  private async requestSystemAction(action: number, confirmed = false, payload?: Uint8Array, timeout = 12000, proCode?: string) {
    if (action === SystemActionV2.FACTORY_RESET) throw new Error('恢复出厂功能已移除。')
    if (action === SystemActionV2.RESET_SETTINGS) throw new Error('清除设置须通过原密码验证入口。')
    const request = payload ?? new Uint8Array([action])
    const flags = confirmed ? RequestFlagV2.CONFIRM : RequestFlagV2.NONE
    const protectedAction = action === SystemActionV2.CLEAR_USER_DATA && !this.supportsLifecycleV2()
    if (protectedAction) await this.authorizeOwnerAction(CommandV2.SYSTEM_ACTION, request, flags, proCode)
    try {
      return (await this.requestV2(CommandV2.SYSTEM_ACTION, request, flags, timeout)).payload
    } catch (error) {
      if (!(error instanceof ProtocolV2Error) || error.status !== StatusV2.BUSY) throw error
      await this.prepareSystemAction()
      if (protectedAction) await this.authorizeOwnerAction(CommandV2.SYSTEM_ACTION, request, flags, proCode)
      return (await this.requestV2(CommandV2.SYSTEM_ACTION, request, flags, timeout)).payload
    }
  }

  private async systemAction(action: number, confirmed = false, payload?: Uint8Array, timeout = 12000, proCode?: string) {
    if (action !== SystemActionV2.RESET &&
        !useDeviceStore.getState().settingsCapabilities.lifecycle) {
      throw new Error('当前设备未提供完整的生命周期操作')
    }
    return this.requestSystemAction(action, confirmed, payload, timeout, proCode)
  }

  private async waitForDeviceRestart(device: BluetoothDevice, label: string, wakeDelayMs = 0, verifyAfterReconnect = false,
      onPhase?: (message: string) => void) {
    const store = useDeviceStore.getState()
    const previousConnection = store.connection
    const generation = this.connectionGeneration
    store.setConnection('connecting')
    store.addLog(`${label}命令已确认，等待蓝牙连接中断后重新连接`)
    onPhase?.('等待设备重连')

    if (device.gatt?.connected) {
      await new Promise<void>((resolve, reject) => {
        const disconnected = () => {
          window.clearTimeout(timer)
          device.removeEventListener('gattserverdisconnected', disconnected)
          resolve()
        }
        const timer = window.setTimeout(() => {
          device.removeEventListener('gattserverdisconnected', disconnected)
          // A missing browser event is not a timeout if the link is already down.
          if (device.gatt?.connected === false) {
            resolve()
            return
          }
          // Keep a still-usable connection visible, without claiming reboot success
          // or restoring an older connection over a newer target/attempt.
          if (previousConnection === 'connected' && this.connectionGeneration === generation &&
              this.device === device && this.server?.connected && this.characteristic &&
              useDeviceStore.getState().deviceId === device.id) {
            store.setConnection('connected')
          }
          const message = `${label}命令已确认，但 20 秒内未检测到蓝牙连接中断，操作结果尚未确认。请先检查设备状态或手动重新连接，不要重复发送该操作。`
          store.addLog(message, 'warning')
          reject(new Error(message))
        }, 20000)
        device.addEventListener('gattserverdisconnected', disconnected)
      })
    }

    if (wakeDelayMs > 0) await sleep(wakeDelayMs)
    const deadline = Date.now() + 45000
    let lastError: unknown
    for (let attempt = 0; attempt < 12 && Date.now() < deadline; attempt += 1) {
      await sleep(attempt === 0 ? 500 : 900)
      if (this.disconnectRequested || (this.device && this.device !== device) ||
          (useDeviceStore.getState().deviceId && useDeviceStore.getState().deviceId !== device.id)) {
        throw new Error('重启回连已取消或设备目标已变化；未重复重启')
      }
      try {
        await this.reconnectDevice(device, Math.min(10000, deadline - Date.now()))
        if (this.device !== device || useDeviceStore.getState().deviceId !== device.id) throw new Error('重连目标已变化')
        store.addLog(verifyAfterReconnect ? `${label}后已重连，正在核对设备状态` : `${label}完成，设备已自动重连`, verifyAfterReconnect ? 'rx' : 'success')
        return
      } catch (error) {
        lastError = error
      }
    }
    throw new Error(`${label}命令已确认，但未能自动重新连接。请手动连接同一设备以确认结果${lastError ? `：${String(lastError)}` : ''}`)
  }

  async softReset(options: { syncTime?: boolean; onPhase?: (message: string) => void } = {}) {
    const device = this.device
    if (!device || !this.protocolV2 || this.otaRecoveryType) throw new Error('请先连接需要重启的应用设备')
    const identity = this.applicationReturnIdentity()
    this.verifyReturnedApplication(device, identity)
    return this.withoutAutomaticClockSync(async () => {
      options.onPhase?.('正在重启设备')
      try { await this.systemAction(SystemActionV2.RESET) }
      catch (error) {
        if (error instanceof ProtocolV2Error) throw error
        throw new Error(`重启请求未获确认，结果未知：${error instanceof Error ? error.message : String(error)}。未重复重启，请只读重连核对。`)
      }
      await this.waitForDeviceRestart(device, '重启', 0, true, options.onPhase)
      options.onPhase?.('正在核对设备')
      await this.passwordBootstrap
      this.verifyReturnedApplication(device, identity)
      if (options.syncTime !== false) {
        options.onPhase?.('正在校时')
        try { await this.syncTimeAndVerify() }
        catch (error) { throw new Error(`设备已重连，但校时未完成：${error instanceof Error ? error.message : String(error)}。未重复重启或校时。`) }
      }
      useDeviceStore.getState().addLog(options.syncTime === false ? '重启完成；已按选择跳过校时' : '重启完成，设备已重连并校时', 'success')
    })
  }

  async enterOtaRecovery() {
    if (useDeviceStore.getState().capabilities.ota === 'none') {
      throw new Error('当前设备未声明固定 Recovery 升级能力')
    }
    const state = useDeviceStore.getState()
    const type = state.capabilities.ota
    if ((type === 'dialog' || type === 'nordic') && this.device && this.runtimeImageInfo) {
      this.dialogOriginalImage = { deviceId: this.device.id, image: { ...this.runtimeImageInfo } }
    }
    if (type === 'goodix' || type === 'onmicro' || type === 'dialog' || type === 'telink' || type === 'nordic') {
      const platform = type === 'goodix' ? 'GR' : type === 'onmicro' ? 'OM' : type === 'dialog' ? 'DA' : type === 'nordic' ? 'NRF' : 'TL'
      const id4 = state.bleAddress.replace(/[^0-9A-F]/gi, '').slice(-4).toUpperCase()
      if (!/^[0-9A-F]{4}$/.test(id4) || !this.device) {
        throw new Error('设备未回传可验证的稳定 BLE 地址，不能安全切换 Recovery')
      }
      this.expectedFixedRecovery = {
        type,
        deviceId: this.device.id,
        name: `IKT_${platform}_${id4}_OTA`,
        legacyName: `InkTag_${platform}_${id4}_OTA`,
      }
      this.recoveryHandoffDevice = this.device
    }
    if (type === 'goodix') {
      // GR prepare is one public action before one vendor mode commit. A BUSY
      // refusal is not permission to CANCEL/replay prepare behind the UI lock.
      await this.requestV2(CommandV2.SYSTEM_ACTION, new Uint8Array([SystemActionV2.OTA_RECOVERY]), RequestFlagV2.NONE, 12000)
    } else await this.requestSystemAction(SystemActionV2.OTA_RECOVERY)
  }

  async enterTimedDeepSleep(seconds: number) {
    if (!Number.isInteger(seconds) || seconds < 5 || seconds > 3600) {
      throw new RangeError('深睡时长必须是 5–3600 秒的整数')
    }
    if (!useDeviceStore.getState().settingsCapabilities.experimentalTimedSleep) {
      throw new Error('当前设备未开启实验性定时深睡能力')
    }
    const payload = new Uint8Array(7)
    payload[0] = SystemActionV2.SLEEP
    payload[1] = 1
    payload[2] = SleepWakeFlagV2.TIMER
    new DataView(payload.buffer).setUint32(3, seconds, true)
    const device = this.device
    await this.systemAction(SystemActionV2.SLEEP, false, payload)
    if (device) await this.waitForDeviceRestart(device, '定时深睡', seconds * 1000)
  }

  async resetDeviceSettings(oldPassword?: string) {
    if (!this.supportsLifecycleV2()) throw new Error('当前固件不支持新的清除设置与密码语义。')
    if (this.passwordOperation) throw new Error('密码操作正在进行。')
    this.passwordOperation = true
    const generation = this.connectionGeneration
    const device = this.device
    try {
      const status = await this.readPasswordStatus()
      const id = status ? this.passwordId() : null
      if (status?.enabled) {
        if (!oldPassword) throw new Error('请输入原密码；已连接或扫码凭据不能替代。')
        if (!status.authorized) {
          const key = await derivePasswordCredential(id!, oldPassword)
          this.assertPasswordConnection(generation)
          await this.provePassword(key)
        }
        if (Date.now() < this.passwordAttemptAfter) await sleep(this.passwordAttemptAfter - Date.now())
        const key = await derivePasswordChangeKey(id!, oldPassword)
        this.assertPasswordConnection(generation)
        await this.provePassword(key, 2, RESET_SETTINGS_PROOF_DATA)
      }
      this.assertPasswordConnection(generation)
      // Single-use W: no BUSY retry, candidate transfer or password-disable step.
      try {
        await this.requestV2(CommandV2.SYSTEM_ACTION, new Uint8Array([SystemActionV2.RESET_SETTINGS]), RequestFlagV2.CONFIRM, 12000)
      } catch (error) {
        if (error instanceof ProtocolV2Error) throw error
        // No ACK is not proof of failure or success. Never resubmit action5 or
        // erase the cached credential; an ordinary read-only reconnect can
        // inspect the resulting state without another destructive command.
        throw new Error('清除设置未收到确认，结果未知；请只读重连核对，勿重复清除。')
      }
      if (device) await this.waitForDeviceRestart(device, '清除设置', 0, true)
      if (!this.supportsLifecycleV2()) throw new Error('清除设置后能力读回不符，请重连核对，勿重复清除。')
      if (id) {
        if (!this.passwordSupported() || this.passwordId() !== id || (await this.readPasswordStatus())?.enabled !== false) {
          throw new Error('清除设置已受理，但密码关闭尚未确认；请重连核对，勿重复清除。')
        }
        this.rememberPasswordCredential(id, null)
        this.rejectedPasswordKeys.delete(id)
      }
    } finally { this.passwordOperation = false; this.passwordNotify() }
  }

  async clearDeviceUserData() {
    if (!this.supportsLifecycleV2()) throw new Error('当前固件不支持仅清除图槽的新语义。')
    const payload = await this.systemAction(SystemActionV2.CLEAR_USER_DATA, true, undefined, 60000)
    if (payload.length < 2) throw new Error('清除图槽已受理，但返回结果未确认。')
    const removed = new DataView(payload.buffer, payload.byteOffset, payload.byteLength).getUint16(0, true)
    if (this.v2Capabilities & CapabilityV2.RESOURCES) {
      const info = (await this.requestV2(CommandV2.GET_RESOURCE_INFO)).payload
      if (info.length < 30 || info[0] !== 1 || new DataView(info.buffer, info.byteOffset, info.byteLength).getUint16(17, true) !== 0) {
        throw new Error('图槽清空读回尚未确认，请重读状态，勿盲目重试。')
      }
    }
    useDeviceStore.getState().setImageStoreItems([])
    await this.refreshV2ResourceInfo()
    return removed
  }

  async setScreen(config: ScreenConfig) {
    if (!this.protocolV2) throw new Error('设备未启用当前 v1 协议')
    if (useDeviceStore.getState().ioWriteUnconfirmed) throw new ScreenWriteUnconfirmedError()
    if (!validateScreenConfig(config)) throw new RangeError('屏幕驱动、分辨率、颜色或可视区域组合无效')
    const generation = this.connectionGeneration
    const deviceId = useDeviceStore.getState().deviceId
    const sameConnection = () => generation === this.connectionGeneration && deviceId === useDeviceStore.getState().deviceId
    try {
      const payload = (await this.requestV2(CommandV2.SET_SCREEN_PROFILE, encodeScreenConfig(config))).payload
      const saved = decodeScreenConfig(payload)
      if (!sameConnection() || !saved || !screenConfigsEqual(saved, config)) throw new ScreenWriteUnconfirmedError()
      useDeviceStore.setState({ screen: saved, reportedPinConfig: null, imageStoreInfo: null, imageStoreItems: [] })
      await this.refreshV2ResourceInfo().catch(() => undefined)
      if (useDeviceStore.getState().settingsCapabilities.runtimePinConfig && sameConnection()) {
        await this.getRuntimePinConfig().catch(() => undefined)
      }
      if (!sameConnection()) throw new ScreenWriteUnconfirmedError()
      return saved
    } catch (error) {
      if (error instanceof ProtocolV2Error && error.status === StatusV2.DENIED && screenRequiresPro(config)) {
        throw new ProtocolV2Error(error.status, '设备拒绝了 E6 屏幕配置。E6 屏幕控制需要有效 Pro，请同时检查设备认证；离线预览和图片转换不受限制。')
      }
      if (error instanceof ProtocolV2Error && ![StatusV2.TIMEOUT, StatusV2.PERSISTENCE_ERROR, StatusV2.IO_ERROR, StatusV2.INTERNAL_ERROR].some(status => status === error.status)) throw error
      if (sameConnection()) {
        this.resourceReadSequence++
        this.resourceReadAbort.abort()
        useDeviceStore.setState({ ioWriteUnconfirmed: true, screen: null, reportedPinConfig: null,
          imageStoreInfo: null, imageStoreItems: [] })
      }
      throw new ScreenWriteUnconfirmedError()
    }
  }

  async sendCanvas(canvas: HTMLCanvasElement, name: string, saveToSlot = false, contentType: ImageSlotContentType = 'image', replaceSlotId?: number, displayAfterStore = true, readingRotation = 0) {
    const store = useDeviceStore.getState()
    const screen = store.screen
    if (!screen) throw new Error('尚未读取设备屏幕配置')
    if (saveToSlot && replaceSlotId !== undefined) {
      const info = store.imageStoreInfo
      const existing = store.imageStoreItems.some((item) => item.id === replaceSlotId)
      const empty = this.protocolV2 && info?.fixedSlotCount && info.occupiedIds !== undefined
        && replaceSlotId < info.fixedSlotCount && info.contiguousFit > 0
        && !info.occupiedIds.includes(replaceSlotId)
      if (!Number.isInteger(replaceSlotId) || replaceSlotId < 0 || replaceSlotId >= (info?.fixedSlotCount ?? 255)
        || (!existing && !empty)) throw new Error('该图槽不可写入；请读取图槽列表或使用自动分配')
    }
    store.setAction('picture', 0, '正在处理页面并准备传输。')
    await new Promise<void>((resolve) => requestAnimationFrame(() => resolve()))
    try {
      const context = canvas.getContext('2d', { willReadFrequently: true })
      if (!context) throw new Error('无法读取页面画布')
      const source = context.getImageData(0, 0, canvas.width, canvas.height)
      const encoded = encodeImage({ width: source.width, height: source.height, data: source.data }, screen)
      const totalBytes = encoded.transfers.reduce((sum, item) => sum + item.data.length, 0)
      store.addLog(`准备发送 ${screen.width}×${screen.height} 页面，共 ${totalBytes} bytes`)
      return await this.sendCanvasV2(canvas, encoded.transfers, encoded.format, screen, name, saveToSlot, contentType, replaceSlotId, displayAfterStore, readingRotation)
    } catch (error) {
      retainDisplayFailure('picture', error, () => this.sendCanvas(canvas, name, saveToSlot, contentType, replaceSlotId, displayAfterStore, readingRotation))
      store.addLog(`图片传输失败：${error instanceof Error ? error.message : String(error)}`, 'error')
      throw error
    }
  }

  private async writeV2TransferChunk(
    transactionId: number,
    objectType: number,
    totalSize: number,
    offset: number,
    chunk: Uint8Array<ArrayBufferLike>,
    decodedLength?: number,
    timeout = 12000,
    imageTiming?: ImageTimingSink,
  ) {
    const generation = this.connectionGeneration
    const workGeneration = this.transferGeneration
    const device = this.device, server = this.server, characteristic = this.characteristic
    const ownsChunk = () => generation === this.connectionGeneration && workGeneration === this.transferGeneration &&
      device === this.device && server === this.server && characteristic === this.characteristic && !this.disconnectRequested
    const logicalLength = decodedLength ?? chunk.byteLength
    const end = offset + logicalLength
    const payload = new Uint8Array(4 + (decodedLength === undefined ? 0 : 2) + chunk.byteLength)
    new DataView(payload.buffer).setUint32(0, offset, true)
    if (decodedLength !== undefined) new DataView(payload.buffer).setUint16(4, decodedLength, true)
    payload.set(chunk, decodedLength === undefined ? 4 : 6)
    // Keep the sequential product ACK; only omit Nordic's qualified redundant ATT ACK.
    // TLSR trials showed no stable benefit, so retain its ordinary ATT response.
    const withResponse = !(objectType === TransferObjectV2.DIRECT_FRAME
      && canonicalBoardId(useDeviceStore.getState().boardId) === 'nrf52832_priceetag'
      && this.characteristic?.properties?.writeWithoutResponse === true
      && payload.byteLength + 3 <= Math.min(this.imageMtu, 244))
    if (offset === 0) {
      useDeviceStore.getState().addLog(`图片链路：ATT response=${withResponse}，数据帧 ${payload.byteLength + 3} B`)
    }
    for (let attempt = 0; attempt < 3; attempt += 1) {
      try {
        await this.requestV2(
          CommandV2.TRANSFER_WRITE, payload, RequestFlagV2.NONE, timeout, transactionId, true, withResponse, imageTiming,
        )
        return
      } catch (error) {
        if (!(error instanceof ProtocolV2Error) || error.status !== StatusV2.TIMEOUT) throw error
        if (!ownsChunk()) throw error
        const status = (await this.requestV2(
          CommandV2.GET_TRANSFER_STATUS, new Uint8Array(0), RequestFlagV2.NONE,
          5000, transactionId, true,
        )).payload
        if (!ownsChunk()) throw error
        if (status.byteLength !== 11) throw new Error('设备返回了无效的传输状态', { cause: error })
        const view = new DataView(status.buffer, status.byteOffset, status.byteLength)
        const written = view.getUint32(3, true)
        const reportedTotal = view.getUint32(7, true)
        if (status[0] !== 1 || status[1] !== objectType || status[2] !== transactionId || reportedTotal !== totalSize) {
          throw new Error('传输 ACK 丢失后，设备事务状态已经改变', { cause: error })
        }
        if (written === end) return
        if (written !== offset || attempt === 2) {
          throw new Error(`设备传输偏移为 ${written}，预期 ${offset} 或 ${end}`, { cause: error })
        }
      }
    }
    throw new Error('传输分块重试次数已用尽')
  }

  private async commitV2Transfer(
    transactionId: number,
    objectType: number,
    totalSize: number,
    completion: Promise<Uint8Array> | null,
    timeout = 300000,
  ): Promise<{ payload: Uint8Array; completionConsumed: boolean }> {
    const generation = this.connectionGeneration
    const workGeneration = this.transferGeneration
    const device = this.device, server = this.server, characteristic = this.characteristic
    const ownsCommit = () => generation === this.connectionGeneration && workGeneration === this.transferGeneration &&
      device === this.device && server === this.server && characteristic === this.characteristic && !this.disconnectRequested
    try {
      const reply = await this.requestV2(
        CommandV2.TRANSFER_COMMIT, new Uint8Array(0), RequestFlagV2.NONE,
        Math.max(15000, timeout), transactionId,
      )
      return { payload: reply.payload, completionConsumed: false }
    } catch (error) {
      if (!(error instanceof ProtocolV2Error) || error.status !== StatusV2.TIMEOUT) throw error
      if (!ownsCommit()) throw error
      const status = (await this.requestV2(
        CommandV2.GET_TRANSFER_STATUS, new Uint8Array(0), RequestFlagV2.NONE,
        5000, transactionId, true,
      )).payload
      if (!ownsCommit()) throw error
      if (status.byteLength !== 11) throw new Error('设备返回了无效的传输状态', { cause: error })
      const view = new DataView(status.buffer, status.byteOffset, status.byteLength)
      const written = view.getUint32(3, true)
      const reportedTotal = view.getUint32(7, true)
      if (status[0] > 1) throw new Error('设备返回了无效的传输活动状态', { cause: error })
      if (status[0] === 1 && (
        status[1] !== objectType || status[2] !== transactionId ||
        written !== totalSize || reportedTotal !== totalSize
      )) {
        throw new Error('提交 ACK 丢失后，设备事务状态已经改变', { cause: error })
      }
      if (!completion) throw new Error('提交 ACK 丢失，且无法验证设备完成状态', { cause: error })
      await completion
      return { payload: new Uint8Array(0), completionConsumed: true }
    }
  }

  private async sendCanvasV2(
    canvas: HTMLCanvasElement,
    transfers: ImageTransfer[],
    format: number,
    screen: ScreenConfig,
    rawName: string,
    saveToSlot: boolean,
    contentType: ImageSlotContentType,
    replaceSlotId?: number,
    displayAfterStore = true,
    readingRotation = 0,
  ) {
    const store = useDeviceStore.getState()
    if (!Number.isInteger(readingRotation) || readingRotation < 0 || readingRotation > 3) throw new Error('图片阅读方向无效')
    if (readingRotation && !(this.v2CapabilityExtensions & CapabilityExtensionV2.IMAGE_READING_ORIENTATION))
      throw new Error('设备尚未声明图片阅读方向支持')
    const generation = ++this.transferGeneration
    const connectionGeneration = this.connectionGeneration
    const device = this.device
    const server = this.server
    const characteristic = this.characteristic
    const ownsImage = () => {
      const current = useDeviceStore.getState()
      return generation === this.transferGeneration && connectionGeneration === this.connectionGeneration &&
        !this.disconnectRequested && device === this.device && server === this.server &&
        characteristic === this.characteristic && (!server || server.connected) &&
        current.deviceId === store.deviceId && current.bleAddress === store.bleAddress &&
        current.connection === store.connection
    }
    let imageCommitted = false
    const assertImageOwner = () => {
      if (!ownsImage()) throw new Error(imageCommitted
        ? '图片已提交，但设备连接已改变；已停止后续显示和读回，请勿重复上传'
        : '图片传输已停止：设备连接或任务已改变')
    }
    const totalSize = transfers.reduce((sum, transfer) => sum + transfer.data.length, 0)
    const name = saveToSlot ? encodeUploadImageName(rawName, Boolean(store.capabilities.imageMetadata)) : new Uint8Array(0)
    // A replacement carries its target after the variable-length name. This
    // leaves the existing append frame byte-for-byte unchanged and lets the
    // device authorize a quota-neutral replacement. An in-place backend may
    // lose the target's old image if interrupted; this frame promises no rollback.
    const begin = new Uint8Array(saveToSlot ? 39 + name.length + (replaceSlotId === undefined ? 0 : 1) : 20)
    const beginView = new DataView(begin.buffer)
    begin[0] = saveToSlot ? TransferObjectV2.STORED_IMAGE : TransferObjectV2.DIRECT_FRAME
    begin[1] = format
    begin[2] = transfers.length
    begin[3] = transfers.reduce((mask, item, index) => mask | ((item.step === 'bw' || item.step === 'gray-msb' || item.step === 'e6-area0') ? 1 << index : 0), 0)
    begin[3] |= readingRotation << 2
    beginView.setUint32(4, totalSize, true)
    beginView.setUint32(8, imageCrc32(transfers), true)
    beginView.setUint32(12, transfers[0]?.data.length || 0, true)
    beginView.setUint32(16, transfers[1]?.data.length || 0, true)
    if (saveToSlot) {
      begin[20] = imageSlotContentTypeIds[contentType]
      begin.set(encodeScreenConfig(screen), 21)
      begin[38] = name.length
      begin.set(name, 39)
      if (replaceSlotId !== undefined) begin[39 + name.length] = replaceSlotId
    }

    type PlannedBlock = { block: PackBitsBlock; step: ImageStep }
    const lzoSupported = Boolean(this.v2CapabilityExtensions & CapabilityExtensionV2.TRANSFER_LZO1X_2000)
    const rleSupported = Boolean(this.v2Capabilities & CapabilityV2.TRANSFER_PACKBITS_RLE)
    let codec: 'lzo1x' | 'packbits-rle' = lzoSupported ? 'lzo1x' : 'packbits-rle'
    const segmentLengths = transfers.map((transfer) => transfer.data.length)
    const segmentStarts = subsequentSegmentStarts(segmentLengths)
    const buildCompressionPlan = (chunkSize: number): PlannedBlock[] => {
      const blocks: PlannedBlock[] = []
      let decodedOffset = 0
      for (const transfer of transfers) {
        const encoded = codec === 'lzo1x'
          ? buildLzo1xBlocks(transfer.data, decodedOffset, chunkSize - 2)
          : buildPackBitsBlocks(transfer.data, decodedOffset, 0xffff, chunkSize - 2)
        blocks.push(...encoded.map((block) => ({ block, step: transfer.step })))
        decodedOffset += transfer.data.length
      }
      return blocks
    }
    const negotiatedChunk = Math.max(20, this.v2WritePayload)
    // A multi-plane controller may synchronously refresh when the next plane
    // starts, delaying the first block's ACK at that decoded start offset.
    let compressedBlocks = buildCompressionPlan(negotiatedChunk)
    if (codec === 'lzo1x' && rleSupported &&
        !packBitsIsWorthwhile(compressedBlocks.map(({ block }) => block), segmentLengths, negotiatedChunk)) {
      codec = 'packbits-rle'
      compressedBlocks = buildCompressionPlan(negotiatedChunk)
    }
    let usePackBits = (
      (codec === 'lzo1x' ? lzoSupported : rleSupported) &&
      packBitsIsWorthwhile(compressedBlocks.map(({ block }) => block), segmentLengths, negotiatedChunk)
    )
    if (usePackBits) begin[3] |= codec === 'lzo1x' ? TransferDescriptorFlagV2.LZO1X : TransferDescriptorFlagV2.PACKBITS_RLE

    let transactionId = this.nextV2Transaction()
    let transferActive = false
    let imageLink: ImageLinkLease | null = null
    try {
      store.setAction('picture', 0, saveToSlot ? '正在准备设备图槽。' : '正在初始化屏幕和传输通道。')
      const currentApp = () => {
        const current = useDeviceStore.getState()
        return ownsImage() && current.connection === 'connected' &&
          Boolean(device && device === this.device && device.id === current.deviceId &&
            this.server?.connected && this.characteristic && this.protocolV2) &&
          current.bleAddress === store.bleAddress
      }
      let lastLinkDetail = ''
      imageLink = await acquireImageLink(store.bleAddress, store.boardId, currentApp, (event) => {
        if (!currentApp()) return
        const p = event.parameters
        const detail = `图片原生链路：${event.phase}${event.reason ? ` reason=${event.reason}` : ''}` +
          ` actualCI=${p?.intervalMs ?? 'unknown'}ms latency=${p?.latency ?? 'unknown'}` +
          ` tx2M=${p?.tx2M ?? 'unknown'} rx2M=${p?.rx2M ?? 'unknown'}`
        if (detail !== lastLinkDetail) { store.addLog(detail); lastLinkDetail = detail }
      })
      assertImageOwner()
      const beginTimeout = saveToSlot ? 30000 : 120000
      let beginReply = (await this.requestV2(CommandV2.TRANSFER_BEGIN, begin, RequestFlagV2.NONE, beginTimeout, transactionId)).payload
      transferActive = true
      assertImageOwner()
      let deviceChunk = beginReply.length >= 4
        ? new DataView(beginReply.buffer, beginReply.byteOffset, beginReply.byteLength).getUint16(2, true)
        : this.v2WritePayload
      let chunkSize = Math.max(20, Math.min(this.v2WritePayload, deviceChunk))
      if (usePackBits && chunkSize !== negotiatedChunk) {
        compressedBlocks = buildCompressionPlan(chunkSize)
        if (!packBitsIsWorthwhile(compressedBlocks.map(({ block }) => block), segmentLengths, chunkSize)) {
          await this.requestV2(CommandV2.TRANSFER_ABORT, new Uint8Array(0), RequestFlagV2.NONE, 5000, transactionId)
          transferActive = false
          assertImageOwner()
          usePackBits = false
          begin[3] &= ~(TransferDescriptorFlagV2.PACKBITS_RLE | TransferDescriptorFlagV2.LZO1X)
          transactionId = this.nextV2Transaction()
          beginReply = (await this.requestV2(CommandV2.TRANSFER_BEGIN, begin, RequestFlagV2.NONE, beginTimeout, transactionId)).payload
          transferActive = true
          assertImageOwner()
          deviceChunk = beginReply.length >= 4
            ? new DataView(beginReply.buffer, beginReply.byteOffset, beginReply.byteLength).getUint16(2, true)
            : this.v2WritePayload
          chunkSize = Math.max(20, Math.min(this.v2WritePayload, deviceChunk))
        }
      }
      let offset = 0
      const startedAt = performance.now()
      const hostTiming = new ImageTransferTiming()
      let measuredChunks = 0, measuredEncoded = 0
      const quarters: number[] = []
      const recordImageChunk = (encoded: number) => {
        measuredChunks++; measuredEncoded += encoded
        while (quarters.length < 3 && offset >= totalSize * (quarters.length + 1) / 4) {
          quarters.push(Number(((performance.now() - startedAt) / 1000).toFixed(3)))
          store.addLog(`图片主机耗时 ${quarters.length * 25}%（平均/最大）：${hostTiming.summary()} visibility=${imageDocumentVisibility()}`)
          // Keep optional native metadata reads out of the DATA interval.
          // Acquisition observes once before BEGIN; lease renewal is local.
        }
      }
      store.setAction('picture', 0, '正在通过蓝牙传输图片。')
      if (usePackBits) {
        store.addLog(`图片数据适合无损压缩，已按设备能力启用 ${codec === 'lzo1x' ? 'LZO1X' : 'RLE'} 传输`)
        for (const { block, step } of compressedBlocks) {
          assertImageOwner()
          await this.writeV2TransferChunk(
            transactionId, begin[0], totalSize, block.decodedOffset, block.encoded, block.decodedLength,
            !saveToSlot && segmentStarts.has(block.decodedOffset) ? 90000 : 12000,
            hostTiming.record,
          )
          assertImageOwner()
          offset += block.decodedLength
          recordImageChunk(block.encoded.byteLength)
          const label = this.stepLabel(step, format, screen)
          store.setAction('picture', totalSize ? offset / totalSize : 0, `正在传输${label}。`)
          if ((offset & 0x0fff) === 0) await new Promise(requestAnimationFrame)
        }
      } else {
        for (const transfer of transfers) {
          const label = this.stepLabel(transfer.step, format, screen)
          store.addLog(`${label} ${transfer.data.length} bytes`)
          for (let localOffset = 0; localOffset < transfer.data.length; localOffset += chunkSize) {
            assertImageOwner()
            const chunk = transfer.data.subarray(localOffset, Math.min(localOffset + chunkSize, transfer.data.length))
            await this.writeV2TransferChunk(
              transactionId, begin[0], totalSize, offset, chunk, undefined,
              !saveToSlot && segmentStarts.has(offset) ? 90000 : 12000,
              hostTiming.record,
            )
            assertImageOwner()
            offset += chunk.length
            recordImageChunk(chunk.byteLength)
            store.setAction('picture', totalSize ? offset / totalSize : 0, `正在传输${label}。`)
            if ((offset & 0x0fff) === 0) await new Promise(requestAnimationFrame)
          }
        }
      }
      const elapsed = Math.max(0.001, (performance.now() - startedAt) / 1000)
      store.addLog(`图片传输完成：${elapsed.toFixed(1)} 秒，${formatImageRate(totalSize / elapsed)}`, 'success')
      store.addLog(`图片统计：decoded=${totalSize} encoded=${measuredEncoded} chunks=${measuredChunks} decoded_crc32=${beginView.getUint32(8, true).toString(16).padStart(8, '0')} quarter_s=${[...quarters, Number(elapsed.toFixed(3))].join(',')}`)
      store.addLog(`图片主机耗时 100%（平均/最大）：${hostTiming.summary()} visibility=${imageDocumentVisibility()}`)
      await imageLink?.close()
      imageLink = null
      assertImageOwner()

      let completion: Promise<Uint8Array> | null = null
      if (!saveToSlot) completion = this.waitForV2Completion(
        transactionId, CommandV2.TRANSFER_COMMIT, 300000,
      )
      void completion?.catch(() => undefined)
      // Once COMMIT is sent the device owns the transaction. A synchronous
      // panel waveform may delay the response notification, so never turn an
      // ambiguous ACK timeout into TRANSFER_ABORT/disconnect.
      transferActive = false
      const commitResult = await this.commitV2Transfer(
        transactionId, begin[0], totalSize, completion,
      )
      imageCommitted = true
      assertImageOwner()
      const commit = commitResult.payload
      if (completion) {
        if (!commitResult.completionConsumed) {
          store.setAction('picture', null, '图片已传完，墨水屏正在刷新。')
          await completion
        }
        assertImageOwner()
        // COMPLETE may precede the COMMIT reply or settle before this await.
        // Never resurrect a finished picture action with a refresh message.
        if (useDeviceStore.getState().action === 'picture') store.setAction('idle')
        return null
      }

      const storedId = commit.length ? commit[0] : null
      if (storedId == null || storedId === 0xff) throw new Error('设备未返回新图槽编号')
      if (replaceSlotId !== undefined && store.imageStoreInfo?.fixedSlotCount && storedId !== replaceSlotId) {
        throw new Error('设备返回的图槽与指定位置不一致，请重新读取图槽列表')
      }
      if (store.deviceId) {
        try {
          await db.deviceSlotPreviews.put({
            id: `${store.deviceId}:${storedId}`, deviceId: store.deviceId, slotId: storedId,
            name: rawName, dataUrl: canvas.toDataURL('image/png'), updatedAt: Date.now(),
          })
          if (replaceSlotId !== undefined && storedId !== replaceSlotId) {
            await db.deviceSlotPreviews.delete(`${store.deviceId}:${replaceSlotId}`)
          }
        } catch {
          store.addLog('图片已保存到设备，但本地预览缓存失败', 'warning')
        }
      }
      assertImageOwner()
      if (displayAfterStore) {
        store.setAction('picture', null, '图片已保存，设备正在从图槽刷新屏幕。')
        await this.runV2DisplayWork(DisplayModeV2.RESOURCE, new Uint8Array([storedId]))
        assertImageOwner()
      }
      // Replacement publishes the new image under the requested ID. The backend
      // owns invalidation/reuse; the client must not issue a second DELETE.
      // Interrupted in-place replacement may lose the old target image.
      await this.listStoredImages({ afterCommit: true })
      assertImageOwner()
      if (store.capabilities.imageMetadata) {
        const effective = useDeviceStore.getState().imageStoreItems.find((item) => item.id === storedId)
        if (!effective || effective.name !== new TextDecoder().decode(name) || effective.contentType !== contentType) {
          throw new Error('图片已提交，但名称或类型读回不一致；请检查设备图片，不要重复上传')
        }
      }
      if (!displayAfterStore) {
        store.setAction('idle')
        store.addLog('图片已保存到设备图槽，屏幕未刷新', 'success')
      }
      return storedId
    } catch (error) {
      await imageLink?.close()
      imageLink = null
      if (transferActive && ownsImage()) {
        await this.requestV2(CommandV2.TRANSFER_ABORT, new Uint8Array(0), RequestFlagV2.NONE, 5000, transactionId).catch(() => undefined)
      }
      throw error
    } finally {
      await imageLink?.close()
    }
  }

  private stepLabel(step: ImageStep, format: number, screen: ScreenConfig) {
    // Osprey's single native six-pigment stream intentionally uses the black
    // wire channel. Its presentation label must not redefine that descriptor.
    if (step === 'bw' && format === TransferFormat.INDEXED_4BPP &&
        screen.color === 10 && (screen.driver === 0x40 || screen.driver === 0x41)) return '六色数据'
    if (step === 'color' && format === TransferFormat.INDEXED_2BPP && screen.color === 3) return '四色数据'
    if (step === 'bw') return '黑白图层'
    if (step === 'red') return '彩色图层'
    if (step === 'gray-msb') return '灰阶 RAM1图层'
    if (step === 'gray-lsb') return '灰阶 RAM2图层'
    if (step === 'e6-area0') return '六色第一阶段（蓝/白/绿/红）图层'
    if (step === 'e6-area6') return '六色第二阶段（黑/黄）图层'
    return '四色图层'
  }

  async listStoredImages(options: { afterCommit?: boolean } = {}) {
    const store = useDeviceStore.getState()
    const generation = this.connectionGeneration
    const sequence = ++this.resourceReadSequence
    store.setResourceReadState('loading')
    try {
      const budget = resourceReadBudget(options.afterCommit)
      const info = await this.readV2ResourceInfo(budget)
      const contentTypes: ImageSlotContentType[] = ['image', 'schedule', 'wifi', 'todo', 'business', 'page']
      const items: ImageStoreItem[] = []
      let cursor = 0xff
      let complete = false
      for (let count = 0; count < 256; count++) {
        const payload = (await this.requestResourceSnapshot(CommandV2.LIST_RESOURCES, new Uint8Array([cursor]), budget)).payload
        if (payload.length >= 2 && payload[0] === 1) {
          if (info.fixedSlotCount && payload.length !== 2) {
            const bitmap = payload.subarray(2), count = info.fixedSlotCount
            if (bitmap.length !== Math.ceil(count / 8) || payload[1] !== 0xff
              || (count % 8 && (bitmap[bitmap.length - 1] >> (count % 8)))) throw new Error('设备图槽占用位图无效')
            info.occupiedIds = Array.from({length: count}, (_, id) => id).filter((id) => bitmap[id >> 3] & (1 << (id & 7)))
            if (items.some((item) => !info.occupiedIds!.includes(item.id))) throw new Error('设备图槽列表与占用位图不一致')
          }
          complete = true; break
        }
        if (payload.length < 19 || payload[0] !== 0) throw new Error('设备图槽列表响应无效')
        const nameLength = payload[6]
        const oriented = Boolean(this.v2CapabilityExtensions & CapabilityExtensionV2.IMAGE_READING_ORIENTATION)
        if (payload.length !== 19 + nameLength + Number(oriented)) throw new Error('设备图槽名称长度无效')
        const readingRotation = oriented ? payload[payload.length - 1] : 0
        if (readingRotation > 3) throw new Error('设备图槽阅读方向无效')
        const view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength)
        const item: ImageStoreItem = {
          id: payload[1],
          contentType: contentTypes[payload[2]] || 'image',
          format: payload[3],
          segments: payload[4],
          compatible: payload[5] !== 0,
          width: view.getUint16(7, true),
          height: view.getUint16(9, true),
          bytes: view.getUint32(11, true),
          createdAt: view.getUint32(15, true),
          name: this.textDecoder.decode(payload.subarray(19, 19 + nameLength)),
          readingRotation,
        }
        if (info.fixedSlotCount && item.id >= info.fixedSlotCount) throw new Error('设备返回的图槽编号超出固定容量')
        items.push(item)
        cursor = item.id
      }
      if (!complete) throw new Error('设备图槽列表未正常结束')
      if (generation !== this.connectionGeneration || sequence !== this.resourceReadSequence) return
      const state = useDeviceStore.getState()
      const visibleItems = visibleImageRecords(items, state.activation, state.screen)
      store.setImageStoreItems(visibleItems)
      store.setImageStoreInfo(constrainedImageStoreInfo(info, items, state.activation, state.screen))
      store.setResourceReadState('idle')
    } catch (error) {
      if (generation === this.connectionGeneration && sequence === this.resourceReadSequence && !this.resourceReadAbort.signal.aborted) {
        store.setResourceReadState('error', error instanceof Error ? error.message : String(error))
      }
      throw error
    }
    return
  }

  async showStoredImage(id: number) {
    const store = useDeviceStore.getState()
    const preserveSlideshow = store.action === 'slideshow'
    if (!findVisibleImageRecord(store.imageStoreItems, id)) {
      throw new Error('找不到图片槽，或当前版本不可使用该图槽')
    }
    store.setAction('picture', null, '正在读取设备图槽并刷新屏幕。')
    try {
      await this.runV2DisplayWork(DisplayModeV2.RESOURCE, new Uint8Array([id]))
      if (preserveSlideshow) {
        await this.refreshV2ResourceInfo()
        if (useDeviceStore.getState().imageStoreInfo?.slideshow) {
          useDeviceStore.getState().setAction('slideshow')
        }
      }
      return
    } catch (error) {
      retainDisplayFailure('picture', error, () => this.showStoredImage(id))
      throw error
    }
  }

  async readStoredImage(id: number, onProgress?: (progress: number) => void) {
    const store = useDeviceStore.getState()
    if (!this.protocolV2) throw new Error('当前固件不支持图片槽回读')
    if (store.action !== 'idle' && store.action !== 'slideshow') {
      throw new Error('设备正忙，请等待当前操作完成后再回读')
    }
    const screen = store.screen
    const item = findVisibleImageRecord(store.imageStoreItems, id)
    if (!screen || !item) throw new Error('找不到图片槽或当前屏幕配置')
    if (!item.compatible || item.width !== screen.width || item.height !== screen.height) {
      throw new Error('图片槽与当前屏幕配置不兼容，不能生成可靠预览')
    }
    if (!item.bytes) throw new Error('图片槽没有可回读的数据')

    const bytes = new Uint8Array(item.bytes)
    let offset = 0
    while (offset < bytes.length) {
      const requested = Math.min(232, bytes.length - offset)
      const request = new Uint8Array(6)
      request[0] = id
      new DataView(request.buffer).setUint32(1, offset, true)
      request[5] = requested
      const response = (await this.requestV2(CommandV2.READ_RESOURCE, request, RequestFlagV2.NONE, 12000, undefined, true)).payload
      if (response.length < 10 || response[0] !== id) throw new Error('设备返回了无效的图片槽数据')
      const view = new DataView(response.buffer, response.byteOffset, response.byteLength)
      const responseOffset = view.getUint32(1, true)
      const totalSize = view.getUint32(5, true)
      const chunk = response.subarray(9)
      if (responseOffset !== offset || totalSize !== bytes.length || !chunk.length || offset + chunk.length > bytes.length) {
        throw new Error('图片槽回读偏移或长度不一致')
      }
      bytes.set(chunk, offset)
      offset += chunk.length
      onProgress?.(offset / bytes.length)
      if ((offset & 0x0fff) < chunk.length) await new Promise(requestAnimationFrame)
    }

    const frame = decodeStoredImage(bytes, screen, item.format, item.segments, item.readingRotation)
    const canvas = document.createElement('canvas')
    canvas.width = frame.width
    canvas.height = frame.height
    const context = canvas.getContext('2d')
    if (!context) throw new Error('浏览器无法创建图片槽预览')
    context.putImageData(new ImageData(Uint8ClampedArray.from(frame.data), frame.width, frame.height), 0, 0)
    const dataUrl = canvas.toDataURL('image/png')
    if (store.deviceId) {
      await db.deviceSlotPreviews.put({
        id: `${store.deviceId}:${id}`, deviceId: store.deviceId, slotId: id,
        name: item.name, dataUrl, updatedAt: Date.now(),
      }).catch(() => undefined)
    }
    return dataUrl
  }

  async deleteStoredImage(id: number) {
    const store = useDeviceStore.getState()
    if (!findVisibleImageRecord(store.imageStoreItems, id)) {
      throw new Error('找不到图片槽，或当前版本不可使用该图槽')
    }
    const deviceId = store.deviceId
    await this.requestV2(CommandV2.DELETE_RESOURCE, new Uint8Array([id]))
    if (deviceId) await db.deviceSlotPreviews.delete(`${deviceId}:${id}`)
    await this.listStoredImages()
    if (useDeviceStore.getState().imageStoreItems.some((item) => item.id === id)) {
      throw new Error(`设备确认删除槽 ${id}，但刷新列表后该槽仍然存在`)
    }
    return
  }

  async clearStoredImages(incompatibleOnly: boolean) {
    const store = useDeviceStore.getState()
    const deviceId = store.deviceId
    const removedIds = incompatibleOnly ? store.imageStoreItems.filter((item) => !item.compatible).map((item) => item.id) : []
    await this.requestV2(CommandV2.CLEAR_RESOURCES, new Uint8Array([
      incompatibleOnly ? ResourceClearScopeV2.INCOMPATIBLE : ResourceClearScopeV2.ALL,
    ]), RequestFlagV2.NONE, 180000)
    if (deviceId) {
      if (incompatibleOnly) await db.deviceSlotPreviews.bulkDelete(removedIds.map((id) => `${deviceId}:${id}`))
      else await db.deviceSlotPreviews.where('deviceId').equals(deviceId).delete()
    }
    await this.listStoredImages()
    return
  }

  async startSlideshow(intervalSeconds: number) {
    const seconds = Math.max(30, Math.min(86400, Math.round(intervalSeconds)))
    const payload = new Uint8Array(4)
    new DataView(payload.buffer).setUint32(0, seconds, true)
    const store = useDeviceStore.getState()
    store.setAction('slideshow-starting', null, '设备正在读取首张图片并刷新屏幕。')
    try {
      await this.runV2DisplayWork(DisplayModeV2.SLIDESHOW, payload)
      await this.refreshV2ResourceInfo()
      store.setAction('slideshow')
      return
    } catch (error) {
      retainDisplayFailure('slideshow-starting', error, () => this.startSlideshow(intervalSeconds))
      throw error
    }
  }

  async stopSlideshow() {
    await this.requestV2(CommandV2.DISPLAY_ACTION, new Uint8Array([DisplayActionV2.PAUSE]))
    await this.refreshV2ResourceInfo()
    useDeviceStore.getState().setAction('idle')
    return
  }

  private onDisconnected = (event?: Event) => {
    const disconnectedDevice = event?.target as BluetoothDevice | undefined
    this.finishDisconnectedDevice(disconnectedDevice)
  }

  private finishDisconnectedDevice(disconnectedDevice?: BluetoothDevice) {
    const store = useDeviceStore.getState()
    if (disconnectedDevice?.gatt?.connected) return
    if (disconnectedDevice && !this.device) {
      disconnectedDevice.removeEventListener('gattserverdisconnected', this.onDisconnected)
      return
    }
    if (disconnectedDevice && this.device && disconnectedDevice !== this.device) return
    this.resourceReadAbort.abort()
    this.connectionGeneration++
    this.connectionAttempt = null
    this.characteristic?.removeEventListener('characteristicvaluechanged', this.onNotification)
    disconnectedDevice?.removeEventListener('gattserverdisconnected', this.onDisconnected)
    this.characteristic = null
    this.server = null
    this.device = null
    this.otaRecoveryType = null
    this.resetImageTransport()
    store.addLog(this.disconnectRequested ? '已主动断开设备' : '设备已断开', this.disconnectRequested ? 'info' : 'warning')
    store.resetDevice(store.action === 'ota')
  }

  private handleV2Notification(bytes: Uint8Array) {
    const notificationAt = performance.now()
    const store = useDeviceStore.getState()
    if (bytes.length < 3) {
      store.addLog(`收到过短的 v1 通知：${bytes.length} B`, 'warning')
      return
    }
    const opcode = bytes[0]
    const transactionId = bytes[1]
    const status = bytes[2]
    if (opcode >= EventV2.READY) {
      if (bytes.length < 5 || transactionId !== 0) {
        store.addLog(`收到格式无效的 v1 事件：event=0x${opcode.toString(16).padStart(2, '0')} tx=${transactionId} bytes=${bytes.length}`, 'warning')
        return
      }
      const originOpcode = bytes[3]
      const originTransaction = bytes[4]
      const payload = bytes.subarray(5)
      if (opcode === EventV2.LOG) {
        const message = this.textDecoder.decode(payload)
        store.addLog(message, 'rx')
        const detail = displayWorkDetailFromLog(store.action, message)
        if (detail) store.setAction(store.action, null, detail)
        return
      }
      if (opcode === EventV2.PROGRESS) {
        const phase = payload.length > 0 ? payload[0] : BusyPhaseV2.IDLE
        const progress = phase === BusyPhaseV2.TRANSFER && payload.length > 1
          ? payload[1] / 100
          : null
        if (store.action !== 'idle') store.setAction(store.action, progress, store.actionDetail)
        return
      }
      if (opcode === EventV2.COMPLETE) {
        const statusLabel = status === StatusV2.OK
          ? 'OK'
          : (v2StatusLabels[status] || `0x${status.toString(16).padStart(2, '0')}`)
        store.addLog(
          `v1 完成：op=0x${originOpcode.toString(16).padStart(2, '0')} tx=${originTransaction} status=${statusLabel} payload=${payload.length} B`,
          status === StatusV2.OK ? 'rx' : 'error',
        )
        const completion = this.v2Completions.get(originTransaction)
        if (completion) {
          if (completion.opcode !== originOpcode) {
            store.addLog(`忽略不匹配的 v1 完成事件：期望 0x${completion.opcode.toString(16)}，收到 0x${originOpcode.toString(16)}`, 'warning')
            return
          }
          window.clearTimeout(completion.timer)
          this.v2Completions.delete(originTransaction)
          if (status === StatusV2.OK) completion.resolve(payload)
          else completion.reject(new ProtocolV2Error(status))
        }
        if (completion && status === StatusV2.OK && store.action === 'slideshow-starting') store.setAction('slideshow')
        else if (completion && store.action !== 'slideshow') store.setAction('idle')
        if (completion && status === StatusV2.OK &&
          (originOpcode === CommandV2.SET_DISPLAY_MODE || originOpcode === CommandV2.RUN_TEST ||
            originOpcode === CommandV2.DISPLAY_ACTION || originOpcode === CommandV2.TRANSFER_COMMIT))
          void this.refreshTelemetryAfterDisplay(this.connectionGeneration)
        return
      }
      if (opcode === EventV2.READY) {
        store.addLog(`v1 就绪：op=0x${originOpcode.toString(16).padStart(2, '0')} tx=${originTransaction} payload=${payload.length} B`, 'rx')
        return
      }
      if (opcode === EventV2.ERROR || status !== StatusV2.OK) {
        store.addLog(`设备事件失败：${v2StatusLabels[status] || `0x${status.toString(16)}`}（命令 0x${originOpcode.toString(16)}）`, 'error')
      }
      return
    }
    const pending = this.v2Pending.get(transactionId)
    if (!pending) {
      store.addLog(`收到未匹配的 v1 响应：${Array.from(bytes, (byte) => byte.toString(16).padStart(2, '0')).join('')}`, 'warning')
      return
    }
    window.clearTimeout(pending.timer)
    this.v2Pending.delete(transactionId)
    if (pending.imageTiming) {
      observeImageTiming(pending.imageTiming, 'reply', notificationAt - pending.startedAt, notificationAt)
    }
    if (!pending.quiet || status !== StatusV2.OK || pending.opcode !== opcode) {
      const statusLabel = status === StatusV2.OK
        ? 'OK'
        : (v2StatusLabels[status] || `0x${status.toString(16).padStart(2, '0')}`)
      const mismatch = pending.opcode === opcode
        ? ''
        : ` expected=0x${pending.opcode.toString(16).padStart(2, '0')}`
      store.addLog(
        `v1 响应：op=0x${opcode.toString(16).padStart(2, '0')}${mismatch} tx=${transactionId} status=${statusLabel} payload=${Math.max(0, bytes.length - 3)} B（${Math.max(0, Math.round(performance.now() - pending.startedAt))} ms）`,
        status === StatusV2.OK && pending.opcode === opcode ? 'rx' : 'error',
      )
    }
    if (pending.imageTiming) observeImageTiming(pending.imageTiming, 'handler', performance.now() - notificationAt)
    if (pending.opcode !== opcode) {
      pending.reject(new Error(`v1 响应命令不匹配：期望 0x${pending.opcode.toString(16)}，收到 0x${opcode.toString(16)}`))
    } else if (status !== StatusV2.OK) {
      pending.reject(new ProtocolV2Error(status))
    } else {
      pending.resolve({ payload: bytes.slice(3), transactionId })
    }
  }

  private onNotification = (event: Event) => {
    const value = (event.target as BluetoothRemoteGATTCharacteristic).value
    if (!value) return
    const bytes = new Uint8Array(value.buffer, value.byteOffset, value.byteLength)
    if (this.protocolV2) {
      this.handleV2Notification(bytes)
      return
    }
    const screen = decodeScreenConfig(bytes)
    const store = useDeviceStore.getState()
    if (screen) {
      store.setScreen(screen)
      store.addLog(`屏幕配置 ${screen.width}×${screen.height}`, 'rx')
      if (this.expectedScreen && screenConfigsEqual(screen, this.expectedScreen)) {
        this.screenResolver?.({ ok: true, screen })
      }
      return
    }
    const message = this.textDecoder.decode(bytes)
    store.addLog(message, 'rx')
    const settingsCapabilities = decodeDeviceSettingsCapabilities(message)
    if (settingsCapabilities) store.setSettingsCapabilities(settingsCapabilities)
    const reportedButtons = decodeReportedButtonSettings(message)
    if (reportedButtons) {
      store.setReportedButtonActions(reportedButtons)
    }
    const reportedLed = decodeReportedLedSettings(message)
    if (reportedLed) store.setReportedLed(reportedLed)
    if (message.startsWith('nfc:status')) {
      const reportedNfc = decodeReportedNfcSettings(
        message, store.reportedNfcTap?.controlUrl || '',
      )
      if (reportedNfc) store.setReportedNfcTap(reportedNfc)
      if (reportedNfc && store.deviceId) {
        const deviceId = store.deviceId
        void getDevicePreference(deviceId).then((preference) => {
          const current = useDeviceStore.getState()
          if (current.deviceId !== deviceId || !current.reportedNfcTap ||
              current.reportedNfcTap.controlUrl) return
          current.setReportedNfcTap({ ...current.reportedNfcTap, controlUrl: preference.nfcTap.controlUrl })
        })
      }
    }
    const reportedNfcUrl = decodeReportedNfcUrl(message)
    if (reportedNfcUrl) {
      const current = useDeviceStore.getState().reportedNfcTap
      if (current) store.setReportedNfcTap({ ...current, controlUrl: reportedNfcUrl })
    }
    if (message.startsWith('board=')) {
      const boardId = canonicalBoardId(message.slice(6))
      const profile = resolveBoardProfile(boardId)
      store.setIdentity({ boardId })
      if (profile) store.setCapabilities(profile.capabilities)
    }
    if (message.startsWith('settings:identity')) {
      const fields = fieldsFrom(message)
      let reportedName = ''
      try { reportedName = fields.name ? decodeURIComponent(fields.name) : '' } catch { reportedName = '' }
      store.setIdentity({
        ...(reportedName ? { deviceName: reportedName } : {}),
        ...(/^[0-9a-f]{2}(?::[0-9a-f]{2}){5}$/i.test(fields.addr || '')
          ? { bleAddress: fields.addr.toUpperCase() }
          : {}),
      })
    }
    const reportedDeviceLabel = decodeReportedDeviceLabel(message)
    if (reportedDeviceLabel) {
      store.setIdentity({ localAlias: reportedDeviceLabel.label })
      if (store.deviceId) void updateDevicePreference(store.deviceId, { localAlias: reportedDeviceLabel.label })
    }
    store.setTelemetry(telemetryFromMessage(message, store.boardId))
    if (message.startsWith('mtu=')) {
      this.imageMtu = Number(message.match(/^mtu=(\d+)/)?.[1] || 22)
    }
    if (message === 'picture:ready') this.imageReadyResolver?.(true)
    if (/^(picture:queue-full|picture:bad-packet|epd:busy|epd:no-screen|epd:spi-error)/.test(message)) {
      this.imageReadyResolver?.(false)
    }
    if (message === 'epd:busy') this.workResolver?.({ ok: false, error: message })
    const awaitedStop = message === 'epd:stopped' && this.stopResolver !== null
    if (message === 'epd:stopped') this.stopResolver?.(true)
    if (message === 'epd:timeout' || message === 'epd:no-busy-edge') {
      this.stopResolver?.(false)
      this.workResolver?.({ ok: false, error: message })
      store.addLog(message === 'epd:no-busy-edge'
        ? '屏幕控制器没有响应刷新命令，请检查屏幕型号、排线和供电'
        : '设备屏幕任务超时，已退出等待状态', 'error')
      store.setAction('idle')
    }
    if (message.startsWith('picture:refresh')) store.setAction('picture', null, '图片已传完，墨水屏正在刷新。这一步通常比蓝牙传输更久。')
    const completedAction = message.match(/^(device-info|clear|calendar|clock|picture):done/)?.[1]
    if (completedAction && this.workAction === completedAction) this.workResolver?.({ ok: true })
    if (completedAction || (message === 'epd:stopped' && !awaitedStop)) store.setAction('idle')
    if (message.startsWith('img:show:done')) {
      if (this.workAction === 'slideshow-starting') {
        this.workResolver?.({ ok: true })
        store.setAction('slideshow')
      } else if (store.action !== 'slideshow') {
        store.setAction('idle')
      }
    }
    if (message.startsWith('img:slideshow:error')) {
      if (this.workAction === 'slideshow-starting') this.workResolver?.({ ok: false, error: message })
      store.setAction('idle')
    }
    if (message.startsWith('img:slideshow:stopped')) store.setAction('idle')
    if (message.startsWith('img:')) this.handleImageStoreNotification(message)
    if (message.startsWith('nfc:config:done')) this.nfcConfigResolver?.({ ok: true })
    if (message.startsWith('nfc:config:error=')) {
      this.nfcConfigResolver?.({ ok: false, error: fieldsFrom(message).error || 'unknown' })
    }
    if (/^settings:(name|device|input|gpio|led):done/.test(message)) this.settingsResolver?.({ ok: true })
    if (/^settings:(name|device|input|gpio|led):error=/.test(message)) {
      this.settingsResolver?.({ ok: false, error: fieldsFrom(message).error || 'unknown' })
    }
    if (message === 'device-info:qr:begin' || message === 'device-info:qr:done') this.deviceInfoQrResolver?.({ ok: true })
    if (message.startsWith('device-info:qr:error=')) this.deviceInfoQrResolver?.({ ok: false, error: fieldsFrom(message).error || 'unknown' })
    if ((message === 'epd:busy' || message.startsWith('screen:error=')) && this.screenResolver) {
      this.screenResolver({ ok: false, error: message })
    }
  }

  private handleImageStoreNotification(message: string) {
    const store = useDeviceStore.getState()
    const fields = fieldsFrom(message)
    if (message === 'img:unsupported') {
      store.setCapabilities({ imageStore: false })
      store.setImageStoreInfo(null)
      store.setImageStoreItems([])
      this.storeResolver?.({ ok: false, error: 'unsupported' })
      if (this.workAction === 'slideshow-starting') {
        this.workResolver?.({ ok: false, error: '当前设备没有安全可用的图片槽存储区。' })
        store.setAction('idle')
      }
      store.addLog('当前硬件批次没有安全可用的图片槽存储区', 'info')
      return
    }
    if (message.startsWith('img:begin:ready') || message.startsWith('img:commit:done') ||
      message.startsWith('img:delete:done') || message.startsWith('img:clear:done') || message.startsWith('img:clear-all:done')) {
      this.storeResolver?.({ ok: true, id: numberField(fields, 'id') })
    } else if (/^img:(begin|commit|write|delete|clear|clear-all):error/.test(message)) {
      this.storeResolver?.({ ok: false, error: fields.error || 'unknown' })
    } else if (message.startsWith('img:item')) {
      store.addImageStoreItem(toImageStoreItem(fields))
    } else if (message.startsWith('img:info')) {
      const info = toImageStoreInfo(fields)
      store.setImageStoreInfo(info)
      if (info.slideshow) store.setAction('slideshow')
    }
    if (message.startsWith('img:delete:done') || message.startsWith('img:clear:done') || message.startsWith('img:clear-all:done')) {
      store.setImageStoreItems([])
    }
  }
}

export const inktagClient = new InkTagClient()

// A WebBluetooth GATT reference can keep a single-connection peripheral off
// the scanner even after the tab is reloaded or closed. Explicitly release it
// on page teardown so the shared CLI and OTA client can discover the tag.
if (typeof window !== 'undefined') {
  const releaseBluetooth = () => inktagClient.disconnect()
  window.addEventListener('pagehide', releaseBluetooth)
  window.addEventListener('beforeunload', releaseBluetooth)
}
