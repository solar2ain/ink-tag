import { ShieldAlert, X } from 'lucide-react'
import { updateDevicePreference } from '../data/devicePreferences'
import { useDeviceStore } from '../store/device'

function formatUptime(milliseconds: number | null) {
  if (milliseconds === null) return '未提供'
  return `${(milliseconds / 1000).toFixed(1)} 秒`
}

export function CrashAlertDialog() {
  const deviceId = useDeviceStore((state) => state.deviceId)
  const alert = useDeviceStore((state) => state.crashAlert)
  if (!alert || alert.newCrashes === null) return null

  const acknowledge = async () => {
    if (!deviceId) return
    try {
      await updateDevicePreference(deviceId, { lastAcknowledgedTotalCrashes: alert.totalCrashes })
      useDeviceStore.getState().setCrashAlert(null)
    } catch (error) {
      useDeviceStore.getState().addLog(`异常复位提醒保存失败：${error instanceof Error ? error.message : String(error)}`, 'error')
    }
  }
  const registers = Object.entries(alert.registers)
  return <div className="modal-backdrop crash-alert-backdrop">
    <section className="crash-alert-dialog" role="alertdialog" aria-modal="true" aria-labelledby="crash-alert-title">
      <header><div><ShieldAlert /><span><h2 id="crash-alert-title">设备发生过新的异常复位</h2><p>{`自本浏览器上次记录后新增 ${alert.newCrashes} 次，累计 ${alert.totalCrashes} 次`}</p></span></div><button className="icon-button" title="确认并关闭" aria-label="确认并关闭" onClick={() => void acknowledge()}><X /></button></header>
      <dl>
        <div><dt>最近原因</dt><dd>{alert.reason}</dd></div>
        <div><dt>操作 / 阶段</dt><dd>{alert.operation} / {alert.stage}</dd></div>
        <div><dt>复位原因码</dt><dd>{alert.resetReason}</dd></div>
        <div><dt>当时电压</dt><dd>{alert.voltageMv ? `${alert.voltageMv} mV` : '未提供'}</dd></div>
        <div><dt>当时运行时间</dt><dd>{formatUptime(alert.uptimeMs)}</dd></div>
      </dl>
      {registers.length > 0 && <div className="crash-registers">{registers.map(([name, value]) => <span key={name}>{name.toUpperCase()} 0x{value.toString(16).padStart(8, '0')}</span>)}</div>}
      <footer><button className="button primary" onClick={() => void acknowledge()}>知道了</button></footer>
    </section>
  </div>
}
