import { Bluetooth, ChevronDown, CircleStop, KeyRound, LoaderCircle, Logs, Plus, Radio, RefreshCw, Search, ShieldCheck, Trash2, Unplug } from 'lucide-react'
import { useEffect, useRef, useState } from 'react'
import { Link } from 'react-router-dom'
import { db } from '../data/db'
import { updateDevicePreference } from '../data/devicePreferences'
import { inktagClient } from '../protocol/client'
import { compareInkTagDeviceNames, isKnownInkTagName } from '../protocol/bluetoothDiscovery'
import { useDeviceStore } from '../store/device'
import { displayDeviceName } from '../device/displayName'
import { ThemeSwitcher } from './ThemeSwitcher'
import { VoltageBatteryIcon } from './VoltageBatteryIcon'
import { bluetoothNameFromDeviceUrl } from '../qr/qr'

const stateLabels = {
  unsupported: '浏览器不支持蓝牙', idle: '未连接', selecting: '选择设备',
  connecting: '连接中', connected: '已连接', error: '连接失败',
}

const actionLabels = {
  idle: '空闲', 'device-info': '正在绘制设备信息页', clear: '正在清屏', calendar: '正在绘制日历', clock: '正在绘制时钟',
  picture: '正在传图', 'font-update': '正在更新字库', 'slideshow-starting': '启动轮播', slideshow: '轮播中', ota: '正在升级',
}

const otaPhaseLabels = {
  'connecting-recovery': '正在连接升级模式',
  'recovery-transfer': 'Recovery 升级中',
  'reconnecting-app': '正在等待应用返回',
  'attesting-app': '正在核对应用',
}

export function DeviceBar() {
  const state = useDeviceStore()
  const actionLabel = state.actionDetail.includes('正在刷新墨水屏') ||
    state.actionDetail.includes('清空并刷新墨水屏')
    ? '正在刷新屏幕'
    : actionLabels[state.action]
  const menuRef = useRef<HTMLDivElement>(null)
  const logRef = useRef<HTMLDivElement>(null)
  const deviceLinkName = bluetoothNameFromDeviceUrl(window.location.search)
  const [menuOpen, setMenuOpen] = useState(false)
  const [logOpen, setLogOpen] = useState(false)
  const [knownDevices, setKnownDevices] = useState<Array<{
    id: string
    device?: BluetoothDevice
    alias: string
    name: string
    boardId: string
    lastConnectedAt: number
  }>>([])
  const [deviceSearch, setDeviceSearch] = useState(deviceLinkName)
  const [onlyInkTag, setOnlyInkTag] = useState(true)
  const otaInProgress = state.action === 'ota'
  const otaLabel = otaInProgress ? state.otaPhase ? otaPhaseLabels[state.otaPhase] : '正在升级' : null
  const otaMetric = state.otaPhase === 'recovery-transfer' ? 'Recovery'
    : state.otaPhase === 'reconnecting-app' ? '等待应用'
      : state.otaPhase === 'attesting-app' ? '核验应用' : '升级连接'
  const busy = otaInProgress || state.connection === 'selecting' || state.connection === 'connecting'
  const connected = state.connection === 'connected'
  const recovery = connected && state.firmwareVersion === 'Recovery'
  const errorLogCount = state.logs.reduce((count, entry) => count + (entry.level === 'error' ? 1 : 0), 0)
  const canRestoreKnownDevices = inktagClient.canRestoreKnownDevices()
  const currentOrigin = window.location.origin

  const refreshKnownDevices = async () => {
    try {
      const [devices, preferences] = await Promise.all([
        inktagClient.getKnownDevices(),
        db.devicePreferences.toArray(),
      ])
      const inMemoryDevice = inktagClient.getLastConnectedDevice()
      if (inMemoryDevice && !devices.some((device) => device.id === inMemoryDevice.id)) devices.push(inMemoryDevice)
      const preferencesById = new Map(preferences.map((preference) => [preference.id, preference]))
      const remembered = new Map<string, {
        id: string; device?: BluetoothDevice; alias: string; name: string; boardId: string; lastConnectedAt: number
      }>()
      for (const preference of preferences) {
        if (!preference.lastConnectedName || preference.reconnectHidden) continue
        remembered.set(preference.id, {
          id: preference.id,
          alias: preference.localAlias || '',
          name: preference.lastConnectedName,
          boardId: preference.lastConnectedBoardId || '',
          lastConnectedAt: preference.lastConnectedAt || 0,
        })
      }
      if (state.lastDeviceId && !preferencesById.get(state.lastDeviceId)?.reconnectHidden && !remembered.has(state.lastDeviceId)) {
        remembered.set(state.lastDeviceId, {
          id: state.lastDeviceId,
          alias: state.lastDeviceAlias,
          name: state.lastDeviceName || state.lastDeviceId,
          boardId: '',
          lastConnectedAt: Number.MAX_SAFE_INTEGER,
        })
      }
      for (const device of devices) {
        const preference = preferencesById.get(device.id)
        if (preference?.reconnectHidden) continue
        remembered.set(device.id, {
          id: device.id,
          device,
          alias: preference?.localAlias || remembered.get(device.id)?.alias || '',
          name: device.name || preference?.lastConnectedName || remembered.get(device.id)?.name || device.id,
          boardId: preference?.lastConnectedBoardId || remembered.get(device.id)?.boardId || '',
          lastConnectedAt: preference?.lastConnectedAt || remembered.get(device.id)?.lastConnectedAt || 0,
        })
      }
      setKnownDevices([...remembered.values()])
    } catch {
      setKnownDevices(state.lastDeviceId ? [{
        id: state.lastDeviceId,
        alias: state.lastDeviceAlias,
        name: state.lastDeviceName || state.lastDeviceId,
        boardId: '',
        lastConnectedAt: Number.MAX_SAFE_INTEGER,
      }] : [])
    }
  }

  useEffect(() => { void refreshKnownDevices() }, [connected])
  useEffect(() => {
    if (!connected || !state.deviceId) return
    void updateDevicePreference(state.deviceId, {
      localAlias: state.localAlias,
      lastConnectedName: state.deviceName || state.deviceId,
      lastConnectedAt: Date.now(),
      // Recovery has no App board identity. Keep the previously read board
      // bound to this grant so a cached App name can be reverified after reload.
      ...(state.boardId ? { lastConnectedBoardId: state.boardId } : {}),
      reconnectHidden: false,
    }).then(refreshKnownDevices).catch(() => undefined)
  }, [connected, state.deviceId, state.deviceName, state.localAlias, state.boardId])
  useEffect(() => {
    const close = (event: PointerEvent) => {
      if (!menuRef.current?.contains(event.target as Node)) setMenuOpen(false)
      if (!logRef.current?.contains(event.target as Node)) setLogOpen(false)
    }
    window.addEventListener('pointerdown', close)
    return () => window.removeEventListener('pointerdown', close)
  }, [])

  const filteredKnownDevices = knownDevices.filter(({ id, name, alias }) => {
    if (onlyInkTag && !isKnownInkTagName(name)) return false
    const query = deviceSearch.trim().toLocaleLowerCase()
    return !query || `${alias} ${name} ${id}`.toLocaleLowerCase().includes(query)
  }).sort((a, b) => {
    if (a.id === state.lastDeviceId) return -1
    if (b.id === state.lastDeviceId) return 1
    return compareInkTagDeviceNames(a.name, b.name) || b.lastConnectedAt - a.lastConnectedAt
  })
  const scan = async () => {
    if (useDeviceStore.getState().action === 'ota') return
    setMenuOpen(false)
    setLogOpen(false)
    if (connected) await inktagClient.disconnectAndWait()
    await inktagClient.connect(deviceLinkName && deviceSearch.trim() === deviceLinkName
      ? { onlyInkTag, preferredName: deviceLinkName }
      : { onlyInkTag, namePrefix: deviceSearch })
    await refreshKnownDevices()
  }

  const scanRemembered = async (name: string) => {
    if (useDeviceStore.getState().action === 'ota') return
    setMenuOpen(false)
    setLogOpen(false)
    if (connected) await inktagClient.disconnectAndWait()
    await inktagClient.connect({ onlyInkTag: true, preferredName: name })
    await refreshKnownDevices()
  }

  const connectKnown = async (entry: (typeof knownDevices)[number]) => {
    if (useDeviceStore.getState().action === 'ota') return
    setMenuOpen(false)
    setLogOpen(false)
    if (!entry.device) return
    if (connected) await inktagClient.disconnectAndWait()
    await inktagClient.connectKnownDevice(entry.device)
    await refreshKnownDevices()
  }

  const connectRemembered = (entry: (typeof knownDevices)[number]) => entry.device
    ? connectKnown(entry)
    : scanRemembered(entry.name)

  const lastKnownDevice = knownDevices.find(({ id }) => id === state.lastDeviceId)
  const lastSessionDevice = inktagClient.getLastConnectedDevice()
  const canDirectReconnect = Boolean(lastSessionDevice && lastSessionDevice.id === state.lastDeviceId)
  const lastDeviceLabel = state.lastDeviceId
    ? displayDeviceName(lastKnownDevice?.alias || state.lastDeviceAlias, lastKnownDevice?.name || state.lastDeviceName)
    : ''

  const clearRemembered = async (id: string) => {
    await updateDevicePreference(id, { reconnectHidden: true })
    setKnownDevices((devices) => devices.filter((device) => device.id !== id))
  }

  return (
    <header className="device-bar">
      <Link className="mobile-brand" to="/" aria-label="InkTag 总览">
        <span className="brand-mark" aria-hidden="true"><i /><i /><i /></span>
      </Link>
      <div className="device-summary">
        <span className={`connection-dot ${otaInProgress ? 'connecting' : state.connection}`} />
        <div aria-live="polite">
          <strong>{otaLabel || (connected ? displayDeviceName(state.localAlias, state.deviceName) : stateLabels[state.connection])}</strong>
          <span>{otaInProgress ? state.deviceName || state.lastDeviceName || '固件升级' : recovery ? 'Recovery 模式' : state.deviceName || '选择附近的 InkTag 设备'}</span>
        </div>
      </div>
      <div className="device-metrics" aria-label="设备状态">
        <span className={recovery ? 'recovery-mode' : ''} title={otaLabel || (recovery ? '蓝牙已连接 · Recovery 模式' : '连接状态')}>{otaInProgress ? <LoaderCircle className="spin" /> : recovery ? <ShieldCheck /> : <Radio />}{otaInProgress ? otaMetric : recovery ? 'Recovery' : connected ? 'BLE' : '离线'}</span>
        <span title="电池电压"><VoltageBatteryIcon voltage={state.voltage} />{state.voltage == null ? '--' : `${state.voltage.toFixed(2)} V`}</span>
        <span title="固件授权"><KeyRound />{connected && !recovery ? state.activation.edition === 'advanced' && state.activation.phase === 'active' ? '高级版' : '免费版' : '--'}</span>
        <span className={state.action === 'idle' ? '' : 'active-action'}>
          {state.action === 'idle' ? <Bluetooth /> : <LoaderCircle className="spin" />}
          {actionLabel}
        </span>
      </div>
      <div className="device-bar-actions">
        {state.connection === 'connecting' && !otaInProgress && (
          <button className="icon-button danger" title="取消连接" aria-label="取消连接" onClick={() => inktagClient.disconnect()}><CircleStop /></button>
        )}
        {state.action !== 'idle' && state.action !== 'slideshow' && !otaInProgress && (
          <button className="icon-button danger" title="停止当前操作" onClick={() => void inktagClient.stop()}>
            <CircleStop />
          </button>
        )}
        <div className="connection-menu" ref={menuRef}>
          <div className="connection-split-button">
            {connected ? <button className="button secondary connection-direct" disabled={otaInProgress} aria-label="断开设备" title="断开设备" onClick={() => { if (useDeviceStore.getState().action !== 'ota') inktagClient.disconnect() }}><Unplug /><span>断开</span></button> : (
              <button
                className="button primary connection-direct"
                disabled={busy || state.connection === 'unsupported'}
                aria-label={busy ? '请稍候' : canDirectReconnect ? `重连 ${lastDeviceLabel}` : '选择设备'}
                title={canDirectReconnect ? `重连 ${lastDeviceLabel}` : '选择附近设备'}
                onClick={() => canDirectReconnect && lastSessionDevice ? void inktagClient.connectKnownDevice(lastSessionDevice) : void scan()}
              >
                {busy ? <LoaderCircle className="spin" /> : canDirectReconnect ? <RefreshCw /> : <Bluetooth />}
                <span>{busy ? '请稍候' : canDirectReconnect ? '重连' : '连接'}</span>
              </button>
            )}
            <button
              className={`button ${connected ? 'secondary' : 'primary'} connection-menu-toggle`}
              disabled={busy || state.connection === 'unsupported'}
              aria-label={connected ? '连接其他设备' : '打开浏览器记住的设备'}
              aria-expanded={menuOpen}
              onClick={() => { setMenuOpen(!menuOpen); setLogOpen(false); if (!menuOpen) void refreshKnownDevices() }}
            ><ChevronDown /></button>
          </div>
          {menuOpen && <div className="connection-popover">
              <header><strong>浏览器记住的设备</strong><span>{filteredKnownDevices.length}/{knownDevices.length}</span></header>
              {filteredKnownDevices.length > 0 ? <div className="known-device-list">{filteredKnownDevices.map((entry) => (
                <div key={entry.id} className="known-device-row">
                  <button disabled={busy || connected && entry.id === state.deviceId} onClick={() => void connectRemembered(entry)} title={connected && entry.id === state.deviceId ? '当前已连接' : entry.device ? '直接连接' : '按名称选择附近设备'}>
                    <Bluetooth /><span><strong>{entry.name}</strong><small className="known-device-name">{displayDeviceName(entry.alias, entry.name, '--')}</small>{(connected && entry.id === state.deviceId || entry.lastConnectedAt > 0 && entry.lastConnectedAt !== Number.MAX_SAFE_INTEGER) && <small>{[
                      connected && entry.id === state.deviceId && '当前已连接',
                      entry.lastConnectedAt > 0 && entry.lastConnectedAt !== Number.MAX_SAFE_INTEGER && `上次 ${new Date(entry.lastConnectedAt).toLocaleString()}`,
                    ].filter(Boolean).join(' · ')}</small>}</span>
                  </button>
                  <button className="known-device-clear" title="清除此设备的本地连接记录" aria-label={`清除 ${displayDeviceName(entry.alias, entry.name)} 的本地连接记录`} onClick={() => void clearRemembered(entry.id)}><Trash2 /></button>
                </div>
              ))}</div> : <p>{knownDevices.length ? '没有符合筛选条件的设备' : canRestoreKnownDevices ? '当前网址暂无浏览器授权记录' : '当前浏览器不支持恢复历史蓝牙授权'}</p>}
              <button className="scan-device" disabled={busy} onClick={() => void scan()}><Plus />{connected ? '连接其他设备' : '选择附近设备'}</button>
              <div className="device-discovery-controls">
                <label className="device-filter"><input type="checkbox" checked={onlyInkTag} onChange={(event) => setOnlyInkTag(event.target.checked)} /><span>仅显示 InkTag</span></label>
                <span className="device-filter-or">或</span>
                <label className="device-search"><Search /><input value={deviceSearch} onChange={(event) => setDeviceSearch(event.target.value)} placeholder="输入筛选的蓝牙名称" /></label>
                <small>扫描时将优先匹配此名称</small>
              </div>
              <footer><ShieldCheck /><span>蓝牙授权按完整网址隔离；当前为 {currentOrigin}。切换主机名或端口后需重新选择一次。</span></footer>
          </div>}
        </div>
        <ThemeSwitcher className="top-theme" />
        <div className="log-menu" ref={logRef}>
          <button className="icon-button log-shortcut" title={`设备连接日志（${state.logs.length} 条）`} aria-label={`打开设备连接日志，当前 ${state.logs.length} 条，其中错误 ${errorLogCount} 条`} aria-expanded={logOpen} onClick={() => { setLogOpen(!logOpen); setMenuOpen(false) }}><Logs />{errorLogCount > 0 && <i>{Math.min(99, errorLogCount)}</i>}</button>
          {logOpen && <div className="log-popover">
            <header><strong>设备连接日志</strong><span>{state.logs.length} 条</span></header>
            <div className="log-popover-list">{state.logs.length ? [...state.logs].reverse().slice(0, 80).map((entry) => <div key={entry.id} className={entry.level}><time>{new Date(entry.at).toLocaleTimeString()}</time><span>{entry.level === 'rx' ? '↓' : entry.level === 'tx' ? '↑' : '•'}</span><code>{entry.message}</code></div>) : <p>暂无日志</p>}</div>
            <footer><button type="button" disabled={!state.logs.length} onClick={state.clearLogs}>清空</button><Link to="/device?tab=logs" onClick={() => setLogOpen(false)}>打开完整日志</Link></footer>
          </div>}
        </div>
      </div>
    </header>
  )
}
