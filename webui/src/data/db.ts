import Dexie, { type EntityTable } from 'dexie'
import type { ClientAssetMetadata, ClientDocumentMetadata } from '../../../packages/inktag-client-core/src/storage'
import type { TemplateKind } from './templates'

export type PageKind = TemplateKind | 'custom'

export interface SavedPage extends ClientDocumentMetadata {
  width: number
  height: number
  document: string
  preview?: string
  kind?: PageKind
}

export interface SavedTemplate extends SavedPage { kind: PageKind }

export interface Asset extends ClientAssetMetadata {
  type: string
  blob: Blob
}

export interface ScreenProfileRecord {
  id: string
  name: string
  model: string
  device: string
  size: string
  config: import('../types').ScreenConfig
  busyActiveHigh?: boolean
  createdAt: number
  updatedAt: number
}

export interface DevicePreference {
  id: string
  localAlias: string
  requestedBluetoothName: string
  lastConnectedName?: string
  lastConnectedAt?: number
  lastConnectedBoardId?: string
  reconnectHidden?: boolean
  lastKnownEdition: import('../types').FirmwareEdition
  led: import('../types').LedSettings
  nfcTap: import('../types').NfcTapSettings
  buttonActions: import('../types').ButtonBehaviorSettings
  auxIo: import('../types').AuxIoSettings
  deviceInfoQrUrl: string
  calendarWeekStart: 0 | 1
  calendarCountdownName: string
  calendarCountdownDate: string
  lastAcknowledgedTotalCrashes?: number
  updatedAt: number
}

export interface DeviceSlotPreview {
  id: string
  deviceId: string
  slotId: number
  name: string
  dataUrl: string
  updatedAt: number
}

export interface DeviceGuiAssetPackage {
  id: string
  deviceId: string
  generation: number
  crc32: number
  bytes: Blob
  chineseSource?: 'catalog'
  updatedAt: number
}

export const db = new Dexie('inktag-webui') as Dexie & {
  pages: EntityTable<SavedPage, 'id'>
  templates: EntityTable<SavedTemplate, 'id'>
  canvasDrafts: EntityTable<SavedPage, 'id'>
  assets: EntityTable<Asset, 'id'>
  screenProfiles: EntityTable<ScreenProfileRecord, 'id'>
  devicePreferences: EntityTable<DevicePreference, 'id'>
  deviceSlotPreviews: EntityTable<DeviceSlotPreview, 'id'>
  deviceGuiAssetPackages: EntityTable<DeviceGuiAssetPackage, 'id'>
}

db.version(1).stores({
  pages: 'id, name, templateId, updatedAt',
  assets: 'id, name, type, createdAt',
})

db.version(2).stores({
  pages: 'id, name, templateId, updatedAt',
  assets: 'id, name, type, createdAt',
  screenProfiles: 'id, name, updatedAt',
})

db.version(3).stores({
  pages: 'id, name, templateId, updatedAt',
  assets: 'id, name, type, createdAt',
  screenProfiles: 'id, name, updatedAt',
  devicePreferences: 'id, updatedAt',
})

db.version(4).stores({
  pages: 'id, name, templateId, updatedAt',
  assets: 'id, name, type, createdAt',
  screenProfiles: 'id, name, updatedAt',
  devicePreferences: 'id, updatedAt',
  nativeTemplateDrafts: 'id, updatedAt',
})

db.version(5).stores({
  pages: 'id, name, templateId, updatedAt',
  assets: 'id, name, type, createdAt',
  screenProfiles: 'id, name, updatedAt',
  devicePreferences: 'id, updatedAt',
  nativeTemplateDrafts: 'id, updatedAt',
  deviceSlotPreviews: 'id, deviceId, [deviceId+slotId], updatedAt',
})

db.version(6).stores({
  pages: 'id, name, templateId, updatedAt',
  assets: 'id, name, type, createdAt',
  screenProfiles: 'id, name, updatedAt',
  devicePreferences: 'id, updatedAt',
  nativeTemplateDrafts: 'id, updatedAt',
  deviceSlotPreviews: 'id, deviceId, [deviceId+slotId], updatedAt',
  deviceGuiAssetPackages: 'id, deviceId, updatedAt',
})

db.version(7).stores({
  pages: 'id, name, templateId, updatedAt',
  assets: 'id, name, type, createdAt',
  screenProfiles: 'id, name, updatedAt',
  devicePreferences: 'id, updatedAt',
  nativeTemplateDrafts: 'id, updatedAt',
  leaferDrafts: 'id, templateId, updatedAt',
  deviceSlotPreviews: 'id, deviceId, [deviceId+slotId], updatedAt',
  deviceGuiAssetPackages: 'id, deviceId, updatedAt',
})

// Retire only the old editing formats. Assets, identities, preferences and device records are untouched.
// Keep new scene pages saved during the editor pilot, without loading/converting old documents.
db.version(8).stores({ nativeTemplateDrafts: null }).upgrade(async transaction => {
  const pages = transaction.table<SavedPage>('pages')
  await pages.clear()
  const scenePages = await transaction.table<SavedPage>('leaferDrafts').toArray()
  await pages.bulkPut(scenePages)
})
db.version(9).stores({ leaferDrafts: null })

db.version(10).stores({ pages: 'id, name, templateId, kind, updatedAt', templates: 'id, name, kind, updatedAt' })
db.version(11).stores({ canvasDrafts: 'id, updatedAt' })
