import { renderToStaticMarkup } from 'react-dom/server'
import { describe, expect, it } from 'vitest'
import { pageTemplates } from '../data/templates'
import { fitTemplatePreview, TemplatePreview } from './TemplatePreview'

describe('template preview geometry', () => {
  it('keeps actual pixels when they fit and only scales oversized pages down', () => {
    expect(fitTemplatePreview(128, 250, 180, 280)).toEqual({ width: 128, height: 250, scale: 1 })
    expect(fitTemplatePreview(128, 250, 180, 200)).toEqual({ width: 102, height: 200, scale: .8 })
    expect(fitTemplatePreview(800, 480, 300, 220)).toEqual({ width: 300, height: 180, scale: .375 })
  })

  it('uses the same scene renderer for every template orientation', () => {
    const calendarTemplate = pageTemplates.find((template) => template.id === 'calendar-native')!
    const landscape = renderToStaticMarkup(<TemplatePreview template={calendarTemplate} />)
    const portraitTemplate = pageTemplates.find((template) => template.layout === 'portrait')!
    const portrait = renderToStaticMarkup(<TemplatePreview template={portraitTemplate} />)

    expect(landscape).toContain('scene-template-preview-canvas')
    expect(portrait).toContain('scene-template-preview-canvas')
    expect(landscape).not.toContain('<svg')
  })

  it('keeps device information out of the creation catalog, without removing calendar and clock presets', () => {
    expect(pageTemplates.some(template => template.nativeAction === 'device-info' || template.id === 'device-info-native')).toBe(false)
    for (const action of ['calendar', 'clock']) {
      const template = pageTemplates.find(item => item.nativeAction === action)!
      expect(template).toBeDefined()
      expect(renderToStaticMarkup(<TemplatePreview template={template} />)).toContain('scene-template-preview-canvas')
    }
  })

  it('renders dedicated structures for structured page types', () => {
    const renderKind = (kind: 'clock' | 'schedule' | 'todo' | 'wifi') => {
      const template = pageTemplates.find((candidate) => candidate.kind === kind)!
      return renderToStaticMarkup(<TemplatePreview template={template} />)
    }

    for (const kind of ['clock', 'schedule', 'todo', 'wifi'] as const) expect(renderKind(kind)).toContain('scene-template-preview-canvas')
  })
})
