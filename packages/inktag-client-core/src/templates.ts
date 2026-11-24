import catalog from './data/catalog.json'
import screenSizes from './data/screen-sizes.json'

export type ClientTemplateKind = 'device-info' | 'calendar' | 'clock' | 'schedule' | 'todo' | 'business' | 'wifi' | 'photo' | 'weather' | 'date-card'
export type ClientTemplateLayout = 'portrait' | 'landscape' | 'square'
export type ClientNativeAction = 'device-info' | 'calendar' | 'clock' | 'agenda' | 'schedule'

export interface ClientTemplateSource {
  id: string
  kind: ClientTemplateKind
  name: string
  summary: string
  accent: string
  width: number
  height: number
  preview: readonly string[]
  nativeAction?: ClientNativeAction
  deviceGuiPreset?: string
  layoutData?: string
}

export interface ClientPageTemplate extends ClientTemplateSource {
  sourceId: string
  layout: ClientTemplateLayout
}

export interface ClientTemplateScreen {
  id: string
  label: string
  width: number
  height: number
}

/** One catalog feeds WebUI cards and the native Mini Program starter editor. */
export const CLIENT_TEMPLATE_SOURCES = catalog.templates as readonly ClientTemplateSource[]
export const COMMON_TEMPLATE_SCREENS = screenSizes.screens as readonly ClientTemplateScreen[]

export const CLIENT_TEMPLATE_KIND_ORDER: readonly ClientTemplateKind[] = [
  'calendar', 'clock', 'weather', 'date-card', 'schedule', 'todo', 'business', 'wifi', 'photo',
]

export const CLIENT_TEMPLATE_KIND_LABELS: Record<ClientTemplateKind | 'all', string> = {
  all: '全部', calendar: '日历', clock: '时钟', 'device-info': '设备', schedule: '课表', todo: '待办',
  business: '名片', wifi: 'Wi-Fi', photo: '图片', weather: '天气站', 'date-card': '日期牌',
}

export const CLIENT_TEMPLATES_PER_PAGE = 6

export interface ClientTemplateFilter {
  kind?: ClientTemplateKind | 'all'
  layout?: ClientTemplateLayout | 'all'
  query?: string
}

/** Keep catalog filtering identical across the React and native renderers. */
export function filterClientPageTemplates<T extends ClientPageTemplate>(templates: readonly T[], filter: ClientTemplateFilter): T[] {
  const query = (filter.query || '').trim().toLocaleLowerCase()
  return templates.filter((template) =>
    (!filter.kind || filter.kind === 'all' || template.kind === filter.kind) &&
    (!filter.layout || filter.layout === 'all' || template.layout === filter.layout) &&
    (!query || `${template.name}${template.summary}${template.width}x${template.height}`.toLocaleLowerCase().includes(query)),
  )
}

export function paginateClientPageTemplates<T extends ClientPageTemplate>(templates: readonly T[], page: number, perPage = CLIENT_TEMPLATES_PER_PAGE) {
  const pageCount = Math.max(1, Math.ceil(templates.length / perPage))
  const currentPage = Math.min(pageCount, Math.max(1, page))
  return {
    page: currentPage,
    pageCount,
    total: templates.length,
    items: templates.slice((currentPage - 1) * perPage, currentPage * perPage),
  }
}

export function compareClientTemplates(left: ClientPageTemplate, right: ClientPageTemplate): number {
  return CLIENT_TEMPLATE_KIND_ORDER.indexOf(left.kind) - CLIENT_TEMPLATE_KIND_ORDER.indexOf(right.kind)
    || left.width * left.height - right.width * right.height
    || Math.max(left.width, left.height) - Math.max(right.width, right.height)
    || right.width - left.width || left.id.localeCompare(right.id)
}

function instantiateClientTemplate(source: ClientTemplateSource, width: number, height: number): ClientPageTemplate {
  return {
    ...source,
    sourceId: source.id,
    id: source.width === width && source.height === height ? source.id : `${source.id}-${width}x${height}`,
    width,
    height,
    layout: width === height ? 'square' : width > height ? 'landscape' : 'portrait',
  }
}

/** Generate a new fixed-size document. Never migrate an existing saved page. */
export function clientTemplatesForResolution(width: number, height: number): ClientPageTemplate[] {
  const long = Math.max(width, height)
  const short = Math.min(width, height)
  return CLIENT_TEMPLATE_SOURCES.flatMap((source) => {
    if(source.kind==='photo'&&(long<400||short<300))return []
    const landscape = instantiateClientTemplate(source, long, short)
    return source.nativeAction || source.kind === 'schedule' || long === short ? [landscape] : [landscape, instantiateClientTemplate(source, short, long)]
  }).sort(compareClientTemplates)
}

export function buildClientPageTemplates(extraScreens: readonly Pick<ClientTemplateScreen, 'width' | 'height'>[] = []): ClientPageTemplate[] {
  return [...new Map([
    ...CLIENT_TEMPLATE_SOURCES.filter(source => source.kind !== 'schedule' || source.width >= source.height)
      .map((source) => instantiateClientTemplate(source, source.width, source.height)),
    ...[...COMMON_TEMPLATE_SCREENS, ...extraScreens].flatMap((screen) => clientTemplatesForResolution(screen.width, screen.height)),
  ].map((template) => [template.id, template])).values()].sort(compareClientTemplates)
}
