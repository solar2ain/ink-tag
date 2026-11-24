import type { DeviceGuiPresetId } from '../device-gui/model'
import { templatePageMode } from './pageMode'
import { COMMON_TEMPLATE_SCREENS, CLIENT_TEMPLATE_KIND_ORDER,
  CLIENT_TEMPLATE_KIND_LABELS, buildClientPageTemplates, clientTemplatesForResolution, type ClientTemplateKind,
} from '../../../packages/inktag-client-core/src/templates'

export type TemplateKind = ClientTemplateKind
export type TemplateLayout = 'portrait' | 'landscape' | 'square'
export interface PageTemplate {
  id: string
  sourceId?: string
  kind: TemplateKind
  name: string
  summary: string
  accent: string
  layout: TemplateLayout
  width: number
  height: number
  preview: string[]
  nativeAction?: 'device-info' | 'calendar' | 'clock' | 'agenda' | 'schedule'
  deviceGuiPreset?: DeviceGuiPresetId
  layoutData?: string
}

const dynamicTemplateKinds = new Set<TemplateKind>(['calendar', 'clock', 'weather', 'date-card'])
const staticKinds = CLIENT_TEMPLATE_KIND_ORDER.filter(kind => !dynamicTemplateKinds.has(kind))
const dynamicKinds = CLIENT_TEMPLATE_KIND_ORDER.filter(kind => dynamicTemplateKinds.has(kind))
export const templateKindOrder = [...staticKinds, ...dynamicKinds]
export const creationTemplateKindOrder = [...staticKinds, 'custom' as const, ...dynamicKinds]
export function compareTemplates(left: PageTemplate, right: PageTemplate) {
  return templateKindOrder.indexOf(left.kind) - templateKindOrder.indexOf(right.kind)
    || left.width * left.height - right.width * right.height
    || Math.max(left.width, left.height) - Math.max(right.width, right.height)
    || right.width - left.width || left.id.localeCompare(right.id)
}

/** Generate a new fixed-size document. Never migrate an existing saved page. */
export function templatesForResolution(width: number, height: number) {
  return clientTemplatesForResolution(width, height) as PageTemplate[]
}
export const commonTemplateScreens = [...COMMON_TEMPLATE_SCREENS]
export const pageTemplates = buildClientPageTemplates() as PageTemplate[]
/** Include actual profile sizes even before the user selects a resolution. */
export const templatesForScreens = (screens: readonly { width: number; height: number }[]) => buildClientPageTemplates(screens) as PageTemplate[]

export const templateResolutionOptions = commonTemplateScreens.map(({width, height, label}) => ({
  id: `${width}x${height}`, width, height, label,
}))
export function pageTemplateTarget(template: PageTemplate, options: { color?: number } = {}) {
  const color = Number.isInteger(options.color) ? `&color=${options.color}` : ''
  return `/create/canvas/${encodeURIComponent(template.id)}?width=${template.width}&height=${template.height}${color}&pageMode=${templatePageMode(template)}`
}
export const templateKindLabels = CLIENT_TEMPLATE_KIND_LABELS
