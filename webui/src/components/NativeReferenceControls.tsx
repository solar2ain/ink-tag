import { calendarMonthRows, calendarRowExample, previewDateLimits, previewDateTime, previewTimestamp } from '../design/layoutPreviewTime'
import type { DeviceGuiDocument } from '../device-gui/model'

export function NativeReferenceControls({ layout, update, showRows = true }: {
  layout: DeviceGuiDocument
  update: (value: NonNullable<DeviceGuiDocument['nativeReference']>) => void
  showRows?: boolean
}) {
  const reference = layout.nativeReference!
  const { date, time } = previewDateTime(reference.timestamp)
  const setDateTime = (nextDate: string, nextTime: string) => {
    const timestamp = previewTimestamp(nextDate, nextTime)
    if (timestamp !== undefined) update({ ...reference, timestamp })
  }
  return <section className="native-reference-controls">
    <h2>预览数据 <small>测试样本 · 非设备实况</small></h2>
    <label>日期<input aria-label="预览日期" type="date" {...previewDateLimits} value={date} onChange={(event) => setDateTime(event.target.value, time)} /></label>
    <label>时间<input aria-label="预览时间" type="time" step="1" value={time} onChange={(event) => setDateTime(date, event.target.value)} /></label>
    <label>一周从<select aria-label="一周起始" value={reference.weekStart} onChange={(event) => update({ ...reference, weekStart: Number(event.target.value) as 0 | 1 })}><option value="1">星期一</option><option value="0">星期日</option></select></label>
    {showRows && <><div className="native-row-presets">{([4, 5, 6] as const).map((rows) => <button key={rows} onClick={() => update({ ...reference, timestamp: calendarRowExample(rows, reference.weekStart, time) })}>{rows} 行月</button>)}</div>
    <p>本月 {calendarMonthRows(reference.timestamp, reference.weekStart)} 行；4 行月仍保留原版最少 5 行空间。电源和身份为示例数据。</p></>}
    <p>固定 {layout.width} × {layout.height} 画板，不迁移到其他分辨率。换屏后需重新创建定制页。</p>
  </section>
}
