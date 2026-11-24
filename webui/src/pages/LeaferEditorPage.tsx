import { useParams, useSearchParams } from 'react-router-dom'
import { RichTextSelectionProvider } from '../lab/leafer/RichTextEditor'
import { Lab } from '../lab/leafer/main'
import { pageTemplates, templatesForResolution } from '../data/templates'
import { colorModes } from '../lab/leafer/palette'
import { pageModes, type PageMode } from '../data/pageMode'

/** The only fixed-resolution creation and saved-page editor. */
export function LeaferEditorPage() {
  const { templateId = '' } = useParams()
  const [params] = useSearchParams()
  const width = Number(params.get('width'))
  const height = Number(params.get('height'))
  const requestedColor = Number(params.get('color'))
  const color = colorModes.find(mode => mode.value === requestedColor)?.value
  const requestedMode = params.get('pageMode') ?? undefined
  if (requestedMode !== undefined && !pageModes.includes(requestedMode as PageMode)) {
    return <div className="page empty-state"><h2>页面类型无效</h2><p>请从创作页重新选择页面类型。</p></div>
  }
  const fixedSize = Number.isInteger(width) && Number.isInteger(height) && width >= 32 && height >= 48 && width <= 4096 && height <= 4096
  const template = (fixedSize ? templatesForResolution(width, height) : pageTemplates).find(item => item.id === templateId)
  // A saved scene is independent of its former starter template. Removing a
  // catalog entry must not prevent loading or protectively refusing its source.
  const savedSource = fixedSize && Boolean(params.get('page') || params.get('template'))
  if (!savedSource && ((!template && !(templateId === 'blank' && fixedSize)) || (template && fixedSize && (template.width !== width || template.height !== height)))) {
    return <div className="page empty-state"><h2>模板与分辨率不匹配</h2><p>请从创作页重新选择固定尺寸模板。</p></div>
  }
  return <RichTextSelectionProvider><Lab key={`${templateId}:${width}x${height}:${requestedMode}:${params.get('page') || params.get('template') || 'new'}`}
    initialTemplateId={templateId} initialWidth={template?.width ?? width} initialHeight={template?.height ?? height}
    initialColor={color} initialPageMode={requestedMode as PageMode | undefined} savedPageId={params.get('page') || undefined} sourceTemplateId={params.get('page') ? undefined : params.get('template') || undefined} embedded /></RichTextSelectionProvider>
}
