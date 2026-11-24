export type ConnectionState = 'unsupported' | 'idle' | 'selecting' | 'connecting' | 'connected' | 'error'
export type DeviceAction = 'idle' | 'device-info' | 'clear' | 'calendar' | 'clock' | 'picture' | 'font-update' | 'slideshow-starting' | 'slideshow' | 'ota'

export interface ScreenConfig {
  schemaVersion: number
  driver: number
  recipe: number
  color: number
  transform: number
  nativeUiScale: number
  width: number
  height: number
  visibleX: number
  visibleY: number
  visibleWidth: number
  visibleHeight: number
}

export interface LogEntry {
  id: number
  at: number
  level: 'info' | 'success' | 'warning' | 'error' | 'rx' | 'tx'
  message: string
}

export interface DeviceCapabilities {
  clockPartialRefresh?: boolean
  imageStore: boolean
  /** Physical 4 KiB sectors declared by firmware; not write authorization. */
  imageStoreSectors?: number
  imageMetadata?: boolean
  imageReadingOrientation?: boolean
  nfc: 'none' | 'hardware' | 'tap'
  button: boolean
  auxReserved: boolean
  led: 'none' | 'mono' | 'rgb'
  ota: 'none' | 'nordic' | 'goodix' | 'onmicro' | 'dialog' | 'telink'
  health?: boolean
}

export interface DeviceHealthCrash {
  reason: number
  resetReason: number
  operation: number
  voltageMv: number
  uptimeMs: number
  stage: string
  pc: number
  lr: number
  cfsr: number
  hfsr: number
}

export interface DeviceSoakTelemetry {
  version: number
  saturated: boolean
  findActive: boolean
  uptimeMs: number | null
  connectionSessions: number | null
  findRequests: number | null
  findStarts: number | null
  findCompletions: number | null
  findStartFailures: number | null
  findStopFailures: number | null
  findConnectionPauses: number | null
  findActiveMs: number | null
  radioMaintenanceTurns: number | null
  sleepEntries: number | null
}

export interface DeviceHealth {
  powerState: 'unknown' | 'normal' | 'warning' | 'critical'
  voltageValid: boolean
  displayAllowed: boolean
  flashAllowed: boolean
  otaAllowed: boolean
  watchdogActive: boolean
  warningPageShown: boolean
  thresholdsQualified: boolean
  resetReason: number
  voltageMv: number
  warningMv: number
  displayMinMv: number
  flashMinMv: number
  otaMinMv: number
  recoveryHysteresisMv: number
  totalCrashes: number
  crashRecordCount: number
  consecutiveCrashes: number
  latestCrash: DeviceHealthCrash | null
  soakTelemetry?: DeviceSoakTelemetry | null
}

export interface DeviceCrashAlert {
  newCrashes: number | null
  totalCrashes: number
  resetReason: number
  reason: string
  operation: string
  stage: string
  voltageMv: number | null
  uptimeMs: number | null
  registers: Partial<Record<'pc' | 'lr' | 'cfsr' | 'hfsr', number>>
}

export interface DeviceSettingsCapabilities {
  protocolVersion: number
  screenDriverIds: number[] | null
  bluetoothName: boolean
  bluetoothNameMaxBytes: number
  deviceLabel: boolean
  deviceLabelMaxBytes: number
  nfcActionMask: number
  nfcNdef: boolean
  buttonActionMask: number
  buttonGestureMask: number
  displayActionMask: number
  nativeDeviceInfo: boolean
  deviceInfoQr: boolean
  guiAssetUpdate: boolean
  deviceInfoLayoutMask: number
  customButtonGpio: boolean
  reservedIo: boolean
  statusLedSettings: boolean
  runtimePinConfig: boolean
  lifecycle: boolean
  experimentalTimedSleep: boolean
  experimentalFind: boolean
}

export type FirmwareBuildEdition = 'basic' | 'pro'
export type FirmwareEdition = 'base' | 'advanced'
export type FirmwareActivationPhase = 'unsupported' | 'unavailable' | 'inactive' | 'active' | 'invalid'

export interface FirmwareActivation {
  phase: FirmwareActivationPhase
  edition: FirmwareEdition
  activationId?: string
  scheme?: number
  lastResult?: number
  codeLength?: number
  keyId?: number
  sequence?: number
  featureMask?: number
  requestActive?: boolean
}

export type LedPolicy = 'off' | 'all' | 'critical'

export interface LedSettings {
  policy: LedPolicy
}

export type DeviceInputAction =
  | 'disabled'
  | 'nextPage'
  | 'showSlot'
  | 'showDeviceInfo'
  | 'showCalendar'
  | 'showClock'
  | 'toggleSlideshow'
  | 'stop'
  | 'controlUrl'
  | 'toggleClearPrevious'
  | 'previousPage'

export type NfcTapAction = Exclude<DeviceInputAction, 'toggleClearPrevious'>
export type ButtonAction = Exclude<DeviceInputAction, 'controlUrl'>
export type ButtonGesture = 'single' | 'double' | 'long'

export interface NfcTapSettings {
  action: NfcTapAction
  slotId: number
  controlUrl: string
}

export interface ButtonActionSettings {
  action: ButtonAction
  slotId: number
}

export interface ButtonBehaviorSettings {
  single: ButtonActionSettings
  double: ButtonActionSettings
  long: ButtonActionSettings
}

export type ButtonPull = 'none' | 'up' | 'down'
export interface AuxIoSettings {
  buttonGpio: number
  buttonActiveLow: boolean
  buttonPull: ButtonPull
  reserved: number[]
}

export interface ImageStoreItem {
  id: number
  name: string
  width: number
  height: number
  bytes: number
  format: number
  segments: number
  createdAt: number
  compatible: boolean
  contentType: ImageSlotContentType
  readingRotation?: number
}

export type ImageSlotContentType = 'image' | 'schedule' | 'wifi' | 'todo' | 'business' | 'page'

export const imageSlotContentTypeIds: Record<ImageSlotContentType, number> = {
  image: 0,
  schedule: 1,
  wifi: 2,
  todo: 3,
  business: 4,
  page: 5,
}

export const imageSlotContentTypeLabels: Record<ImageSlotContentType, string> = {
  image: '轮播图片',
  schedule: '课表',
  wifi: 'Wi-Fi',
  todo: '待办',
  business: '名片',
  page: '页面',
}

export interface ImageStoreInfo {
  width: number
  height: number
  bytesPerImage: number
  requiredSectors: number
  totalSectors: number
  freeSectors: number
  largestFreeRun: number
  fit: number
  contiguousFit: number
  /** Raw device staging capacity before the client applies the base record quota. */
  physicalContiguousFit?: number
  /** Complete occupancy snapshot, including records outside the base visibility quota. */
  occupiedIds?: number[]
  /** Exact fixed capacity; a nonzero INFO tail also permits targeting empty slots. */
  fixedSlotCount?: number
  saved: number
  compatible: number
  playable: number
  incompatible: number
  slideshow: boolean
  intervalSeconds: number
}
