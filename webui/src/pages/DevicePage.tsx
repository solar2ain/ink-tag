import { NfcCommandHelp } from '../components/NfcCommandHelp'
import { FeedbackNotice, useFeedbackMessage } from '../components/FeedbackNotice'
import { FirmwareBuildBadge } from '../components/FirmwareUpdateDialog'
import { Activity, BellRing, BookmarkPlus, CheckCircle2, CircleAlert, CircleDot, CircleHelp, Copy, Cpu, FileUp, FlaskConical, KeyRound, LoaderCircle, Logs, Nfc, PanelTop, Pencil, QrCode, RadioTower, RefreshCw, RotateCcw, Save, ShieldCheck, SlidersHorizontal } from 'lucide-react'
import { useEffect, useMemo, useRef, useState } from 'react'
import { useSearchParams } from 'react-router-dom'
import { PageHeader } from '../components/PageHeader'
import { verifyDownload } from '../protocol/updates'
import { downloadFirmwareFile } from '../protocol/firmwareUpdate'
import { AboutInkTag } from '../components/GettingStarted'
import { ClientPreferences } from './PreferencesPage'
import { useTranslation } from '../preferences'
import { RuntimePinSettings } from '../components/RuntimePinSettings'
import { AuxIoSettings } from '../components/AuxIoSettings'
import { NativeFontPreview } from '../components/NativeFontPreview'
import { nativeFontRenderer, previewScreen } from '../protocol/nativeFontPreview'
import { rasterNativeDigits } from '../protocol/rasterNativeDigits'
import { PinWriteUnconfirmedError } from '../protocol/pinConfig'
import { TemperatureFact } from '../components/TemperatureFact'
import { DeviceDiagnostics } from '../components/DeviceDiagnostics'
import { BrandVisibilitySetting } from '../components/BrandVisibilitySetting'
import { DeviceIdentityDialog, type DeviceIdentityField } from '../components/DeviceIdentityDialog'
import { DeviceLocateDialog } from '../components/DeviceLocateDialog'
import { DeviceLifecycleDialog, type DeviceLifecycleAction } from '../components/DeviceLifecycleDialog'
import { DevicePasswordSettingsDialog } from '../components/DevicePassword'
import { useConfirmationDialog } from '../components/ConfirmationDialog'
import { ScreenProfileSelect } from '../components/ScreenProfileSelect'
import { RecoveryExitButton } from '../components/RecoveryExitButton'
import { RecoveryEnterButton } from '../components/RecoveryEnterButton'
import { RecoveryReadButton } from '../components/RecoveryReadButton'
import { LabPanel } from './LabPage'
import { db, type ScreenProfileRecord } from '../data/db'
import { getDevicePreference, updateDevicePreference } from '../data/devicePreferences'
import { builtInScreenProfiles, findScreenProfileForDevice, screenDriverAllowedOnBoard, screenProfileAllowedOnBoard, screenProfileConfigForDevice, screenRecipeDisplayName, sortScreenProfiles, type ScreenProfile } from '../data/screenProfiles'
import { inktagClient } from '../protocol/client'
import { AdvertisingSettings } from '../components/AdvertisingSettings'
import { PROTOCOL_V2_VERSION } from '../protocol/generatedCommandsV2'
import { activationResultLabels, ACTIVATION_CODE_CHARS, hasFirmwareEdition, type FirmwareActivationRequest } from '../protocol/activation'
import { mcuDisplayName } from '../protocol/boards'
import { displayDeviceName } from '../device/displayName'
import {
  defaultButtonBehaviorSettings, defaultLedSettings,
  deviceInputActions, inputActionSupported,
} from '../protocol/deviceSettings'
import { canvasSizeForScreen, colorCode, colors, controllerDisplayName, drivers, imageStoreCompatibilityChanged, makeScreenConfig, nativeUiScales, recipesForDriver, resolutions, screenConfigsEqual, screenRequiresPro, supportedColorsForDriver, validateScreenConfig } from '../protocol/screen'
import { useDeviceStore } from '../store/device'
import { useFirmwareUpdateStore } from '../store/firmwareUpdate'
import type { ButtonAction, ButtonBehaviorSettings, ButtonGesture, LedSettings, ScreenConfig } from '../types'
import type { ParsedOtaPackage } from '../protocol/ota'
import { otaPackagePresentation, otaPlatformLabel } from './otaPresentation'
import { defaultGuiFonts, GUI_ASSET_MAX_BYTES, SINGLE_USER_FONT_WARNING, type GuiAssetInfo, type GuiAssetPackage } from '../protocol/guiAssets'
import { fontInfoLimit, parseFontPackage, nextFontGeneration, buildFontRoles, userFontRoles, parseNativeDigitFont,
  type FontRole, type FontRoleChoices, type FontRoleChoice } from '../protocol/fontRoles'
import { rememberGuiAssetPackage } from '../data/guiAssetPackages'
import { addCatalogGlyphs, catalogMissingGlyphs } from '../device-gui/fontEditor'
import { buildInkTagDeviceUrl, isManagedInkTagDeviceUrl } from '../qr/qr'
import { CLIENT_DEVICE_TABS, type ClientDeviceTabKey } from '../../../packages/inktag-client-core/src/application'

type DeviceTab = ClientDeviceTabKey
const tabIcons = { overview: Activity, screen: PanelTop, hardware: Cpu, behavior: Nfc,
  activation: KeyRound, ota: FileUp, logs: Logs, test: FlaskConical,
  preferences: SlidersHorizontal, about: CircleHelp }
const tabs = CLIENT_DEVICE_TABS.map(({ key, label }) => [key, label, tabIcons[key]] as const)

export function DevicePage() {
  const t = useTranslation()
  const state = useDeviceStore()
  const firmwareUpdateAvailable = useFirmwareUpdateStore((value) => value.status === 'available' &&
    value.update?.edition === state.firmwareBuildEdition && value.update?.boardId === state.boardId &&
    value.update?.currentVersion === state.firmwareVersion) && state.connection === 'connected'
  const [searchParams, setSearchParams] = useSearchParams()
  const requestedTab = searchParams.get('tab')
  const tab = tabs.some(([id]) => id === requestedTab) ? requestedTab as DeviceTab : 'overview'
  const selectTab = (next: DeviceTab) => {
    setSearchParams(next === 'overview' ? {} : { tab: next }, { replace: true })
  }
  const clientTab = tab === 'preferences' || tab === 'about'
  const tabButton = ([id, label, Icon]: (typeof tabs)[number]) => <button key={id} aria-current={tab === id ? 'page' : undefined} className={tab === id ? 'active' : ''} onClick={() => selectTab(id)}><Icon />{t(label)}{id === 'ota' && firmwareUpdateAvailable && <span className="tab-update-dot" title="有可用固件更新" aria-label="有可用固件更新" />}</button>
  return (
    <div className="page device-page">
      <PageHeader title={t('设置')} />
      <div className="device-layout">
        <nav className="sub-nav" aria-label={t('设置')}>
          <h2 className="settings-group-title">{t('设备设置')}</h2>
          {tabs.filter(([id]) => id !== 'preferences' && id !== 'about').map(tabButton)}
          <h2 className="settings-group-title">{t('客户端设置')}</h2>
          {tabs.filter(([id]) => id === 'preferences' || id === 'about').map(tabButton)}
        </nav>
        <div className="device-panel">
          {tab === 'overview' && <DeviceOverview />}
          {tab === 'screen' && <><ScreenSettings /><GuiAssetSettings /></>}
          {tab === 'hardware' && <><RuntimePinSettings /><HardwareSettings /></>}
          {tab === 'behavior' && <BehaviorSettings />}
          {tab === 'activation' && <ActivationSettings />}
          {tab === 'ota' && <OtaSettings />}
          {tab === 'logs' && <LogViewer />}
          {tab === 'test' && <LabPanel />}
          {tab === 'preferences' && <ClientPreferences />}
          {tab === 'about' && <AboutInkTag />}
        </div>
      </div>
      {!clientTab && state.connection !== 'connected' && <div className="offline-ribbon"><RadioTower />连接设备后可读取和保存实际配置。</div>}
    </div>
  )
}

function ActivationSettings() {
  const { activation, connection } = useDeviceStore()
  const [code, setCode] = useState('')
  const [restoreCode, setRestoreCode] = useState(false)
  const [message, setMessage, messageRevision] = useFeedbackMessage('')
  const [request, setRequest] = useState<FirmwareActivationRequest | null>(null)
  const available = connection === 'connected' && activation.phase !== 'unsupported'
  const canActivate = available && activation.phase !== 'unavailable'
  useEffect(() => {
    if (connection !== 'connected' || activation.phase !== 'inactive') setRestoreCode(false)
  }, [connection, activation.phase])
  const phaseLabels = {
    unsupported: '该设备不支持激活', unavailable: '无法读取设备激活身份',
    inactive: '基础版', active: '高级版已激活', invalid: '设备激活信息损坏',
  }
  const loadRequest = async (force = false) => {
    const next = await inktagClient.readActivationRequest(force)
    setRequest(next)
    return next
  }
  useEffect(() => {
    if (!available) { setRequest(null); return }
    void loadRequest(false).catch((error) => setMessage(error instanceof Error ? error.message : String(error)))
  }, [available])
  const refresh = async () => {
    try {
      await inktagClient.readActivationStatus()
      await loadRequest(false)
      setMessage('已读取授权状态和当前激活请求。')
    } catch (error) { setMessage(error instanceof Error ? error.message : String(error)) }
  }
  const rotate = async () => {
    try {
      await loadRequest(true)
      setMessage('已生成新的挑战码；此前未安装的激活码不再有效。')
    } catch (error) { setMessage(error instanceof Error ? error.message : String(error)) }
  }
  const copyRequest = async () => {
    if (!request) return
    try {
      await navigator.clipboard.writeText(request.requestCode)
      setMessage('已复制设备码和挑战码。')
    } catch { setMessage('复制失败，请手动选择请求码。') }
  }
  const activate = async () => {
    if (!code.trim()) { setMessage('请输入激活码。'); return }
    try {
      const status = await inktagClient.installActivationCode(code.trim(), restoreCode)
      if (status.phase === 'active') setCode('')
      if (status.phase === 'active') setRequest(null)
      setMessage(status.phase === 'active' ? `设备已安装序号 ${status.sequence || 0} 的永久授权。` :
        activationResultLabels[status.lastResult || 0] || phaseLabels[status.phase])
    } catch (error) { setMessage(error instanceof Error ? error.message : String(error)) }
  }
  return <section><div className="panel-title"><div><h2>设备激活</h2><p>使用设备请求码获取并安装授权。</p></div><button className="button secondary" disabled={!available} onClick={() => void refresh()}><RefreshCw />刷新状态</button></div>
    <dl className="settings-list activation-status"><div><dt>当前版本</dt><dd><span className={`edition-badge ${activation.edition}`}>{activation.edition === 'advanced' && activation.phase === 'active' ? '高级版' : '基础版'}</span></dd></div><div><dt>激活状态</dt><dd>{connection === 'connected' ? phaseLabels[activation.phase] : '连接设备后读取'}</dd></div><div><dt>设备码</dt><dd>{activation.activationId || '--'}</dd></div><div><dt>授权序号</dt><dd>{activation.sequence || '--'}</dd></div><div><dt>功能位</dt><dd>{activation.featureMask ? `0x${activation.featureMask.toString(16).padStart(8, '0')}` : '--'}</dd></div><div><dt>授权方式</dt><dd>{activation.phase === 'active' ? '永久设备绑定签名' : '--'}</dd></div></dl>
    <div className="activation-actions"><label>设备请求码<input type="text" readOnly value={request?.requestCode || ''} placeholder="连接设备后生成" /></label><div className="button-row"><button className="button secondary" disabled={!request} onClick={() => void copyRequest()}><Copy />复制请求</button><button className="button secondary" disabled={!available} onClick={() => void rotate()}><RefreshCw />重新生成</button></div><span>{request ? '请求码已生成，请用于获取此设备的激活码。' : '重新生成后，之前未使用的激活码将失效。'}</span></div>
    <div className="activation-actions"><div className="activation-code-field"><label>激活码<textarea rows={3} value={code} disabled={!canActivate} autoComplete="off" spellCheck={false} maxLength={ACTIVATION_CODE_CHARS} onChange={(event) => setCode(event.target.value.trim())} placeholder="粘贴激活码" /></label>{activation.phase === 'inactive' && <label className="checkbox-label"><input type="checkbox" checked={restoreCode} disabled={!canActivate} onChange={(event) => setRestoreCode(event.target.checked)} />使用已保存的原激活码恢复授权</label>}</div><button className="button primary" disabled={!canActivate || code.length !== ACTIVATION_CODE_CHARS} onClick={() => void activate()}><KeyRound />{restoreCode ? '恢复授权' : activation.phase === 'active' ? '更新授权' : '激活设备'}</button><span>{activation.phase === 'unavailable' ? '当前设备无法激活。' : '激活码仅适用于当前设备。'}</span></div>
    {message && <FeedbackNotice className="library-message" noticeKey={messageRevision}>{message}</FeedbackNotice>}
    <div className="notice activation-notice"><ShieldCheck /><div><strong>如何激活</strong><span>复制设备请求码并获取激活码，再粘贴到上方完成激活。</span></div></div>
  </section>
}

function DeviceOverview() {
  const state = useDeviceStore()
  const connected = state.connection === 'connected'
  const [identityField, setIdentityField] = useState<DeviceIdentityField | null>(null)
  const [lifecycleAction, setLifecycleAction] = useState<DeviceLifecycleAction | null>(null)
  const [locateDialog, setLocateDialog] = useState(false)
  const [passwordDialog, setPasswordDialog] = useState(false)
  const advanced = state.activation.phase === 'active' && state.activation.edition === 'advanced'
  const lifecycleAvailable = state.settingsCapabilities.lifecycle && inktagClient.supportsLifecycleV2()
  const identityButton = (field: DeviceIdentityField, label: string) => {
    const pro = field === 'bluetoothName'
    return <button className="inline-edit-button" title={label} aria-label={label} disabled={!connected || !state.deviceId || (pro && !advanced)} onClick={() => setIdentityField(field)}><Pencil /></button>
  }
  return <section><div className="panel-title"><div><h2>设备概览</h2></div></div><dl className="settings-list">
    <div><dt>设备名</dt><dd className="editable-value"><span>{connected ? displayDeviceName(state.localAlias, state.deviceName, '--') : '--'}</span>{identityButton('localAlias', '修改设备名')}</dd></div><div><dt>蓝牙广播名</dt><dd className="editable-value"><span>{connected ? state.deviceName || '--' : '--'}</span>{identityButton('bluetoothName', '修改蓝牙广播名')}</dd></div>
    <div><dt>BLE 地址</dt><dd>{connected ? state.bleAddress || '--' : '--'}</dd></div><div><dt>主控</dt><dd>{connected ? mcuDisplayName(state.boardId) : '--'}</dd></div>
    <div><dt>固件版本</dt><dd>{connected ? state.firmwareVersion || '--' : '--'}</dd></div><div><dt>协议版本</dt><dd>{connected && state.protocolVersion != null ? `0x${state.protocolVersion.toString(16).padStart(2, '0')}` : '--'}</dd></div>
    <div><dt>电池电压</dt><dd>{connected && state.voltage != null ? `${state.voltage.toFixed(2)} V` : '--'}</dd></div>
    <TemperatureFact connected={connected} boardId={state.boardId} temperature={state.temperature} mcuTemperature={state.mcuTemperature} panelTemperature={state.panelTemperature} />
    <div><dt>NFC</dt><dd>{connected ? state.capabilities.nfc === 'none' ? '无板载' : '板载 NFC' : '--'}</dd></div><div><dt>按键</dt><dd>{connected ? state.capabilities.button ? '板载按键' : '可扩展' : '--'}</dd></div>
    <div><dt>状态灯</dt><dd>{connected ? state.capabilities.led === 'rgb' ? '板载 RGB' : state.capabilities.led === 'mono' ? '板载单色' : '无板载' : '--'}</dd></div>
    <div><dt>授权</dt><dd className="activation-fact"><span>{connected ? advanced ? '高级版' : '基础版' : '--'}</span>{connected && !advanced && <button className="inline-action-button" title="激活设备" aria-label="激活设备" onClick={() => { location.hash = '/device?tab=activation' }}><KeyRound /></button>}</dd></div><div><dt>浏览器设备 ID</dt><dd title={connected ? state.deviceId : undefined}>{connected ? state.deviceId || '--' : '--'}</dd></div>
  </dl>
    <div className="device-lifecycle"><div><strong>设备操作</strong><span>清除图槽只删除图片；清除设置会关闭密码，两者均保留 Pro、Find 和板型数据。</span></div><div className="device-lifecycle-actions"><button className="button secondary" onClick={() => setPasswordDialog(true)}>设备密码</button><button className="button secondary" disabled={!connected || state.protocolVersion !== PROTOCOL_V2_VERSION} onClick={() => setLocateDialog(true)}>寻找设备</button><button className="button secondary" disabled={!connected || state.protocolVersion !== PROTOCOL_V2_VERSION} onClick={() => setLifecycleAction('reset')}>重启设备</button><button className="button secondary" title={lifecycleAvailable ? '仅删除图片及图片元数据' : '固件尚未支持新的清除范围'} disabled={!connected || !lifecycleAvailable} onClick={() => setLifecycleAction('clear-user-data')}>清除图槽</button><button className="button secondary danger-text" title={lifecycleAvailable ? '清除设置和密码，保留图片；密码开启时需原密码' : '固件尚未支持新的清除范围'} disabled={!connected || !lifecycleAvailable} onClick={() => setLifecycleAction('reset-settings')}>清除设置</button></div></div>
    {identityField && <DeviceIdentityDialog initialField={identityField} onClose={() => setIdentityField(null)} />}
    {locateDialog && <DeviceLocateDialog onClose={() => setLocateDialog(false)} />}
    {passwordDialog && <DevicePasswordSettingsDialog onClose={() => setPasswordDialog(false)} />}
    {lifecycleAction && <DeviceLifecycleDialog action={lifecycleAction} onClose={() => setLifecycleAction(null)} />}
  </section>
}

function ScreenSettings() {
  const confirmation = useConfirmationDialog()
  const current = useDeviceStore((state) => state.screen)
  const boardId = useDeviceStore((state) => state.boardId)
  const deviceId = useDeviceStore((state) => state.deviceId)
  const connected = useDeviceStore((state) => state.connection === 'connected')
  const screenDriverIds = useDeviceStore((state) => state.settingsCapabilities.screenDriverIds)
  const hasImageStore = useDeviceStore((state) => state.capabilities.imageStore)
  const imageStoreInfo = useDeviceStore((state) => state.imageStoreInfo)
  const imageStoreItems = useDeviceStore((state) => state.imageStoreItems)
  const isPro = useDeviceStore((state) => state.activation.phase === 'active' && state.activation.edition === 'advanced')
  const fallback = useMemo<ScreenConfig>(() => makeScreenConfig({ driver: 0x24, color: 2, transform: 0, width: 400, height: 300, visibleX: 0, visibleY: 0, visibleWidth: 400, visibleHeight: 300 }), [])
  const [form, setForm] = useState<ScreenConfig>(current || fallback)
  const [advanced, setAdvanced] = useState(false)
  const [selectedProfile, setSelectedProfile] = useState('')
  const [userProfiles, setUserProfiles] = useState<ScreenProfile[]>([])
  const [profileEditor, setProfileEditor] = useState(false)
  const [profileMeta, setProfileMeta] = useState({ name: '', model: '', device: '', size: '4.2' })
  const [screenMessage, setScreenMessageText, screenMessageRevision] = useFeedbackMessage('')
  const [screenMessageError, setScreenMessageError] = useState(false)
  const setScreenMessage = (text: string, error = false) => {
    setScreenMessageText(text)
    setScreenMessageError(error)
  }
  const [screenSaving, setScreenSaving] = useState(false)
  const [screenTesting, setScreenTesting] = useState(false)
  const action = useDeviceStore((state) => state.action)
  const [localScreenWriteUncertain, setScreenWriteUncertain] = useState(false)
  const ioWriteUnconfirmed = useDeviceStore(state => state.ioWriteUnconfirmed)
  const screenWriteUncertain = localScreenWriteUncertain || ioWriteUnconfirmed
  const currentProfileApplied = Boolean(current && screenConfigsEqual(form, current) && !screenWriteUncertain)
  useEffect(() => { setScreenWriteUncertain(false) }, [connected, deviceId])
  const [bootRefreshSupported, setBootRefreshSupported] = useState(false)
  const [bootRefreshEnabled, setBootRefreshEnabled] = useState(false)
  const [bootRefreshSaving, setBootRefreshSaving] = useState(false)
  const [bootRefreshHelpOpen, setBootRefreshHelpOpen] = useState(false)
  const [brandVisibilitySupported, setBrandVisibilitySupported] = useState(false)
  const [brandVisible, setBrandVisible] = useState(true)
  const [brandSaving, setBrandSaving] = useState(false)
  const availableDrivers = useMemo(() => drivers.filter(([id]) => !connected ||
    (screenDriverAllowedOnBoard(id, boardId) &&
      (!screenDriverIds?.length || screenDriverIds.includes(id)))), [connected, boardId, screenDriverIds])
  const profiles = useMemo(() => {
    const available = [...builtInScreenProfiles, ...userProfiles].filter((profile) => !connected ||
      (screenProfileAllowedOnBoard(profile, boardId) &&
        (!screenDriverIds?.length || screenDriverIds.includes(profile.config.driver))))
    return sortScreenProfiles(available)
  }, [connected, boardId, screenDriverIds, userProfiles])
  const selectedId = (selectedProfile && findScreenProfileForDevice(
    profiles.filter((profile) => profile.id === selectedProfile), form,
  )?.id) || findScreenProfileForDevice(profiles, form)?.id || ''
  const update = (patch: Partial<ScreenConfig>) => {
    setSelectedProfile('')
    setForm((value) => ({ ...value, ...patch }))
  }
  const rotation = form.transform & 3
  const valid = validateScreenConfig(form)
  const availableRecipes = recipesForDriver(form.driver)
  const availableColors = colors.filter(([id]) => supportedColorsForDriver(form.driver, form.recipe).includes(id))

  const loadUserProfiles = async () => {
    const records = await db.screenProfiles.orderBy('updatedAt').reverse().toArray()
    setUserProfiles(records.map((item) => {
      const stored = item.config
      const config = makeScreenConfig({
        driver: stored.driver, recipe: stored.recipe ?? 0, color: stored.color,
        transform: stored.transform, nativeUiScale: stored.nativeUiScale ?? 0,
        width: stored.width, height: stored.height,
        visibleX: stored.visibleX, visibleY: stored.visibleY,
        visibleWidth: stored.visibleWidth, visibleHeight: stored.visibleHeight,
      })
      return {
        id: item.id, tag: 'user', group: 'user', name: item.name?.trim() || '',
        model: item.model, device: item.device, size: item.size, config,
      }
    }))
  }

  useEffect(() => { void loadUserProfiles() }, [])
  useEffect(() => {
    let active = true
    if (!connected) {
      setBootRefreshSupported(false)
      setBrandVisibilitySupported(false)
      return () => { active = false }
    }
    void inktagClient.getDisplaySettings().then((settings) => {
      if (!active) return
      setBootRefreshSupported(settings.supported)
      setBootRefreshEnabled(settings.bootRefreshEnabled)
      setBrandVisibilitySupported(settings.brandVisibilitySupported)
      setBrandVisible(settings.brandVisible)
    }).catch(() => {
      if (active) {
        setBootRefreshSupported(false)
        setBrandVisibilitySupported(false)
      }
    })
    return () => { active = false }
  }, [connected, isPro])
  useEffect(() => {
    if (!current) return
    setForm(current)
    setSelectedProfile('')
  }, [current])

  const chooseProfile = (profile: ScreenProfile) => {
    setSelectedProfile(profile.id)
    setForm(screenProfileConfigForDevice(profile, current))
  }
  const saveProfile = async () => {
    if (!profileMeta.name.trim()) { setScreenMessage('请输入 Profile 备注名。', true); return }
    if (!valid) { setScreenMessage('当前组合不受该驱动支持，不能保存 Profile。', true); return }
    const now = Date.now()
    const record: ScreenProfileRecord = {
      id: `user-${now}`, name: profileMeta.name.trim(),
      model: profileMeta.model.trim(), device: profileMeta.device.trim(), size: profileMeta.size.trim() || '--',
      config: { ...form }, createdAt: now, updatedAt: now,
    }
    await db.screenProfiles.put(record)
    setSelectedProfile(record.id)
    setProfileEditor(false)
    setProfileMeta({ name: '', model: '', device: '', size: profileMeta.size })
    await loadUserProfiles()
  }
  const deleteProfile = async (profile: ScreenProfile) => {
    await db.screenProfiles.delete(profile.id)
    if (selectedProfile === profile.id) setSelectedProfile('')
    await loadUserProfiles()
  }
  const saveToDevice = async () => {
    if (screenSaving || screenWriteUncertain) return
    const next = { ...form }
    const deviceId = useDeviceStore.getState().deviceId
    const compatibilityChanged = Boolean(current && imageStoreCompatibilityChanged(current, next))
    const savedImages = imageStoreInfo?.saved ?? imageStoreItems.length
    const clearImages = compatibilityChanged && hasImageStore && savedImages > 0
    let imagesCleared = false
    let clearStarted = false
    const sameDevice = () => useDeviceStore.getState().connection === 'connected' && useDeviceStore.getState().deviceId === deviceId
    const requireScreenAccess = () => {
      if (screenRequiresPro(next) && !hasFirmwareEdition(useDeviceStore.getState().activation, 'advanced')) {
        throw new Error('E6 屏幕控制需要在设备上激活 Pro；未清空图片，也未保存屏幕配置。')
      }
    }
    setScreenSaving(true)
    try {
      requireScreenAccess()
      if (clearImages && !await confirmation.confirm({
        title: '更换屏幕配置并清空图片槽',
        message: <>屏幕配置将从 {current?.width}×{current?.height} / {current ? colorCode(current.color) : '--'} 改为 {next.width}×{next.height} / {colorCode(next.color)}。{'\n\n'}将先清空设备中的 {savedImages} 个历史图片槽，再保存新屏幕配置；<strong>清空的图片无法恢复。</strong>如果之后保存失败，图片仍已删除。</>,
        confirmLabel: '保存并清空',
        danger: true,
      })) return
      const live = useDeviceStore.getState()
      if (live.connection !== 'connected' || live.deviceId !== deviceId) {
        setScreenMessage('连接的设备已变化，未执行清图或保存。请重新确认。', true)
        return
      }
      requireScreenAccess()
      if (clearImages) {
        // The fixed-slot backend requires an empty store before changing geometry.
        clearStarted = true
        await inktagClient.clearStoredImages(false)
        imagesCleared = true
      }
      if (!sameDevice()) throw Error('设备已变化，未发送屏幕配置')
      await inktagClient.setScreen(next)
      if (!sameDevice()) throw Error('设备已变化，未采用保存结果')
      setScreenMessage(imagesCleared
        ? '屏幕配置已保存；原有设备图片已清空，容量已按新尺寸重新计算。'
        : '屏幕配置已保存。')
    } catch (error) {
      const detail = error instanceof Error ? error.message : String(error)
      if (error instanceof PinWriteUnconfirmedError) setScreenWriteUncertain(true)
      setScreenMessage(imagesCleared
        ? `原有设备图片已清空，但屏幕配置未确认保存；请刷新设备状态后再操作。${detail}`
        : clearStarted
          ? `图片清理未确认完成，未发送新屏幕配置；请先刷新图槽状态。${detail}`
          : detail, true)
    } finally { setScreenSaving(false) }
  }
  const testClearScreen = async () => {
    const live = useDeviceStore.getState()
    if (screenTesting || screenSaving || live.connection !== 'connected' ||
        live.action !== 'idle' || !live.screen || !screenConfigsEqual(form, live.screen)) return
    setScreenTesting(true)
    setScreenMessage('正在测试清屏，请等待墨水屏刷新完成。')
    try {
      await inktagClient.runAction('clear')
      setScreenMessage('测试清屏完成。')
    } catch (error) {
      setScreenMessage(`测试清屏失败：${error instanceof Error ? error.message : String(error)}`, true)
    } finally { setScreenTesting(false) }
  }
  const saveBootRefresh = async (enabled: boolean) => {
    setBootRefreshSaving(true)
    try {
      const saved = await inktagClient.setBootRefresh(enabled)
      setBootRefreshEnabled(saved)
      setScreenMessage(saved
        ? '已启用上电后台重刷；设备会先开放蓝牙连接，并在空闲时执行。'
        : '已关闭上电重刷；上电时保留电子纸当前画面。')
    } catch (error) {
      setScreenMessage(error instanceof Error ? error.message : String(error), true)
    } finally {
      setBootRefreshSaving(false)
    }
  }
  const saveBrandVisible = async (visible: boolean) => {
    setBrandSaving(true)
    try {
      const saved = await inktagClient.setBrandVisible(visible)
      setBrandVisible(saved)
      setScreenMessage(saved ? '已显示 InkTag Pro 标识。' : '已关闭 InkTag Pro 标识。')
    } catch (error) {
      setScreenMessage(error instanceof Error ? error.message : String(error), true)
    } finally {
      setBrandSaving(false)
    }
  }

  return <section>
    <div className="panel-title"><div><h2>屏幕配置</h2><p>配置屏幕驱动、尺寸、颜色及方向。</p></div><button className="button secondary" onClick={() => { setProfileMeta((value) => ({ ...value, size: resolutions.find(([, width, height]) => width === form.width && height === form.height)?.[0] || value.size })); setProfileEditor(!profileEditor) }}><BookmarkPlus />保存新 Profile</button></div>
    <div className="form-section profile-form-section"><label>屏幕 Profile<ScreenProfileSelect profiles={profiles} selectedId={selectedId} showCustom={Boolean(current) || !selectedId} onSelect={chooseProfile} onDeleteUser={(profile) => void deleteProfile(profile)} /></label><button className="button primary" disabled={!connected || !valid || screenSaving || screenWriteUncertain} onClick={() => void saveToDevice()}><Save />{screenSaving ? '正在保存…' : '保存到设备'}</button><button type="button" className="button secondary" disabled={!connected || !currentProfileApplied || action !== 'idle' || screenSaving || screenTesting} title={!currentProfileApplied ? '请先将屏幕配置保存到设备，再测试清屏。' : '使用设备当前屏幕配置清白，不删除图片槽。'} onClick={() => void testClearScreen()}>{screenTesting ? <LoaderCircle className="spin" /> : <RotateCcw />}{screenTesting ? '正在清屏…' : '测试清屏'}</button></div>
    {form.color === 10 && <div className="notice warning e6-panel-warning" role="note" aria-label="E6 屏幕接线提醒"><CircleAlert aria-hidden="true" /><div><strong>E6 屏幕接线提醒</strong><span>在普通设备主板上使用时，请用绝缘胶带贴住屏幕排线的第 16 脚，使其悬空；否则屏幕会发热，且非常耗电。</span></div></div>}
    {screenRequiresPro(form) && <p className="hint" role="note" aria-label="E6 Pro 说明">E6 屏幕控制需要 Pro；离线预览和图片转换不受限制。</p>}
    {connected && current && <p className="hint screen-readback" aria-label="设备当前屏参">已读取：{current.width}×{current.height} · {colorCode(current.color)} · 旋转 {(current.transform & 3) * 90}° · 逻辑 {canvasSizeForScreen(current).width}×{canvasSizeForScreen(current).height} · 驱动 0x{current.driver.toString(16).padStart(2, '0')} / recipe {current.recipe} · 可视区 {current.visibleX},{current.visibleY},{current.visibleWidth}×{current.visibleHeight} · UI scale {current.nativeUiScale} / schema {current.schemaVersion}</p>}
    {profileEditor && <div className="profile-editor"><label>Profile 备注名（必填）<input value={profileMeta.name} onChange={(event) => setProfileMeta({ ...profileMeta, name: event.target.value })} placeholder="例如：会议室门牌屏" /></label><label>屏幕型号（可空）<input value={profileMeta.model} onChange={(event) => setProfileMeta({ ...profileMeta, model: event.target.value })} placeholder="例如：GDEQ0426T82" /></label><label>来源设备（可空）<input value={profileMeta.device} onChange={(event) => setProfileMeta({ ...profileMeta, device: event.target.value })} placeholder="例如：ZKC21VM" /></label><label>尺寸<input value={profileMeta.size} onChange={(event) => setProfileMeta({ ...profileMeta, size: event.target.value })} placeholder="4.2" /></label><button className="button primary" disabled={!profileMeta.name.trim()} onClick={() => void saveProfile()}><Save />保存为用户 Profile</button></div>}
    <div className="form-grid">
      <label>屏幕控制器<select value={form.driver} onChange={(event) => { const driver = Number(event.target.value); const recipe = recipesForDriver(driver)[0]?.id ?? 0; const supported = supportedColorsForDriver(driver, recipe); update({ driver, recipe, color: supported.includes(form.color) ? form.color : supported[0] }) }}>{availableDrivers.map(([id]) => <option key={id} value={id}>{controllerDisplayName(id, id === form.driver ? form.recipe : undefined)}</option>)}</select></label>
      <label>尺寸 / 分辨率<select aria-label="尺寸 / 分辨率" value={`${form.width}x${form.height}`} onChange={(event) => {
        const [width, height] = event.target.value.split('x').map(Number)
        update({ width, height, visibleX: 0, visibleY: 0, visibleWidth: width, visibleHeight: height })
      }}>
        {!resolutions.some(([, width, height]) => width === form.width && height === form.height) &&
          <option value={`${form.width}x${form.height}`}>{form.width} × {form.height}（当前分辨率）</option>}
        {resolutions.map(([size, width, height]) => <option key={`${size}-${width}-${height}`} value={`${width}x${height}`}>{size} 寸 / {width} × {height}</option>)}
      </select></label>
      <label>颜色<select value={form.color} onChange={(event) => update({ color: Number(event.target.value) })}>{availableColors.map(([id, label]) => <option key={id} value={id}>{label}</option>)}</select></label>
      <fieldset className="direction-field"><legend>方向</legend><div className="segmented-control compact">{[0, 1, 2, 3].map((value) => <button type="button" key={value} className={rotation === value ? 'active' : ''} onClick={() => update({ transform: (form.transform & 0x0c) | value })}>{value * 90}°</button>)}</div><label><input type="checkbox" checked={(form.transform & 4) !== 0} onChange={(event) => update({ transform: event.target.checked ? form.transform | 4 : form.transform & ~4 })} />左右镜像</label><label><input type="checkbox" checked={(form.transform & 8) !== 0} onChange={(event) => update({ transform: event.target.checked ? form.transform | 8 : form.transform & ~8 })} />上下镜像</label></fieldset>
    </div>
    <div className={`display-setting boot-refresh-setting${!connected || !bootRefreshSupported ? ' unavailable' : ''}`}><div className="display-setting-head"><label className="setting-check"><input type="checkbox" checked={bootRefreshEnabled} disabled={!connected || !bootRefreshSupported || bootRefreshSaving} onChange={(event) => void saveBootRefresh(event.target.checked)} />上电后自动重绘上一页</label><button type="button" className="help-icon-button" aria-label="了解上电后自动重绘" aria-expanded={bootRefreshHelpOpen} aria-controls="boot-refresh-help" onClick={() => setBootRefreshHelpOpen((open) => !open)}><CircleHelp /></button>{bootRefreshSaving && <span className="setting-saving" role="status">正在保存…</span>}</div><p id="boot-refresh-help" hidden={!bootRefreshHelpOpen}>启用后，设备上电会先开放蓝牙连接，再重绘关机前的页面；连接期间会延后刷新。{!connected ? '连接设备后才可设置。' : !bootRefreshSupported ? '当前设备未提供此设置。' : ''}</p></div>
    {isPro && brandVisibilitySupported && <BrandVisibilitySetting visible={brandVisible} saving={brandSaving} disabled={!connected} onChange={(visible) => void saveBrandVisible(visible)} />}
    {!valid && <div className="notice warning"><PanelTop /><div><strong>当前组合无效</strong><span>请选择该驱动支持的分辨率和颜色，或在高级设置中修正可视区域。</span></div></div>}
    {(screenMessage || screenWriteUncertain) && <FeedbackNotice className={`library-message${screenMessageError || screenWriteUncertain ? ' error' : ''}`} role={screenMessageError || screenWriteUncertain ? 'alert' : 'status'} noticeKey={screenMessageRevision} persistent={screenWriteUncertain}>{screenMessage || '上次引脚写入结果未确认，请重新连接核对，不要重复保存。'}</FeedbackNotice>}
    <button className="advanced-toggle" onClick={() => setAdvanced(!advanced)}><SlidersHorizontal />{advanced ? '收起高级设置' : '高级设置'}</button>
    {advanced && <div className="advanced-grid"><label>屏幕驱动方案<select value={form.recipe} onChange={(event) => { const recipe = Number(event.target.value); const supported = supportedColorsForDriver(form.driver, recipe); update({ recipe, color: supported.includes(form.color) ? form.color : supported[0] }) }}>{availableRecipes.map((recipe) => <option key={recipe.id} value={recipe.id} title={recipe.name}>{screenRecipeDisplayName(form.driver, recipe.id)}</option>)}</select></label><label>原生内容缩放<select value={form.nativeUiScale} onChange={(event) => update({ nativeUiScale: Number(event.target.value) })}>{nativeUiScales.map(([value, label]) => <option key={value} value={value}>{label}</option>)}</select></label>{(['width','height','visibleX','visibleY','visibleWidth','visibleHeight'] as const).map((key) => <label key={key}>{key}<input type="number" min="0" max="65535" value={form[key]} onChange={(event) => update({ [key]: Number(event.target.value) })} /></label>)}</div>}
    {confirmation.dialog}
  </section>
}

function HardwareSettings() {
  return <AuxIoSettings />
}
function BehaviorSettings() {
  const { capabilities: caps, deviceId, deviceName, connection, action, reportedButtonActions, reportedLed, reportedAuxIoConfiguration } = useDeviceStore()
  const settingsCaps = useDeviceStore((state) => state.settingsCapabilities)
  const officialDeviceUrl = useMemo(() => buildInkTagDeviceUrl(deviceName), [deviceName])
  const [led, setLed] = useState<LedSettings>(() => ({ ...defaultLedSettings(), policy: caps.led === 'none' ? 'off' : 'all' }))
  const [buttonActions, setButtonActions] = useState<ButtonBehaviorSettings>(defaultButtonBehaviorSettings())
  const [deviceInfoQrUrl, setDeviceInfoQrUrl] = useState(officialDeviceUrl)
  const [message, setMessage, messageRevision] = useFeedbackMessage('')
  useEffect(() => {
    if (!deviceId) {
      setLed({ ...defaultLedSettings(), policy: caps.led === 'none' ? 'off' : 'all' })
      setButtonActions(defaultButtonBehaviorSettings())
      setDeviceInfoQrUrl(officialDeviceUrl)
      return
    }
    void getDevicePreference(deviceId).then((preference) => {
      setLed({ policy: caps.led === 'none' ? 'off' : preference.led.policy })
      const reported = useDeviceStore.getState()
      if (!reported.reportedButtonActions) setButtonActions(preference.buttonActions)
      setDeviceInfoQrUrl(isManagedInkTagDeviceUrl(preference.deviceInfoQrUrl)
        ? officialDeviceUrl : preference.deviceInfoQrUrl)
    })
  }, [deviceId, deviceName, caps.led, officialDeviceUrl])
  useEffect(() => { if (reportedButtonActions) setButtonActions(reportedButtonActions) }, [reportedButtonActions])
  useEffect(() => { if (reportedLed) setLed(reportedLed) }, [reportedLed])
  useEffect(() => {
    if (connection === 'connected' && caps.nfc !== 'none') void inktagClient.requestNfcStatus().catch(error => setMessage(error instanceof Error ? error.message : String(error)))
  }, [connection, caps.nfc, deviceId])
  const setPolicy = (policy: LedSettings['policy']) => setLed({ policy })
  const writeButtonSettings = async () => {
    if (!deviceId) return
    try {
      await inktagClient.setButtonActions(buttonActions)
      await updateDevicePreference(deviceId, { buttonActions })
      setMessage('单击、双击和长按行为已写入设备并永久保存。')
    } catch (error) {
      setMessage(error instanceof Error ? error.message : String(error))
    }
  }
  const writeLedSettings = async () => {
    if (!deviceId) return
    const normalized = caps.led === 'none' ? { ...led, policy: 'off' as const } : led
    try {
      await inktagClient.setLedSettings(normalized)
      await updateDevicePreference(deviceId, { led: normalized })
      setLed(normalized)
      setMessage('状态灯策略已写入设备并永久保存。')
    } catch (error) {
      setMessage(error instanceof Error ? error.message : String(error))
    }
  }
  const writeDeviceInfoQr = async () => {
    if (!deviceId || !deviceInfoQrUrl.trim()) return
    try {
      setMessage('正在写入设备信息二维码…')
      await inktagClient.writeDeviceInfoQr(0, deviceInfoQrUrl.trim())
      await updateDevicePreference(deviceId, { deviceInfoQrUrl })
      setMessage('二维码已保存，正在刷新设备信息页。')
      await inktagClient.showNativeDeviceInfo()
    } catch (error) {
      setMessage(error instanceof Error ? error.message : String(error))
    }
  }
  const hasNfc = connection === 'connected' && caps.nfc !== 'none'

  const buttonProtocolReady = settingsCaps.buttonActionMask !== 0
    && (settingsCaps.buttonGestureMask & 0x07) === 0x07
    && Object.values(buttonActions).every((setting) => inputActionSupported(settingsCaps.buttonActionMask, setting.action))
  const configuredButtons = connection === 'connected' ? reportedAuxIoConfiguration?.buttons.flatMap((button, index) => button.gpio === 255 ? [] : [{ index, gpio: button.gpio }]) ?? [] : []
  const hasButton = caps.button || (settingsCaps.customButtonGpio && configuredButtons.length > 0)
  const buttonGestures: Array<{ id: ButtonGesture; label: string; bit: number }> = [
    { id: 'single', label: '单击', bit: 0x01 },
    { id: 'double', label: '双击', bit: 0x02 },
    { id: 'long', label: '长按', bit: 0x04 },
  ]
  const updateButtonGesture = (gesture: ButtonGesture, patch: Partial<ButtonBehaviorSettings[ButtonGesture]>) => {
    setButtonActions((value) => ({ ...value, [gesture]: { ...value[gesture], ...patch } }))
  }
  return <section className="behavior-settings"><div className="panel-title"><h2>灯控与交互行为</h2></div>
    <AdvertisingSettings key={`${connection}:${deviceId}`} wakeAvailable={hasButton || hasNfc} />
    <div className="behavior-block">
    <div className="led-heading"><div><BellRing /><span><strong>状态灯</strong></span></div><button className="button secondary" disabled={connection !== 'connected' || !settingsCaps.statusLedSettings} onClick={() => void writeLedSettings()}>写入设备</button></div>
    <div className="policy-options">
      {([['all', '全部', '使用全部固件内置状态提示'], ['critical', '关键', '仅保留开机、查找和 OTA'], ['off', '关闭', '任何状态均不亮灯']] as const).map(([id, title, copy]) => <label key={id} title={copy} className={led.policy === id ? 'active' : ''}><input type="radio" name="led-policy" value={id} checked={led.policy === id} disabled={caps.led === 'none' && id !== 'off'} onChange={() => setPolicy(id)} /><span><strong>{title}</strong></span></label>)}
    </div>
    {caps.led !== 'none' && <p className="behavior-policy-hint behavior-section-note">{led.policy === 'off' ? '关闭所有状态灯' : led.policy === 'critical' ? '仅显示开机、寻找和 OTA 等关键提示' : '显示全部状态提示'}</p>}
    {connection !== 'connected' && caps.led === 'none' && <p className="behavior-section-note">连接后读取状态灯策略。</p>}
    {connection === 'connected' && caps.led === 'none' && <p className="behavior-unavailable" role="note"><CircleAlert />当前设备未提供状态灯。</p>}
    </div>
    <div className="behavior-block">
    <div className="behavior-section-heading"><div><QrCode /><span><strong>设备信息二维码</strong></span></div></div>
    <div className="device-info-qr-grid single"><label>设备信息 URL<input type="url" maxLength={180} value={deviceInfoQrUrl} onChange={(event) => setDeviceInfoQrUrl(event.target.value)} /></label></div>
    <div className="section-actions behavior-qr-actions"><button className="button secondary" title="恢复官方地址" aria-label="恢复官方地址" onClick={() => { setDeviceInfoQrUrl(officialDeviceUrl); setMessage('已恢复官方地址，尚未写入设备。') }}><RotateCcw />恢复默认</button><button className="button secondary compact-command" title="写入并预览" aria-label="写入并预览" disabled={connection !== 'connected' || action !== 'idle' || !settingsCaps.deviceInfoQr || !settingsCaps.nativeDeviceInfo || !deviceInfoQrUrl.trim()} onClick={() => void writeDeviceInfoQr()}><QrCode /><span>写入并预览</span></button></div>
    <p className="behavior-section-note">此链接用于设备信息页的二维码。</p>
    </div>
    <div className="behavior-block nfc-usage-section">
      <div className="behavior-section-heading"><div><Nfc /><span><strong>NFC</strong></span></div><span className="nfc-capability-label" role="status">{connection === 'connected' ? hasNfc ? '设备具有 NFC 功能' : '设备无 NFC 功能' : '未连接设备'}</span></div>
      {hasNfc && <NfcCommandHelp />}
    </div>
    <div className="behavior-block">
    <div className="behavior-section-heading"><div><CircleDot /><span><strong>实体按键</strong></span></div><button className="button secondary" disabled={!hasButton || connection !== 'connected' || !buttonProtocolReady} onClick={() => void writeButtonSettings()}>写入设备</button></div>
    <div className="behavior-button-pins"><div>{configuredButtons.length ? <span>{configuredButtons.map(button => `按键 ${button.index + 1} · GPIO ${button.gpio}`).join("　／　")}</span> : <span>{caps.button ? "板载按键" : "尚未读到已配置的按键引脚"}</span>}</div><a href="#/device?tab=hardware">配置引脚</a></div>
    <div className="button-gesture-list">{buttonGestures.map((gesture) => {
      const value = buttonActions[gesture.id]
      const gestureReady = (settingsCaps.buttonGestureMask & gesture.bit) !== 0
      return <div className="button-gesture-row" key={gesture.id}><strong>{gesture.label}</strong><label><span>动作</span><select value={value.action} disabled={!hasButton || (settingsCaps.buttonGestureMask !== 0 && !gestureReady)} onChange={(event) => updateButtonGesture(gesture.id, { action: event.target.value as ButtonAction })}>{deviceInputActions.filter((item) => item.id !== 'controlUrl').map((item) => <option key={item.id} value={item.id} disabled={settingsCaps.buttonActionMask !== 0 && !inputActionSupported(settingsCaps.buttonActionMask, item.id)}>{item.label}</option>)}</select></label>{value.action === 'showSlot' ? <label className="gesture-slot"><span>图槽</span><input type="number" min="0" max="255" value={value.slotId} onChange={(event) => updateButtonGesture(gesture.id, { slotId: Math.max(0, Math.min(255, Number(event.target.value))) })} /></label> : null}</div>
    })}</div>
    <p className="behavior-section-note">{hasButton ? `所有实体按键共用动作设置。${buttonProtocolReady ? "" : "设备尚未声明完整动作能力。"}` : connection === "connected" ? "当前设备未提供可配置按键。" : "连接后读取按键设置。"}</p>
    </div>
    {message && <FeedbackNotice className="library-message" noticeKey={messageRevision}>{message}</FeedbackNotice>}
  </section>
}

function OtaSettings() {
  const { capabilities, boardId, connection, action, firmwareVersion, firmwareBuildEdition, deviceId } = useDeviceStore()
  const firmwareUpdate = useFirmwareUpdateStore()
  const [pkg, setPackage] = useState<ParsedOtaPackage | null>(null)
  const [status, setStatus, statusRevision] = useFeedbackMessage('')
  const [progress, setProgress] = useState(0)
  const [phase, setPhase] = useState<'idle' | 'downloading' | 'checking' | 'ready' | 'running' | 'success' | 'package-error' | 'transfer-error'>('idle')
  const selectionEpoch = useRef(0)
  const downloadController = useRef<AbortController | null>(null)
  const [resumeError, setResumeError, resumeErrorRevision] = useFeedbackMessage('')
  const [resumeRunning, setResumeRunning] = useState(false)
  const [recoverySelection, setRecoverySelection] = useState<ReturnType<typeof inktagClient.getExpectedRecoveryIdentity>>(null)
  const recoveryHandoff = inktagClient.getExpectedRecoveryIdentity()
  const reselectRecovery = phase === 'transfer-error' && recoverySelection && recoveryHandoff &&
    recoverySelection.deviceId === recoveryHandoff.deviceId && recoverySelection.recoveryName === recoveryHandoff.recoveryName
  const connected = connection === 'connected'
  const recovery = connected && firmwareVersion === 'Recovery'
  const appConnected = connected && /^\d+\.\d+\.\d+(?:\.\d+)?$/.test(firmwareVersion)
  const showCloudUpdate = appConnected && capabilities.ota !== 'none' && Boolean(firmwareBuildEdition)
  const cloudPackage = firmwareUpdate.update
  const update = cloudPackage?.edition === firmwareBuildEdition && cloudPackage.boardId === boardId &&
    cloudPackage.currentVersion === firmwareVersion ? cloudPackage : null
  const pendingRecoveryRead = /^(?:IKT|InkTag)_TL_[0-9A-F]{4}_OTA$/i.test(inktagClient.getExpectedRecoveryIdentity()?.recoveryName || '')
  useEffect(() => { setResumeError('') }, [connection])
  useEffect(() => {
    selectionEpoch.current++
    downloadController.current?.abort()
    // App -> Recovery temporarily resets board/capabilities. Keep the active
    // transaction and its eventual result visible across that role change.
    if (action !== 'ota' && !(recovery && recoverySelection?.deviceId === deviceId)) {
      setRecoverySelection(null); setPackage(null); setStatus(''); setProgress(0); setPhase('idle')
    }
    return () => { selectionEpoch.current++; downloadController.current?.abort() }
  }, [capabilities.ota, boardId, deviceId, connection, firmwareVersion, firmwareBuildEdition])
  const choose = async (file?: File, epoch = ++selectionEpoch.current) => {
    if (!file) return
    setPackage(null); setProgress(0); setStatus('正在校验升级包'); setPhase('checking')
    try {
      const ota = await import('../protocol/ota')
      if (update?.artifactName === file.name) {
        await verifyDownload(await file.arrayBuffer(), {
          type: 'ota', name: file.name, bytes: update.bytes,
          downloadUrl: update.downloadUrl, sha256: update.sha256,
        })
      }
      const parsed = await ota.parseOtaPackage(capabilities.ota, file, boardId)
      if (epoch !== selectionEpoch.current) return
      setPackage(parsed); setStatus(parsed.summary); setPhase('ready')
    } catch (error) {
      if (epoch !== selectionEpoch.current) return
      setStatus(error instanceof Error ? error.message : String(error))
      setPhase('package-error')
    }
  }
  const chooseOnline = async () => {
    if (!update || !connected || action !== 'idle' || capabilities.ota === 'none' || resumeRunning) return
    const epoch = ++selectionEpoch.current
    downloadController.current?.abort()
    const controller = new AbortController()
    downloadController.current = controller
    setPackage(null); setProgress(0); setPhase('downloading'); setStatus('正在下载在线固件')
    try {
      const file = await downloadFirmwareFile(update, { signal: controller.signal,
        onProgress: (received, total) => {
          if (epoch === selectionEpoch.current) setStatus(`正在下载在线固件：${Math.round(received / total * 100)}%`)
        } })
      if (epoch === selectionEpoch.current) await choose(file, epoch)
    } catch (error) {
      if (epoch !== selectionEpoch.current) return
      const cancelled = controller.signal.aborted
      setStatus(cancelled ? '在线固件下载已取消，可重新载入或选择本地文件。' : error instanceof Error ? error.message : String(error))
      setPhase(cancelled ? 'idle' : 'package-error')
    } finally {
      if (downloadController.current === controller) downloadController.current = null
    }
  }
  const start = async () => {
    if (!pkg) return
    setRecoverySelection(null)
    setProgress(0); setStatus('正在准备更新'); setPhase('running')
    try {
      const ota = await import('../protocol/ota')
      await ota.transferOta(pkg, (next) => { setProgress(next.percent); setStatus(next.message) })
      // 100% is a transport checkpoint, not proof that the returning App passed.
      setPhase('success')
    } catch (error) {
      if (error instanceof Error && 'code' in error && error.code === 'RECOVERY_SELECT_REQUIRED') {
        setRecoverySelection(inktagClient.getExpectedRecoveryIdentity())
        setStatus(error.message)
      } else setStatus(`OTA 失败：${error instanceof Error ? error.message : String(error)}`)
      setPhase('transfer-error')
    }
  }
  const selectRecovery = async () => {
    if (!recoverySelection) return
    setResumeRunning(true)
    try {
      await inktagClient.selectRecoveryForOta(recoverySelection)
      setStatus(pkg?.summary ?? '')
      setPhase('ready')
    } catch (error) {
      if (!(error instanceof DOMException && error.name === 'NotFoundError')) {
        setResumeError(error instanceof Error ? error.message : String(error))
      }
    } finally { setResumeRunning(false) }
  }
  const failed = phase === 'package-error' || phase === 'transfer-error'
  const packageInfo = pkg ? otaPackagePresentation(pkg, boardId) : null
  // Recovery role switching shares the device lock, not transfer progress.
  const running = phase === 'running' || (action === 'ota' && !resumeRunning)
  const preparing = phase === 'downloading' || phase === 'checking'
  const title = reselectRecovery ? '需要重新连接 Recovery'
    : failed ? (phase === 'package-error' ? '升级包校验失败' : 'OTA 更新失败')
    : running ? '正在更新固件' : phase === 'success' ? 'OTA 已完成'
      : phase === 'downloading' ? '正在下载在线固件' : phase === 'checking' ? '正在校验升级包' : pkg ? '升级包已通过校验' : '选择固件包'
  return <section><div className="panel-title"><div><h2>固件更新</h2><p>请连接设备查看可用的 OTA 固件包，或选择本地固件包。</p></div>{appConnected && firmwareBuildEdition && <FirmwareBuildBadge edition={firmwareBuildEdition} />}</div>
    {appConnected && capabilities.ota !== 'none' && !firmwareBuildEdition && <div className="firmware-update-line"><FileUp />等待确认固件版本类型，可选择本地 OTA 固件包。</div>}
    {showCloudUpdate && firmwareUpdate.status === 'available' && update && <div className="notice firmware-update-notice"><RefreshCw /><div><strong>可更新到 {update.latestVersion}</strong><span>载入后自动下载并校验，确认包信息后点击“开始 OTA”。</span><small>{update.artifactName}</small><div className="firmware-online-actions"><button className="button secondary" disabled={running || preparing || resumeRunning || action !== 'idle'} onClick={() => void chooseOnline()}>{phase === 'downloading' ? <LoaderCircle className="spin" /> : <FileUp />}{phase === 'downloading' ? '正在下载' : '载入在线固件'}</button>{phase === 'downloading' && <button className="button secondary" onClick={() => downloadController.current?.abort()}>取消下载</button>}<a href={update.downloadUrl} target="_blank" rel="noreferrer">手动下载</a></div></div></div>}
    {showCloudUpdate && firmwareUpdate.status === 'checking' && <div className="firmware-update-line"><LoaderCircle className="spin" />正在检查固件更新…</div>}
    {showCloudUpdate && firmwareUpdate.status === 'current' && <div className="firmware-update-line"><CheckCircle2 />当前已是发布库中的最新固件（{firmwareUpdate.latestVersion}）</div>}
    {showCloudUpdate && firmwareUpdate.status === 'not-published' && <div className="firmware-update-line"><FileUp />{firmwareBuildEdition === 'pro' ? 'Pro 在线更新暂未提供，可选择本地 OTA 固件包。' : '发布库暂未提供当前主板的 BASIC OTA 包。'}</div>}
    {showCloudUpdate && firmwareUpdate.status === 'unavailable' && <div className="firmware-update-line warning"><RefreshCw /><span>暂时无法检查公开发布：{firmwareUpdate.error}</span><button className="button secondary" onClick={firmwareUpdate.requestCheck}>重试</button></div>}
    {recovery && <div className="notice recovery-notice"><ShieldCheck /><div><strong>设备处于 Recovery</strong><span>{inktagClient.getRecoverySafeResumeSupported() ? '可更新固件或安全返回原应用。' : '当前 Recovery 不支持安全退出，请完成固件更新。'}</span></div><div className="recovery-notice-actions"><RecoveryReadButton collapsed onRunningChange={setResumeRunning} /></div></div>}
    <div className="section-actions ota-file-actions">
      {recovery ? <RecoveryExitButton alwaysVisible disabled={running || resumeRunning || preparing} onError={setResumeError} onRunningChange={setResumeRunning} /> : <RecoveryEnterButton alwaysVisible disabled={running || resumeRunning || preparing} onError={setResumeError} onRunningChange={setResumeRunning} />}
      <button className="button primary" disabled={!pkg || !connected || action !== 'idle' || resumeRunning || running} onClick={() => void start()}>{running ? <LoaderCircle className="spin" /> : <FileUp />}{running ? '正在更新' : '开始 OTA'}</button>
    </div>
    <label className={`drop-zone ${failed ? 'failed' : running || preparing ? 'working' : pkg ? 'ready' : ''}`}>{failed ? <CircleAlert /> : running || preparing ? <LoaderCircle className="spin" /> : pkg ? <CheckCircle2 /> : <FileUp />}<strong>{title}</strong><span>{failed && pkg ? '已选升级包信息见下方。' : phase === 'ready' ? '核对下方包信息后开始更新。' : status || (capabilities.ota === 'none' ? '连接设备后识别更新格式' : `当前平台：${otaPlatformLabel(capabilities.ota, boardId)}`)}</span>{pkg && phase !== 'ready' && <small className="ota-package-note">升级包已通过校验{running || phase === 'transfer-error' ? '；这不代表更新成功。' : ''}</small>}<input type="file" accept=".zip,.img" disabled={capabilities.ota === 'none' || running || resumeRunning || preparing} onChange={(event) => { const file = event.currentTarget.files?.[0]; event.currentTarget.value = ''; void choose(file) }} /></label>
    {failed && !reselectRecovery && <FeedbackNotice className="library-message error" persistent noticeKey={statusRevision}>{title}：{status} 请重新连接设备确认结果；若升级未完成，重新选择 OTA 应用文件并重试。</FeedbackNotice>}
    {failed && <div className="ota-failure" role="alert"><CircleAlert aria-hidden="true" /><div><strong>{title}</strong><p>{status}</p><span>{reselectRecovery ? '重新选择同一台 Recovery。连接成功后可用已选固件继续。' : phase === 'package-error' ? '请选择与当前设备匹配的 OTA 应用文件，然后重新开始升级。' : '请重新连接设备确认更新结果；若升级未完成，请重新选择 OTA 应用文件并重试。'}</span>{reselectRecovery && <div className="firmware-online-actions"><button type="button" className="button secondary" disabled={resumeRunning || action !== 'idle'} onClick={() => void selectRecovery()}>重新选择 Recovery</button></div>}</div></div>}
    {resumeError && <FeedbackNotice className="library-message error" role="alert" noticeKey={resumeErrorRevision}>Recovery 操作未完成：{resumeError}</FeedbackNotice>}
    {pendingRecoveryRead && !recovery && !connected && <RecoveryReadButton collapsed onRunningChange={setResumeRunning} />}
    {packageInfo && <section className="ota-package-card" aria-label="升级包信息"><header><h3>升级包信息</h3><span className="ota-platform-label">{packageInfo.platform}</span></header><dl><div><dt>文件名</dt><dd>{packageInfo.fileName}</dd></div><div><dt>应用大小</dt><dd>{packageInfo.applicationSize}</dd></div><div><dt>{packageInfo.versionLabel}</dt><dd>{packageInfo.version}</dd></div>{packageInfo.signatureLabel && <div><dt>签名</dt><dd>{packageInfo.signatureLabel}</dd></div>}</dl><details className="ota-technical-details"><summary>详情</summary><p>{packageInfo.technicalSummary}</p></details></section>}
    {(running || progress > 0) && <div className={`ota-progress ${failed ? 'failed' : ''}`} role="progressbar" aria-label="固件传输进度（不代表更新结果）" aria-valuemin={0} aria-valuemax={100} aria-valuenow={progress}><div style={{ width: `${progress}%` }} /><span>{progress.toFixed(0)}%</span></div>}<div className="notice warning"><FileUp /><div><strong>更新期间保持页面前台运行</strong><span>不要关闭浏览器、切断设备电源或离开蓝牙范围；升级完成后会尝试自动重连。</span></div></div></section>
}

function GuiAssetSettings() {
  const confirmation = useConfirmationDialog()
  const screen = useDeviceStore((state) => state.screen)
  const connection = useDeviceStore((state) => state.connection)
  const action = useDeviceStore((state) => state.action)
  const supported = useDeviceStore((state) => state.settingsCapabilities.guiAssetUpdate)
  const activation = useDeviceStore((state) => state.activation)
  const deviceId = useDeviceStore((state) => state.deviceId)
  const [info, setInfo] = useState<GuiAssetInfo | null>(null)
  const [pkg, setPkg] = useState<GuiAssetPackage | null>(null)
  const [imported, setImported] = useState<GuiAssetPackage | null>(null)
  const [digitFile, setDigitFile] = useState<File | null>(null)
  const digitSourceInput = useRef<HTMLInputElement>(null)
  useEffect(() => { if (!digitFile && digitSourceInput.current) digitSourceInput.current.value = '' }, [digitFile])
  const [digitsReady, setDigitsReady] = useState(false)
  const [smallHeight, setSmallHeight] = useState('')
  const [largeHeight, setLargeHeight] = useState('')
  const [names, setNames] = useState(['内置字体', '内置字体', '内置字体', '内置七段数码字'])
  const [baseFonts, setBaseFonts] = useState(defaultGuiFonts)
  const [builtinChinese, setBuiltinChinese] = useState(true)
  const [choices, setChoices] = useState<Partial<Record<FontRole, Partial<FontRoleChoice>>>>({})
  const [largeFonts, setLargeFonts] = useState<[Uint8Array?, Uint8Array?]>([])
  const [status, setStatus, statusRevision] = useFeedbackMessage('')
  const [progress, setProgress] = useState(0)
  const [editorText, setEditorText] = useState('')
  const fontEpoch = useRef(0)
  const installing = useRef(false)
  const [uploading, setUploading] = useState(false)
  const pro = activation.phase === 'active' && activation.edition === 'advanced'
  useEffect(() => {
    const epoch = ++fontEpoch.current
    setUploading(false)
    setInfo(null); setPkg(null); setImported(null); setDigitFile(null); setProgress(0); setStatus('')
    setBaseFonts(defaultGuiFonts()); setChoices({}); setLargeFonts([]); setBuiltinChinese(true)
    setNames(['内置字体', '内置字体', '内置字体', '内置七段数码字'])
    if (connection === 'connected' && supported) {
      void inktagClient.readGuiAssetInfo().then(value => { if (epoch === fontEpoch.current) setInfo(value) }).catch((error) => {
        if (epoch === fontEpoch.current) setStatus(error instanceof Error ? error.message : String(error))
      })
    }
    return () => { fontEpoch.current++ }
  }, [connection, supported, deviceId])
  const generation = nextFontGeneration(info?.generation ?? 0)
  const generationInput = useRef({ screen, digitFile, smallHeight, largeHeight, baseFonts, choices, generation })
  generationInput.current = { screen, digitFile, smallHeight, largeHeight, baseFonts, choices, generation }
  useEffect(() => { setPkg(null); setDigitsReady(false) }, [screen])
  const maximumBytes = info ? fontInfoLimit(info) : GUI_ASSET_MAX_BYTES
  const makePackage = (base = baseFonts, pending = choices, big = largeFonts) => {
    if (digitFile && !digitsReady && big === largeFonts) throw new Error('字体或屏幕已变化，请先重新生成时钟及日历大字。')
    const complete: FontRoleChoices = {}
    for (const role of ['numbers', 'latin', 'chinese'] as const) {
      const choice = pending[role]
      if (!choice) continue
      if (!choice.small || !choice.large) throw new Error('请同时选择该类字体的小号和大号字形')
      complete[role] = { small: choice.small, large: choice.large, characters: choice.characters }
    }
    if (big.some(Boolean) && (!big[0] || !big[1])) throw new Error('时钟大字需要同时选择小号和大号字形')
    return buildFontRoles(base, complete, generation, maximumBytes,
      big[0] && big[1] ? [big[0], big[1]] : undefined)
  }
  const useDefaults = () => {
    try {
      const base = defaultGuiFonts()
      setPkg(makePackage(base, {}, [])); setBaseFonts(base); setChoices({}); setLargeFonts([]); setBuiltinChinese(true)
      setNames(['内置字体', '内置字体', '内置字体', '内置七段数码字'])
      setDigitFile(null); setDigitsReady(false)
      setStatus('已生成内置默认字体包，大字使用七段数码绘制。')
    } catch (error) { setStatus(error instanceof Error ? error.message : String(error)) }
  }
  const replaceFont = async (role: FontRole, size: 'small' | 'large', file?: File) => {
    if (!file) return
    const epoch = fontEpoch.current
    try {
      const next = { ...choices, [role]: { ...choices[role], [size]: new Uint8Array(await file.arrayBuffer()) } }
      if (epoch !== fontEpoch.current) return
      setChoices(next); setPkg(null)
      const index = ['numbers', 'latin', 'chinese'].indexOf(role)
      setNames((value) => value.map((name, item) => item === index ? '自选 U8g2 字形源' : name))
      setPkg(makePackage(baseFonts, next)); setStatus('已合成完整资源包，使用字体文件中的实际尺寸、字距和基线。')
    } catch (error) { if (epoch === fontEpoch.current) { setPkg(null); setStatus(error instanceof Error ? error.message : String(error)) } }
  }
  const replaceLarge = async (index: 0 | 1, file?: File) => {
    if (!file) return
    const epoch = fontEpoch.current
    try {
      const font = new Uint8Array(await file.arrayBuffer())
      if (epoch !== fontEpoch.current) return
      parseNativeDigitFont(font)
      setDigitFile(null); setDigitsReady(false)
      const next: [Uint8Array?, Uint8Array?] = [...largeFonts]; next[index] = font
      setLargeFonts(next); setPkg(null); setNames((value) => value.map((name, item) => item === 3 ? '自选固定单元格大字' : name))
      setPkg(makePackage(baseFonts, choices, next)); setStatus('已加入定宽数字与冒号；不会启用局刷。')
    } catch (error) { if (epoch === fontEpoch.current) { setPkg(null); setStatus(error instanceof Error ? error.message : String(error)) } }
  }
  const choosePackage = async (file?: File) => {
    if (!file) return
    const epoch = fontEpoch.current
    try {
      const bytes = new Uint8Array(await file.arrayBuffer())
      if (epoch !== fontEpoch.current) return
      // Importing is editing: older fonts remain usable as source material.
      // The rebuilt package gets a new serial; fresh live admission precedes BEGIN.
      const parsed = parseFontPackage(bytes, maximumBytes)
      setImported(parsed)
      setDigitFile(null); setDigitsReady(false)
      const faces = [...parsed.entries].sort((a, b) => a.id - b.id)
        .map((entry) => parsed.bytes.slice(entry.offset, entry.offset + entry.length))
      const big: [Uint8Array?, Uint8Array?] = faces.length === 6 ? [faces[4], faces[5]] : []
      setBaseFonts(faces.slice(0, 4)); setChoices({}); setLargeFonts(big); setBuiltinChinese(false)
      setPkg(buildFontRoles(faces.slice(0, 4), {}, generation, maximumBytes,
        big[0] && big[1] ? [big[0], big[1]] : undefined))
      setNames([file.name, file.name, file.name, big.length ? file.name : '内置七段数码字'])
      setStatus('完整资源包已通过结构与 CRC 校验。导入的汉字风格不会自动混入另一字形源。')
    } catch (error) { if (epoch === fontEpoch.current) { setPkg(null); setStatus(error instanceof Error ? error.message : String(error)) } }
  }
  const download = () => {
    if (!pkg) return
    const url = URL.createObjectURL(new Blob([pkg.bytes.slice().buffer], { type: 'application/octet-stream' }))
    const anchor = document.createElement('a')
    anchor.href = url; anchor.download = `InkTag-fonts-g${pkg.generation}.itfa`; anchor.click()
    URL.revokeObjectURL(url)
  }
  const generateDigits = async () => {
    if (!digitFile || !screen) return
    const epoch = fontEpoch.current
    const input = generationInput.current
    setPkg(null)
    try {
      const suggested = (await nativeFontRenderer()).defaultSizes(previewScreen(screen))
      const heights = [smallHeight ? Number(smallHeight) : suggested.small, largeHeight ? Number(largeHeight) : suggested.large]
      const fonts = await rasterNativeDigits(digitFile, heights)
      if (epoch !== fontEpoch.current) return
      const latest = generationInput.current
      if (Object.keys(input).some((key) => input[key as keyof typeof input] !== latest[key as keyof typeof input])) return
      const big: [Uint8Array, Uint8Array] = [fonts[0], fonts[1]]
      setLargeFonts(big); setPkg(makePackage(baseFonts, choices, big))
      setDigitsReady(true)
      setNames((names) => names.map((n, i) => i === 3 ? digitFile.name : n))
      setStatus(`已按当前屏幕预留区域生成小号 ${heights[0]} / 大号 ${heights[1]} px 字形。请查看实际页面预览。`)
    } catch (error) { if (epoch === fontEpoch.current) setStatus(error instanceof Error ? error.message : String(error)) }
  }
  const install = async () => {
    if (!pkg || installing.current || !info) return
    const epoch = fontEpoch.current
    installing.current = true
    try {
      if (!await confirmation.confirm({title:'写入设备字库',message:SINGLE_USER_FONT_WARNING,confirmLabel:'确认上传',cancelLabel:'暂不上传'})) return
      if (epoch !== fontEpoch.current) throw new Error('设备连接已变化，请重新读取设备字体状态。')
      setUploading(true)
      setProgress(0); setStatus('正在替换用户字库…')
      const next = await inktagClient.writeGuiAssetPackage(pkg, setProgress, { confirmSingleUser: true })
      await rememberGuiAssetPackage(deviceId, pkg, builtinChinese && !choices.chinese ? 'catalog' : undefined)
      if (epoch !== fontEpoch.current) return
      setInfo(next); setStatus(`用户字库已提交并核验，generation ${next.generation}。`)
    } catch (error) { if (epoch === fontEpoch.current) setStatus(error instanceof Error ? error.message : String(error)) }
    finally { installing.current = false; if (epoch === fontEpoch.current) setUploading(false) }
  }
  const addGlyphs = async () => {
    const text = editorText.trim()
    if (!text || !info) return
    const epoch = fontEpoch.current
    try {
      if (choices.chinese) {
        const next = { ...choices, chinese: { ...choices.chinese, characters: text } }
        setPkg(makePackage(baseFonts, next)); setChoices(next)
      } else {
        if (!builtinChinese) throw new Error('请先选择同一风格的大小字形源，或明确生成默认包后再补字；不会混入另一种汉字风格')
        const unavailable = await catalogMissingGlyphs(text)
        if (epoch !== fontEpoch.current) return
        if (unavailable.length) throw new Error(`内置字形源暂不包含：${unavailable.join('、')}`)
        const nextBase = await addCatalogGlyphs(baseFonts, text)
        if (epoch !== fontEpoch.current) return
        setPkg(makePackage(nextBase)); setBaseFonts(nextBase)
      }
      setStatus('已使用所选字形源补充汉字；请检查容量后写入。')
    } catch (error) { if (epoch === fontEpoch.current) setStatus(error instanceof Error ? error.message : String(error)) }
  }
  return <section className="gui-asset-settings" aria-label="设备字体">
    <div className="panel-title"><div><h2>设备字体</h2><p>编辑或更换设备字库。</p></div><div className="section-actions"><button className="button secondary" onClick={useDefaults}>生成默认包</button><label className="button secondary">导入资源包<input hidden type="file" accept=".itfa,.bin" onChange={(event) => void choosePackage(event.target.files?.[0])} /></label></div></div>
    {supported ? <><div className="field-note">小号、大号表示用途。请选择适合当前屏幕的字体资源包。</div><div className="font-asset-grid">{userFontRoles.map((role, index) => <div key={role.id} className="font-role-card"><strong>{role.label}</strong><small>{role.note}</small><small>{names[index]}</small>
      {role.id === 'largeDigits' ? <>{([0, 1] as const).map((index) => <label key={index}>{index === 0 ? '小号字形' : '大号字形'}<input type="file" accept=".bin" disabled={!info} onChange={(event) => void replaceLarge(index, event.target.files?.[0])} /></label>)}<small>U8g2 压缩字形，按文件实际大小显示。可使用 CLI 从本地 TTF/OTF 生成完整资源包后导入。</small><button className="button secondary" onClick={() => { try { setPkg(makePackage(baseFonts, choices, [])); setLargeFonts([]); setNames((value) => value.map((n, i) => i === 3 ? '内置七段数码字' : n)) } catch (error) { setStatus(String(error)) } }}>使用内置数码字</button></>
        : <>{(['small', 'large'] as const).map((size) => <label key={size}>{size === 'small' ? '小号字形' : '大号字形'}<input type="file" accept=".bin" onChange={(event) => void replaceFont(role.id, size, event.target.files?.[0])} /></label>)}</>}
    </div>)}</div>
      <div className="font-glyph-editor"><label><strong>从本地字体生成时钟及日历大字</strong><span>默认按当前屏幕、旋转和 common 预留区域计算。</span><input ref={digitSourceInput} type="file" accept=".ttf,.otf" onChange={(e) => { setDigitFile(e.target.files?.[0] ?? null); setDigitsReady(false); setPkg(null) }} /></label><button className="button secondary" disabled={!digitFile || !screen} onClick={() => void generateDigits()}>按当前屏幕生成</button></div>
      <details><summary>高级：覆盖原生字号</summary><label>小号 <input type="number" min="1" max="127" placeholder="自动" value={smallHeight} onChange={(e) => { setSmallHeight(e.target.value); setDigitsReady(false); setPkg(null) }} /></label><label>大号 <input type="number" min="1" max="127" placeholder="自动" value={largeHeight} onChange={(e) => { setLargeHeight(e.target.value); setDigitsReady(false); setPkg(null) }} /></label></details>
      <div className="font-glyph-editor"><label><strong>补充汉字</strong><span>同时加入大小两套中文字库。</span><input value={editorText} maxLength={64} placeholder="例如：鹏程万里" onChange={(event) => setEditorText(event.target.value)} /></label><button className="button secondary" disabled={!pro || !info || !editorText.trim()} onClick={() => void addGlyphs()}>生成</button></div>
      <div className="field-note">{info ? `${info.activeBank == null ? '内置默认字体' : '用户字体'} · generation ${info.generation} · ${(info.activeSize / 1024).toFixed(1)} / ${(maximumBytes / 1024).toFixed(0)} KiB` : '正在读取设备字体资源状态…'}</div>
      {pkg && <div className="section-actions"><span>{(pkg.totalSize / 1024).toFixed(1)} KiB · generation {pkg.generation}</span><button className="button secondary" onClick={download}>保存资源包</button><button className="button primary" disabled={uploading || Boolean(confirmation.dialog) || connection !== 'connected' || action !== 'idle' || !pro} onClick={() => void install()}>{uploading ? `正在写入 ${progress}%` : '写入设备'}</button></div>}
      <NativeFontPreview pending={pkg} imported={imported} info={info} deviceId={deviceId} screen={screen} pro={pro} />
      {!pro && <div className="field-note">自定义字体写入需要已激活的 Pro 设备；官方资源维护包后续使用签名通道。</div>}</> : <div className="field-note font-asset-unavailable">当前固件尚未声明动态字体资源能力。</div>}
    {status && <FeedbackNotice className="library-message" noticeKey={statusRevision}>{status}</FeedbackNotice>}
    {confirmation.dialog}
  </section>
}

function LogViewer() {
  const logs = useDeviceStore((state) => state.logs)
  const clear = useDeviceStore((state) => state.clearLogs)
  return <section>
    <DeviceDiagnostics />
    <div className="panel-title log-title"><div><h2>设备日志</h2><p>接收、发送和设备诊断使用一致时间线。</p></div><button className="button secondary" onClick={clear}>清空</button></div><div className="log-viewer">{logs.length === 0 ? <p>暂无日志</p> : logs.map((entry) => <div key={entry.id} className={entry.level}><time>{new Date(entry.at).toLocaleTimeString()}</time><span>{entry.level === 'rx' ? '↓' : entry.level === 'tx' ? '↑' : '•'}</span><code>{entry.message}</code></div>)}</div>
  </section>
}
