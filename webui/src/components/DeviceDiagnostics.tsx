import { FeedbackNotice, useFeedbackMessage } from './FeedbackNotice'
import { Clock3, RefreshCw } from 'lucide-react'
import { useEffect, useState } from 'react'
import { inktagClient } from '../protocol/client'
import type { ClockDiagnostic } from '../protocol/clockDiagnostic'
import { mcuDisplayName } from '../protocol/boards'
import { displayDeviceName } from '../device/displayName'
import { PROTOCOL_V2_VERSION } from '../protocol/generatedCommandsV2'
import { useDeviceStore } from '../store/device'

const validNumber = (value: number | null | undefined): value is number => value != null && Number.isFinite(value)
const temperature = (value: number | null) => validNumber(value) ? `${value} °C` : '--'
const duration = (value: number | null | undefined) => !validNumber(value) ? '--' : value >= 3600000 ? `${(value / 3600000).toFixed(1)} 小时` : `${(value / 1000).toFixed(1)} 秒`
const count = (value: number | null | undefined) => validNumber(value) ? value.toLocaleString() : '--'
const allowed = (value: boolean | undefined) => value == null ? '--' : value ? '允许' : '拒绝'

export function DeviceDiagnostics() {
  const state = useDeviceStore()
  const { deviceId, connection, action, protocolVersion, boardId, firmwareVersion, voltage, temperature: effective, mcuTemperature, panelTemperature, health } = state
  const [reading, setReading] = useState(false)
  const [clock, setClock] = useState<ClockDiagnostic | null>(null)
  const [status, setStatus] = useState<Awaited<ReturnType<typeof inktagClient.readDiagnostics>> | null>(null)
  const [message, setMessage, messageRevision] = useFeedbackMessage('')
  useEffect(() => { setClock(null); setStatus(null); setMessage('') }, [deviceId, connection])
  const ready = connection === 'connected' && protocolVersion === PROTOCOL_V2_VERSION && action === 'idle' && !reading
  const read = async (kind: 'runtime' | 'clock') => {
    setReading(true)
    setMessage('')
    if (kind === 'clock') setClock(null)
    try {
      if (kind === 'clock') {
        const sample = await inktagClient.readClockDiagnostic()
        if (useDeviceStore.getState().deviceId !== deviceId || useDeviceStore.getState().connection !== 'connected') return
        setClock(sample)
      } else {
        setStatus(null)
        if (state.capabilities.health) useDeviceStore.getState().setHealth(null)
        const report = await inktagClient.readDiagnostics()
        if (useDeviceStore.getState().deviceId !== deviceId || useDeviceStore.getState().connection !== 'connected') return
        setStatus(report)
        if (state.capabilities.health) await inktagClient.readHealth()
      }
    } catch (error) {
      const text = error instanceof Error ? error.message : String(error)
      setMessage(text); useDeviceStore.getState().addLog(`诊断读取失败：${text}`, 'error')
    } finally { setReading(false) }
  }
  const soak = health?.soakTelemetry
  const appConnected = connection === 'connected' && protocolVersion === PROTOCOL_V2_VERSION
  const healthUnavailable = !appConnected ? '连接 App 后读取' : state.capabilities.health ? '未读取' : '不支持健康诊断'
  return <section className="device-diagnostics">
    <div className="panel-title"><div><h2>设备诊断</h2><p>与 CLI diagnostic 对齐；读取不校时、不刷屏、不改设置。</p></div><div className="diagnostic-actions"><button className="button secondary" disabled={!ready} onClick={() => void read('runtime')}><RefreshCw />运行诊断</button><button className="button secondary" disabled={!ready} onClick={() => void read('clock')}><Clock3 />走时诊断</button></div></div>
    {reading && <p role="status">正在读取…</p>}{message && <FeedbackNotice className="library-message" role="alert" noticeKey={messageRevision}>{message}</FeedbackNotice>}
    <dl className="diagnostic-grid">
      <div><dt>主控</dt><dd>{mcuDisplayName(boardId)}</dd></div>
      <div><dt>固件</dt><dd>{firmwareVersion || '--'}</dd></div>
      <div><dt>协议</dt><dd>{protocolVersion == null ? '--' : `0x${protocolVersion.toString(16).padStart(2, '0').toUpperCase()}`}</dd></div>
      <div><dt>设备名</dt><dd>{displayDeviceName(state.localAlias, state.deviceName, '--')}</dd></div>
      <div><dt>蓝牙名</dt><dd>{state.deviceName || '--'}</dd></div>
      <div><dt>地址</dt><dd title={!state.bleAddress ? '浏览器未提供' : undefined}>{state.bleAddress || '--'}</dd></div>
      {state.screen && <><div><dt>屏幕 / 可见区域</dt><dd>{state.screen.width}×{state.screen.height} / {state.screen.visibleWidth}×{state.screen.visibleHeight}</dd></div><div><dt>驱动 / 配方</dt><dd>{state.screen.driver} / {state.screen.recipe}</dd></div></>}
      <div><dt>电压</dt><dd>{validNumber(voltage) ? `${voltage.toFixed(3)} V` : '--'}</dd></div>
      <div><dt>有效温度</dt><dd>{temperature(effective)}</dd></div><div><dt>MCU 温度</dt><dd>{temperature(mcuTemperature)}</dd></div>
      <div><dt>屏幕温度</dt><dd>{temperature(panelTemperature)}</dd></div><div><dt>运行模式 / 标志 / 进度</dt><dd>{status ? `${status.mode} / 0x${status.flags.toString(16).padStart(2, '0')} / ${status.progress}%` : '--'}</dd></div>
      <div><dt>电源状态 / 电压有效</dt><dd title={!health ? healthUnavailable : undefined}>{health ? `${health.powerState} / ${health.voltageValid ? '是' : '否'}` : '--'}</dd></div>
      <div><dt>显示 / Flash / OTA 准入</dt><dd title={!health ? healthUnavailable : undefined}>{health ? `${allowed(health.displayAllowed)} / ${allowed(health.flashAllowed)} / ${allowed(health.otaAllowed)}` : '--'}</dd></div>
      <div><dt>看门狗 / 电压阈值已验证</dt><dd title={!health ? healthUnavailable : undefined}>{health ? `${health.watchdogActive ? '启用' : '未启用'} / ${health.thresholdsQualified ? '是' : '否'}` : '--'}</dd></div>
      <div><dt>复位原因 / 累计异常 / 连续异常</dt><dd title={!health ? healthUnavailable : undefined}>{health ? `${health.resetReason} / ${health.totalCrashes} / ${health.consecutiveCrashes}` : '--'}</dd></div>
      {health && <div><dt>警告 / 显示 / Flash / OTA 阈值</dt><dd>{health.warningMv} / {health.displayMinMv} / {health.flashMinMv} / {health.otaMinMv} mV</dd></div>}
      {health?.latestCrash && <div><dt>最近异常：原因 / 操作 / 阶段</dt><dd>{health.latestCrash.reason} / {health.latestCrash.operation} / {health.latestCrash.stage || '--'} · {health.latestCrash.voltageMv} mV · PC 0x{health.latestCrash.pc.toString(16)}</dd></div>}
      {!soak && <div><dt>上电遥测</dt><dd title={!appConnected ? '连接 App 后读取' : state.capabilities.health ? '未读取或固件未提供扩展' : '不支持健康诊断'}>--</dd></div>}
    </dl>
    <p className="diagnostic-note">-- 表示无有效值；屏温仅使用已有采样，复位原因保留原始码。走时诊断不校时，普通连接可能已校时；长期测量用 CLI --no-time-sync time accuracy。</p>
    {(status || health || clock) && <details className="diagnostic-raw"><summary>原始诊断字段（与 CLI JSON 对照）</summary><pre>{JSON.stringify({status, healthSupported: appConnected ? Boolean(state.capabilities.health) : null, health: health && { ...health, latestCrash: health.latestCrash && { ...health.latestCrash, ...Object.fromEntries((['pc', 'lr', 'cfsr', 'hfsr'] as const).map((key) => [key, `0x${(health.latestCrash![key] >>> 0).toString(16).padStart(8, '0').toUpperCase()}`])) } }, clock}, null, 2)}</pre></details>}
    {clock && <div className="soak-telemetry clock-diagnostic"><header><strong>走时偏差（设备 − 本地）</strong><span title={clock.offsetFromHostSeconds == null ? '采样无效' : undefined}>{clock.offsetFromHostSeconds == null ? '--' : `${clock.offsetFromHostSeconds > 0 ? '+' : ''}${clock.offsetFromHostSeconds.toFixed(3)} 秒`}</span></header><dl>
      <div><dt>设备时间（设备时区）</dt><dd>{clock.deviceTimeValid ? clock.deviceLocalTime : '--'}</dd></div><div><dt>本地时间（采样中点）</dt><dd>{clock.hostLocalTime}</dd></div>
      <div><dt>UTC 对照 / 设备 UTC 秒</dt><dd>{clock.hostUtcMidpoint} / {clock.deviceTimeValid ? clock.deviceUtcTimestamp : '--'}</dd></div><div><dt>往返耗时 / 估计误差范围</dt><dd>{clock.roundTripMilliseconds} ms / ±{clock.uncertaintySeconds} 秒</dd></div>
    </dl><p>{!clock.hostClockStable ? '采样期间电脑时钟发生跳变，请重新读取。' : !clock.deviceTimeValid ? '设备时间未初始化或超出有效范围。' : '正值表示设备快，负值表示设备慢。协议分辨率为 1 秒；单次偏差不能证明长期精度或 ppm。'}</p></div>}
    {soak ? <div className="soak-telemetry"><header><strong>本次上电遥测</strong><span>{soak.findActive ? 'Find 窗口正在运行' : '只读 RAM 计数'}{soak.saturated ? ' · 计数已饱和' : ''}</span></header><dl>
      <div><dt>运行时间 / 连接会话</dt><dd>{duration(soak.uptimeMs)} / {count(soak.connectionSessions)}</dd></div>
      <div><dt>Find 请求 / 启动 / 完成</dt><dd>{count(soak.findRequests)} / {count(soak.findStarts)} / {count(soak.findCompletions)}</dd></div>
      <div><dt>Find 启停失败 / 连接暂停</dt><dd>{count(soak.findStartFailures)} / {count(soak.findStopFailures)} / {count(soak.findConnectionPauses)}</dd></div>
      <div><dt>Find 累计活跃</dt><dd>{duration(soak.findActiveMs)}</dd></div><div><dt>无线维护 / 休眠进入</dt><dd>{count(soak.radioMaintenanceTurns)} / {count(soak.sleepEntries)}</dd></div>
    </dl><p>记录调度窗口，不代表实际空口包数或 Apple 网络上报数。</p></div> : null}
  </section>
}
