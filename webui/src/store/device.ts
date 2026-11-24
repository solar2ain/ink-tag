import type { NfcCapabilities } from '../protocol/nfcCommand'
import { create } from 'zustand'
import type {
  ConnectionState, DeviceAction, DeviceCapabilities, ImageStoreInfo, ImageStoreItem,
  AuxIoSettings, ButtonBehaviorSettings, DeviceSettingsCapabilities, FirmwareActivation, FirmwareBuildEdition,
  DeviceCrashAlert, DeviceHealth, LedSettings, LogEntry, NfcTapSettings, ScreenConfig,
} from '../types'
import type { DeviceFindCapabilities, DeviceFindConfig } from '../protocol/find'
import { emptyFirmwareActivation } from '../protocol/activation'
import { emptyDeviceSettingsCapabilities } from '../protocol/deviceSettings'
import type { PinReport } from '../protocol/pinConfig'
import type { AuxIoConfiguration } from '../protocol/auxIo'

const emptyCapabilities: DeviceCapabilities = {
  clockPartialRefresh: false,
  imageReadingOrientation: false,
  imageStore: false,
  imageStoreSectors: 0,
  nfc: 'none',
  button: false,
  auxReserved: false,
  led: 'none',
  ota: 'none',
}

interface DeviceState {
  connection: ConnectionState
  deviceId: string
  bleAddress: string
  deviceName: string
  localAlias: string
  boardId: string
  firmwareVersion: string
  firmwareBuildEdition: FirmwareBuildEdition | null
  protocolVersion: number | null
  voltage: number | null
  temperature: number | null
  mcuTemperature: number | null
  panelTemperature: number | null
  health: DeviceHealth | null
  crashAlert: DeviceCrashAlert | null
  action: DeviceAction
  otaPhase: 'connecting-recovery' | 'recovery-transfer' | 'reconnecting-app' | 'attesting-app' | null
  actionProgress: number | null
  actionDetail: string
  actionStartedAt: number | null
  operationFailure: {
    action: DeviceAction
    detail: string
    startedAt: number
    failedAt: number
    retry?: () => Promise<unknown>
  } | null
  screen: ScreenConfig | null
  capabilities: DeviceCapabilities
  settingsCapabilities: DeviceSettingsCapabilities
  nfcCapabilities: NfcCapabilities | null
  reportedNfcTap: NfcTapSettings | null
  reportedButtonActions: ButtonBehaviorSettings | null
  reportedAuxIo: AuxIoSettings | null
  reportedPinConfig: PinReport | null
  reportedAuxIoConfiguration: AuxIoConfiguration | null
  ioWriteUnconfirmed: boolean
  reportedLed: LedSettings | null
  brandVisible: boolean | null
  activation: FirmwareActivation
  findCapabilities: DeviceFindCapabilities | null
  findConfig: DeviceFindConfig | null
  imageStoreInfo: ImageStoreInfo | null
  imageStoreItems: ImageStoreItem[]
  resourceReadState: 'idle' | 'loading' | 'error'
  resourceReadError: string
  lastDeviceId: string
  lastDeviceName: string
  lastDeviceAlias: string
  logs: LogEntry[]
  setConnection: (connection: ConnectionState) => void
  setIdentity: (identity: Partial<Pick<DeviceState, 'deviceId' | 'bleAddress' | 'deviceName' | 'localAlias' | 'boardId' | 'firmwareVersion' | 'firmwareBuildEdition' | 'protocolVersion'>>) => void
  setTelemetry: (telemetry: Partial<Pick<DeviceState, 'voltage' | 'temperature' | 'mcuTemperature' | 'panelTemperature'>>) => void
  setHealth: (health: DeviceHealth | null) => void
  setCrashAlert: (alert: DeviceCrashAlert | null) => void
  setAction: (action: DeviceAction, progress?: number | null, detail?: string) => void
  setOtaPhase: (phase: DeviceState['otaPhase']) => void
  failAction: (action: DeviceAction, detail: string, retry?: () => Promise<unknown>) => void
  clearOperationFailure: () => void
  setScreen: (screen: ScreenConfig) => void
  setCapabilities: (capabilities: Partial<DeviceCapabilities>) => void
  setSettingsCapabilities: (capabilities: DeviceSettingsCapabilities) => void
  setNfcCapabilities: (capabilities: NfcCapabilities | null) => void
  setReportedNfcTap: (settings: NfcTapSettings) => void
  setReportedButtonActions: (settings: ButtonBehaviorSettings) => void
  setReportedAuxIo: (settings: AuxIoSettings) => void
  setReportedPinConfig: (report: PinReport | null) => void
  setReportedAuxIoConfiguration: (config: AuxIoConfiguration | null) => void
  setReportedLed: (settings: LedSettings) => void
  setActivation: (activation: FirmwareActivation) => void
  setFindCapabilities: (capabilities: DeviceFindCapabilities | null) => void
  setFindConfig: (config: DeviceFindConfig | null) => void
  setImageStoreInfo: (info: ImageStoreInfo | null) => void
  setImageStoreItems: (items: ImageStoreItem[]) => void
  setResourceReadState: (state: 'idle' | 'loading' | 'error', error?: string) => void
  addImageStoreItem: (item: ImageStoreItem) => void
  addLog: (message: string, level?: LogEntry['level']) => void
  clearLogs: () => void
  resetDevice: (preserveAction?: boolean) => void
}

let logId = 0

const LAST_CONNECTED_DEVICE_KEY = 'inktag-last-connected-device-v1'

function loadLastConnectedDevice() {
  try {
    if (typeof window === 'undefined' || typeof document === 'undefined') return { id: '', name: '', alias: '' }
    const stored = window.localStorage.getItem(LAST_CONNECTED_DEVICE_KEY)
    if (!stored) return { id: '', name: '', alias: '' }
    const value = JSON.parse(stored) as Partial<{ id: string; name: string; alias: string }>
    return {
      id: typeof value.id === 'string' ? value.id : '',
      name: typeof value.name === 'string' ? value.name : '',
      alias: typeof value.alias === 'string' ? value.alias : '',
    }
  } catch {
    return { id: '', name: '', alias: '' }
  }
}

function saveLastConnectedDevice(id: string, name: string, alias: string) {
  try {
    if (typeof window === 'undefined' || typeof document === 'undefined') return
    window.localStorage.setItem(
      LAST_CONNECTED_DEVICE_KEY,
      JSON.stringify({ id, name, alias }),
    )
  } catch {
    // Private browsing or a storage policy may reject persistence. The
    // in-memory shortcut remains available for the current page session.
  }
}

const initialLastDevice = loadLastConnectedDevice()

export const useDeviceStore = create<DeviceState>((set) => ({
  connection: navigator.bluetooth ? 'idle' : 'unsupported',
  deviceId: '',
  bleAddress: '',
  deviceName: '',
  localAlias: '',
  boardId: '',
  firmwareVersion: '',
  firmwareBuildEdition: null,
  protocolVersion: null,
  voltage: null,
  temperature: null,
  mcuTemperature: null,
  panelTemperature: null,
  health: null,
  crashAlert: null,
  action: 'idle',
  otaPhase: null,
  actionProgress: null,
  actionDetail: '',
  actionStartedAt: null,
  operationFailure: null,
  screen: null,
  capabilities: emptyCapabilities,
  settingsCapabilities: emptyDeviceSettingsCapabilities,
  nfcCapabilities: null,
  reportedNfcTap: null,
  reportedButtonActions: null,
  reportedAuxIo: null,
  reportedPinConfig: null,
  reportedAuxIoConfiguration: null,
  ioWriteUnconfirmed: false,
  reportedLed: null,
  brandVisible: null,
  activation: emptyFirmwareActivation,
  findCapabilities: null,
  findConfig: null,
  imageStoreInfo: null,
  imageStoreItems: [],
  resourceReadState: 'idle',
  resourceReadError: '',
  lastDeviceId: initialLastDevice.id,
  lastDeviceName: initialLastDevice.name,
  lastDeviceAlias: initialLastDevice.alias,
  logs: [],
  setConnection: (connection) => set((state) => {
    if (connection !== 'connected' || !state.deviceId) return { connection }
    saveLastConnectedDevice(state.deviceId, state.deviceName, state.localAlias)
    return {
      connection,
      lastDeviceId: state.deviceId,
      lastDeviceName: state.deviceName,
      lastDeviceAlias: state.localAlias,
    }
  }),
  setIdentity: (identity) => set((state) => {
    const next = { ...identity }
    if (state.connection === 'connected' && state.deviceId) {
      const id = identity.deviceId ?? state.deviceId
      const name = identity.deviceName ?? state.deviceName
      const alias = identity.localAlias ?? state.localAlias
      saveLastConnectedDevice(id, name, alias)
      return { ...next, lastDeviceId: id, lastDeviceName: name, lastDeviceAlias: alias }
    }
    return next
  }),
  setTelemetry: (telemetry) => set(telemetry),
  setHealth: (health) => set({ health }),
  setCrashAlert: (crashAlert) => set({ crashAlert }),
  setAction: (action, actionProgress = null, actionDetail = '') => set((state) => ({
    action,
    otaPhase: action === 'ota' ? state.otaPhase : null,
    actionProgress,
    actionDetail: action === 'idle' ? '' : actionDetail,
    actionStartedAt: action === 'idle' ? null : state.action === action && state.actionStartedAt ? state.actionStartedAt : Date.now(),
    operationFailure: action === 'idle' ? state.operationFailure : null,
  })),
  setOtaPhase: (otaPhase) => set((state) => state.action === 'ota' ? { otaPhase } : {}),
  failAction: (action, detail, retry) => set((state) => ({
    action: 'idle',
    otaPhase: null,
    actionProgress: null,
    actionDetail: '',
    actionStartedAt: null,
    operationFailure: {
      action,
      detail,
      startedAt: state.actionStartedAt ?? Date.now(),
      failedAt: Date.now(),
      ...(retry ? { retry } : {}),
    },
  })),
  clearOperationFailure: () => set({ operationFailure: null }),
  setScreen: (screen) => set({ screen }),
  setCapabilities: (capabilities) => set((state) => ({ capabilities: { ...state.capabilities, ...capabilities } })),
  setSettingsCapabilities: (settingsCapabilities) => set({ settingsCapabilities }),
  setNfcCapabilities: (nfcCapabilities) => set({ nfcCapabilities }),
  setReportedNfcTap: (reportedNfcTap) => set({ reportedNfcTap }),
  setReportedButtonActions: (reportedButtonActions) => set({ reportedButtonActions }),
  setReportedAuxIo: (reportedAuxIo) => set({ reportedAuxIo }),
  setReportedPinConfig: (reportedPinConfig) => set({ reportedPinConfig }),
  setReportedAuxIoConfiguration: (reportedAuxIoConfiguration) => set({ reportedAuxIoConfiguration }),
  setReportedLed: (reportedLed) => set({ reportedLed }),
  setActivation: (activation) => set({ activation }),
  setFindCapabilities: (findCapabilities) => set({ findCapabilities }),
  setFindConfig: (findConfig) => set({ findConfig }),
  setImageStoreInfo: (imageStoreInfo) => set({ imageStoreInfo }),
  setImageStoreItems: (imageStoreItems) => set({ imageStoreItems }),
  setResourceReadState: (resourceReadState, resourceReadError = '') => set({ resourceReadState, resourceReadError }),
  addImageStoreItem: (item) => set((state) => ({ imageStoreItems: [...state.imageStoreItems, item] })),
  addLog: (message, level = 'info') => set((state) => ({
    logs: [...state.logs.slice(-399), { id: ++logId, at: Date.now(), level, message }],
  })),
  clearLogs: () => set({ logs: [] }),
  resetDevice: (preserveAction = false) => set((state) => ({
    connection: navigator.bluetooth ? 'idle' : 'unsupported',
    deviceId: '',
    bleAddress: '',
    deviceName: '',
    localAlias: '',
    boardId: '',
    firmwareVersion: '',
    firmwareBuildEdition: null,
    protocolVersion: null,
    voltage: null,
    temperature: null,
    mcuTemperature: null,
    panelTemperature: null,
    health: null,
    crashAlert: null,
    action: preserveAction ? state.action : 'idle',
    otaPhase: preserveAction && state.action === 'ota' ? state.otaPhase : null,
    actionProgress: preserveAction ? state.actionProgress : null,
    actionDetail: preserveAction ? state.actionDetail : '',
    actionStartedAt: preserveAction ? state.actionStartedAt : null,
    operationFailure: preserveAction ? state.operationFailure : null,
    screen: null,
    capabilities: emptyCapabilities,
    settingsCapabilities: emptyDeviceSettingsCapabilities,
    nfcCapabilities: null,
    reportedNfcTap: null,
    reportedButtonActions: null,
    reportedAuxIo: null,
    reportedPinConfig: null,
    reportedAuxIoConfiguration: null,
    ioWriteUnconfirmed: false,
    reportedLed: null,
    brandVisible: null,
    activation: emptyFirmwareActivation,
    findCapabilities: null,
    findConfig: null,
    imageStoreInfo: null,
    imageStoreItems: [],
    resourceReadState: 'idle',
    resourceReadError: '',
  })),
}))
