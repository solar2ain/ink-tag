import { FeedbackNotice } from '../components/FeedbackNotice'
import { claimRestoredSessionNotice } from '../find/sessionNotice'
import { runFindOwnerTransaction } from '../find/ownerTransaction'
import { FindTransferTools, type FindTransferRequest } from '../find/FindTransferTools'
import { FindDeleteGuard } from '../find/FindDeleteGuard'
import { markFindBackupHistorical } from '../find/offlineOwner'
import { BrowserFindBackups } from '../find/BrowserFindBackups'
import { findDeviceStatus } from '../find/deviceStatus'
import { AlertTriangle, ExternalLink, KeyRound, LoaderCircle, MapPinned, Maximize2, Pencil, Radio, RefreshCw, Tag, Trash2, X } from 'lucide-react'
import { useEffect, useMemo, useRef, useState, type FormEvent } from 'react'
import { Link } from 'react-router-dom'
import { PageHeader } from '../components/PageHeader'
import { useConfirmationDialog } from '../components/ConfirmationDialog'
import { findClient, type FindIdentity, type FindReport, type FindSessionState, type FindTrackHistory, type FindTrackPoint } from '../find/client'
import {
  compactFindFingerprint,
  distinctFindIdentityNote,
  findDeviceDisplayName,
  findIdentityDeviceName,
  findIdentityDisplayName,
  findIdentityUsage,
  groupFindIdentities,
  identitiesForDevice,
  selectedFindIdentity,
  stableFindDeviceId,
} from '../find/identities'
import { buildFindMapDetails, FIND_TRACK_RETENTION_DAYS, findMapProviderName, findTrackMapKeyLabel, formatFindReportAge, type FindMapProvider } from '../find/map'
import { findTrackMapNeedsKey } from '../find/map'
import { orderOsmTrackPoints, OsmTrackMap } from '../find/OsmTrackMap'
import { useRuntimeStore } from '../server/runtime'
import { inktagClient } from '../protocol/client'
import { supportsFindOwnerEnrollment, supportsFindOwnerReplacement, supportsFindOwnerUnregister } from '../protocol/find'
import { useDeviceStore } from '../store/device'
import { setPreferences, useFindMapProvider, usePreferences } from '../preferences'

type Notice = { kind: 'info' | 'success' | 'error'; text: string }

function errorText(error: unknown) {
  return error instanceof Error ? error.message : String(error)
}

function identityTime(value: string | null, empty = '尚未查询') {
  return value ? new Date(value).toLocaleString() : empty
}

function identityCardTime(value: string | null, empty = '尚未查询') {
  if (!value) return empty
  const date = new Date(value)
  if (Number.isNaN(date.getTime())) return empty
  const year = date.getFullYear() === new Date().getFullYear() ? '' : `${date.getFullYear()}/`
  return `${year}${date.getMonth() + 1}/${date.getDate()} ${String(date.getHours()).padStart(2, '0')}:${String(date.getMinutes()).padStart(2, '0')}`
}

function localDateTimeValue(value: Date) {
  const local = new Date(value.getTime() - value.getTimezoneOffset() * 60_000)
  return local.toISOString().slice(0, 16)
}

function recentTrackRange(hours: number) {
  const end = new Date()
  const start = new Date(end.getTime() - (hours || FIND_TRACK_RETENTION_DAYS * 24) * 3_600_000)
  return { start: localDateTimeValue(start), end: localDateTimeValue(end) }
}

function UnavailableFind({ mode, localServer }: { mode: 'checking' | 'static' | 'standalone' | 'server'; localServer: boolean }) {
  if (mode === 'checking') return <section className="find-unavailable"><LoaderCircle className="spin" /><div><h2>正在加载 Find</h2><p>正在连接本机服务。</p></div></section>
  const title = localServer ? 'Find 服务未启动' : '当前模式不支持 Find 查询'
  const description = localServer
    ? '请启动 InkTag 本机服务后重试。'
    : 'Find 查询需要本地 WebUI 服务，当前模式没有查询后端。'
  return <section className="find-unavailable">
    <AlertTriangle />
    <div><h2>{title}</h2><p>{description}</p><Link className="button secondary" to="/help/local-webui">查看如何本地部署 WebUI 服务 <ExternalLink /></Link></div>
  </section>
}

export function FindPage() {
  const preferences = usePreferences()
  const provider = useFindMapProvider()
  const trackMapKeys = preferences.findMapKeys
  const runtime = useRuntimeStore()
  const device = useDeviceStore()
  const confirmation = useConfirmationDialog()
  const enabled = runtime.mode === 'server' && runtime.capabilities?.features.includes('find-query') === true
  const [session, setSession] = useState<FindSessionState | null>(null)
  const [email, setEmail] = useState('')
  const [password, setPassword] = useState('')
  const [consent, setConsent] = useState(false)
  const [methodIndex, setMethodIndex] = useState(0)
  const [code, setCode] = useState('')
  const [position, setPosition] = useState<{ fingerprint: string; report: FindReport } | null>(null)
  const positionRevision = useRef(0)
  const [mapRevision, setMapRevision] = useState(0)
  const [mapOpen, setMapOpen] = useState(false)
  const pageRef = useRef<HTMLDivElement>(null)
  const mapPreviewRef = useRef<HTMLIFrameElement>(null)
  const [trackFingerprint, setTrackFingerprint] = useState('')
  const [trackHistory, setTrackHistory] = useState<FindTrackHistory | null>(null)
  const [trackWindowHours, setTrackWindowHours] = useState(24)
  const [trackRangeStart, setTrackRangeStart] = useState(() => recentTrackRange(24).start)
  const [trackRangeEnd, setTrackRangeEnd] = useState(() => recentTrackRange(24).end)
  const [trackView, setTrackView] = useState<'plot' | 'list'>('plot')
  const [trackMapImage, setTrackMapImage] = useState('')
  const [trackMapError, setTrackMapError] = useState('')
  const [trackMapBusy, setTrackMapBusy] = useState(false)
  const [editingFingerprint, setEditingFingerprint] = useState('')
  const [editingFindNote, setEditingFindNote] = useState('')
  const [deleteFingerprint, setDeleteFingerprint] = useState('')
  const [busy, setBusy] = useState('')
  const [offlineFindAction, setOfflineFindAction] = useState<'enroll' | 'enable' | 'disable' | 'replace' | null>(null)
  const [transferRequest, setTransferRequest] = useState<FindTransferRequest | null>(null)
  const [notice, setNotice] = useState<Notice | null>(null)
  const autoSelectedConnection = useRef('')
  const manuallyReadFindConfig = useRef<typeof device.findConfig>(null)

  useEffect(() => {
    let lastFocused = document.activeElement
    let restoreScroll: (() => void) | undefined
    let scrollTimer = 0
    const clearScrollGuard = () => {
      if (restoreScroll) window.removeEventListener('scroll', restoreScroll)
      window.clearTimeout(scrollTimer)
    }
    const rememberFocus = () => {
      if (document.activeElement !== mapPreviewRef.current) lastFocused = document.activeElement
    }
    const keepPageFocus = () => {
      if (!mapPreviewRef.current || document.activeElement !== mapPreviewRef.current) return
      const { scrollX, scrollY } = window
      // The map's document can scroll the parent after focus has been restored.
      clearScrollGuard()
      restoreScroll = () => window.scrollTo({ left: scrollX, top: scrollY, behavior: 'instant' })
      window.addEventListener('scroll', restoreScroll, { once: true })
      scrollTimer = window.setTimeout(clearScrollGuard, 100)
      if (lastFocused instanceof HTMLElement) lastFocused.focus({ preventScroll: true })
      if (document.activeElement === mapPreviewRef.current) pageRef.current?.focus({ preventScroll: true })
    }
    window.addEventListener('focusin', rememberFocus)
    window.addEventListener('blur', keepPageFocus)
    return () => {
      window.removeEventListener('focusin', rememberFocus)
      window.removeEventListener('blur', keepPageFocus)
      clearScrollGuard()
    }
  }, [])

  const connectedDeviceId = useMemo(() => stableFindDeviceId({
    activationId: device.activation.activationId,
    bleAddress: device.bleAddress,
    browserDeviceId: device.deviceId,
  }), [device.activation.activationId, device.bleAddress, device.deviceId])

  useEffect(() => {
    if (!enabled) return
    let live = true
    setBusy('status')
    findClient.status().then((state) => {
      if (!live) return
      setSession(state)
      if (state.appleAccountUsername) setEmail(state.appleAccountUsername)
      setMethodIndex(state.activeMethodIndex ?? state.methods[0]?.index ?? 0)
      if (state.loggedIn && state.queryReady && state.sessionRestored && !state.queryError && !claimRestoredSessionNotice()) return
      if (state.loggedIn) setNotice({ kind: state.queryReady ? 'info' : 'error', text: state.queryError || (state.sessionRestored ? '已加载保存的 Apple 会话，查询时将验证是否仍有效。' : '已取得 Find 查询权限。') })
      else if (state.needs2fa) setNotice({ kind: 'success', text: '账号密码验证通过，请继续完成双重认证。' })
      else if (state.sessionError) setNotice({ kind: 'error', text: state.sessionError })
    }).catch((error) => live && setNotice({ kind: 'error', text: errorText(error) })).finally(() => live && setBusy(''))
    return () => { live = false }
  }, [enabled])

  const deviceSupportsDerivedIdentity = Boolean(device.findCapabilities?.publicOnlyP224)
  const deviceHasFindIdentity = Boolean(device.findConfig?.identityPresent)
  const connectedDeviceIdentities = useMemo(() => identitiesForDevice(
    session?.identities || [], connectedDeviceId,
  ), [connectedDeviceId, session?.identities])
  const connectedIdentity = connectedDeviceIdentities[0]
  const canDeleteDeviceIdentity = enabled && device.connection === 'connected' && deviceHasFindIdentity
    && supportsFindOwnerUnregister(device.findCapabilities) && Boolean(connectedIdentity && !connectedIdentity.historyOnly)
  const selectedIdentity = selectedFindIdentity(session?.identities || [])
  const selectedFingerprint = selectedIdentity?.keyFingerprint || ''
  const report = position?.fingerprint === selectedFingerprint ? position.report : null
  const setReport = (next: FindReport | null, fingerprint = selectedFingerprint) => {
    positionRevision.current += 1
    setPosition(next ? { fingerprint, report: next } : null)
  }
  const mapDeviceName = selectedIdentity ? findIdentityDeviceName(selectedIdentity) : '未命名 InkTag'
  const identityGroups = useMemo(() => groupFindIdentities(
    session?.identities || [], device.connection === 'connected' ? connectedDeviceId : '',
  ), [connectedDeviceId, device.connection, session?.identities])
  const connectedGroup = identityGroups.find((group) => group.current)
  const otherGroups = identityGroups.filter((group) => !group.current)
  useEffect(() => {
    if (!enabled || !selectedFingerprint) return
    let live = true
    const revision = positionRevision.current
    // Read the newest locally saved point, including previous identities of
    // this tag. Opening/selecting a tag does not contact Apple or enable tracking.
    findClient.trackingHistory(selectedFingerprint, 1, 0, undefined, undefined, true).then((history) => {
      if (!live || revision !== positionRevision.current) return
      const point = history.points[0]
      if (!point) {
        setPosition((current) => current?.fingerprint === selectedFingerprint ? null : current)
        return
      }
      setPosition((current) => current?.fingerprint === selectedFingerprint
        && Date.parse(current.report.timestamp) >= Date.parse(point.timestamp)
        ? current : { fingerprint: selectedFingerprint, report: { found: true, ...point, fromHistory: true } })
    }).catch(() => { /* No local position: keep the map hidden and allow a manual query. */ })
    return () => { live = false }
  }, [enabled, selectedFingerprint, selectedIdentity?.lastReportAt, selectedIdentity?.trackPointCount])
  useEffect(() => {
    if (manuallyReadFindConfig.current && manuallyReadFindConfig.current === device.findConfig) return
    if (!enabled || device.connection !== 'connected' || !device.findConfig?.enabled) {
      autoSelectedConnection.current = ''
      return
    }
    if (!deviceHasFindIdentity) {
      autoSelectedConnection.current = ''
      return
    }
    if (!connectedIdentity || !device.deviceId) return
    if (autoSelectedConnection.current === device.deviceId) return
    autoSelectedConnection.current = device.deviceId
    if (connectedIdentity.selected) return
    let live = true
    findClient.selectIdentity(connectedIdentity.keyFingerprint).then((state) => {
      if (live) {
        setSession(state)
        setMapOpen(false)
      }
    }).catch((error) => {
      if (live) {
        autoSelectedConnection.current = ''
        setNotice({ kind: 'error', text: `无法选中已连接设备：${errorText(error)}` })
      }
    })
    return () => { live = false }
  }, [connectedIdentity?.keyFingerprint, connectedIdentity?.selected, device.connection, device.deviceId, device.findConfig?.enabled, deviceHasFindIdentity, enabled])
  useEffect(() => {
    if (manuallyReadFindConfig.current && manuallyReadFindConfig.current === device.findConfig) return
    if (!enabled || !connectedIdentity || device.connection !== 'connected' || !deviceHasFindIdentity) return
    if (connectedIdentity.deviceId === connectedDeviceId && connectedIdentity.deviceName === device.localAlias && connectedIdentity.bluetoothName === device.deviceName) return
    let live = true
    findClient.updateIdentity(connectedIdentity.keyFingerprint, {
      deviceId: connectedDeviceId,
      deviceName: device.localAlias,
      bluetoothName: device.deviceName,
    }).then((state) => {
      if (live) setSession(state)
    }).catch(() => { /* Name synchronization is retried the next time this identity is connected. */ })
    return () => { live = false }
  }, [connectedDeviceId, connectedIdentity, device.connection, device.deviceName, deviceHasFindIdentity, device.localAlias, enabled])

  useEffect(() => {
    manuallyReadFindConfig.current = null
  }, [device.findConfig])

  useEffect(() => {
    if (!mapOpen && !trackFingerprint) return
    const escape = (event: KeyboardEvent) => {
      if (event.key !== 'Escape') return
      setMapOpen(false)
      setTrackFingerprint('')
    }
    document.addEventListener('keydown', escape)
    const previousOverflow = document.body.style.overflow
    document.body.style.overflow = 'hidden'
    return () => {
      document.removeEventListener('keydown', escape)
      document.body.style.overflow = previousOverflow
    }
  }, [mapOpen, trackFingerprint])

  const map = useMemo(() => report ? buildFindMapDetails(report, session?.address || '--', mapDeviceName) : null, [mapDeviceName, report, session?.address])

  const selectMapProvider = (next: FindMapProvider) => {
    setPreferences({ findMapProvider: next })
    setMapRevision((revision) => revision + 1)
    setTrackMapImage('')
    setTrackMapError('')
    if (trackFingerprint && findTrackMapNeedsKey(next) && trackMapKeys[next].trim()) void loadTrackMap(trackFingerprint, next, trackMapKeys[next])
  }

  const mapRecovery = map && <div className="find-map-recovery">
    <span>地图空白？</span>
    <button type="button" onClick={() => setMapRevision((revision) => revision + 1)}><RefreshCw />重新加载</button>
    <a href={map.openUrls[provider]} target="_blank" rel="noreferrer"><ExternalLink />在{findMapProviderName(provider)}打开</a>
  </div>

  const applySession = (state: FindSessionState) => {
    setSession(state)
    if (state.loggedIn) setPassword('')
    if (state.appleAccountUsername) setEmail(state.appleAccountUsername)
    setMethodIndex(state.activeMethodIndex ?? state.methods[0]?.index ?? 0)
  }

  const canReadDeviceFind = device.connection === 'connected' && device.settingsCapabilities.experimentalFind
  const refreshFind = async () => {
    setBusy('find-read')
    setNotice(null)
    try {
      const details = await inktagClient.refreshFindDetails()
      // Applying a manual read must not run connection-time identity updates.
      manuallyReadFindConfig.current = details.findConfig
      useDeviceStore.setState(details)
      setNotice({ kind: 'success', text: '设备 Find 信息已重新读取。' })
    } catch (error) {
      setNotice({ kind: 'error', text: `读取设备 Find 信息失败：${errorText(error)}` })
    } finally {
      setBusy('')
    }
  }

  const login = async (event: FormEvent) => {
    event.preventDefault()
    let submittedPassword = password
    setBusy('login')
    setNotice({ kind: 'info', text: '正在建立本地 Anisette 会话并登录…' })
    try {
      const state = await findClient.login(email.trim(), submittedPassword, consent)
      submittedPassword = ''
      applySession(state)
      setNotice({ kind: state.queryError ? 'error' : 'success', text: state.queryError || (state.needs2fa ? '账号密码验证通过，请继续完成双重认证。' : '已取得 Find 查询权限。') })
    } catch (error) {
      submittedPassword = ''
      setNotice({ kind: 'error', text: errorText(error) })
    } finally {
      setBusy('')
    }
  }

  const requestCode = async () => {
    const label = session?.methods.find((method) => method.index === methodIndex)?.label || '所选方式'
    setBusy('request-code')
    setNotice({ kind: 'info', text: `正在通过“${label}”请求验证码…` })
    try {
      applySession(await findClient.requestSecondFactor(methodIndex))
      setNotice({ kind: 'success', text: `验证码已通过“${label}”请求，请在对应设备上查看。` })
    } catch (error) {
      setNotice({ kind: 'error', text: `账号密码已验证；“${label}”发送验证码失败：${errorText(error)}` })
    } finally {
      setBusy('')
    }
  }

  const submitCode = async (event: FormEvent) => {
    event.preventDefault()
    const submittedCode = code.trim()
    setCode('')
    setBusy('submit-code')
    try {
      const state = await findClient.submitSecondFactor(methodIndex, submittedCode)
      applySession(state)
      setNotice({ kind: state.queryReady ? 'success' : 'error', text: state.queryError || '双重认证完成，已取得 Find 查询权限。' })
    } catch (error) {
      try { applySession(await findClient.status()) } catch { /* Keep the original operation error. */ }
      setNotice({ kind: 'error', text: errorText(error) })
    } finally {
      setBusy('')
    }
  }

  const selectIdentityForQuery = async (identity: FindIdentity) => {
    applySession(await findClient.selectIdentity(identity.keyFingerprint))
    if (selectedIdentity?.keyFingerprint !== identity.keyFingerprint) {
      setMapOpen(false)
    }
  }

  const selectSavedIdentity = async (identity: FindIdentity) => {
    if (busy || identity.selected) return
    setBusy(`select-${identity.keyFingerprint}`)
    setNotice(null)
    try {
      await selectIdentityForQuery(identity)
    } catch (error) {
      setNotice({ kind: 'error', text: `无法选中标签：${errorText(error)}` })
    } finally {
      setBusy('')
    }
  }

  const query = async (identity: FindIdentity | undefined = selectedIdentity) => {
    if (!identity) {
      setNotice({ kind: 'error', text: '请先选择需要查询的标签。' })
      return
    }
    setBusy(`query-${identity.keyFingerprint}`)
    setNotice({ kind: 'info', text: `正在查询“${findIdentityDisplayName(identity)}”的最新位置…` })
    try {
      await selectIdentityForQuery(identity)
      const result = await findClient.query({ fingerprint: identity.keyFingerprint, saveHistory: true, queryWindow: 'latest' })
      if (!result.found) {
        setReport(null)
        setNotice({ kind: 'info', text: '暂未查到新的位置报告，请稍后重试。' })
      } else {
        setReport(result, identity.keyFingerprint)
        setNotice({ kind: 'success', text: result.fromHistory
          ? '暂未查询到新位置，已显示最近的历史位置。'
          : '已查询到最新位置。' })
      }
    } catch (error) {
      setNotice({ kind: 'error', text: errorText(error) })
    } finally {
      try { applySession(await findClient.status()) } catch { /* Keep the query result visible if status refresh fails. */ }
      setBusy('')
    }
  }

  const loadTrackMap = async (
    fingerprint: string,
    nextProvider: FindMapProvider = provider,
    apiKey: string = trackMapKeys[nextProvider],
  ) => {
    setTrackMapImage('')
    setTrackMapError('')
    const normalizedKey = apiKey.trim()
    if (!findTrackMapNeedsKey(nextProvider) || !normalizedKey) return
    setTrackMapBusy(true)
    try {
      const custom = trackWindowHours < 0
      const result = await findClient.trackingMap(
        fingerprint, nextProvider, normalizedKey,
        custom ? 0 : trackWindowHours,
        custom ? new Date(trackRangeStart).toISOString() : undefined,
        custom ? new Date(trackRangeEnd).toISOString() : undefined,
        true,
      )
      setTrackMapImage(result.imageDataUrl)
    } catch (error) {
      setTrackMapError(errorText(error))
    } finally {
      setTrackMapBusy(false)
    }
  }

  const readTrackHistory = (
    fingerprint: string,
    hours = trackWindowHours,
    start = trackRangeStart,
    end = trackRangeEnd,
  ) => findClient.trackingHistory(
    fingerprint,
    hours <= 0 ? 30000 : 5000,
    hours < 0 ? 0 : hours,
    hours < 0 ? new Date(start).toISOString() : undefined,
    hours < 0 ? new Date(end).toISOString() : undefined,
    true,
  )

  const updateTrajectory = async (identity: FindIdentity) => {
    setBusy(`trajectory-update-${identity.keyFingerprint}`)
    setNotice({ kind: 'info', text: '正在查询最新位置并更新轨迹…' })
    let queryError = ''
    try {
      await selectIdentityForQuery(identity)
      const result = await findClient.query({ fingerprint: identity.keyFingerprint, saveHistory: true, queryWindow: 'latest' })
      if (result.found) setReport(result, identity.keyFingerprint)
      else queryError = '本次没有查到新的位置报告。'
    } catch (error) {
      queryError = `最新位置查询失败：${errorText(error)}`
    }
    try {
      const history = await readTrackHistory(identity.keyFingerprint)
      setTrackFingerprint(identity.keyFingerprint)
      setTrackHistory(history)
      setTrackView('plot')
      if (findTrackMapNeedsKey(provider) && trackMapKeys[provider].trim()) await loadTrackMap(identity.keyFingerprint)
      setNotice(queryError
        ? { kind: 'error', text: `${queryError} 已打开本机保存的历史轨迹。` }
        : { kind: 'success', text: '最新位置和历史轨迹已更新。' })
    } catch (error) {
      setNotice({ kind: 'error', text: queryError || errorText(error) })
    } finally {
      try { applySession(await findClient.status()) } catch { /* The history view remains usable. */ }
      setBusy('')
    }
  }

  const queryDeviceWindow = async (identity: FindIdentity) => {
    setBusy(`query-window-${identity.keyFingerprint}`)
    setNotice({ kind: 'info', text: `正在查询“${findIdentityDisplayName(identity)}”的近期位置，查询范围较大，预计需要约半分钟，请勿关闭页面。` })
    try {
      await selectIdentityForQuery(identity)
      const result = await findClient.query({ fingerprint: identity.keyFingerprint, saveHistory: true, queryWindow: 'recent' })
      applySession(await findClient.status())
      setNotice({
        kind: result.found ? 'success' : 'info',
        text: result.savedPointCount
          ? `查询完成，新增 ${result.savedPointCount} 个轨迹点。`
          : '查询完成，暂无新的轨迹点。',
      })
    } catch (error) {
      setNotice({ kind: 'error', text: errorText(error) })
    } finally {
      try { applySession(await findClient.status()) } catch { /* Preserve the query error. */ }
      setBusy('')
    }
  }

  const logout = async () => {
    setBusy('logout')
    try {
      applySession(await findClient.logout())
      setReport(null)
      setMapOpen(false)
      setNotice({ kind: 'success', text: '已退出 Apple 账号。账号名仍会保留，密码不会保存。' })
    } catch (error) {
      setNotice({ kind: 'error', text: errorText(error) })
    } finally {
      setBusy('')
    }
  }

  const configureDeviceFind = async (nextEnabled: boolean) => {
    if (device.connection !== 'connected') {
      setNotice({ kind: 'error', text: '请先连接需要配置的 InkTag。' })
      return
    }
    if (!device.settingsCapabilities.experimentalFind) {
      setNotice({ kind: 'error', text: '当前固件没有开放 Find 广播配置。' })
      return
    }
    if (!enabled) {
      if (nextEnabled && (device.activation.phase !== 'active' || device.activation.edition !== 'advanced')) {
        setNotice({ kind: 'error', text: '请先激活Pro，再登记Find。' })
        return
      }
      setOfflineFindAction(nextEnabled ? deviceHasFindIdentity ? 'enable' : 'enroll' : 'disable')
      return
    }
    setBusy(nextEnabled ? 'find-enable' : 'find-disable')
    try {
      if (!nextEnabled) {
        await inktagClient.configureFind(false)
        setNotice({ kind: 'success', text: 'Find 广播已关闭，已登记标签和轨迹记录将继续保留。' })
        return
      }
      if (!deviceSupportsDerivedIdentity) {
        throw new Error('当前设备需要更新固件后才能使用 Find。')
      }
      if (!deviceHasFindIdentity) {
        if (!enabled) throw new Error('请先启动本机服务，再开启 Find。')
        if (device.activation.phase !== 'active' || device.activation.edition !== 'advanced') {
          throw new Error('请先激活 Pro，再启用 Find。')
        }
        if (!supportsFindOwnerEnrollment(device.findCapabilities)) {
          throw new Error('当前设备需要更新固件后才能登记 Find。')
        }
        if (!/^activation:(?!0{16}$)[0-9A-F]{16}$/i.test(connectedDeviceId)) throw new Error('无法确认设备的固定身份。')
        const deviceName = device.localAlias || device.deviceName || 'InkTag'
        const metadata = {
          deviceId: connectedDeviceId,
          deviceName,
          bluetoothName: device.deviceName || deviceName,
          findNote: deviceName,
        }
        const challenge = await inktagClient.beginPublicOnlyFindOwner()
        applySession(await runFindOwnerTransaction({
          prepare: () => findClient.preparePublicOnlyOwner(challenge.nonceHex, metadata),
          stage: async (prepared) => {
            if (prepared.deviceId !== connectedDeviceId || prepared.bluetoothName !== metadata.bluetoothName) {
              throw new Error('本机 Find 服务返回的设备身份不匹配')
            }
            await inktagClient.stagePublicOnlyFindOwner(prepared.payloadHex, challenge.nonceHex)
          },
          commit: () => inktagClient.commitPublicOnlyFindOwner(),
          finalize: (prepared, proof) => {
            if (proof.fingerprintHex !== prepared.fingerprintHex) throw new Error('设备保存的 Find 身份与本机准备的身份不匹配')
            return findClient.finalizePublicOnlyOwner(prepared.transactionId, proof.slot, proof.publicKeyHex, proof.fingerprintHex)
          },
          abortDevice: () => inktagClient.abortFindOwnerRotation(),
          abortLocal: (prepared) => findClient.abortPublicOnlyOwner(prepared.transactionId),
        }))
      }
      await inktagClient.configureFind(true)
      setNotice({ kind: 'success', text: deviceHasFindIdentity
        ? 'Find 广播已开启。设备断开连接后将自动开始广播。'
        : '设备已登记，Find 广播已开启。' })
    } catch (error) {
      setNotice({ kind: 'error', text: errorText(error) })
    } finally {
      setBusy('')
    }
  }

  const rotateDeviceFindIdentity = async (restoredIdentity?: FindIdentity) => {
    if (device.connection !== 'connected') {
      setNotice({ kind: 'error', text: '请先连接需要替换身份的设备。' })
      return
    }
    if (!supportsFindOwnerReplacement(device.findCapabilities)) {
      setNotice({ kind: 'error', text: '当前设备不支持更新查找身份。' })
      return
    }
    if (!enabled) { setOfflineFindAction('replace'); return }
    const identity = restoredIdentity || connectedIdentity
    if (!identity) {
      setTransferRequest({ mode: 'import', replacementDeviceId: connectedDeviceId })
      return
    }
    if (!await confirmation.confirm({
      title: '更新查找身份',
      message: `将更新“${findIdentityDisplayName(identity)}”的查找身份。更新后设备将使用新身份，原有轨迹仍会保留。`,
      confirmLabel: '确认更新',
    })) return
    setBusy('find-owner-rotation')
    try {
      const assertTarget = () => {
        const current = useDeviceStore.getState()
        if (current.connection !== 'connected' ||
            `activation:${current.activation.activationId}`.toLowerCase() !== connectedDeviceId.toLowerCase())
          throw new Error('设备连接已变化，请连接原设备后重新核对身份。')
      }
      assertTarget()
      const owner = await inktagClient.readPublicOnlyFindOwnerStatus()
      assertTarget()
      if (!owner.identityPresent || !owner.timeValid) throw new Error('请先确认设备已登记并已校时。')
      if (owner.fingerprintHex.toLowerCase() !== identity.keyFingerprint.toLowerCase()) {
        try {
          applySession(await findClient.recoverPublicOnlyOwner(owner.fingerprintHex, owner.slot, owner.publicKeyHex, connectedDeviceId))
          setNotice({ kind: 'success', text: '已恢复上次身份更新，请再次点击替换身份。' })
        } catch {
          setTransferRequest({ mode: 'import', replacementDeviceId: connectedDeviceId })
        }
        return
      }
      if (!/^activation:(?!0{16}$)[0-9A-F]{16}$/i.test(connectedDeviceId)) throw new Error('无法确认设备的固定身份。')
      const challenge = await inktagClient.beginPublicOnlyFindOwner()
      assertTarget()
      applySession(await runFindOwnerTransaction({
        prepare: () => findClient.preparePublicOnlyReplacement(owner.fingerprintHex, challenge.nonceHex, connectedDeviceId),
        stage: async (prepared) => {
          assertTarget()
          if (prepared.currentFingerprintHex !== owner.fingerprintHex || prepared.deviceId.toLowerCase() !== connectedDeviceId.toLowerCase()) {
            throw new Error('本机 Find 服务返回的当前设备身份不匹配')
          }
          await inktagClient.stagePublicOnlyFindOwnerReplacement(prepared.payloadHex, challenge.nonceHex, owner.fingerprintHex)
        },
        commit: () => { assertTarget(); return inktagClient.commitPublicOnlyFindOwner() },
        finalize: (prepared, proof) => {
          assertTarget()
          if (proof.fingerprintHex !== prepared.fingerprintHex) throw new Error('设备更新后的 Find 身份与本机准备的身份不匹配')
          return findClient.finalizePublicOnlyOwner(prepared.transactionId, proof.slot, proof.publicKeyHex, proof.fingerprintHex)
        },
        abortDevice: async () => { assertTarget(); await inktagClient.abortFindOwnerRotation() },
        abortLocal: (prepared) => findClient.abortPublicOnlyOwner(prepared.transactionId),
        recoverAfterCommit: async (prepared) => {
          assertTarget()
          const status = await inktagClient.readPublicOnlyFindOwnerStatus().catch(() => null)
          assertTarget()
          if (status?.identityPresent && status.fingerprintHex === prepared.fingerprintHex) {
            return findClient.recoverPublicOnlyOwner(status.fingerprintHex, status.slot, status.publicKeyHex, connectedDeviceId)
          }
          return undefined
        },
      }))
      setReport(null)
      setMapOpen(false)
      try { markFindBackupHistorical(owner.fingerprintHex.toLowerCase()) } catch { /* Keep any unreadable backup; never delete it. */ }
      setNotice({ kind: 'success', text: '查找身份已更新，原有轨迹仍可查看。' })
    } catch (error) {
      // The transaction helper preserves pending owner secrets after COMMIT.
      setNotice({ kind: 'error', text: errorText(error) })
    } finally {
      setBusy('')
    }
  }

  const deleteDeviceFindIdentity = async () => {
    if (!canDeleteDeviceIdentity || !connectedIdentity) return
    if (!await confirmation.confirm({
      title: '删除设备上的 Find 身份？',
      message: <>将删除“{findIdentityDisplayName(connectedIdentity)}”设备上的 Find 身份并关闭广播。<strong>本机的设备资料和历史轨迹会保留。</strong></>,
      confirmLabel: '确认删除身份',
      danger: true,
    })) return
    setBusy('find-owner-unregister')
    try {
      await inktagClient.unregisterFindOwner(connectedIdentity.deviceId, connectedIdentity.keyFingerprint)
      setNotice({ kind: 'success', text: '设备上的 Find 身份已删除，本机资料和历史轨迹已保留。' })
    } catch (error) {
      setNotice({ kind: 'error', text: `删除设备身份失败：${errorText(error)}` })
    } finally {
      setBusy('')
    }
  }

  const editFindIdentityNote = (identity: FindIdentity) => {
    setEditingFingerprint(identity.keyFingerprint)
    setEditingFindNote(identity.findNote)
  }

  const renameFindIdentity = async (fingerprint: string, currentNote: string) => {
    const findNote = editingFindNote.trim()
    if (!findNote) {
      setNotice({ kind: 'error', text: 'Find 备注不能为空。' })
      return
    }
    if (findNote === currentNote) {
      setEditingFingerprint('')
      return
    }
    setBusy(`identity-rename-${fingerprint}`)
    try {
      applySession(await findClient.updateIdentity(fingerprint, { findNote }, true))
      setEditingFingerprint('')
      setNotice({ kind: 'success', text: `Find 备注已更新为“${findNote}”。` })
    } catch (error) {
      setNotice({ kind: 'error', text: errorText(error) })
    } finally {
      setBusy('')
    }
  }

  const deleteFindIdentity = async (identity: FindIdentity) => {
    setBusy(`identity-delete-${identity.keyFingerprint}`)
    try {
      const wasSelected = identity.selected
      applySession(await findClient.deleteIdentity(identity.keyFingerprint))
      if (trackFingerprint === identity.keyFingerprint) {
        setTrackFingerprint('')
        setTrackHistory(null)
      }
      if (wasSelected) {
        setReport(null)
        setMapOpen(false)
      }
      if (editingFingerprint === identity.keyFingerprint) setEditingFingerprint('')
      setDeleteFingerprint('')
      const remaining = session?.identities.length ? Math.max(0, session.identities.length - 1) : 0
      setNotice({ kind: 'success', text: wasSelected && remaining > 0 ? '标签及轨迹已删除，并已选择另一标签。' : '标签及轨迹已删除。' })
    } catch (error) {
      setNotice({ kind: 'error', text: errorText(error) })
    } finally {
      setBusy('')
    }
  }

  const confirmDeleteHistoricalIdentity = async (identity: FindIdentity) => {
    if (!await confirmation.confirm({
      title: '删除历史身份的本机资料？',
      message: <>将删除“{findIdentityDisplayName(identity)}”这份历史身份及其 {identity.trackPointCount} 个轨迹点。<strong>删除后无法恢复。</strong></>,
      confirmLabel: '删除本机资料',
      danger: true,
    })) return
    await deleteFindIdentity(identity)
  }

  const configureFindTracking = async (identity: FindIdentity, nextEnabled: boolean, intervalMinutes = session?.trackingIntervalMinutes || 60) => {
    if (nextEnabled && !await confirmation.confirm({
      title: '开启记录',
      message: `启用后，将每 ${intervalMinutes} 分钟查询一次“${findIdentityDisplayName(identity)}”的位置，并在本机保存轨迹。`,
      confirmLabel: '开启记录',
    })) return
    setBusy(`tracking-${identity.keyFingerprint}`)
    try {
      const state = await findClient.configureTracking(identity.keyFingerprint, nextEnabled, intervalMinutes)
      applySession(state)
      setNotice({ kind: 'success', text: nextEnabled ? `已启用轨迹记录，每 ${intervalMinutes} 分钟查询一次。` : '已暂停记录；已有轨迹仍保留在本机。' })
    } catch (error) {
      setNotice({ kind: 'error', text: errorText(error) })
    } finally {
      setBusy('')
    }
  }

  const openTrackingHistory = async (identity: FindIdentity) => {
    setTrackFingerprint(identity.keyFingerprint)
    setTrackHistory(null)
    setTrackView('plot')
    setTrackMapImage('')
    setTrackMapError('')
    setBusy(`history-${identity.keyFingerprint}`)
    try {
      setTrackHistory(await readTrackHistory(identity.keyFingerprint))
      if (trackMapKeys[provider].trim()) await loadTrackMap(identity.keyFingerprint)
    } catch (error) {
      setTrackFingerprint('')
      setNotice({ kind: 'error', text: errorText(error) })
    } finally {
      setBusy('')
    }
  }

  const changeTrackingInterval = async (identity: FindIdentity, intervalMinutes: number) => {
    setBusy(`tracking-interval-${identity.keyFingerprint}`)
    try {
      applySession(await findClient.configureTracking(identity.keyFingerprint, identity.trackingEnabled, intervalMinutes))
      setNotice({ kind: 'success', text: `所有已启用身份的定期查询间隔已改为 ${intervalMinutes} 分钟。` })
    } catch (error) {
      setNotice({ kind: 'error', text: errorText(error) })
    } finally {
      setBusy('')
    }
  }

  const clearTrackingHistory = async (identity: FindIdentity) => {
    if (!await confirmation.confirm({
      title: '清除历史轨迹',
      message: <>将清除“{findIdentityDisplayName(identity)}”保存在本机的全部轨迹点。<strong>此操作无法撤销。</strong></>,
      confirmLabel: '清除轨迹',
      danger: true,
    })) return
    setBusy(`tracking-clear-${identity.keyFingerprint}`)
    try {
      applySession(await findClient.clearTrackingHistory(identity.keyFingerprint, true))
      setTrackHistory(await readTrackHistory(identity.keyFingerprint))
      setTrackMapImage('')
      setTrackMapError('')
      setNotice({ kind: 'success', text: '本机轨迹记录已清除。' })
    } catch (error) {
      setNotice({ kind: 'error', text: errorText(error) })
    } finally {
      setBusy('')
    }
  }

  const changeTrackWindow = async (hours: number) => {
    const range = recentTrackRange(hours)
    setTrackWindowHours(hours)
    setTrackRangeStart(range.start)
    setTrackRangeEnd(range.end)
    setTrackMapImage('')
    setTrackMapError('')
    if (!trackFingerprint) return
    setBusy(`history-window-${trackFingerprint}`)
    try {
      setTrackHistory(await findClient.trackingHistory(trackFingerprint, hours === 0 ? 30000 : 5000, hours, undefined, undefined, true))
      if (findTrackMapNeedsKey(provider) && trackMapKeys[provider].trim()) {
        setTrackMapBusy(true)
        try {
          const result = await findClient.trackingMap(trackFingerprint, provider, trackMapKeys[provider].trim(), hours, undefined, undefined, true)
          setTrackMapImage(result.imageDataUrl)
        } finally {
          setTrackMapBusy(false)
        }
      }
    } catch (error) {
      setTrackMapError(errorText(error))
    } finally {
      setBusy('')
    }
  }

  const applyCustomTrackWindow = async () => {
    const start = new Date(trackRangeStart)
    const end = new Date(trackRangeEnd)
    if (!Number.isFinite(start.getTime()) || !Number.isFinite(end.getTime()) || start >= end) {
      setTrackMapError('请选择有效的开始和结束时间，且开始时间必须早于结束时间。')
      return
    }
    setTrackWindowHours(-1)
    setTrackMapImage('')
    setTrackMapError('')
    if (!trackFingerprint) return
    setBusy(`history-window-${trackFingerprint}`)
    try {
      setTrackHistory(await findClient.trackingHistory(
        trackFingerprint, 30000, 0, start.toISOString(), end.toISOString(), true,
      ))
      if (findTrackMapNeedsKey(provider) && trackMapKeys[provider].trim()) {
        const result = await findClient.trackingMap(
          trackFingerprint, provider, trackMapKeys[provider].trim(), 0,
          start.toISOString(), end.toISOString(), true,
        )
        setTrackMapImage(result.imageDataUrl)
      }
    } catch (error) {
      setTrackMapError(errorText(error))
    } finally {
      setBusy('')
    }
  }

  const viewTrackPoint = (point: FindTrackPoint) => {
    setReport({ found: true, ...point })
    setTrackFingerprint('')
    setMapOpen(true)
  }

  const disabled = Boolean(busy)
  const deletingIdentity = session?.identities.find(identity => identity.keyFingerprint === deleteFingerprint)
  const deviceCanFind = device.connection === 'connected' && device.settingsCapabilities.experimentalFind
  const broadcastStatus = findDeviceStatus({ connected: device.connection === 'connected',
    reporting: Boolean(device.findConfig && device.findCapabilities),
    supported: device.settingsCapabilities.experimentalFind, publicOnly: deviceSupportsDerivedIdentity,
    pro: device.activation.phase === 'active' && device.activation.edition === 'advanced',
    active: device.activation.phase !== 'unavailable', enabled: Boolean(device.findConfig?.enabled) })
  const deviceKeyMissingPrivate = Boolean(deviceHasFindIdentity && session && (!connectedIdentity || connectedIdentity.historyOnly))
  const selectedOtherIdentity = Boolean(deviceHasFindIdentity && connectedIdentity && selectedIdentity && connectedIdentity.deviceId !== selectedIdentity.deviceId)
  const trackIdentity = session?.identities.find((identity) => identity.keyFingerprint === trackFingerprint)
  const configureAllTracking = async (next: boolean) => {
    if (next && !await confirmation.confirm({ title: '开启全部',
      message: '开启每台设备当前身份的定期查询。历史身份可单独开启；本机需保持运行。', confirmLabel: '开启记录' })) return
    setBusy('tracking-all')
    try { applySession(await findClient.configureAllTracking(next)); setNotice({ kind: 'success', text: next ? '已开启当前身份的定期记录。' : '已暂停全部，已有轨迹保留。' }) }
    catch (error) { setNotice({ kind: 'error', text: errorText(error) }) } finally { setBusy('') }
  }
  const renderDeviceHeading = (identities: FindIdentity[]) => {
    const identity = identities[0], editing = editingFingerprint === identity.keyFingerprint
    return <>
      <div className="find-device-group-heading">
        <button type="button" className="find-device-group-title" disabled={disabled}
          aria-pressed={identity.selected} onClick={() => void selectSavedIdentity(identity)}>
          <strong>{findDeviceDisplayName(identities)}</strong><span>{identities.length} 个身份</span>
        </button>
        <div className="find-group-actions"><button className="button ghost find-group-link" disabled={disabled} onClick={() => void openTrackingHistory(identity)}><MapPinned />轨迹</button>
          <button className="button ghost find-group-link" disabled={disabled || editing} onClick={() => editFindIdentityNote(identity)}><Pencil />备注</button></div>
      </div>
      {editing && <form className="find-note-editor" onSubmit={event => { event.preventDefault(); void renameFindIdentity(identity.keyFingerprint, identity.findNote) }}>
        <label htmlFor="find-device-note">设备备注</label><input id="find-device-note" autoFocus maxLength={64} value={editingFindNote} onChange={event => setEditingFindNote(event.target.value)} disabled={disabled} />
        <button className="button primary" disabled={disabled || !editingFindNote.trim()}>保存</button><button className="button ghost" type="button" disabled={disabled} onClick={() => setEditingFingerprint('')}>取消</button>
      </form>}
    </>
  }
  const numberedTrackPoints = useMemo(() => orderOsmTrackPoints(trackHistory?.points || [])
    .map((point, index) => ({ point, number: index + 1 }))
    .reverse(), [trackHistory?.points])
  const trackMapKey = trackMapKeys[provider]
  const trackMapKeyLabel = findTrackMapKeyLabel(provider)
  const trackMapNeedsKey = findTrackMapNeedsKey(provider)
  const trackWindowLabel = trackWindowHours < 0
    ? `${new Date(trackRangeStart).toLocaleString()} 至 ${new Date(trackRangeEnd).toLocaleString()}`
    : trackWindowHours === 0 ? '全部保留记录' : trackWindowHours < 24 ? `最近 ${trackWindowHours} 小时` : `最近 ${trackWindowHours / 24} 天`
  const renderIdentityCard = (identity: FindIdentity, deviceDisplayName: string, deviceIdentities: FindIdentity[]) => {
    const distinctNote = distinctFindIdentityNote(identity, deviceDisplayName)
    const usage = findIdentityUsage(deviceIdentities, identity.keyFingerprint)
    const querySupported = identity.derivationVersion === 2 && !identity.historyOnly
    return <article className={identity.selected ? 'selected' : ''} key={identity.keyFingerprint}
      aria-busy={busy === `select-${identity.keyFingerprint}`}
      onClick={event => {
        if (!(event.target as Element).closest('button, a, input, select, textarea, summary, label'))
          void selectSavedIdentity(identity)
      }}>
      <button type="button" className="find-identity-list-main" disabled={disabled}
        aria-label={`选择 ${deviceDisplayName} 的${usage === 'current' ? '当前' : '历史'}身份`}
        aria-pressed={identity.selected} onClick={() => void selectSavedIdentity(identity)}>
        {distinctNote && <strong>{distinctNote}</strong>}
        <span className="find-identity-address"><small>MAC</small><span className="find-identity-address-value" title={identity.address}>{identity.address}</span><span className={`find-identity-usage ${usage}`}>{usage === 'current' ? '当前' : '历史'}</span>{identity.trackingEnabled && <span className="find-identity-usage tracking"><Tag aria-hidden="true" />定期查询</span>}</span>
        <span className="find-identity-seed"><small>指纹</small><code title={identity.keyFingerprint}>{compactFindFingerprint(identity.keyFingerprint)}</code></span>
      </button>
      <div className="find-identity-list-meta">
        {!querySupported && <span>{identity.historyOnly ? '仅历史轨迹 · 无查询密钥' : '旧版 · 仅本地历史'}</span>}
        <span title={identityTime(identity.lastQueriedAt)}>查询 {identityCardTime(identity.lastQueriedAt)}</span>
        <span title={identityTime(identity.lastReportAt, '尚无记录')}>最新记录 {identityCardTime(identity.lastReportAt, '尚无记录')}</span>
        <span>{identity.trackingEnabled ? `轨迹记录中 · ${identity.trackPointCount} 点` : `轨迹 ${identity.trackPointCount} 点`}</span>
        {identity.lastAutoError && <span className="error-text">{identity.lastAutoError}</span>}
      </div>
      <div className="find-identity-list-actions">
        <button className={identity.selected ? 'button primary' : 'button secondary'} type="button" disabled={disabled || !session?.queryReady || !querySupported} onClick={() => void query(identity)}>{busy === `query-${identity.keyFingerprint}` ? <LoaderCircle className="spin" /> : null}查询最新</button>
        <button className="button secondary" type="button" disabled={disabled || !session?.queryReady || !querySupported} title="查询此身份的近期位置" onClick={() => void queryDeviceWindow(identity)}>查询近期</button>
        <button className="button secondary" type="button" disabled={disabled || !querySupported} onClick={() => void configureFindTracking(identity, !identity.trackingEnabled)}>{identity.trackingEnabled ? '暂停记录' : '开启记录'}</button>
        <button className="button ghost danger-text" type="button" disabled={disabled} title="删除标签和轨迹" onClick={() => {
          if (usage === 'history' || identity.historyOnly) void confirmDeleteHistoricalIdentity(identity)
          else setDeleteFingerprint(identity.keyFingerprint)
        }}><Trash2 />删除</button>
      </div>
    </article>
  }
  return <div className="page find-page" ref={pageRef} tabIndex={-1}>
    <PageHeader title="InkTag Find" />

    <section className="find-panel find-device-config">
      <div className="find-panel-heading find-heading-toolbar"><b><Radio /></b><div><h2>Find 广播</h2><p>开启后，可在设备离线时查询位置。</p></div>
        <div className="find-group-actions">
          <button className="button secondary" type="button" disabled={disabled || !canReadDeviceFind} onClick={() => void refreshFind()}>
            <RefreshCw className={busy === 'find-read' ? 'spin' : undefined} aria-hidden="true" />
            {busy === 'find-read' ? '正在读取' : '重新读取'}
          </button>
          <button className="button secondary" disabled={disabled || device.connection !== 'connected' || (enabled && !connectedDeviceIdentities.length)} onClick={() => setTransferRequest({ mode: 'export', deviceId: connectedDeviceId })}>导出</button>
        </div>
      </div>
      <label className={`find-broadcast-switch${device.findConfig?.enabled ? ' enabled' : ''}`}>
        <span><strong>启用 Find 广播</strong><small>{device.findConfig?.enabled ? '已开启，连接设备时自动暂停' : '已关闭'}</small></span>
        <input
          type="checkbox"
          role="switch"
          aria-label="启用 Find 广播"
          checked={Boolean(device.findConfig?.enabled)}
          disabled={disabled || runtime.mode === 'checking' || Boolean(offlineFindAction) || !deviceCanFind || (!device.findConfig?.enabled && !broadcastStatus.canEnable)}
          onChange={(event) => void configureDeviceFind(event.target.checked)}
        />
      </label>
      <dl className="find-device-status">
        <div><dt>设备连接</dt><dd>{device.connection === 'connected' ? device.localAlias || device.deviceName || '已连接' : '未连接'}</dd></div>
        <div><dt>Find 状态</dt><dd>{broadcastStatus.label}</dd></div>
        <div><dt>登记状态</dt><dd>{!deviceHasFindIdentity ? '未登记' : !session ? '已在设备中登记' : !connectedIdentity ? '本机缺少此标签资料' : connectedIdentity.selected ? '已登记并选中' : '已登记'}</dd></div>
      </dl>
      <div className="find-device-actions">
        <button className="button secondary" type="button" disabled={disabled || device.connection !== 'connected' || !deviceHasFindIdentity || !supportsFindOwnerReplacement(device.findCapabilities)} onClick={() => void rotateDeviceFindIdentity()}>
          {busy === 'find-owner-rotation' ? <LoaderCircle className="spin" /> : <KeyRound />}
          {busy === 'find-owner-rotation' ? '正在替换身份' : '替换身份'}
        </button>
        <button className="button secondary" type="button" disabled={disabled || !canDeleteDeviceIdentity}
          title={!enabled ? '请在支持查询的本地 WebUI 中操作' : '删除设备上的 Find 身份，保留本机资料'}
          onClick={() => void deleteDeviceFindIdentity()}>
          {busy === 'find-owner-unregister' ? <LoaderCircle className="spin" /> : <Trash2 />}
          {busy === 'find-owner-unregister' ? '正在删除身份' : '删除身份'}
        </button>
      </div>
      {device.connection === 'connected' && deviceHasFindIdentity && !supportsFindOwnerReplacement(device.findCapabilities) && <p className="find-device-hint">当前固件尚未开放身份替换。</p>}
      {!enabled && <p className="find-device-hint">开启 Find 时，查询密钥会加密保存在当前浏览器。导出备份后，可导入支持查询的本地 WebUI 查看位置。</p>}
      {deviceKeyMissingPrivate && <div className="notice warning"><AlertTriangle /><div><strong>本机缺少查询私钥</strong><span>请导入此前保存的设备信息。替换或删除设备身份需要原身份资料，无法从设备找回私钥。</span></div></div>}
      {selectedOtherIdentity && <div className="notice warning"><AlertTriangle /><div><strong>当前选中了其他标签</strong><span>请在“已登记标签”中选择当前设备。</span></div></div>}
      <p className="find-device-note">关闭广播不会删除已登记标签和历史轨迹。</p>
    </section>

    {(runtime.mode === 'static' || runtime.mode === 'standalone') && <BrowserFindBackups onMessage={text => setNotice({ kind: 'success', text })} />}

    <FindTransferTools serverAvailable={enabled} identities={session?.identities || []} request={transferRequest} closeRequest={() => setTransferRequest(null)}
      onSession={applySession} offlineAction={offlineFindAction} closeOfflineAction={() => setOfflineFindAction(null)}
      onReplacementRestored={identity => void rotateDeviceFindIdentity(identity)}
      onMessage={text => setNotice({ kind: 'success', text })} />
    {deletingIdentity && !deletingIdentity.historyOnly && <FindDeleteGuard key={deletingIdentity.keyFingerprint}
      deviceId={deletingIdentity.deviceId} name={findIdentityDisplayName(deletingIdentity)} visible={!transferRequest}
      onBackup={() => setTransferRequest({ mode: 'export', deviceId: deletingIdentity.deviceId })}
      onUnregister={() => inktagClient.unregisterFindOwner(deletingIdentity.deviceId, deletingIdentity.keyFingerprint)}
      onDelete={() => deleteFindIdentity(deletingIdentity)} onCancel={() => setDeleteFingerprint('')} />}

    {!enabled ? <UnavailableFind mode={runtime.mode} localServer={runtime.mode === 'server'} /> : <>
      <section className="find-privacy notice warning">
        <AlertTriangle />
        <div><strong>隐私与账号说明</strong><span>本机服务直接向 Apple 登录和查询，不经过 InkTag 或公共 Anisette 服务器；密码不保存，标签私钥仅保存在本机。本工具不是 Apple 官方客户端，使用 Apple 非公开接口，服务可用性可能变化。建议使用专门注册的 Apple 账号，不要使用个人常用账号。</span></div>
      </section>

      <section className="find-panel find-identity-library">
        <div className="find-panel-heading find-heading-toolbar"><b><KeyRound /></b><div><h2>已登记标签</h2><p>历史身份默认暂停记录，可单独开启。</p></div>
          <div className="find-group-actions"><button className="button secondary" disabled={disabled || !session?.identities.some(e => !e.historyOnly && e.derivationVersion === 2)} onClick={() => void configureAllTracking(true)}>开启全部</button>
            <button className="button secondary" disabled={disabled || !session?.identities.some(e => e.trackingEnabled)} onClick={() => void configureAllTracking(false)}>暂停全部</button>
            <button className="button secondary" disabled={disabled || !session?.identities.length} onClick={() => setTransferRequest({ mode: 'export' })}>导出标签</button>
            <button className="button secondary" disabled={disabled} onClick={() => setTransferRequest({ mode: 'import' })}>导入标签</button></div>
        </div>
        <div className="find-identity-list">
          {device.connection === 'connected' && <div className="find-device-group current">
            {connectedGroup && renderDeviceHeading(connectedGroup.identities)}
            {connectedGroup?.identities.map((identity) => renderIdentityCard(identity, findDeviceDisplayName(connectedGroup.identities), connectedGroup.identities))}
            {!connectedGroup && <p className="find-empty-device">{device.findConfig?.identityPresent ? '本机缺少此标签的身份资料，请从备份恢复。' : '此设备尚未登记。'}</p>}
          </div>}
          {otherGroups.length > 0 && <details className="find-other-devices">
            <summary>其他标签（{otherGroups.length} 台）</summary>
            {otherGroups.map((group) => {
              const deviceDisplayName = findDeviceDisplayName(group.identities)
              return <div className="find-device-group" key={group.id}>
                {renderDeviceHeading(group.identities)}
                {group.identities.map((identity) => renderIdentityCard(identity, deviceDisplayName, group.identities))}
              </div>
            })}
          </details>}
        </div>
      </section>

      <section className="find-identity find-account-summary" aria-label="Find 账号状态">
        <div><span>Apple 查询账号</span><strong>{session?.appleAccountUsername || '--'}</strong></div>
        <div><span>查询状态</span><strong>{session?.loggedIn ? !session.queryReady ? '账号已验证 · 未取得查询权限' : session.sessionRestored ? '已加载查询会话' : '已取得查询权限' : session?.needs2fa ? '等待双重认证' : session?.sessionError ? '会话失效，请重新登录' : '未登录'}</strong></div>
      </section>

      {!session?.loggedIn && !session?.needs2fa && <section className="find-panel">
        <div className="find-panel-heading"><b>1</b><div><h2>登录查询账号</h2><p>登录后可查询已登记标签的位置。</p></div></div>
        <form onSubmit={login} autoComplete="off">
          <label>Apple 账号<input type="email" inputMode="email" autoComplete="off" value={email} onChange={(event) => setEmail(event.target.value)} required /></label>
          <label>密码<input type="password" autoComplete="new-password" value={password} onChange={(event) => setPassword(event.target.value)} required /></label>
          <label className="find-consent"><input type="checkbox" checked={consent} onChange={(event) => setConsent(event.target.checked)} required /><span>我已阅读隐私与账号说明，并了解本机将直接访问 Apple 的非公开接口，并承担可能的账号风险。</span></label>
          <button className="button primary" type="submit" disabled={disabled}>{busy === 'login' ? '正在登录…' : '登录'}</button>
        </form>
      </section>}

      {session?.needs2fa && <section className="find-panel">
        <div className="find-panel-heading"><b>2</b><div><h2>双重认证</h2><p>账号密码已验证，请选择验证码接收方式。</p></div></div>
        <label>验证方式<select value={methodIndex} onChange={(event) => setMethodIndex(Number(event.target.value))} disabled={disabled}>{session.methods.map((method) => <option key={method.index} value={method.index}>{method.label}</option>)}</select></label>
        {session.secondFactorWarning && <p>{session.secondFactorWarning}</p>}
        <div className="find-button-row"><button className="button secondary" type="button" onClick={() => void requestCode()} disabled={disabled}>发送验证码</button><button className="button ghost" type="button" onClick={() => void logout()} disabled={disabled}>更换账号</button></div>
        <form onSubmit={submitCode} autoComplete="off"><label>验证码<input type="text" inputMode="numeric" pattern="[0-9]{4,8}" maxLength={8} autoComplete="one-time-code" value={code} onChange={(event) => setCode(event.target.value)} required /></label><button className="button primary" type="submit" disabled={disabled}>完成登录</button></form>
      </section>}

      {session && (session.loggedIn || report) && <section className="find-panel">
        <div className="find-panel-heading"><b>3</b><div><h2>查询位置</h2><p>查询当前选中标签的最新位置。</p></div></div>
        {session.queryError && <p role="alert">{session.queryError}</p>}
        <div className="find-query-target"><span>查询标签</span>{selectedIdentity ? <><strong>{findIdentityDisplayName(selectedIdentity)}</strong><code title={selectedIdentity.keyFingerprint}>{compactFindFingerprint(selectedIdentity.keyFingerprint)}</code></> : <strong>尚未选择标签</strong>}</div>
        <div className="find-button-row"><button className="button primary" type="button" onClick={() => void query()} disabled={disabled || !session.queryReady || selectedIdentity?.derivationVersion !== 2 || selectedIdentity?.historyOnly}>{busy.startsWith('query-') ? '正在查询…' : '查询最新位置'}</button><button className="button secondary" type="button" onClick={() => selectedIdentity && void updateTrajectory(selectedIdentity)} disabled={disabled || !session.queryReady || selectedIdentity?.derivationVersion !== 2 || selectedIdentity?.historyOnly}><MapPinned />查看最新轨迹</button><button className="button secondary" type="button" onClick={() => void logout()} disabled={disabled}>退出并清除会话</button></div>
        {report && map && <>
          <dl className="find-report">
            <div><dt>报告时间</dt><dd>{new Date(report.timestamp).toLocaleString()}</dd></div>
            <div><dt>距现在</dt><dd>{formatFindReportAge(report.timestamp)}</dd></div>
            <div><dt>纬度</dt><dd>{report.latitude.toFixed(7)}</dd></div>
            <div><dt>经度</dt><dd>{report.longitude.toFixed(7)}</dd></div>
            <div><dt>水平精度</dt><dd>{report.horizontalAccuracy} m</dd></div>
            <div><dt>置信度 / 状态</dt><dd>{report.confidence} / 0x{report.status.toString(16).padStart(2, '0')}</dd></div>
          </dl>
          <section className="find-map-card">
            <div className="find-map-copy"><div><span>位置地图</span><h3>{map.coordinates}</h3><p>图钉不是精确门牌，请结合水平精度范围判断。</p></div><div className="find-map-actions"><button type="button" aria-pressed={provider === 'baidu'} onClick={() => selectMapProvider('baidu')}>百度地图</button><button type="button" aria-pressed={provider === 'amap'} onClick={() => selectMapProvider('amap')}>高德地图</button><button type="button" aria-pressed={provider === 'osm'} onClick={() => selectMapProvider('osm')}>OSMap</button></div></div>
            <div className="find-map-frame">
              {provider === 'osm'
                ? <OsmTrackMap key={`preview-osm-${mapRevision}`} points={[{ ...report, receivedAt: report.timestamp }]} />
                : <iframe ref={mapPreviewRef} tabIndex={-1} key={`preview-${provider}-${mapRevision}`} src={map.urls[provider]} title="Find 报告位置地图" referrerPolicy="strict-origin-when-cross-origin" sandbox="allow-scripts allow-same-origin allow-forms allow-popups allow-popups-to-escape-sandbox" />}
              <button className="find-map-expand" type="button" onClick={() => setMapOpen(true)} aria-label="放大地图" title="放大地图"><Maximize2 /></button>
              <div className="find-map-info"><strong>{map.deviceName} · {map.accuracyText}</strong><small>{map.address} · {map.reportTime}</small></div>
            </div>
            {mapRecovery}
            <p className="find-map-privacy">打开地图后，当前位置和图钉名称会发送给所选地图服务。</p>
          </section>
        </>}
      </section>}

    </>}

    {notice && <FeedbackNotice key={notice.text} className={`library-message ${notice.kind}`} role={notice.kind === 'error' ? 'alert' : 'status'}>{notice.text}</FeedbackNotice>}

    {trackIdentity && <div className="modal-backdrop" onMouseDown={(event) => { if (event.target === event.currentTarget) setTrackFingerprint('') }}>
      <section className="find-track-dialog" role="dialog" aria-modal="true" aria-labelledby="find-track-dialog-title">
        <header><div><span>设备全部身份的历史轨迹</span><h2 id="find-track-dialog-title">{findIdentityDisplayName(trackIdentity)}</h2><p>{trackHistory?.total || 0} 个位置点 · {trackWindowLabel} · 保留 {trackHistory?.retentionDays || FIND_TRACK_RETENTION_DAYS} 天</p></div><button className="icon-button" type="button" onClick={() => setTrackFingerprint('')} aria-label="关闭轨迹"><X /></button></header>
        <div className="find-track-toolbar">
          <label>定期查询间隔<select value={session?.trackingIntervalMinutes || 60} disabled={disabled} onChange={(event) => void changeTrackingInterval(trackIdentity, Number(event.target.value))}><option value={15}>15 分钟</option><option value={30}>30 分钟</option><option value={60}>1 小时</option><option value={180}>3 小时</option><option value={360}>6 小时</option></select></label>
          <button className={trackIdentity.trackingEnabled ? 'button secondary' : 'button primary'} type="button" disabled={disabled || trackIdentity.derivationVersion !== 2 || trackIdentity.historyOnly} onClick={() => void configureFindTracking(trackIdentity, !trackIdentity.trackingEnabled)}>{trackIdentity.trackingEnabled ? '停止当前身份记录' : '开启当前身份记录'}</button>
          <button className="button ghost danger-text" type="button" disabled={disabled || !trackHistory?.total} onClick={() => void clearTrackingHistory(trackIdentity)}>清除轨迹</button>
          <div className="find-track-view-switch" aria-label="轨迹显示方式">
            <button type="button" aria-pressed={trackView === 'plot'} onClick={() => setTrackView('plot')}>轨迹图</button>
            <button type="button" aria-pressed={trackView === 'list'} onClick={() => setTrackView('list')}>记录列表</button>
          </div>
        </div>
        <div className="find-track-window" aria-label="轨迹时间范围">
          <label>开始时间<input type="datetime-local" value={trackRangeStart} max={trackRangeEnd} disabled={disabled} onChange={(event) => { setTrackRangeStart(event.target.value); setTrackWindowHours(-1) }} /></label>
          <label>结束时间<input type="datetime-local" value={trackRangeEnd} min={trackRangeStart} disabled={disabled} onChange={(event) => { setTrackRangeEnd(event.target.value); setTrackWindowHours(-1) }} /></label>
          <button className="button secondary" type="button" disabled={disabled} onClick={() => void applyCustomTrackWindow()}>应用范围</button>
          <div className="find-track-shortcuts"><span>快捷范围</span>{[[1, '1 小时'], [2, '2 小时'], [6, '6 小时'], [12, '12 小时'], [24, '1 天'], [48, '2 天'], [0, '全部']].map(([hours, label]) => <button key={hours} type="button" aria-pressed={trackWindowHours === hours} disabled={disabled} onClick={() => void changeTrackWindow(hours as number)}>{label}</button>)}</div>
        </div>
        <p className="find-track-note">当前展示{trackWindowLabel}，历史位置最多保留 90 天。</p>
        {trackView === 'plot' && <div className="find-track-map-config">
          <div className="find-map-actions" aria-label="轨迹地图服务"><button type="button" aria-pressed={provider === 'baidu'} onClick={() => selectMapProvider('baidu')}>百度地图</button><button type="button" aria-pressed={provider === 'amap'} onClick={() => selectMapProvider('amap')}>高德地图</button><button type="button" aria-pressed={provider === 'osm'} onClick={() => selectMapProvider('osm')}>OSMap</button></div>
          {trackMapNeedsKey ? <label>{trackMapKeyLabel}<input type="password" autoComplete="off" maxLength={256} value={trackMapKey} placeholder={'请输入 ' + trackMapKeyLabel} onChange={(event) => { setPreferences({ findMapKeys: { ...trackMapKeys, [provider]: event.target.value } }); setTrackMapImage(''); setTrackMapError('') }} /></label> : <span className="find-track-keyless">免 API Key · OSMap</span>}
          {trackMapNeedsKey && trackMapKey.trim() && <button className="button primary" type="button" disabled={disabled || !trackHistory?.total} onClick={() => void loadTrackMap(trackIdentity.keyFingerprint, provider, trackMapKey)}>绘制轨迹</button>}
        </div>}
        {trackView === 'plot' ? <div className="find-track-plot-shell">
          {!trackHistory && <p className="find-track-empty">正在读取历史轨迹…</p>}
          {trackHistory && trackHistory.points.length === 0 && <p className="find-track-empty">{trackWindowLabel}还没有轨迹点。启用定期记录后，后台会按设定间隔保存查询结果。</p>}
          {trackHistory && trackHistory.points.length > 0 && <>
            {trackMapNeedsKey && !trackMapKey.trim() && <div className="find-map-fallback" role="status"><AlertTriangle aria-hidden="true" /><p>未设置<strong>{trackMapKeyLabel}</strong>，轨迹暂用 <strong>OSMap</strong>。可在上方填写。</p></div>}
            {(!trackMapNeedsKey || !trackMapKey.trim()) && <div className="find-track-map-frame"><OsmTrackMap points={trackHistory.points} /></div>}
            {trackMapNeedsKey && trackMapKey.trim() && trackMapImage && <div className="find-track-map-frame"><img src={trackMapImage} alt={`${findMapProviderName(provider)}地图中的${trackWindowLabel}轨迹`} />{trackMapBusy && <div className="find-track-map-loading"><LoaderCircle className="spin" />正在绘制轨迹地图…</div>}</div>}
            {trackMapNeedsKey && trackMapKey.trim() && !trackMapImage && !trackMapBusy && !trackMapError && <p className="find-track-empty">点击“绘制轨迹”，通过 {findMapProviderName(provider)} 显示{trackWindowLabel}的历史点。</p>}
            {trackMapBusy && !trackMapImage && <p className="find-track-empty"><LoaderCircle className="spin" />正在绘制轨迹地图…</p>}
            {trackMapError && <p className="find-track-map-error">{trackMapError}</p>}
          </>}
        </div> : <div className="find-track-list">
          {!trackHistory && <p>正在读取历史记录…</p>}
          {trackHistory && trackHistory.points.length === 0 && <p>{trackWindowLabel}还没有轨迹点。</p>}
          {numberedTrackPoints.map(({ point, number }) => <article key={`${point.timestamp}-${point.latitude}-${point.longitude}`}><b className="find-track-list-index" aria-label={`轨迹点 ${number}`}>{number}</b><div><strong>{new Date(point.timestamp).toLocaleString()}</strong><span>{point.latitude.toFixed(7)}, {point.longitude.toFixed(7)} · 精度约 {point.horizontalAccuracy} m</span></div><button className="button ghost" type="button" onClick={() => viewTrackPoint(point)}>查看位置</button></article>)}
        </div>}
      </section>
    </div>}

    {mapOpen && map && <div className="modal-backdrop" onMouseDown={(event) => { if (event.target === event.currentTarget) setMapOpen(false) }}>
      <section className="find-map-dialog" role="dialog" aria-modal="true" aria-labelledby="find-map-dialog-title">
        <header><div><span>位置详情</span><h2 id="find-map-dialog-title">{map.deviceName}</h2><p>{map.reportTime} · {map.accuracyText}</p></div><button className="icon-button" type="button" onClick={() => setMapOpen(false)} aria-label="关闭地图"><X /></button></header>
        <div className="find-map-toolbar"><div className="find-map-actions"><button type="button" aria-pressed={provider === 'baidu'} onClick={() => selectMapProvider('baidu')}>百度地图</button><button type="button" aria-pressed={provider === 'amap'} onClick={() => selectMapProvider('amap')}>高德地图</button><button type="button" aria-pressed={provider === 'osm'} onClick={() => selectMapProvider('osm')}>OSMap</button></div>{mapRecovery}<strong>{map.accuracyText}</strong></div>
        <div className="find-map-dialog-frame">{provider === 'osm' ? <OsmTrackMap key={`dialog-osm-${mapRevision}`} points={[{ ...report!, receivedAt: report!.timestamp }]} /> : <iframe key={`dialog-${provider}-${mapRevision}`} src={map.urls[provider]} title="放大的 Find 报告位置地图" referrerPolicy="strict-origin-when-cross-origin" sandbox="allow-scripts allow-same-origin allow-forms allow-popups allow-popups-to-escape-sandbox" />}<div className="find-map-info"><strong>{map.deviceName} · {map.accuracyText}</strong><small>{map.address} · {map.reportTime}</small></div></div>
        <dl><div><dt>Find 地址</dt><dd>{map.address}</dd></div><div><dt>报告时间</dt><dd>{map.reportTime}</dd></div><div><dt>报告坐标</dt><dd>{map.coordinates}</dd></div></dl>
      </section>
    </div>}
    {confirmation.dialog}
  </div>
}
