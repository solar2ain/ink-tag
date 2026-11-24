export interface FindSecondFactorMethod {
  index: number
  kind: 'trusted_device' | 'sms' | 'unknown'
  label: string
}

export interface FindSessionState {
  ok: boolean
  loggedIn: boolean
  queryReady: boolean
  queryError?: string | null
  needs2fa: boolean
  appleAccountUsername: string
  sessionStored: boolean
  sessionRestored: boolean
  sessionError?: string | null
  secondFactorWarning?: string | null
  methods: FindSecondFactorMethod[]
  activeMethodIndex: number | null
  address: string
  keyFingerprint: string
  advertisementKey: string
  identityChanged?: boolean
  previousKeyFingerprint?: string
  selectedKeyFingerprint: string
  sessionProtection: 'windows-dpapi' | 'aes256-local-key'
  trackingIntervalMinutes: number
  identities: FindIdentity[]
}

export interface FindIdentity {
  historyOnly?: boolean
  keyFingerprint: string
  deviceId: string
  address: string
  advertisementKey: string
  deviceName: string
  bluetoothName: string
  findNote: string
  trackingEnabled: boolean
  lastAutoQueryAt: string | null
  lastAutoError: string | null
  trackPointCount: number
  createdAt: string
  lastQueriedAt: string | null
  lastReportAt: string | null
  selected: boolean
  derivationVersion: number
  periodSeconds?: number
  currentSlot: number
}

export interface FindTrackPoint {
  timestamp: string
  receivedAt: string
  latitude: number
  longitude: number
  horizontalAccuracy: number
  confidence: number
  status: number
  sourceSlot?: number
}

export interface FindTrackHistory {
  ok: boolean
  keyFingerprint: string
  total: number
  windowHours: number
  windowStart?: string
  windowEnd?: string
  retentionDays: number
  points: FindTrackPoint[]
}

export interface FindReport {
  found: true
  timestamp: string
  latitude: number
  longitude: number
  horizontalAccuracy: number
  confidence: number
  status: number
  matchedSlot?: number
  queryScope?: 'window'
  queryWindow?: 'latest' | 'recent'
  derivedSlotCount?: number
  queriedKeyCount?: number
  savedPointCount?: number
  fromHistory?: boolean
  queryReportTimestamp?: string
}

export interface FindTrackMap {
  ok: boolean
  provider: 'baidu' | 'amap' | 'osm'
  pointCount: number
  sampledPointCount: number
  imageDataUrl: string
}

export type FindQueryResult = FindReport | {
  found: false
  queryScope?: 'window'
  queryWindow?: 'latest' | 'recent'
  derivedSlotCount?: number
  queriedKeyCount?: number
  savedPointCount?: number
}

export interface FindOwnerRotationPreparation {
  ok: true
  rotationId: string
  currentKeyFingerprint: string
  replacementKeyFingerprint: string
  payloadHex: string
}

export interface FindPublicOnlyPreparation {
  ok: true
  transactionId: string
  fingerprintHex: string
  payloadHex: string
  deviceId: string
  bluetoothName: string
}

export interface FindPublicOnlyReplacementPreparation {
  ok: true
  transactionId: string
  currentFingerprintHex: string
  fingerprintHex: string
  payloadHex: string
  deviceId: string
}

export interface FindIdentityImportResult extends FindSessionState {
  importedKeyFingerprint: string
}

async function request<T>(path: string, body?: Record<string, unknown>): Promise<T> {
  const response = await fetch(`api/v1/find${path}`, body === undefined ? {
    cache: 'no-store',
    credentials: 'same-origin',
    headers: { Accept: 'application/json' },
  } : {
    method: 'POST',
    cache: 'no-store',
    credentials: 'same-origin',
    headers: { Accept: 'application/json', 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  })
  let payload: Record<string, unknown>
  try {
    payload = await response.json() as Record<string, unknown>
  } catch {
    throw new Error(`Find 本机服务返回了无效响应（HTTP ${response.status}）`)
  }
  if (!response.ok || payload.ok === false) {
    const message = payload.error === 'find_service_unavailable'
      ? '主 WebUI 尚未连接 InkTag Find 本机服务。'
      : typeof payload.error === 'string' ? payload.error : `请求失败（HTTP ${response.status}）`
    throw new Error(message)
  }
  return payload as T
}

export const findClient = {
  exportBackup: (password: string, includeHistory = true, fingerprints?: string[], includeKeys = true, encrypt = true, plaintextConfirmed = false) => request<{ ok: true; encryptedBackup?: string; backupText?: string; encrypted: boolean; identityCount: number }>('/backup/export', { password, includeHistory, fingerprints, includeKeys, encrypt, plaintextConfirmed }),
  importBackup: (encryptedBackup: string, password: string, allowPlaintext = false) => request<FindSessionState & { importedCount: number; existingCount: number; importedTrackCount: number }>('/backup/import', { encryptedBackup, password, allowPlaintext }),
  status: () => request<FindSessionState>('/status'),
  login: (email: string, password: string, consent: boolean) => request<FindSessionState>('/login', { email, password, consent }),
  requestSecondFactor: (index: number) => request<FindSessionState>('/2fa/request', { index }),
  submitSecondFactor: (index: number, code: string) => request<FindSessionState>('/2fa/submit', { index, code }),
  query: (options: { fingerprint?: string; saveHistory?: boolean; queryWindow?: 'latest' | 'recent' } = {}) => request<FindQueryResult & { ok: boolean }>('/query', options),
  logout: () => request<FindSessionState>('/logout', {}),
  selectIdentity: (fingerprint: string) => request<FindSessionState>('/identity/select', { fingerprint }),
  importIdentity: (seedHex: string, metadata: {
    deviceId: string; deviceName: string; bluetoothName: string; findNote: string
  }) => request<FindIdentityImportResult>('/identity/import', { seedHex, ...metadata, select: true }),
  verifyIdentityProof: (fingerprint: string, slot: number, publicKeyHex: string) => request<FindSessionState>('/identity/proof/verify', { fingerprint, slot, publicKeyHex }),
  proveOwnerAction: (fingerprint: string, deviceIdHex: string, nonceHex: string, actionHashHex: string) =>
    request<{ ok: true; proofHex: string; fingerprintHex: string }>('/identity/owner-action/prove', {
      fingerprint, deviceIdHex, nonceHex, actionHashHex,
    }),
  updateIdentity: (fingerprint: string, metadata: Partial<Pick<FindIdentity, 'deviceId' | 'findNote' | 'deviceName' | 'bluetoothName'>>, deviceScope = false) => request<FindSessionState>('/identity/update', { fingerprint, ...metadata, deviceScope }),
  prepareOwnerRotation: (fingerprint: string, nonceHex: string) => request<FindOwnerRotationPreparation>('/identity/rotate/prepare', { fingerprint, nonceHex }),
  finalizeOwnerRotation: (rotationId: string, slot: number, publicKeyHex: string) => request<FindSessionState>('/identity/rotate/finalize', { rotationId, slot, publicKeyHex }),
  abortOwnerRotation: (rotationId: string) => request<FindSessionState>('/identity/rotate/abort', { rotationId }),
  preparePublicOnlyOwner: (nonceHex: string, metadata: {
    deviceId: string; deviceName: string; bluetoothName: string; findNote: string
  }) => request<FindPublicOnlyPreparation>('/identity/public-only/prepare', { nonceHex, ...metadata }),
  preparePublicOnlyReplacement: (fingerprintHex: string, nonceHex: string, deviceId: string) =>
    request<FindPublicOnlyReplacementPreparation>('/identity/public-only/replace/prepare', {
      fingerprintHex, nonceHex, deviceId,
    }),
  provePublicOnlyUnregister: (fingerprintHex: string, nonceHex: string, deviceId: string) =>
    request<{ ok: true; payloadHex: string }>('/identity/public-only/unregister/prove', { fingerprintHex, nonceHex, deviceId }),
  finalizePublicOnlyOwner: (transactionId: string, slot: number, publicKeyHex: string, fingerprintHex: string) =>
    request<FindSessionState>('/identity/public-only/finalize', { transactionId, slot, publicKeyHex, fingerprintHex }),
  abortPublicOnlyOwner: (transactionId: string) => request<FindSessionState>('/identity/public-only/abort', { transactionId }),
  recoverPublicOnlyOwner: (fingerprintHex: string, slot: number, publicKeyHex: string, deviceId: string) =>
    request<FindSessionState>('/identity/public-only/recover', { fingerprintHex, slot, publicKeyHex, deviceId }),
  deleteIdentity: (fingerprint: string) => request<FindSessionState>('/identity/delete', { fingerprint, confirm: true }),
  configureTracking: (fingerprint: string, enabled: boolean, intervalMinutes: number) => request<FindSessionState>('/tracking/configure', { fingerprint, enabled, intervalMinutes }),
  configureAllTracking: (enabled: boolean) => request<FindSessionState>('/tracking/configure-all', { enabled }),
  trackingHistory: (fingerprint: string, limit = 250, hours = 24, startAt?: string, endAt?: string, deviceScope = false) => request<FindTrackHistory>('/tracking/history', { fingerprint, limit, hours, startAt, endAt, deviceScope }),
  trackingMap: (fingerprint: string, provider: 'baidu' | 'amap' | 'osm', apiKey: string, hours = 24, startAt?: string, endAt?: string, deviceScope = false) => request<FindTrackMap>('/tracking/map', { fingerprint, provider, apiKey, hours, startAt, endAt, deviceScope }),
  clearTrackingHistory: (fingerprint: string, deviceScope = false) => request<FindSessionState>('/tracking/clear', { fingerprint, confirm: true, deviceScope }),
}
