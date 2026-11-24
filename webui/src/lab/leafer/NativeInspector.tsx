import { locked, type EditorCore, type NativeControl } from '../../../../packages/inktag-editor-core/src/index'
import { NativeReferenceControls } from '../../components/NativeReferenceControls'
import { deviceGuiFontOptions, type DeviceGuiFontId } from '../../device-gui/model'
import { missingDeviceGlyphs } from '../../device-gui/fontRenderer'
import { nativeSettings, reference, refreshNativeWidget } from './nativeBridge'
import { deviceBatteryPercent } from '../../device-gui/deviceGuiComponents'

export function NativeInspector({ core, act }: { core: EditorCore; act: (fn: () => unknown) => void }) {
  const w = core.selected!, n = w.native!
  const patch = (value: Partial<NativeControl>) => act(() => core.change(() => { w.native = { ...w.native!, ...value }; refreshNativeWidget(w,core.doc) }))
  if (locked(w)) return <p className="note">InkTag base 是固定设备标识，不可移动、缩放或删除；这里只展示占位。</p>
  if(n.key === 'battery:0') return <section className="inspector-section"><h3>电池图标</h3>
    <label>预览电压（mV）<input aria-label="电池图标预览电压" type="number" min={0} max={6000} step={10} value={n.sample?.voltage??3000} onChange={event => {const voltage=Number(event.target.value);if(event.target.value!==''&&voltage>=0&&voltage<=6000)patch({sample:{...n.sample,voltage}})}} /></label>
    <p>预览电量 {deviceBatteryPercent(n.sample?.voltage??3000)}%。仅修改此图标的样本，不读取或写入设备；发送后为静态图片。</p>
  </section>
  const base = reference(n.page, core.doc.width, core.doc.height)
  const layout = { ...base, nativeReference: { ...base.nativeReference!, ...n.sample } }
  const options = { ...nativeSettings(w,core.doc), ...n.settings }
  const option = (key: string, value: boolean | number) => patch({ settings: { ...n.settings, [key]: value } })
  const font = (n.fontId || base.widgets.find(w => w.nativeElement?.key === n.key)?.nativeElement?.fontId || 'wqy9-lunar') as DeviceGuiFontId
  const missing = n.role === 'text' && n.text !== undefined ? missingDeviceGlyphs(font, n.text) : []
  return <>
    <p className="field-hint">{n.role === 'calendar' ? '日历调整区域留白，字体不随控件拉伸。' : n.role === 'clock' ? '时间数字等比适配区域，保持居中。' : '独立控件，保持预置混排与像素字号。'}</p>
    <details className="inspector-section" open={n.role === 'calendar' || n.role === 'clock'}><summary>本控件预览数据</summary>
      <NativeReferenceControls showRows={n.role === 'calendar'} layout={layout} update={value => patch({ sample: { ...n.sample, timestamp: value.timestamp, weekStart: value.weekStart } })} />
      <label>蓝牙名<input value={n.sample?.bluetoothName || ''} placeholder="使用预置名称" maxLength={32} onChange={e => patch({ sample: { ...n.sample, bluetoothName: e.target.value || undefined } })} /></label>
    </details>
    {['text','calendar'].includes(n.role) && <section className="inspector-section"><h3>字体</h3><select aria-label="点阵字体" value={n.fontId || ''} onChange={e => patch({ fontId: e.target.value || undefined })}>
      <option value="">预置混排字号</option>{deviceGuiFontOptions.filter(f => n.role !== 'calendar' || f.glyphs === 'text').map(f => <option key={f.id} value={f.id}>{f.shortLabel}</option>)}
    </select></section>}
    {n.role === 'text' && <section className="inspector-section"><label>固定文字<textarea aria-label="固定文字" placeholder={w.cells[0][0].text} value={n.text ?? ''} onChange={e => patch({ text: e.target.value || undefined })} /></label>
      <p className="field-hint">留空时跟随预置数据；输入后仅覆盖本控件。</p>{missing.length > 0 && <p role="alert">所选字库缺少：{missing.join('、')}。请修改文字或使用普通文字控件。</p>}</section>}
    {n.role === 'calendar' && <section className="inspector-section"><h3>日历样式</h3>{([['showLunar','农历日期'],['bandedWeekdays','星期栏底色'],['gridLines','日期分隔线']] as const).map(([key,label]) =>
      <label className="lab-check" key={key}><input type="checkbox" checked={Boolean(options[key])} onChange={e => option(key,e.target.checked)} />{label}</label>)}</section>}
    {n.role === 'clock' && <section className="inspector-section"><h3>时钟样式</h3><label>时间制式<select aria-label="时间制式" value={options.twelveHour ? '12' : '24'} onChange={e => option('twelveHour',e.target.value === '12')}><option value="24">24 小时</option><option value="12">12 小时</option></select></label></section>}
  </>
}
