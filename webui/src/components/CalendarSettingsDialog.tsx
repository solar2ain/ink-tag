import { CalendarCog, Save, X } from 'lucide-react'
import { useEffect, useMemo, useRef, useState } from 'react'
import { updateDevicePreference } from '../data/devicePreferences'
import { editableGuiFontPackage, rememberGuiAssetPackage } from '../data/guiAssetPackages'
import { calendarCountdownNameIssue } from '../dashboard/calendarSettings'
import { addCatalogGlyphs, catalogMissingGlyphs, missingChineseFontGlyphs } from '../device-gui/fontEditor'
import { inktagClient } from '../protocol/client'
import { defaultGuiFonts, SINGLE_USER_FONT_WARNING } from '../protocol/guiAssets'
import { buildFontRoles, fontInfoLimit, nextFontGeneration } from '../protocol/fontRoles'
import { calendarDateToTargetDay } from '../protocol/deviceSettings'
import { useDeviceStore } from '../store/device'
import { useConfirmationDialog } from './ConfirmationDialog'

interface CalendarSettingsDialogProps {
  deviceId: string
  countdownSupported: boolean
  onClose: () => void
}

export function CalendarSettingsDialog({ deviceId, countdownSupported, onClose }: CalendarSettingsDialogProps) {
  const [weekStart, setWeekStart] = useState<0 | 1>(1)
  const [countdownName, setCountdownName] = useState('')
  const [countdownDate, setCountdownDate] = useState('')
  const [loading, setLoading] = useState(true)
  const [saving, setSaving] = useState(false)
  const [message, setMessage] = useState('')
  const [fontState, setFontState] = useState<Awaited<ReturnType<typeof editableGuiFontPackage>> | null>(null)
  const [fontError, setFontError] = useState('')
  const [unavailableGlyphs, setUnavailableGlyphs] = useState<string[]>([])
  const [catalogError, setCatalogError] = useState('')
  const pending = useRef(false)
  const confirmation = useConfirmationDialog()
  const guiAssetUpdate = useDeviceStore((state) => state.settingsCapabilities.guiAssetUpdate)
  const activation = useDeviceStore((state) => state.activation)
  const pro = activation.phase === 'active' && activation.edition === 'advanced'
  const nameIssue = calendarCountdownNameIssue(countdownName)
  const countdownIncomplete = Boolean(countdownName.trim()) !== Boolean(countdownDate)
  const invalidDate = Boolean(countdownDate) && calendarDateToTargetDay(countdownDate) <= 0
  const invalid = Boolean(nameIssue) || countdownIncomplete || invalidDate
  const missingGlyphs = useMemo(() => missingChineseFontGlyphs(fontState?.fonts ?? defaultGuiFonts(), countdownName), [fontState, countdownName])

  useEffect(() => {
    let active = true
    setUnavailableGlyphs([]);setCatalogError('')
    if (!missingGlyphs.length || fontState?.chineseSource !== 'catalog') return () => { active = false }
    void catalogMissingGlyphs(missingGlyphs.join(''))
      .then((value) => { if (active) setUnavailableGlyphs(value) })
      .catch(() => { if (active) setCatalogError('字形资源加载失败，可点击保存重试检查。') })
    return () => { active = false }
  }, [missingGlyphs.join(''),fontState?.chineseSource])

  useEffect(() => {
    let active = true
    inktagClient.getDisplaySettings()
      .then((display) => {
        if (!display.complete) throw new Error('当前固件不支持设备端日历配置，请先更新固件。')
        if (!active) return
        setWeekStart(display.weekStart)
        setCountdownName(display.countdownName)
        setCountdownDate(display.countdownDate)
      })
      .catch((error) => { if (active) setMessage(error instanceof Error ? error.message : String(error)) })
      .finally(() => { if (active) setLoading(false) })
    return () => { active = false }
  }, [deviceId])

  useEffect(() => {
    let active = true
    setFontState(null);setFontError('')
    if (!guiAssetUpdate) {
      setFontState({fonts:defaultGuiFonts(),chineseSource:'catalog'})
      return () => { active = false }
    }
    void inktagClient.readGuiAssetInfo().then(async (info) => {
      const current = await editableGuiFontPackage(deviceId, info)
      if (active) setFontState(current)
    }).catch((error) => {
      if (active) setFontError(error instanceof Error ? error.message : String(error))
    })
    return () => { active = false }
  }, [deviceId, guiAssetUpdate])

  useEffect(() => {
    const closeOnEscape = (event: KeyboardEvent) => { if (event.key === 'Escape' && !pending.current) onClose() }
    window.addEventListener('keydown', closeOnEscape)
    return () => window.removeEventListener('keydown', closeOnEscape)
  }, [onClose])

  const save = async () => {
    if (invalid || pending.current) return
    pending.current = true
    setSaving(true)
    setMessage('')
    try {
      const checkDevice = () => {
        const current=useDeviceStore.getState()
        if(current.connection!=='connected' || current.deviceId!==deviceId) throw new Error('设备连接已变化，请重新打开日历设置。')
        if(current.action!=='idle') throw new Error('设备正在执行其他操作，请稍后重试。')
      }
      checkDevice()
      const name = countdownName.trim()
      const date = countdownDate
      if (name) {
        // Refresh INFO for this save: another client may have changed the font
        // since the dialog opened. Never extend a stale/default guess.
        const info = guiAssetUpdate ? await inktagClient.readGuiAssetInfo() : null
        const current = info ? await editableGuiFontPackage(deviceId,info) : {fonts:defaultGuiFonts(),chineseSource:'catalog' as const}
        const fonts = current.fonts
        const missing = missingChineseFontGlyphs(fonts, name)
        if (missing.length) {
          if (!guiAssetUpdate) throw new Error(`设备字库缺少“${missing.join('')}”，当前固件不支持在线补字。`)
          if (!pro) throw new Error(`设备字库缺少“${missing.join('')}”。自定义字库属于 Pro 功能，请改用已有文字。`)
          if (current.chineseSource !== 'catalog') throw new Error(`设备字库缺少“${missing.join('')}”。请在设备字体中使用原字形源补字，不会覆盖现有自定义字体。`)
          const unavailable = await catalogMissingGlyphs(missing.join(''))
          if (unavailable.length) throw new Error(`官方字形源暂不包含：${unavailable.join('、')}`)
          const extended = await addCatalogGlyphs(fonts, missing.join(''))
          if (!info) throw new Error('无法确认设备字库容量，未开始写入')
          const pkg = buildFontRoles(extended.slice(0,4), {}, nextFontGeneration(info.generation), fontInfoLimit(info), extended.length===6 ? extended.slice(4) : undefined)
          checkDevice()
          if (!await confirmation.confirm({title:'补充倒计时字库',confirmLabel:'补字并上传',cancelLabel:'暂不上传',
            message:`倒计时缺少“${missing.join('、')}”，将补入大小两套中文字库并上传（${(pkg.totalSize/1024).toFixed(1)} KiB），保留已有字形和数字。\n\n${SINGLE_USER_FONT_WARNING}`})) return
          checkDevice()
          setMessage(`正在补充字形 ${missing.join('、')}…`)
          await inktagClient.writeGuiAssetPackage(pkg, (percent) => setMessage(`正在更新设备字库 ${Math.round(percent)}%…`), { confirmSingleUser: true })
          await rememberGuiAssetPackage(deviceId, pkg, 'catalog')
          setFontState({fonts:extended,chineseSource:'catalog'})
        }
      }
      checkDevice()
      setMessage('正在保存配置并刷新日历…')
      await inktagClient.setDisplayConfiguration(weekStart, name, date)
      await updateDevicePreference(deviceId, {
        calendarWeekStart: weekStart,
        calendarCountdownName: name,
        calendarCountdownDate: date,
      })
      checkDevice()
      useDeviceStore.getState().addLog('日历与时间页配置已保存到设备，正在刷新日历。')
      await inktagClient.runAction('calendar')
      onClose()
    } catch (error) {
      setMessage(error instanceof Error ? error.message : String(error))
    } finally {
      pending.current = false
      setSaving(false)
    }
  }

  const coverageHint = !countdownName.trim() ? '' : fontError || (!fontState ? '保存时将检查实际字库；缺字时确认后补充并上传。' :
    !missingGlyphs.length ? '' : !guiAssetUpdate ? `设备字库缺少“${missingGlyphs.join('')}”，当前固件不支持在线补字。` :
    !pro ? `设备字库缺少“${missingGlyphs.join('')}”，补字需要 Pro 激活。` :
    fontState.chineseSource !== 'catalog' ? `设备字库缺少“${missingGlyphs.join('')}”，需使用原字形源补充。` :
    catalogError || (unavailableGlyphs.length ? `官方字形源缺少：${unavailableGlyphs.join('、')}。` :
      `设备字库缺少“${missingGlyphs.join('')}”；保存时将请求确认，补充并上传后再保存倒计时。`))

  return <><div className="modal-backdrop" aria-hidden={confirmation.dialog ? true : undefined} onMouseDown={(event) => { if (event.target === event.currentTarget && !pending.current) onClose() }}>
    <section className="calendar-settings-dialog" role="dialog" aria-modal="true" aria-labelledby="calendar-settings-title">
      <header><div><CalendarCog /><div><h2 id="calendar-settings-title">日历设置</h2><p>设置星期顺序和大屏右上角的日期倒计时。</p></div></div><button className="icon-button" title="关闭" aria-label="关闭" disabled={saving} onClick={onClose}><X /></button></header>
      <div className="calendar-settings-fields">
        <fieldset disabled={loading || saving}><legend>每周第一天</legend><div className="segmented-control"><button type="button" className={weekStart === 0 ? 'active' : ''} onClick={() => setWeekStart(0)}>周日</button><button type="button" className={weekStart === 1 ? 'active' : ''} onClick={() => setWeekStart(1)}>周一</button></div></fieldset>
        {countdownSupported ? <fieldset disabled={loading || saving}><legend>目标倒计时（可留空）</legend><label>名称<input maxLength={4} value={countdownName} placeholder="例如：国庆" onChange={(event) => setCountdownName(event.target.value)} /></label><label>目标日期<input type="date" value={countdownDate} onChange={(event) => setCountdownDate(event.target.value)} /></label><small className={invalid || unavailableGlyphs.length > 0 ? 'input-error' : ''}>{nameIssue || (countdownIncomplete ? '名称和目标日期需要同时填写，或同时留空。' : invalidDate ? '请选择有效的目标日期。' : coverageHint || '名称最多 4 个字，保存到设备后同时用于日历页和时间页。')}</small></fieldset> : <div className="calendar-countdown-unavailable"><strong>目标倒计时</strong><span>仅在 400×300（4.2 寸）及更大布局中显示。</span></div>}
        {message && <p className="form-message error" role="alert">{message}</p>}
      </div>
      <footer><button className="button secondary" disabled={saving} onClick={onClose}>取消</button><button className="button primary" disabled={loading || saving || invalid} onClick={() => void save()}><Save />{saving ? '保存并刷新中' : '保存并刷新日历'}</button></footer>
    </section>
  </div>{confirmation.dialog}</>
}
