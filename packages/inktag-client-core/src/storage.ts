export interface ClientAssetMetadata {
  id: string
  name: string
  size?: number
  createdAt: number
}

export interface ClientDocumentMetadata {
  id: string
  name: string
  templateId: string
  createdAt?: number
  updatedAt: number
}

export interface ClientReconnectHint {
  deviceId: string
  name: string
  updatedAt: number
}

/** Platform adapters may use IndexedDB, wx storage/files, or another local store. */
export interface ClientStoragePort<TDocument extends ClientDocumentMetadata, TAsset extends ClientAssetMetadata> {
  listDocuments(): Promise<TDocument[]>
  putDocument(document: TDocument): Promise<void>
  removeDocument(id: string): Promise<void>
  listAssets(): Promise<TAsset[]>
  putAsset(asset: TAsset): Promise<void>
  removeAsset(id: string): Promise<void>
}

export function durableBleDeviceKey(bleAddress?: string | null): string | null {
  const value = bleAddress?.trim()
  return value ? `ble:${value}` : null
}

export function newestFirst<T>(items: readonly T[], timestamp: (item: T) => number): T[] {
  return [...items].sort((left, right) => timestamp(right) - timestamp(left))
}
