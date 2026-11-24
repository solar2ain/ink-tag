import { FeedbackNotice, useFeedbackMessage } from './FeedbackNotice'
import { useEffect, useRef, useState } from 'react'
import { Save } from 'lucide-react'
import { useDeviceStore } from '../store/device'
import { inktagClient } from '../protocol/client'
import { emptyAuxIoConfiguration, encodeAuxIoConfiguration, unusedButton, type AuxIoConfiguration } from '../protocol/auxIo'
import { PinWriteUnconfirmedError } from '../protocol/pinConfig'
import type { ButtonPull } from '../types'
import { useConfirmationDialog } from './ConfirmationDialog'

export function AuxIoSettings() {
  const { connection, deviceId, settingsCapabilities, reportedAuxIoConfiguration, ioWriteUnconfirmed } = useDeviceStore()
  const connected = connection === 'connected'
  const writable = !connected || settingsCapabilities.customButtonGpio
  const [form, setForm] = useState(emptyAuxIoConfiguration)
  const [saving, setSaving] = useState(false)
  const [localUncertain, setUncertain] = useState(false)
  const uncertain = localUncertain || ioWriteUnconfirmed
  const [message, setMessage, messageRevision] = useFeedbackMessage('')
  const baseline = useRef<AuxIoConfiguration | null>(null)
  const owner = useRef('')
  const confirmation = useConfirmationDialog()
  const report = connected ? reportedAuxIoConfiguration : null
  useEffect(() => {
    const id = connected ? deviceId : ''
    const previous = id && id === owner.current ? baseline.current : null
    if (id !== owner.current) { setMessage(''); setUncertain(false) }
    if (report) setForm(draft => previous && JSON.stringify(draft) !== JSON.stringify(previous) ? draft : report)
    else if (id !== owner.current || !connected) setForm(emptyAuxIoConfiguration())
    baseline.current = report; owner.current = id
  }, [connected, deviceId, report])
  let error = ''
  try { encodeAuxIoConfiguration(form) } catch (cause) { error = cause instanceof Error ? cause.message : String(cause) }
  const dirty = Boolean(report && JSON.stringify(form) !== JSON.stringify(report))
  const updateButton = (index: number, patch: Partial<AuxIoConfiguration['buttons'][number]>) =>
    setForm(current => ({ ...current, buttons: current.buttons.map((button, i) => i === index ? { ...button, ...patch } : button) }))
  const number = (raw: string) => raw === '' ? Number.NaN : Number(raw)
  const save = async () => {
    if (!report || !writable || saving || uncertain || error) return
    const target = deviceId
    const sameDevice = () => useDeviceStore.getState().connection === 'connected' && useDeviceStore.getState().deviceId === target
    setSaving(true)
    try {
      if (!await confirmation.confirm({ title: '保存按键配置', message: <><strong>请核对按键接线。</strong>固件会校验保护引脚及 GPIO 冲突。</>, confirmLabel: '保存到设备', danger: true })) return
      if (!target || !sameDevice()) throw Error('设备已变化，未发送配置')
      const applied = await inktagClient.setAuxIoConfiguration(form)
      if (sameDevice()) { setForm(applied); setMessage('设备已确认按键配置。') }
    } catch (cause) {
      if (sameDevice()) {
        if (cause instanceof PinWriteUnconfirmedError) setUncertain(true)
        setMessage(cause instanceof Error ? cause.message : String(cause))
      }
    } finally { setSaving(false) }
  }
  const editable = connected && Boolean(report) && writable && !saving && !uncertain
  const buttonEditable = editable && settingsCapabilities.customButtonGpio
  return <section className="board-aux-section">
    <div className="panel-title"><div><h2>实体按键</h2><p>{!connected ? "连接后自动读回按键引脚。" : writable ? "设备读回值；修改后保存到设备。" : "当前固件暂未支持。"}</p></div>
      <button className="button primary" disabled={!connected || !report || !writable || saving || uncertain || !dirty || Boolean(error)} onClick={() => void save()}><Save />保存按键配置</button></div>
    <div className="aux-io-grid">
      <fieldset aria-label="实体按键引脚">
        <p className="aux-button-help">引脚配置保存在设备上，触发动作在<a href="#/device?tab=behavior">交互页</a>设置。</p>
        <div className="aux-buttons-grid">{form.buttons.slice(0, 2).map((button, index) => <div className="aux-button-row" key={index}>
          <div className="aux-button-heading"><strong>按键 {index + 1}</strong></div>
          <div className="aux-control-grid"><label>GPIO<input aria-label={`按键 ${index + 1} GPIO`} type="number" min="0" max="254" placeholder="未配置" value={button.gpio === 255 || Number.isNaN(button.gpio) ? '' : button.gpio} disabled={!buttonEditable} onChange={event => updateButton(index, event.target.value === '' ? unusedButton() : { gpio: number(event.target.value), ...(button.gpio === 255 ? { activeLow: true, pull: 'up' as const } : {}) })} /></label>
            <label>有效电平<select aria-label={`按键 ${index + 1} 有效电平`} value={button.gpio === 255 ? '' : button.activeLow ? 'low' : 'high'} disabled={!buttonEditable || button.gpio === 255} onChange={event => updateButton(index, { activeLow: event.target.value === 'low' })}><option value="" disabled>--</option><option value="low">低电平有效</option><option value="high">高电平有效</option></select></label>
            <label>内部上下拉<select aria-label={`按键 ${index + 1} 上下拉`} value={button.gpio === 255 ? '' : button.pull} disabled={!buttonEditable || button.gpio === 255} onChange={event => updateButton(index, { pull: event.target.value as ButtonPull })}><option value="" disabled>--</option><option value="up">上拉</option><option value="down">下拉</option><option value="none">无</option></select></label></div>
        </div>)}</div>
        {report && report.buttons[2].gpio !== 255 && <p className="field-note">设备已有的额外按键配置保持不变。</p>}
      </fieldset>
    </div>
    {error && <p className="notice warning" role="alert">{error}</p>}
    {(message || uncertain) && <FeedbackNotice noticeKey={messageRevision} persistent={uncertain} className={`library-message${uncertain ? ' error' : ''}`}>{message || '上次引脚写入结果未确认。'}{uncertain ? ' 请重新连接核对，不要重复写入。' : ''}</FeedbackNotice>}
    {confirmation.dialog}
  </section>
}
