import { useEffect, useState } from 'react'
import { useDeviceStore } from '../store/device'
import { BROWSER_FIND_BACKUPS_CHANGED, removeFindBackup, savedFindBackups, type SavedFindBackup } from './offlineOwner'
import { FindDeleteGuard } from './FindDeleteGuard'
import { useConfirmationDialog } from '../components/ConfirmationDialog'

export function BrowserFindBackups({ onMessage }: { onMessage: (text: string) => void }) {
  const [items, setItems] = useState<SavedFindBackup[]>([])
  const [selectedFingerprint, setSelectedFingerprint] = useState('')
  const [error, setError] = useState('')
  const [deleting, setDeleting] = useState<SavedFindBackup | null>(null)
  const confirmation = useConfirmationDialog()
  const device = useDeviceStore()
  const name = (item: SavedFindBackup) => item.deviceName || (
    device.connection === 'connected' && device.activation.activationId && item.deviceId?.toLowerCase() === `activation:${device.activation.activationId}`.toLowerCase()
      ? device.localAlias || device.deviceName : '') || `设备 ${item.fingerprint.slice(-8).toUpperCase()}`
  useEffect(() => {
    const read = () => {
      try { setItems(savedFindBackups()); setError('') }
      catch (e) { setError(e instanceof Error ? e.message : '无法读取本地设备信息。') }
    }
    read()
    window.addEventListener(BROWSER_FIND_BACKUPS_CHANGED, read)
    window.addEventListener('storage', read)
    return () => { window.removeEventListener(BROWSER_FIND_BACKUPS_CHANGED, read); window.removeEventListener('storage', read) }
  }, [])
  const clear = async (item: SavedFindBackup) => {
    try { removeFindBackup(item.fingerprint); setSelectedFingerprint(''); setDeleting(null); onMessage('已清除当前浏览器中的这份设备信息。') }
    catch (e) { setError(e instanceof Error ? e.message : '清除失败。'); throw e }
  }
  const requestClear = async (item: SavedFindBackup) => {
    if (!item.historyOnly) { setDeleting(item); return }
    if (await confirmation.confirm({ title: '清除历史身份备份？', message: <>仅清除当前浏览器中的<strong>这份历史备份</strong>，其他身份和设备不受影响。</>, danger: true, confirmLabel: '确认清除' })) await clear(item)
  }
  const copy = async (item: SavedFindBackup) => {
    try { await navigator.clipboard.writeText(item.encryptedBackup); onMessage('已复制加密设备信息，导入时仍需原口令。') }
    catch { setError('无法复制，请使用“保存文件”。') }
  }
  const download = (item: SavedFindBackup) => {
    const url = URL.createObjectURL(new Blob([item.encryptedBackup], { type: 'text/plain;charset=utf-8' }))
    const link = document.createElement('a'); link.href = url
    link.download = `InkTag-Find-${item.fingerprint.slice(-8)}.iktfind`; link.click()
    window.setTimeout(() => URL.revokeObjectURL(url), 1000)
  }
  if (!items.length && !error) return null
  const selected = items.find(item => item.fingerprint === selectedFingerprint) || items[0]
  return <section className="find-panel find-browser-backups" aria-labelledby="find-browser-backups-title">
    <h3 id="find-browser-backups-title">当前浏览器保存的设备信息</h3>
    <p>名称用于识别标签；备份已加密，口令不会保存。</p>
    {error && <p role="alert" className="error-text">{error}</p>}
    {selected && <>
      <label className="find-browser-backup-selector">标签<select aria-label="选择已保存的标签" value={selected.fingerprint} onChange={event => setSelectedFingerprint(event.target.value)}>
        {items.map(item => <option key={item.fingerprint} value={item.fingerprint}>{name(item)} · {item.fingerprint.slice(-8).toUpperCase()}</option>)}
      </select></label>
      <ul><li key={selected.fingerprint}>
        <span title={selected.fingerprint}>{name(selected)}<small>已加密 · {selected.fingerprint.slice(-8).toUpperCase()}</small></span>
        <div><button type="button" className="button ghost" onClick={() => void copy(selected)}>复制</button>
          <button type="button" className="button ghost" onClick={() => download(selected)}>保存文件</button>
          <button type="button" className="button ghost danger-text" onClick={() => void requestClear(selected)}>清除</button></div>
      </li></ul>
    </>}
    {deleting && <FindDeleteGuard key={deleting.fingerprint} name={name(deleting)} deviceId={deleting.deviceId}
      onBackup={() => download(deleting)} onDelete={() => clear(deleting)} onCancel={() => setDeleting(null)} />}
    {confirmation.dialog}
  </section>
}
