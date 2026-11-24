import { useEffect, useRef, useState } from 'react'
import { X } from 'lucide-react'
import { useConfirmationDialog } from '../components/ConfirmationDialog'
import { useDeviceStore } from '../store/device'
import { inktagClient } from '../protocol/client'
import { supportsFindOwnerUnregister } from '../protocol/find'

export const FIND_DELETE_SKIP_WARNING = <>如果没有其他备份，删除后将无法再查询此身份的位置，也无法替换设备正在使用的 Find 身份；只能通过<strong className="find-delete-warning">重新完整刷机</strong>清除设备身份后重新登记。<strong className="find-delete-warning">完整刷机可能清除设备数据。</strong>确定仍要删除吗？</>

/** An accidental-deletion guard, not firmware authorization or a backup proof. */
export function FindDeleteGuard({ deviceId, name, visible = true, onBackup, onDelete, onUnregister, onCancel }: {
  deviceId?: string; name: string; visible?: boolean
  onUnregister?: () => Promise<void>
  onBackup: () => void; onDelete: () => Promise<void>; onCancel: () => void
}) {
  const device = useDeviceStore()
  const confirmation = useConfirmationDialog()
  const [saved, setSaved] = useState(false), [busy, setBusy] = useState(false)
  const [confirming, setConfirming] = useState(false), [error, setError] = useState('')
  const inFlight = useRef(false)
  const connected = device.connection === 'connected' && Boolean(deviceId) &&
    deviceId!.toLowerCase() === `activation:${device.activation.activationId}`.toLowerCase()
  const canUnregister = connected && supportsFindOwnerUnregister(device.findCapabilities)
  useEffect(() => {
    if (!visible || confirming) return
    const escape = (event: KeyboardEvent) => { if (event.key === 'Escape' && !busy) onCancel() }
    window.addEventListener('keydown', escape)
    return () => window.removeEventListener('keydown', escape)
  }, [visible, confirming, busy, onCancel])
  const connect = async () => {
    setBusy(true); setError('')
    try {
      await inktagClient.connect({ onlyInkTag: true })
      const current = useDeviceStore.getState()
      if (current.connection !== 'connected' || !deviceId ||
          deviceId.toLowerCase() !== `activation:${current.activation.activationId}`.toLowerCase())
        setError('请连接这份设备信息对应的标签；也可以先保存备份。')
    } catch (e) { setError(e instanceof Error ? e.message : '连接失败，请重试或先保存备份。') }
    finally { setBusy(false) }
  }
  const remove = async (force = false) => {
    if (inFlight.current) return
    inFlight.current = true
    try {
      const current = useDeviceStore.getState()
      const exact = current.connection === 'connected' && deviceId &&
        deviceId.toLowerCase() === `activation:${current.activation.activationId}`.toLowerCase()
      if (force || (!onUnregister && !exact && !saved)) {
        setConfirming(true)
        const accepted = await confirmation.confirm({ title: force ? '仅强制删除本机资料？' : '跳过连接和备份？', danger: true,
          message: force ? <>此操作只删除本机资料，设备上的 Find 身份会保留。{FIND_DELETE_SKIP_WARNING}</> : FIND_DELETE_SKIP_WARNING,
          confirmLabel: '仍然删除' })
        setConfirming(false)
        if (!accepted) return
      }
      setBusy(true)
      setError('')
      if (onUnregister && !force) await onUnregister()
      await onDelete()
    } catch (e) {
      setError(e instanceof Error ? e.message : '删除失败，设备信息仍保留，请重试。')
    } finally { setBusy(false); inFlight.current = false }
  }
  return <div className="find-delete-guard">
    {visible && !confirming && <div className="modal-backdrop">
      <section className="confirmation-dialog find-delete-safety" role="dialog" aria-modal="true" aria-labelledby="find-delete-safety-title">
        <header><h2 id="find-delete-safety-title">删除“{name}”的本机 Find 资料？</h2><button className="icon-button" aria-label="关闭" disabled={busy} onClick={onCancel}><X /></button></header>
        {onUnregister
          ? <p>请先连接对应标签或备份设备信息。设备确认解除 Find 登记后，本机才会删除资料。</p>
          : <p>请先连接对应标签或备份设备信息。<strong>设备会保留当前 Find 身份。</strong> 连接后仍建议先备份。</p>}
        {onUnregister && connected && !canUnregister && <p>当前固件不支持解除 Find 登记。</p>}
        {error && <p className="error-text" role="alert">{error}</p>}
        <div className="find-delete-safety-options">
          <button className="button secondary" disabled={busy || connected} onClick={() => void connect()}>{connected ? '对应设备已连接' : '连接设备'}</button>
          <button className="button secondary" disabled={busy} onClick={onBackup}>导出备份</button>
        </div>
        {!onUnregister && <label className="find-transfer-ack"><input type="checkbox" checked={saved} disabled={busy} onChange={e => setSaved(e.target.checked)} />我已保存这份身份的备份</label>}
        {!onUnregister && <p>若没有其他备份，删除后将失去此身份的查询私钥。</p>}
        <footer>
          <button className="button secondary" disabled={busy} onClick={onCancel}>取消</button>
          {onUnregister && <button className="button secondary danger-text" disabled={busy} onClick={() => void remove(true)}>仅删除本机资料</button>}
          <button className="button danger" disabled={busy || Boolean(onUnregister && !canUnregister)} onClick={() => void remove()}>
            {onUnregister ? '解除登记并删除' : connected || saved ? '确认删除' : '跳过并继续'}
          </button>
        </footer>
      </section>
    </div>}
    {confirmation.dialog}
  </div>
}
