import type { PageKind } from './db'
import { pageTemplates, templateKindLabels, type TemplateLayout } from './templates'
import { savedPageMode, templatePageMode, type PageMode } from './pageMode'

export const documentKindLabels = { ...templateKindLabels, custom: '自定义' }
export type DocumentFilters = { kind: PageKind | 'all'; pageMode: PageMode | 'all'; resolution: string; layout: TemplateLayout | 'all'; query: string }
export const emptyDocumentFilters: DocumentFilters = { kind: 'all', pageMode: 'all', resolution: 'all', layout: 'all', query: '' }
type Metadata = { name: string; width: number; height: number; kind?: PageKind; templateId?: string; summary?: string; document?: string; nativeAction?: string; layoutData?: string }
export function documentMode(record: Pick<Metadata,'document'|'nativeAction'|'layoutData'|'kind'>) {
  return record.document !== undefined ? savedPageMode({document:record.document}) : templatePageMode(record)
}
export function documentKind(record: Pick<Metadata, 'kind' | 'templateId'>): PageKind {
  const sourceId = record.templateId?.replace(/-\d+x\d+$/, '')
  // Classify saved date placards without rewriting their documents or database.
  if (sourceId === 'weather-date-card' && (!record.kind || record.kind === 'weather')) return 'date-card'
  return record.kind ?? pageTemplates.find(template => template.id === record.templateId || template.id === sourceId || template.sourceId === sourceId)?.kind ?? 'custom'
}
export function documentLayout({ width, height }: Pick<Metadata, 'width' | 'height'>): TemplateLayout {
  return width === height ? 'square' : width > height ? 'landscape' : 'portrait'
}
export function documentResolution({ width, height }: Pick<Metadata, 'width' | 'height'>) {
  return `${Math.max(width, height)}x${Math.min(width, height)}`
}
export function matchesDocumentFilters(record: Metadata, filters: DocumentFilters) {
  return (filters.kind === 'all' || documentKind(record) === filters.kind)
    && (filters.pageMode === 'all' || documentMode(record) === filters.pageMode)
    && (filters.resolution === 'all' || documentResolution(record) === filters.resolution)
    && (filters.layout === 'all' || documentLayout(record) === filters.layout)
    && `${record.name} ${record.summary ?? ''} ${record.width}x${record.height} ${record.width} × ${record.height}`.toLowerCase().includes(filters.query.trim().toLowerCase())
}
export function documentResolutions(records: readonly Pick<Metadata, 'width' | 'height'>[]) {
  return [...new Set(records.map(documentResolution))].sort((a, b) => {
    const [aw, ah] = a.split('x').map(Number), [bw, bh] = b.split('x').map(Number)
    return aw * ah - bw * bh || aw - bw || ah - bh
  })
}
