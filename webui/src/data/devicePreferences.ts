import { defaultAuxIoSettings, defaultButtonBehaviorSettings, defaultLedSettings, defaultNfcTapSettings } from '../protocol/deviceSettings'
import type { LedSettings } from '../types'
import type { FirmwareEdition } from '../types'
import type { DevicePreference } from './db'
import { db } from './db'
import { INKTAG_WEB_URL } from '../qr/qr'

function normalizeLedSettings(value: unknown): LedSettings {
  const defaults = defaultLedSettings()
  if (!value || typeof value !== 'object') return defaults
  const saved = value as Partial<LedSettings>
  const policy = saved.policy === 'off' || saved.policy === 'all' ||
    saved.policy === 'critical'
    ? saved.policy : defaults.policy
  return { policy }
}

export async function getDevicePreference(id: string): Promise<DevicePreference> {
  const saved = await db.devicePreferences.get(id)
  const defaults: DevicePreference = {
    id,
    localAlias: '',
    requestedBluetoothName: '',
    lastKnownEdition: 'base',
    led: defaultLedSettings(),
    nfcTap: defaultNfcTapSettings(),
    buttonActions: defaultButtonBehaviorSettings(),
    auxIo: defaultAuxIoSettings(),
    deviceInfoQrUrl: INKTAG_WEB_URL,
    calendarWeekStart: 1,
    calendarCountdownName: '',
    calendarCountdownDate: '',
    updatedAt: Date.now(),
  }
  return saved ? {
    ...defaults,
    ...saved,
    led: normalizeLedSettings(saved.led),
    nfcTap: saved.nfcTap
      ? { ...saved.nfcTap, controlUrl: saved.nfcTap.controlUrl || defaults.nfcTap.controlUrl }
      : defaults.nfcTap,
    buttonActions: saved.buttonActions || defaults.buttonActions,
    auxIo: saved.auxIo || defaults.auxIo,
    deviceInfoQrUrl: saved.deviceInfoQrUrl || defaults.deviceInfoQrUrl,
  } : defaults
}

export async function updateDevicePreference(
  id: string,
  patch: Partial<Omit<DevicePreference, 'id' | 'updatedAt'>>,
) {
  // Connection history, entitlement and settings may finish together. Keep
  // their read/merge/write atomic so one patch cannot discard another.
  return db.transaction('rw', db.devicePreferences, async () => {
    const current = await getDevicePreference(id)
    const next = { ...current, ...patch, id, updatedAt: Date.now() }
    await db.devicePreferences.put(next)
    return next
  })
}

export const DEVICE_ENTITLEMENT_CHANGED_EVENT = 'inktag-device-entitlement-changed'

export async function rememberDeviceEdition(id: string, edition: FirmwareEdition) {
  const preference = await updateDevicePreference(id, { lastKnownEdition: edition })
  if (typeof window !== 'undefined') window.dispatchEvent(new Event(DEVICE_ENTITLEMENT_CHANGED_EVENT))
  return preference
}

export async function hasKnownProDevice() {
  return (await db.devicePreferences.toArray()).some((preference) => preference.lastKnownEdition === 'advanced')
}
