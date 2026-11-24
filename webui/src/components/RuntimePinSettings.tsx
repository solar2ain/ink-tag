import { FeedbackNotice, useFeedbackMessage } from './FeedbackNotice'
import { useEffect, useRef, useState } from 'react'
import { Save } from 'lucide-react'
import { inktagClient } from '../protocol/client'
import { activeLowMask, encodePinBlock, gpioLabel, pinRoles, PinWriteUnconfirmedError, type PinConfig, type PinReport, type PinRole } from '../protocol/pinConfig'
import { useDeviceStore } from '../store/device'
import { useConfirmationDialog } from './ConfirmationDialog'

const groups = [
  { title: '屏幕引脚', roles: pinRoles.slice(0, 7) },
  { title: 'LED 引脚', roles: pinRoles.slice(7, 10) },
] as const
const labels: Record<PinRole, string> = { mosi: 'MOSI', clock: 'SCLK', cs: 'CS', dc: 'DC', reset: 'RESET', busy: 'BUSY', enable: '供电 EN', red: '红', green: '绿', blue: '蓝', txd: '', rxd: '' }

export function RuntimePinSettings() {
  const { connection, deviceId, boardId, settingsCapabilities, reportedPinConfig, ioWriteUnconfirmed } = useDeviceStore()
  const connected = connection === 'connected'
  const report = connected && settingsCapabilities.runtimePinConfig ? reportedPinConfig : null
  const [form, setForm] = useState<PinConfig | null>(null)
  const [saving, setSaving] = useState(false)
  const [localUncertain, setUncertain] = useState(false)
  const uncertain = localUncertain || ioWriteUnconfirmed
  const [message, setMessage, messageRevision] = useFeedbackMessage('')
  const baseline = useRef<PinReport | null>(null)
  const owner = useRef('')
  const confirmation = useConfirmationDialog()
  useEffect(() => {
    const nextOwner = connected ? deviceId : ''
    const previous = nextOwner && nextOwner === owner.current ? baseline.current : null
    if (nextOwner !== owner.current) { setUncertain(false); setMessage('') }
    owner.current = nextOwner
    setForm(draft => {
      if (!report) return null
      const next: PinConfig = { pins: { ...report.effective.pins }, activeLow: report.effective.activeLow }
      if (draft && previous) for (const [i, role] of pinRoles.entries()) {
        if (draft.pins[role] !== previous.effective.pins[role]) next.pins[role] = draft.pins[role]
        const bit = 1 << i
        if ((draft.activeLow ^ previous.effective.activeLow) & bit) next.activeLow = (next.activeLow & ~bit) | (draft.activeLow & bit)
      }
      return next
    })
    baseline.current = report
  }, [connected, deviceId, report])

  const save = async (roles: readonly PinRole[], title: string) => {
    if (!report?.defaultValid || !form || saving || uncertain) return
    const target = deviceId
    setSaving(true)
    const sameDevice = () => useDeviceStore.getState().connection === 'connected' && useDeviceStore.getState().deviceId === target
    try {
      if (!await confirmation.confirm({ title: `保存${title}`, message: <><strong>请核对实际接线。</strong>仅保存本区域，其他引脚和屏幕 BUSY 有效电平保持设备当前值；保护脚、外设冲突和忙状态仍由固件校验。</>, confirmLabel: '保存到设备', danger: true })) return
      if (!target || !sameDevice()) throw Error('设备已变化，未发送引脚写入')
      const fresh = await inktagClient.getRuntimePinConfig()
      if (!sameDevice()) throw Error('读取期间设备已变化，未发送引脚写入')
      const next: PinConfig = { pins: { ...fresh.effective.pins }, activeLow: fresh.effective.activeLow }
      for (const role of roles) {
        const bit = 1 << pinRoles.indexOf(role)
        next.pins[role] = form.pins[role]
        if (role !== 'busy') next.activeLow = (next.activeLow & ~bit) | (form.activeLow & bit)
        if (next.pins[role] === 255) next.activeLow &= ~bit
      }
      encodePinBlock(next)
      await inktagClient.setRuntimePinConfig(next)
      if (sameDevice()) setMessage(`设备已确认${title}。`)
    } catch (error) {
      if (sameDevice()) {
        if (error instanceof PinWriteUnconfirmedError) setUncertain(true)
        setMessage(error instanceof Error ? error.message : String(error))
      }
    } finally { setSaving(false) }
  }

  let error = ''
  if (form) try { encodePinBlock(form) } catch (cause) { error = cause instanceof Error ? cause.message : String(cause) }
  return <>
    {groups.map(group => {
      const dirty = Boolean(form && report && group.roles.some(role => form.pins[role] !== report.effective.pins[role] ||
        (role !== 'busy' && ((form.activeLow ^ report.effective.activeLow) & (1 << pinRoles.indexOf(role))))))
      return <section className="board-pin-section" key={group.title} aria-label={group.title}>
        <div className="panel-title"><div><h2>{group.title}</h2><p>{!connected ? '连接后自动读回。' : !settingsCapabilities.runtimePinConfig ? '当前固件未提供引脚读回。' : report ? !report.editableRoles ? '设备当前有效引脚（只读）。' : '设备读回值；修改后保存到设备。' : '尚未读回，请重新连接或刷新设备状态。'}</p></div>
          <button className="button primary" disabled={!connected || !report?.defaultValid || !form || saving || uncertain || !dirty || Boolean(error)} onClick={() => void save(group.roles, group.title)}><Save />保存{group.title}</button></div>
        <div className={`pin-grid${group.roles.length === 3 ? ' pin-grid-led' : ''}`}>{group.roles.map(role => {
          const index = pinRoles.indexOf(role), editable = Boolean(report?.editableRoles && (report.editableRoles & (1 << index)))
          const pin = form?.pins[role]
          return <div className="pin-field" key={role}><label htmlFor={`board-pin-${role}`}>{labels[role]}</label>
            <input id={`board-pin-${role}`} aria-label={`${group.title} ${labels[role]}`} type="number" min="0" max="254" placeholder={report ? '未连接' : '--'} value={pin === undefined || pin === 255 || Number.isNaN(pin) ? '' : pin}
              disabled={!editable || !report?.defaultValid || saving || uncertain} onChange={event => {
                if (!form) return
                const value = event.target.value === '' ? 255 : Number(event.target.value)
                setForm({ pins: { ...form.pins, [role]: value }, activeLow: value === 255 ? form.activeLow & ~(1 << index) : form.activeLow })
              }} />
            <small>当前：{report ? gpioLabel(report.effective.pins[role], boardId) : '--'}</small>
            {role !== 'busy' && Boolean(activeLowMask & (1 << index)) && <label className="pin-polarity"><input type="checkbox" aria-label={`${group.title} ${labels[role]}低有效`} checked={Boolean(form && (form.activeLow & (1 << index)))} disabled={!editable || !report?.defaultValid || saving || uncertain || pin === 255} onChange={event => { if (form) setForm({ ...form, activeLow: event.target.checked ? form.activeLow | (1 << index) : form.activeLow & ~(1 << index) }) }} />低有效</label>}
          </div>
        })}</div>
      </section>
    })}
    {error && <p className="notice warning" role="alert">{error}</p>}
    {(message || uncertain) && <FeedbackNotice noticeKey={messageRevision} persistent={uncertain} className={`library-message${uncertain ? ' error' : ''}`}>{message || '上次引脚写入结果未确认。'}{uncertain ? ' 请重新连接核对，不要重复写入。' : ''}</FeedbackNotice>}
    {confirmation.dialog}
  </>
}
