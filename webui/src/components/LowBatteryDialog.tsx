import { BatteryWarning, ShieldAlert, X } from 'lucide-react'
import { useEffect, useState } from 'react'
import { useDeviceStore } from '../store/device'
import type { DeviceHealth } from '../types'

function volts(millivolts: number) {
  return `${(millivolts / 1000).toFixed(2)} V`
}

export function lowBatteryMessage(health: DeviceHealth) {
  if (health.powerState === 'critical') {
    return '设备已暂停当前不安全的写入操作；屏幕刷新、Flash 写入和 OTA 以各自状态为准。'
  }
  if (health.displayMinMv === 0) {
    return '设备会继续运行，屏幕刷新不受此低电提示限制；Flash 写入和 OTA 仍按各自门限判断。'
  }
  return `设备会继续运行，但低于 ${volts(health.displayMinMv)} 后不再启动屏幕刷新。`
}

export function displayRefreshStatus(health: DeviceHealth) {
  if (!health.displayAllowed) return '已暂停'
  return '可用'
}

export function LowBatteryDialog() {
  const connection = useDeviceStore((state) => state.connection)
  const health = useDeviceStore((state) => state.health)
  const [dismissed, setDismissed] = useState(false)

  useEffect(() => {
    if (connection !== 'connected') setDismissed(false)
  }, [connection])

  if (connection !== 'connected' || dismissed || !health ||
      (health.powerState !== 'warning' && health.powerState !== 'critical')) return null

  const critical = health.powerState === 'critical'
  return <div className="modal-backdrop" onMouseDown={(event) => {
    if (event.target === event.currentTarget) setDismissed(true)
  }}>
    <section className="low-battery-dialog" role="dialog" aria-modal="true" aria-labelledby="low-battery-title">
      <header><div><BatteryWarning /><span><h2 id="low-battery-title">{critical ? '设备电量过低' : '设备电量偏低'}</h2><p>当前电压 {volts(health.voltageMv)}</p></span></div><button className="icon-button" title="关闭" aria-label="关闭" onClick={() => setDismissed(true)}><X /></button></header>
      <div className="low-battery-message"><ShieldAlert /><span><strong>{critical ? '危险操作已暂停' : '请尽快更换电池'}</strong>{lowBatteryMessage(health)}</span></div>
      <dl><div><dt>屏幕刷新</dt><dd>{displayRefreshStatus(health)}</dd></div><div><dt>数据写入</dt><dd>{health.flashAllowed ? '可用' : '已暂停'}</dd></div><div><dt>固件更新</dt><dd>{health.otaAllowed ? '可用' : '已暂停'}</dd></div></dl>
      <footer><button className="button primary" onClick={() => setDismissed(true)}>知道了</button></footer>
    </section>
  </div>
}
