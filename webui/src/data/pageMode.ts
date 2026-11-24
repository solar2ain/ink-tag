import { canSendDocument, documentPageMode, parseDocument } from '../../../packages/inktag-editor-core/src/index'
export type { PageMode } from '../../../packages/inktag-editor-core/src/index'
export { pageModes } from '../../../packages/inktag-editor-core/src/index'

export const pageModeLabels = { static: '静态页面', dynamic: '设备动态页面', 'host-dynamic': '主机动态页面' } as const
export const dynamicPageNotice = '由设备自行渲染与刷新，无需主机持续在线。定制动态页面暂不支持保存到设备。日历、时钟及设备控件仅用于预览，可保存到本机继续编辑或导出 PNG。'
export const hostPageNotice = '由 CLI 定期渲染，通过蓝牙推送到设备。主机需持续运行，设备须在主机蓝牙范围内。完成编辑后导出主机任务，由 CLI 导入、绑定设备并显式启用；网页不会自动推送。'
export const pageModeNotices = {
  static: '静态图片页面，不含日历、时钟或设备控件。可发送到设备或保存到图槽，内容不会自动更新。',
  dynamic: dynamicPageNotice, 'host-dynamic': hostPageNotice,
} as const
export const staticContentNotice = '静态页面不能包含动态控件或变量，请改用普通文字。'

export const templatePageMode = (template: { nativeAction?: string; layoutData?: string; kind?: string }) =>
  template.kind === 'weather' || template.kind === 'date-card' || template.layoutData?.startsWith('weather-') ? 'host-dynamic' as const : template.nativeAction === 'calendar' || template.nativeAction === 'clock' ? 'dynamic' as const : 'static' as const

export function savedPageMode(record: { document: string }) {
  try { return documentPageMode(parseDocument(record.document)) } catch { return undefined }
}
export function canSendSavedPage(record: { document: string }) {
  try { return canSendDocument(parseDocument(record.document)) } catch { return false }
}
