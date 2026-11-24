import { useEffect, useState } from 'react'
import { inktagClient } from '../protocol/client'
import { DEVICE_PASSWORD_WARNING, generateDevicePassword } from '../protocol/devicePassword'
import { useDeviceStore } from '../store/device'
import { ProBadge } from './ProBadge'
import { savePasswordCredential } from '../data/passwordCredentials'
import { X } from 'lucide-react'

function usePasswordState() {
  const [, redraw] = useState(0)
  useEffect(() => {
    const update = () => redraw((v) => v + 1)
    window.addEventListener('inktag-password-state', update)
    return () => window.removeEventListener('inktag-password-state', update)
  }, [])
  return inktagClient.passwordStatus
}

export function DevicePasswordPrompt() {
  const status = usePasswordState()
  const connection = useDeviceStore((s) => s.connection)
  const [open, setOpen] = useState(false)
  const [password, setPassword] = useState('')
  const [remember, setRemember] = useState(false)
  const [message, setMessage] = useState('')
  const [busy, setBusy] = useState(false)
  useEffect(() => {
    const show = (event: Event) => { if ((event as CustomEvent).detail?.prompt) setOpen(true) }
    window.addEventListener('inktag-password-state', show)
    return () => window.removeEventListener('inktag-password-state', show)
  }, [])
  useEffect(() => {
    if (!['connecting', 'connected'].includes(connection) || status?.authorized || !status?.enabled) {
      setOpen(false); setPassword(''); setMessage('')
    }
  }, [connection, status?.authorized, status?.enabled])
  useEffect(() => {
    if (!open) return
    const cancel = () => { setPassword(''); setOpen(false); inktagClient.disconnect() }
    const escape = (event: KeyboardEvent) => { if (event.key === 'Escape' && !busy) cancel() }
    const timer = window.setTimeout(cancel, 60000)
    window.addEventListener('keydown', escape)
    return () => { window.clearTimeout(timer); window.removeEventListener('keydown', escape) }
  }, [open, busy])
  if (!open) return null
  return <div className="modal-backdrop"><section className="confirmation-dialog device-password-dialog" role="dialog" aria-modal="true" aria-labelledby="device-password-title">
    <header><h2 id="device-password-title">输入设备密码</h2></header>
    <form onSubmit={(event) => { event.preventDefault(); setBusy(true); setMessage(''); void inktagClient.authenticateDevicePassword(password, undefined, remember).then(() => { setPassword(''); setOpen(false) }).catch((error) => setMessage(error instanceof Error ? error.message : '验证失败，请稍后重试。')).finally(() => setBusy(false)) }}>
      <label>密码<input type="password" required autoFocus autoComplete="off" minLength={4} maxLength={16} value={password} onChange={(event) => setPassword(event.target.value)} /></label>
      <label className="device-password-remember"><input type="checkbox" checked={remember} disabled={busy} onChange={(event) => setRemember(event.target.checked)} />记住密码（仅此浏览器）</label>
      {message && <p role="alert">{message}</p>}
      <footer><button className="button secondary" type="button" disabled={busy} onClick={() => { setBusy(true); void inktagClient.showPasswordDeviceInfo().then(() => { useDeviceStore.getState().addLog('设备已显示二维码，请扫码连接。'); inktagClient.disconnect() }).catch(() => setMessage('暂时无法显示设备页，请稍后重试。')).finally(() => setBusy(false)) }}>忘记密码</button><button className="button secondary" type="button" disabled={busy} onClick={() => { setPassword(''); setOpen(false); inktagClient.disconnect() }}>取消并断开</button><button className="button primary" disabled={busy}>验证</button></footer>
    </form>
  </section></div>
}

export function DevicePasswordSettingsDialog({ onClose }: { onClose: () => void }) {
  const state = useDeviceStore()
  const status = usePasswordState()
  const [mode, setMode] = useState<'set' | 'disable' | null>(null)
  const [oldPassword, setOld] = useState('')
  const [newPassword, setNew] = useState('')
  const [confirmation, setConfirmation] = useState('')
  const [busy, setBusy] = useState(false)
  const [message, setMessage] = useState('')
  const available = state.connection === 'connected' && inktagClient.passwordSupported() && state.activation.phase === 'active'
  const availabilityHint = state.connection !== 'connected' ? '连接设备后可查看及设置密码。'
    : !inktagClient.passwordSupported() ? '当前固件未提供设备密码能力。'
    : state.activation.phase !== 'active' ? '开启设备密码需要已激活的 Pro 设备。' : ''
  const close = () => { setMode(null); setOld(''); setNew(''); setConfirmation(''); setMessage('') }
  useEffect(() => {
    setMode(null); setOld(''); setNew(''); setConfirmation(''); setMessage('')
  }, [state.connection, state.deviceId])
  useEffect(() => {
    const escape = (event: KeyboardEvent) => { if (event.key === 'Escape' && !busy) onClose() }
    window.addEventListener('keydown', escape)
    return () => window.removeEventListener('keydown', escape)
  }, [busy, onClose])
  return <div className="modal-backdrop" onMouseDown={(event) => { if (event.target === event.currentTarget && !busy) onClose() }}><section className="confirmation-dialog device-password-dialog device-password-settings" role="dialog" aria-modal="true" aria-labelledby="device-password-settings-title">
    <header><h2 id="device-password-settings-title">{mode === 'disable' ? '删除设备密码' : mode === 'set' ? (status?.enabled ? '修改设备密码' : '设置设备密码') : <>设备密码 <ProBadge /></>}</h2><button className="icon-button" type="button" aria-label="关闭窗口" disabled={busy} onClick={onClose}><X /></button></header>
    {!mode ? <>
      <p>设置连接密码，保护设备操作权限。修改或删除时仍需输入原密码。</p>
      <dl className="settings-list"><div><dt>密码状态</dt><dd>{state.connection === 'connected' && status ? (status.enabled ? '已开启' : '未开启') : '尚未读取'}</dd></div><div><dt>当前连接</dt><dd>{state.connection !== 'connected' ? '未连接' : !inktagClient.passwordSupported() ? '不支持密码' : status?.enabled ? (status.authorized ? '已验证' : '待验证') : status ? '无需验证' : '尚未读取'}</dd></div></dl>
      {availabilityHint && <p className="device-password-hint">{availabilityHint}</p>}
      <div className="button-row"><button className="button secondary" disabled={!available || busy} onClick={() => { setMessage(''); setMode('set') }}>{status?.enabled ? '修改密码' : '设置密码'}</button>
        {status?.enabled && <button className="button secondary danger-text" disabled={!available || busy} onClick={() => { setMessage(''); setMode('disable') }}>删除密码</button>}
        {status?.enabled && !status.authorized && <button className="button secondary" disabled={!available || busy} onClick={() => { onClose(); window.dispatchEvent(new CustomEvent('inktag-password-state', { detail: { prompt: true } })) }}>验证密码</button>}
        {state.activation.activationId && <button className="button secondary" disabled={busy} onClick={() => {
          const removed = savePasswordCredential(state.activation.activationId!, null)
          setMessage(removed ? '本机记录已清除，下次连接需输入密码。' : '无法清除本机记录，请检查浏览器存储权限。')
        }}>清除本机记录</button>}
      </div>
      {message && <p className="device-password-message" role="status">{message}</p>}
      <footer className="device-password-overview-footer"><button className="button secondary" type="button" onClick={onClose}>关闭</button></footer>
    </> : <>
      <p className="device-password-warning">{mode === 'disable' ? '删除后，附近的其他用户可以连接并操作设备。' : DEVICE_PASSWORD_WARNING}</p>
      <form onSubmit={(event) => { event.preventDefault(); setMessage(''); if (mode === 'set' && newPassword !== confirmation) { setMessage('两次输入的新密码不一致。'); return } setBusy(true); void inktagClient.configureDevicePassword(mode === 'disable' ? null : newPassword, oldPassword).then(() => { close(); setMessage('设备密码已更新。') }).catch((error) => setMessage(error instanceof Error ? error.message : '保存失败。')).finally(() => setBusy(false)) }}>
        {status?.enabled && <label>原密码<input type="password" autoComplete="off" required minLength={4} maxLength={16} value={oldPassword} onChange={(event) => setOld(event.target.value)} /></label>}
        {mode === 'set' && <><label>新密码<input type="password" autoComplete="new-password" required minLength={4} maxLength={16} value={newPassword} onChange={(event) => setNew(event.target.value)} /></label><label>确认新密码<input type="password" autoComplete="new-password" required minLength={4} maxLength={16} value={confirmation} onChange={(event) => setConfirmation(event.target.value)} /></label></>}
        {message && <p role="alert">{message}</p>}
        <footer>{mode === 'set' && <button className="button secondary password-generate" type="button" disabled={busy} onClick={() => { const password = generateDevicePassword(); setNew(password); setConfirmation(password); setMessage(`请保存本次随机密码：${password}`) }}>生成 8 位密码</button>}<button className="button secondary" type="button" disabled={busy} onClick={close}>取消</button><button className="button primary" disabled={busy}>确认</button></footer>
      </form>
    </>}
  </section></div>
}
