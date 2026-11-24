import { drawPlan, kindOf, parseDocument, strokePixelRuns, type Document } from './index'
import { validateWeather, weatherPrimitives, validWeatherSource, type WeatherData } from './weather'
import { layoutRichText, type TextMetrics } from './richText'
import type { RichText, Style } from './index'
export { weatherDocument, weatherDateCardDocument } from './weatherTemplate'
export { weatherPrimitives, weatherCalendarLabels } from './weather'

/** Shared import contract, not executable scripts or a second device protocol. */
export function parseHostScene(json: string): Document {
  if (json.length > 8 * 1024 * 1024) throw new Error('主机页面超过 8 MiB，请缩小图片')
  const doc = parseDocument(json)
  if (doc.pageMode !== 'host-dynamic' || !validWeatherSource(doc.weatherSource)) throw new Error('请导出已配置数据源的主机动态页面')
  if (doc.width < 96 || doc.height < 96 || doc.width > 2048 || doc.height > 2048 || doc.width * doc.height > 2097152) throw new Error('主机画板尺寸不受支持')
  if (!doc.widgets.some(w => w.hostWeather)) throw new Error('请添加与数据源匹配的主机控件')
  for (const w of doc.widgets) {
    if (w.native || w.imageSlots || w.style.fontId || w.cells.some(row => row.some(c => c.style?.fontId || c.richText?.runs.some(r => r.fontId || r.style?.fontId))))
      throw new Error('主机页面请使用天气控件、普通控件和浏览器字体，不支持设备原生控件或图槽轮播')
    if (w.cells.some(row => row.some(c => /\{\{/.test(c.richText ? c.richText.runs.map(r => r.text).join('') : c.text)))) throw new Error('主机页面请用天气数据控件绑定数据，不支持设备变量')
  }
  return doc
}
export function scenePlan(json: string, weather: unknown) {
  const doc = parseHostScene(json), data = validateWeather(weather), cells = drawPlan(doc)
  return { width: doc.width, height: doc.height, color: doc.colorMode ?? 2, demo: data.demo,
    widgets: doc.widgets.map(w => ({ ...w, kind: kindOf(w),
      primitives: w.hostWeather ? weatherPrimitives(w, data as WeatherData) : undefined,
      cells: cells.filter(c => c.widgetId === w.id), runs: w.stroke ? strokePixelRuns(w) : undefined })) }
}
/** The host supplies font metrics; baseline/gap/alignment are the editor's layout. */
export function layoutHostText(content: RichText, style: Style, width: number, height: number, metrics: TextMetrics[]) {
  let index = 0
  return layoutRichText(content, style, width, height, () => {
    const value = metrics[index++]
    if (!value || Object.values(value).some(n => !Number.isFinite(n))) throw new Error('Invalid host font metrics')
    return value
  })
}
