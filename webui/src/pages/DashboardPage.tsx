import { Bluetooth, CalendarDays, CirclePause, Clock3, Contact, Eraser, FileText, KeyRound, ListChecks, Nfc, PanelsTopLeft, Pencil, Play, RefreshCw, Settings2, Table2, Wifi } from 'lucide-react'
import { useEffect, useMemo, useRef, useState } from 'react'
import { Link, useNavigate } from 'react-router-dom'
import { CalendarSettingsDialog } from '../components/CalendarSettingsDialog'
import { useConfirmationDialog } from '../components/ConfirmationDialog'
import { DeviceIdentityDialog, type DeviceIdentityField } from '../components/DeviceIdentityDialog'
import { NfcCommandControls } from '../components/NfcCommandControls'
import { PageHeader } from '../components/PageHeader'
import { SlideshowDialog } from '../components/SlideshowDialog'
import { TemplatePreview } from '../components/TemplatePreview'
import { TemperatureFact } from '../components/TemperatureFact'
import { calendarCountdownSupported, canStartSlideshow, clockPartialRefreshAvailable, dashboardStoredPages, dashboardTemplates, slideshowImageCount } from '../dashboard/quickControls'
import { pageTemplateTarget } from '../data/templates'
import { displayDeviceName } from '../device/displayName'
import { mcuDisplayName } from '../protocol/boards'
import { usePhoneNdefReader } from '../nfc/browserNdef'
import { webSerial } from '../nfc/pn532'
import { useTranslation } from '../preferences'
import { inktagClient } from '../protocol/client'
import { canvasSizeForScreen, colorCode } from '../protocol/screen'
import { useDeviceStore } from '../store/device'
import { DEFAULT_SLIDESHOW_MINUTES, slideshowIntervalSeconds } from '../slideshow/interval'
import { paintDevicePixelFrame } from '../device-gui/devicePixelGfx'
import { renderFirmwareNativePage } from '../device-gui/nativePageRenderer'

const pageIcons = { schedule: Table2, todo: ListChecks, wifi: Wifi, business: Contact, page: FileText } as const

export function DashboardPage() {
  const t = useTranslation()
  const { connection, action, capabilities, settingsCapabilities, screen, firmwareVersion, protocolVersion, boardId, deviceId, bleAddress, deviceName, localAlias, voltage, temperature, mcuTemperature, panelTemperature, activation, imageStoreInfo, imageStoreItems } = useDeviceStore()
  const [refreshing, setRefreshing] = useState(false)
  const [quickMode, setQuickMode] = useState<'ble' | 'nfc'>('ble')
  const PhoneReader = usePhoneNdefReader()
  const phoneNfc = typeof PhoneReader?.prototype.write === 'function'
  const canUseNfc = phoneNfc || Boolean(webSerial())
  const nfcSelected = canUseNfc && quickMode === 'nfc'
  const [refreshMessage, setRefreshMessage] = useState('')
  const refreshPending = useRef(false)
  useEffect(() => setRefreshMessage(''), [deviceId])
  useEffect(() => {
    if (refreshMessage !== '设备配置已更新。') return
    const timer = window.setTimeout(() => setRefreshMessage(''), 5000)
    return () => window.clearTimeout(timer)
  }, [refreshMessage])
  const connected = connection === 'connected'
  useEffect(() => {
    if (connected) setQuickMode('ble')
  }, [connected])
  const quickNote = nfcSelected
    ? (phoneNfc ? '选择命令并将手机NFC贴近设备' : '连接PN532并放置好设备后使用')
    : (!connected ? '连接设备蓝牙后使用' : '')
  const switching = refreshing || (action !== 'idle' && action !== 'slideshow')
  const slideshowCount = slideshowImageCount(imageStoreItems)
  const slideshowReady = connected && slideshowCount > 0 && canStartSlideshow(capabilities.imageStore, imageStoreInfo)
  const storedPages = connected && capabilities.imageStore && imageStoreInfo ? dashboardStoredPages(imageStoreItems) : []
  const advanced = activation.phase === 'active' && activation.edition === 'advanced'
  const [identityField, setIdentityField] = useState<DeviceIdentityField | null>(null)
  const [deviceImagesDialogOpen, setDeviceImagesDialogOpen] = useState(false)
  const [calendarSettingsOpen, setCalendarSettingsOpen] = useState(false)
  const [stopping, setStopping] = useState(false)
  const stopPending = useRef(false)
  const navigate = useNavigate()
  const { confirm, dialog: confirmationDialog } = useConfirmationDialog()
  const countdownSupported = calendarCountdownSupported(screen)
  const commonTemplates = useMemo(() => dashboardTemplates(connected ? screen : null), [connected, screen])
  const showDeviceInfo = async () => {
    if (useDeviceStore.getState().settingsCapabilities.nativeDeviceInfo) {
      await inktagClient.showNativeDeviceInfo()
      return
    }
    if (!screen) return
    const canvas = document.createElement('canvas')
    const size = canvasSizeForScreen(screen)
    canvas.width = size.width; canvas.height = size.height
    paintDevicePixelFrame(canvas.getContext('2d')!, renderFirmwareNativePage({
      ...size, page: 'device-info', layoutRevision: 'v1', colorCount: screen.color === 1 ? 1 : 3,
      bluetoothName: deviceName,
      activated: activation.phase === 'active',
      edition: activation.phase === 'active' ? 'pro' : 'base',
      deviceName: displayDeviceName(localAlias, deviceName, 'InkTag'),
      mcuName: mcuDisplayName(boardId),
      firmwareVersion,
      screenInfo: `${screen.width} × ${screen.height} / ${colorCode(screen.color)}`,
    }))
    await inktagClient.sendCanvas(canvas, 'InkTag 设备信息页')
  }

  const refreshDeviceInfo = async () => {
    if (!connected || switching || refreshPending.current) return
    const selectedDevice = deviceId
    refreshPending.current = true
    setRefreshing(true)
    setRefreshMessage('正在重新读取设备配置…')
    try {
      const complete = await inktagClient.refreshDeviceInfo()
      const current = useDeviceStore.getState()
      if (current.connection !== 'connected' || current.deviceId !== selectedDevice) return
      const message = complete ? '设备配置已更新。' : '部分配置仍未读到，可再次刷新重试；详情见连接日志。'
      setRefreshMessage(message)
      current.addLog(message, complete ? 'success' : 'warning')
    } catch (error) {
      const current = useDeviceStore.getState()
      if (current.connection !== 'connected' || current.deviceId !== selectedDevice) return
      const message = `配置读取失败，可再次刷新重试：${error instanceof Error ? error.message : String(error)}`
      setRefreshMessage(message)
      current.addLog(message, 'error')
    } finally {
      refreshPending.current = false
      setRefreshing(false)
    }
  }

  const runQuickDisplay = async (displayAction: 'calendar' | 'clock' | 'device-info' | 'clear', run: () => Promise<unknown>) => {
    try {
      await run()
    } catch (error) {
      const store = useDeviceStore.getState()
      if (!store.operationFailure) {
        const detail = error instanceof Error ? error.message : String(error)
        store.addLog(`${displayAction} 失败：${detail}`, 'error')
        store.failAction(displayAction, detail, () => runQuickDisplay(displayAction, run))
      }
    }
  }

  const showCalendar = () => inktagClient.runAction('calendar')

  const showClock = async () => {
    const device = useDeviceStore.getState().deviceId
    await inktagClient.readClockPartialRefreshCapability()
    const current = useDeviceStore.getState()
    if (current.connection !== 'connected' || current.deviceId !== device) return
    const accepted = await confirm({
      title: '进入时钟页？',
      message: clockPartialRefreshAvailable(current.capabilities, current.activation)
        ? '该设备支持局部刷新，但每天仍会全屏刷新并改变背景。时钟频繁刷新较耗电；温度或屏幕条件不满足时也可能全屏刷新。'
        : '当前设备不支持时钟局刷（需要固件支持并激活 Pro），可能每分钟全刷。频繁全刷耗电且会闪烁，不建议长时间停留。',
      confirmLabel: '仍然进入',
    })
    if (accepted && useDeviceStore.getState().connection === 'connected' && useDeviceStore.getState().deviceId === device)
      await runQuickDisplay('clock', () => inktagClient.runAction('clock'))
  }

  const showSlideshow = () => {
    if (!connected) return
    if (!slideshowReady) {
      useDeviceStore.getState().addLog('设备中没有可轮播图片，已打开图片页面。', 'warning')
      navigate('/image?notice=slideshow-empty')
      return
    }
    setDeviceImagesDialogOpen(true)
  }

  const startDefaultSlideshow = () => {
    if (!connected) return
    if (!slideshowReady) {
      useDeviceStore.getState().addLog('设备中没有可轮播图片，已打开图片页面。', 'warning')
      navigate('/image?notice=slideshow-empty')
      return
    }
    void startSlideshow(slideshowIntervalSeconds(DEFAULT_SLIDESHOW_MINUTES, false))
  }

  const startSlideshow = async (intervalSeconds: number) => {
    try {
      await inktagClient.startSlideshow(intervalSeconds)
      useDeviceStore.getState().addLog(`图片轮播已启动，间隔 ${intervalSeconds / 60} 分钟。`, 'success')
    } catch (error) {
      useDeviceStore.getState().addLog(error instanceof Error ? error.message : String(error), 'error')
    }
  }

  const showStoredImage = async (id: number) => {
    try {
      await inktagClient.showStoredImage(id)
      useDeviceStore.getState().addLog(`已请求设备显示图槽 ${id}。`, 'success')
    } catch (error) {
      useDeviceStore.getState().addLog(error instanceof Error ? error.message : String(error), 'error')
    }
  }

  const stopSlideshow = async () => {
    try {
      await inktagClient.stopSlideshow()
      useDeviceStore.getState().addLog('图片轮播已停止。', 'success')
    } catch (error) {
      useDeviceStore.getState().addLog(error instanceof Error ? error.message : String(error), 'error')
    }
  }

  const stopCurrentAction = async () => {
    const current = useDeviceStore.getState()
    if (stopPending.current || current.connection !== 'connected' || current.action === 'ota') return
    stopPending.current = true
    setStopping(true)
    try {
      // Automatic device refreshes need not have a matching browser action.
      await inktagClient.stop()
      if (useDeviceStore.getState().deviceId === current.deviceId)
        useDeviceStore.getState().addLog('已请求设备停止当前动作。', 'success')
    } catch (error) {
      if (useDeviceStore.getState().deviceId === current.deviceId)
        useDeviceStore.getState().addLog(`停止失败：${error instanceof Error ? error.message : String(error)}`, 'error')
    } finally {
      stopPending.current = false
      setStopping(false)
    }
  }

  return (
    <div className="page dashboard-page">
      <PageHeader title="工作台" />

      <section className="quick-control" aria-labelledby="quick-title">
        <div className="section-heading">
          <h2 id="quick-title">{t('快捷控制')}</h2>
          {quickNote && <span className="quiet-note">{t(quickNote)}</span>}
        </div>
        {canUseNfc && <div className="segmented-control quick-control-mode" role="group" aria-label={t('控制方式')}>
          <button type="button" className={!nfcSelected ? 'active' : undefined} aria-pressed={!nfcSelected} onClick={() => setQuickMode('ble')}><Bluetooth />{t('蓝牙控制')}</button>
          <button type="button" className={nfcSelected ? 'active' : undefined} aria-pressed={nfcSelected} onClick={() => setQuickMode('nfc')}><Nfc />{t('NFC 控制')}</button>
        </div>}
        {nfcSelected ? <NfcCommandControls disabled={switching} /> : <><div className="command-strip">
          <div className={`quick-command-split ${action === 'calendar' ? 'active' : ''}`}>
            <button className="quick-command-main" disabled={!connected || switching} onClick={() => void runQuickDisplay('calendar', showCalendar)}><CalendarDays /><span>{t('日历')}</span></button>
            <button className="quick-command-settings" title={t('日历设置')} aria-label={t('日历设置')} disabled={!connected || switching || !deviceId} onClick={() => setCalendarSettingsOpen(true)}><Settings2 /></button>
          </div>
          <button className={action === 'clock' ? 'active' : ''} disabled={!connected || switching} onClick={() => void showClock()}><Clock3 /><span>{t('时钟')}</span></button>
          <button className={action === 'device-info' ? 'active' : ''} disabled={!connected || (!settingsCapabilities.nativeDeviceInfo && !screen) || switching} title={t(connected && !settingsCapabilities.nativeDeviceInfo && !screen ? '屏幕信息未读到，请点击当前设备旁的刷新按钮重读' : '显示设备信息页')} onClick={() => void runQuickDisplay('device-info', showDeviceInfo)}><Contact /><span>{t('设备')}</span></button>
          <div className={`quick-command-split ${action === 'slideshow' ? 'active' : ''}`}>
            <button className="quick-command-main" disabled={!connected || switching || action === 'slideshow'} onClick={startDefaultSlideshow}><Play /><span>{t('图片')}{slideshowCount > 0 ? `（${slideshowCount}）` : ''}</span></button>
            <button className="quick-command-settings" title={t('图片设置')} aria-label={t('图片设置')} disabled={!connected || switching} onClick={showSlideshow}><Settings2 /></button>
          </div>
          <button className={action === 'clear' ? 'active' : ''} disabled={!connected || switching} onClick={() => void runQuickDisplay('clear', () => inktagClient.runAction('clear'))}><Eraser /><span>{t('清屏')}</span></button>
          <button disabled={!connected || action === 'ota' || stopping} title={t('请求设备停止当前刷新或轮播')} onClick={() => void stopCurrentAction()}><CirclePause /><span>{t(stopping ? '停止中' : '暂停')}</span></button>
        </div>
        {storedPages.length > 0 && <div className="stored-page-actions" aria-label="设备常用页面">
          {storedPages.map(({ item, label }) => {
            const Icon = pageIcons[item.contentType as keyof typeof pageIcons] || FileText
            return <button key={item.id} disabled={switching || action === 'slideshow'} onClick={() => void inktagClient.showStoredImage(item.id)}><Icon /><span>{label}</span></button>
          })}
        </div>}</>}
      </section>

      <div className="dashboard-grid">
        <section className="device-overview">
          <div className="section-heading"><div><div className="device-overview-title"><h2>当前设备</h2><button className="device-refresh-button" title="重新读取设备配置" aria-label="重新读取设备配置" aria-busy={refreshing} disabled={!connected || switching} onClick={() => void refreshDeviceInfo()}><RefreshCw className={refreshing ? 'spin' : undefined} /></button>{refreshMessage && <span className="device-refresh-status" role="status">{refreshMessage}</span>}</div></div><Link to="/device"><Settings2 />设备设置</Link></div>
          <dl className="fact-grid device-fact-grid">
            <div><dt>设备名</dt><dd className="editable-value"><span>{connected ? displayDeviceName(localAlias, deviceName) : '--'}</span><button className="inline-edit-button" title="修改设备名" aria-label="修改设备名" disabled={!connected} onClick={() => setIdentityField('localAlias')}><Pencil /></button></dd></div>
            <div><dt>蓝牙名</dt><dd className="editable-value"><span>{connected ? deviceName || '--' : '--'}</span><button className="inline-edit-button" title="修改蓝牙广播名" aria-label="修改蓝牙广播名" disabled={!connected || !advanced} onClick={() => setIdentityField('bluetoothName')}><Pencil /></button></dd></div>
            <div><dt>BLE 地址</dt><dd>{connected ? bleAddress || '待设备回传' : '--'}</dd></div>
            <div><dt>主控</dt><dd>{connected ? mcuDisplayName(boardId) : '--'}</dd></div>
            <div><dt>固件</dt><dd>{connected ? firmwareVersion || '--' : '--'}</dd></div>
            <div><dt>协议</dt><dd>{connected && protocolVersion != null ? `0x${protocolVersion.toString(16).padStart(2, '0')}` : '--'}</dd></div>
            <TemperatureFact connected={connected} boardId={boardId} temperature={temperature} mcuTemperature={mcuTemperature} panelTemperature={panelTemperature} />
            <div><dt>电压</dt><dd>{connected && voltage != null ? `${voltage.toFixed(2)} V` : '--'}</dd></div>
            <div><dt>屏幕</dt><dd>{connected && screen ? `${screen.width} × ${screen.height} / ${colorCode(screen.color)}` : '--'}</dd></div>
            <div><dt>设备图库</dt><dd>{connected ? capabilities.imageStore ? imageStoreInfo ? `已用 ${imageStoreInfo.saved} / 约 ${imageStoreInfo.saved + imageStoreInfo.contiguousFit} 图槽` : '可用' : '不可用' : '--'}</dd></div>
            <div><dt>NFC</dt><dd>{connected ? capabilities.nfc === 'none' ? '无板载' : '板载 NFC' : '--'}</dd></div>
            <div><dt>按键</dt><dd>{connected ? capabilities.button ? '板载按键' : '可扩展' : '--'}</dd></div>
            <div><dt>状态灯</dt><dd>{connected ? capabilities.led === 'rgb' ? '板载 RGB' : capabilities.led === 'mono' ? '板载单色' : '无板载' : '--'}</dd></div>

            <div><dt>授权</dt><dd className="activation-fact"><span>{connected ? advanced ? '高级版' : '基础版' : '--'}</span>{!advanced && (connected ? <Link className="inline-action-button" to="/device?tab=activation" title="激活设备" aria-label="激活设备"><KeyRound /></Link> : <button className="inline-action-button" title="连接设备后激活" aria-label="连接设备后激活" disabled><KeyRound /></button>)}</dd></div>
            <div><dt>浏览器设备 ID</dt><dd title={connected ? deviceId : undefined}>{connected ? deviceId || '--' : '--'}</dd></div>
          </dl>
          {identityField && <DeviceIdentityDialog initialField={identityField} onClose={() => setIdentityField(null)} />}
        </section>

        <section className="recent-pages">
          <div className="section-heading"><div><h2>常用模板</h2><p>选择模板创建页面。</p></div><Link to="/create"><PanelsTopLeft />全部模板</Link></div>
          <div className="mini-template-row">
            {commonTemplates.map((template) => (
              <Link to={pageTemplateTarget(template)} key={template.id}>
                <TemplatePreview template={template} compact />
                <span>{template.name}</span>
              </Link>
            ))}
          </div>
        </section>
      </div>
      {deviceImagesDialogOpen && <SlideshowDialog imageItems={imageStoreItems} storeInfo={imageStoreInfo} action={action} onClose={() => setDeviceImagesDialogOpen(false)} onShow={(id) => void showStoredImage(id)} onStart={(intervalSeconds) => void startSlideshow(intervalSeconds)} onStop={() => void stopSlideshow()} />}
      {calendarSettingsOpen && deviceId && <CalendarSettingsDialog deviceId={deviceId} countdownSupported={countdownSupported} onClose={() => setCalendarSettingsOpen(false)} />}
      {confirmationDialog}
    </div>
  )
}
