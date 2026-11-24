import { AlertTriangle, LoaderCircle, Power, RotateCcw, X } from 'lucide-react'
import { useEffect, useState } from 'react'
import { inktagClient } from '../protocol/client'
import { useDeviceStore } from '../store/device'

export type DeviceLifecycleAction = 'reset' | 'clear-user-data' | 'reset-settings'

const descriptions = {
  reset: { title: '重启设备', detail: '重启后等待设备重连并校时。', preserved: null, confirm: '重启' },
  'clear-user-data': { title: '清除图槽', detail: '删除全部图片及其名称、类型和播放引用，不刷新屏幕。', preserved: '保留设置、密码、设备二维码、NFC、Pro、Find 和板型数据。', confirm: '清除图槽' },
  'reset-settings': { title: '清除设置', detail: '恢复普通设置并关闭设备密码。开启密码时必须输入原密码，扫码或已连接不能替代。', preserved: '保留图片、Pro、Find、板型、工厂默认及校准；设备二维码恢复无密码默认。', confirm: '清除设置' },
}

interface DeviceLifecycleDialogProps { action: DeviceLifecycleAction; onClose: () => void }

export function DeviceLifecycleDialog({ action, onClose }: DeviceLifecycleDialogProps) {
  const state = useDeviceStore()
  const [running, setRunning] = useState(false)
  const [phase, setPhase] = useState('正在处理')
  const [message, setMessage] = useState('')
  const [oldPassword, setOldPassword] = useState('')
  const description = descriptions[action]
  const Icon = action === 'reset' ? Power : RotateCcw

  useEffect(() => {
    const closeOnEscape = (event: KeyboardEvent) => { if (event.key === 'Escape' && !running) onClose() }
    window.addEventListener('keydown', closeOnEscape)
    return () => window.removeEventListener('keydown', closeOnEscape)
  }, [onClose, running])

  const confirm = async () => {
    setRunning(true)
    setMessage('')
    try {
      if (action === 'reset') await inktagClient.softReset({ onPhase: setPhase })
      else if (action === 'clear-user-data') {
        const removed = await inktagClient.clearDeviceUserData()
        state.addLog(`已清除 ${removed} 张图片；设置、密码、Pro 和 Find 保留`, 'success')
      } else {
        await inktagClient.resetDeviceSettings(oldPassword || undefined)
        state.addLog('设置已清除，密码已关闭；图片、Pro 和 Find 保留', 'success')
      }
      onClose()
    } catch (error) {
      setMessage(error instanceof Error ? error.message : String(error))
      setRunning(false)
    } finally { setOldPassword('') }
  }

  return <div className="modal-backdrop" onMouseDown={(event) => { if (event.target === event.currentTarget && !running) onClose() }}>
    <section className="lifecycle-dialog" role="dialog" aria-modal="true" aria-labelledby="lifecycle-dialog-title">
      <header><div className={action === 'reset' ? '' : 'danger'}><Icon /><span><h2 id="lifecycle-dialog-title">{description.title}</h2><p>{description.detail}</p></span></div><button className="icon-button" title="关闭" aria-label="关闭" disabled={running} onClick={onClose}><X /></button></header>
      {description.preserved && <div className="lifecycle-preserved"><AlertTriangle /><span><strong>保留范围</strong>{description.preserved}</span></div>}
      {action === 'reset-settings' && inktagClient.passwordSupported() && <div className="lifecycle-final-check"><label>原密码（未开启则留空）<input type="password" value={oldPassword} disabled={running} onChange={(event) => setOldPassword(event.target.value)} autoComplete="off" spellCheck={false} maxLength={16} placeholder="仅用于本次验证，不保存" /></label></div>}
      {message && <div className="identity-dialog-message error">{message}</div>}
      <footer><button className="button secondary" disabled={running} onClick={onClose}>取消</button><button className={action === 'reset' ? 'button primary' : 'button danger'} disabled={running || state.connection !== 'connected'} onClick={() => void confirm()}>{running ? <LoaderCircle className="spin" /> : <Icon />}{running ? phase : description.confirm}</button></footer>
    </section>
  </div>
}
