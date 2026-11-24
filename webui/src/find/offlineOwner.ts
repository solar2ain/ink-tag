import { runFindOwnerTransaction } from './ownerTransaction'
import { materialFingerprint, ownerStage, replacementOwnerStage, readPortableBackup, verifyOwnerProof, type PortableIdentity } from './portable'
import type { FindOwnerRotationProof, FindPublicOnlyOwnerStatus } from '../protocol/find'

const STORAGE_KEY = 'inktag.find.encrypted-backups.v1'
export const BROWSER_FIND_BACKUPS_CHANGED = 'inktag-find-browser-backups-changed'
function changed() { if (typeof window !== 'undefined') window.dispatchEvent(new Event(BROWSER_FIND_BACKUPS_CHANGED)) }
export interface SavedFindBackup { fingerprint: string; encryptedBackup: string; deviceId?: string; deviceName?: string; historyOnly?: boolean }
export function savedFindBackups(storage: Pick<Storage, 'getItem'> = localStorage): SavedFindBackup[] {
  const raw = storage.getItem(STORAGE_KEY)
  if (!raw) return []
  let items: unknown
  try { items = JSON.parse(raw) } catch { throw new Error('本浏览器保存的迁移资料无法读取，请先保留原资料。') }
  if (!Array.isArray(items) || items.length > 256 || items.some(x => !x || typeof x !== 'object' ||
      !/^[0-9a-f]{32}$/.test(x.fingerprint) || typeof x.encryptedBackup !== 'string' || !x.encryptedBackup.startsWith('IKTFIND1.') ||
      (x.deviceName !== undefined && (typeof x.deviceName !== 'string' || x.deviceName.length > 160)) ||
      (x.historyOnly !== undefined && typeof x.historyOnly !== 'boolean'))) {
    throw new Error('本浏览器保存的迁移资料格式无效。')
  }
  return items
}
export function saveFindBackup(record: SavedFindBackup, storage: Pick<Storage, 'getItem' | 'setItem'> = localStorage) {
  if (!/^[0-9a-f]{32}$/.test(record.fingerprint) || !record.encryptedBackup.startsWith('IKTFIND1.'))
    throw new Error('浏览器只保存加密的迁移资料。')
  const items = savedFindBackups(storage), index = items.findIndex(x => x.fingerprint === record.fingerprint)
  const named: SavedFindBackup = { fingerprint: record.fingerprint, encryptedBackup: record.encryptedBackup,
    ...(record.historyOnly ? { historyOnly: true } : {}),
    ...(record.deviceId ? { deviceId: record.deviceId } : {}),
    ...((record.deviceName || items[index]?.deviceName)?.trim() ? { deviceName: (record.deviceName || items[index]?.deviceName)!.replace(/[\u0000-\u001f\u007f]/g, '').trim().slice(0, 160) } : {}) }
  if (index >= 0) items[index] = named
  else items.push(named)
  if (items.length > 256) throw new Error('本浏览器保存的标签已达256条。')
  try { storage.setItem(STORAGE_KEY, JSON.stringify(items)) }
  catch { throw new Error('无法保存加密迁移资料，本次未开始设备登记。请允许浏览器存储后重试。') }
  changed()
}

export function removeFindBackup(fingerprint: string, storage: Pick<Storage, 'getItem' | 'setItem'> = localStorage) {
  const items = savedFindBackups(storage)
  if (!/^[0-9a-f]{32}$/.test(fingerprint)) throw new Error('设备信息标识无效。')
  try { storage.setItem(STORAGE_KEY, JSON.stringify(items.filter(x => x.fingerprint !== fingerprint))) }
  catch { throw new Error('未能清除浏览器中的设备信息，请重试。') }
  changed()
}

export function markFindBackupHistorical(fingerprint: string, storage: Pick<Storage, 'getItem' | 'setItem'> = localStorage) {
  const records = savedFindBackups(storage)
  if (!records.some(record => record.fingerprint === fingerprint)) return
  storage.setItem(STORAGE_KEY, JSON.stringify(records.map(record => record.fingerprint === fingerprint ? { ...record, historyOnly: true } : record)))
  changed()
}

export interface OfflineFindDevice {
  beginPublicOnlyFindOwner(): Promise<{ nonceHex: string }>
  stagePublicOnlyFindOwner(payload: string, nonce: string): Promise<unknown>
  stagePublicOnlyFindOwnerReplacement?(payload: string, nonce: string, fingerprint: string): Promise<unknown>
  commitPublicOnlyFindOwner(): Promise<FindOwnerRotationProof>
  readPublicOnlyFindOwnerStatus(): Promise<FindPublicOnlyOwnerStatus>
  abortFindOwnerRotation(): Promise<unknown>
  configureFind(enabled: boolean): Promise<unknown>
}
export async function enrollOfflineFind(identity: PortableIdentity, encryptedBackup: string,
  device: OfflineFindDevice, assertTarget: () => void, storage: Pick<Storage, 'getItem'> = localStorage,
  plaintextSaved = false, previous?: PortableIdentity) {
  async function guarded<T>(operation: () => Promise<T>) {
    assertTarget()
    const result = await operation()
    assertTarget()
    return result
  }
  // This check is before BEGIN, not merely before the final commit.
  const plaintext = encryptedBackup.trimStart().startsWith('{')
  const saved = plaintext ? plaintextSaved && (await readPortableBackup(encryptedBackup, '', true)).identities.some(e =>
    !e.historyOnly && e.fingerprint === identity.fingerprint && e.materialHex === identity.materialHex && e.deviceId === identity.deviceId)
    : savedFindBackups(storage).some(x => x.fingerprint === identity.fingerprint && x.encryptedBackup === encryptedBackup)
  if (!saved ||
      await materialFingerprint(identity.materialHex) !== identity.fingerprint) throw new Error('请先保存此标签的迁移资料。')
  if (previous) {
    if (!device.stagePublicOnlyFindOwnerReplacement || previous.deviceId.toLowerCase() !== identity.deviceId.toLowerCase())
      throw new Error('当前设备不支持替换此身份。')
    const status = await guarded(() => device.readPublicOnlyFindOwnerStatus())
    await verifyOwnerProof(previous, status)
  }
  await runFindOwnerTransaction({
    prepare: () => guarded(() => device.beginPublicOnlyFindOwner()),
    stage: async p => {
      if (previous) {
        const stage = await replacementOwnerStage(previous, identity, p.nonceHex)
        await guarded(() => device.stagePublicOnlyFindOwnerReplacement!(stage, p.nonceHex, previous.fingerprint))
      } else await guarded(() => device.stagePublicOnlyFindOwner(ownerStage(identity, p.nonceHex), p.nonceHex))
    },
    commit: () => guarded(() => device.commitPublicOnlyFindOwner()),
    finalize: async (_p, proof) => { await verifyOwnerProof(identity, proof); return true },
    abortDevice: () => guarded(() => device.abortFindOwnerRotation()),
    abortLocal: async () => undefined, // Never discard the only recovery copy.
    recoverAfterCommit: async () => {
      const status = await guarded(() => device.readPublicOnlyFindOwnerStatus())
      if (!status.identityPresent || status.fingerprintHex.toLowerCase() !== identity.fingerprint) return undefined
      await verifyOwnerProof(identity, status)
      return true
    },
  })
  await guarded(() => device.configureFind(true))
}
