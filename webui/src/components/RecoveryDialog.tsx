import { FileUp, ShieldCheck } from 'lucide-react'
import { useEffect, useState } from 'react'
import { useNavigate } from 'react-router-dom'
import { inktagClient } from '../protocol/client'
import { useDeviceStore } from '../store/device'
import { RecoveryExitButton } from './RecoveryExitButton'

export function RecoveryDialog() {
  const { connection, deviceId, deviceName, firmwareVersion, action } = useDeviceStore()
  const navigate = useNavigate()
  const [dismissed, setDismissed] = useState(false)
  const [running, setRunning] = useState(false)
  const [error, setError] = useState('')
  useEffect(() => {
    setDismissed(false)
    setError('')
  }, [connection, deviceId])

  if (connection !== 'connected' || firmwareVersion !== 'Recovery' ||
      !['goodix', 'telink', 'onmicro', 'dialog', 'nordic'].includes(inktagClient.getOtaRecoveryType() || '') || dismissed || action !== 'idle') return null
  const canExit = inktagClient.getRecoverySafeResumeSupported()

  return <div className="modal-backdrop">
    <section className="lifecycle-dialog recovery-dialog" role="dialog" aria-modal="true" aria-labelledby="recovery-dialog-title">
      <header><div><ShieldCheck /><span><h2 id="recovery-dialog-title">设备处于 Recovery</h2><p>{deviceName}</p></span></div></header>
      <p>{canExit ? '可以更新固件，或请求安全返回原应用。退出不会升级固件或清除用户数据。原应用无效或更新未完成时，请重新选择与设备匹配的 OTA 应用文件，并完成 OTA 升级；供电不足时请先检查电源。' : '当前 Recovery 未提供安全退出协议，请选择与设备匹配的 OTA 应用文件，并完成 OTA 升级后返回应用。'}</p>
      {error && <div className="identity-dialog-message error" role="alert">{error}</div>}
      <footer>
        <RecoveryExitButton onError={setError} onRunningChange={setRunning} />
        <button className="button primary" disabled={running} onClick={() => { setDismissed(true); navigate('/device?tab=ota') }}><FileUp />进入 OTA 页面</button>
      </footer>
    </section>
  </div>
}
