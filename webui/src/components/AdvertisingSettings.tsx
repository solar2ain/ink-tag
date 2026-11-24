import { useEffect, useState } from 'react'
import { Bluetooth } from 'lucide-react'
import { inktagClient } from '../protocol/client'
import type { AdvertisingConfig } from '../protocol/advertising'
import { useDeviceStore } from '../store/device'
import { useTranslation } from '../preferences'

export function AdvertisingSettings({ wakeAvailable }: { wakeAvailable: boolean }) {
  const t = useTranslation()
  const { connection, deviceId, activation, action } = useDeviceStore()
  const [config, setConfig] = useState<AdvertisingConfig | null>(null)
  const [working, setWorking] = useState(false)
  const [uncertain, setUncertain] = useState(false)
  const [message, setMessage] = useState('')
  const [failed, setFailed] = useState(false)
  useEffect(() => {
    let current = true
    setConfig(null); setMessage(''); setUncertain(false); setFailed(false)
    setWorking(connection === 'connected')
    if (connection === 'connected') {
      void inktagClient.getAdvertisingConfig().then(value => {
        if (current) setConfig(value)
      }).catch(error => {
        if (current) { setFailed(true); setMessage(String(error instanceof Error ? error.message : error)) }
      }).finally(() => { if (current) setWorking(false) })
    }
    return () => { current = false }
  }, [connection, deviceId])
  const pro = activation.phase === 'active' && activation.edition === 'advanced'
  const editable = connection === 'connected' && !!config && pro && wakeAvailable && !working
  const unavailableReason = connection !== 'connected' ? '连接设备后可读取广播设置。'
    : !config ? working ? '正在读取广播设置…' : failed ? '广播设置未读到，请重新读取。' : '当前固件不支持调整广播方式。'
    : !wakeAvailable ? '设备没有按键或 NFC，无法调整广播方式。' : !pro ? '修改广播设置需要激活 Pro。' : undefined
  const valid = config && Number.isInteger(config.timeoutSeconds) && config.timeoutSeconds >= 1 && config.timeoutSeconds <= 65535
    && (config.mode !== 'delayed' || config.timeoutSeconds >= 30)
  const save = async () => {
    if (!config || !valid) return
    setWorking(true); setMessage('')
    try {
      setConfig(await inktagClient.setAdvertisingConfig(config)); setFailed(false); setMessage(t('广播设置已保存。'))
    } catch (error) {
      setFailed(true); setUncertain(true)
      setMessage(`${error instanceof Error ? error.message : String(error)}。${t('请重新读取设置后再试。')}`)
    } finally { setWorking(false) }
  }
  const read = async () => {
    setWorking(true)
    try {
      setConfig(await inktagClient.getAdvertisingConfig()); setMessage(''); setUncertain(false); setFailed(false)
    } catch (error) { setFailed(true); setMessage(error instanceof Error ? error.message : String(error)) }
    finally { setWorking(false) }
  }
  return <div className="behavior-block advertising-settings">
    <div className="behavior-section-heading"><div><Bluetooth /><strong>{t('蓝牙广播方式')}</strong></div><button className="button secondary" title={unavailableReason && t(unavailableReason)} disabled={!valid || !editable || uncertain || action !== 'idle'} onClick={() => void save()}>{t(working ? '请稍候…' : '写入设备')}</button></div>
      <div className="policy-options advertising-options" role="radiogroup" aria-label={t('蓝牙广播方式')} title={unavailableReason && t(unavailableReason)}>
        {([['always', '持续广播'], ['delayed', '延时关闭']] as const).map(([mode, title]) => <label key={mode} className={config?.mode === mode ? 'active' : ''}><input type="radio" name="advertising-mode" value={mode} checked={config?.mode === mode} disabled={!editable} onChange={() => { if (config) setConfig({ ...config, mode }) }} /><span><strong>{t(title)}</strong></span></label>)}
        {config?.mode === 'delayed' && <div className="advertising-delay-field"><label htmlFor="advertising-delay-seconds">{t('延时（秒）')}</label><input id="advertising-delay-seconds" type="number" min={30} max={65535} step={1} value={config.timeoutSeconds} disabled={!editable} aria-invalid={!valid || undefined} aria-describedby={valid ? 'advertising-delay-help' : 'advertising-delay-error advertising-delay-help'} onChange={event => setConfig({ ...config, timeoutSeconds: event.target.valueAsNumber })} /></div>}
      </div>
      {config?.mode === 'delayed' && !valid && <div id="advertising-delay-error" className="notice error advertising-delay-notice" role="alert">{t('延时需为 30–65535 秒的整数，请调整后写入设备。')}</div>}
      <p id="advertising-delay-help" className="behavior-section-note">{t(config?.mode === 'delayed' ? '延时至少 30 秒。启动或断开连接后开始计时；按键或 NFC 触碰可重新开放连接。日历、时钟、轮播和 Find My 继续运行。' : '未连接时每秒广播一次。Find My 不受此设置影响。')}</p>
      {unavailableReason && <p className="behavior-unavailable" role="note">{t(unavailableReason)}</p>}
    {message && <div className={`notice ${failed ? 'error' : ''}`} role={failed ? 'alert' : 'status'}>{message}</div>}
    {(uncertain || failed) && <button className="button secondary" disabled={connection !== 'connected' || working || action !== 'idle'} onClick={() => void read()}>{t('重新读取')}</button>}
  </div>
}
