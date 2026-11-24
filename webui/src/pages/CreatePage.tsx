import {
  ChevronLeft, ChevronRight, Monitor, PanelTop, RectangleHorizontal, RectangleVertical, Square, Search, FilePlus2, FolderOpen, LayoutTemplate, Trash2, X,
} from 'lucide-react'
import { useEffect, useMemo, useRef, useState } from 'react'
import { Link, useNavigate } from 'react-router-dom'
import { PageHeader } from '../components/PageHeader'
import { TemplatePreview } from '../components/TemplatePreview'
import { PageModeFilter } from '../components/PageModeFilter'
import { useConfirmationDialog } from '../components/ConfirmationDialog'
import { db, type PageKind, type SavedTemplate } from '../data/db'
import { documentKindLabels, documentLayout, documentResolution, documentMode, matchesDocumentFilters } from '../data/documentLibrary'
import {
  builtInScreenProfiles, findScreenProfileForDevice, findScreenProfileById, screenProfileFamily, screenProfileFamilyLabels,
  screenProfileTitle, screenProfileAllowedOnBoard, sortScreenProfiles, type ScreenProfile, type ScreenProfileFamily,
} from '../data/screenProfiles'
import {
  creationTemplateKindOrder, pageTemplateTarget, templateResolutionOptions, templatesForResolution, templatesForScreens,
  type PageTemplate, type TemplateLayout,
} from '../data/templates'
import { canvasSizeForScreen } from '../protocol/screen'
import { useDeviceStore } from '../store/device'
import { colorModes } from '../lab/leafer/palette'
import { pageModeLabels, pageModeNotices, type PageMode } from '../data/pageMode'

const TEMPLATES_PER_PAGE = 6
const profileFamilyOrder: ScreenProfileFamily[] = ['uc8xxx', 'ssd1xxx', 'jd79xxx', 'other']
type CatalogTemplate = { source: 'preset'; record: PageTemplate } | { source: 'mine'; record: SavedTemplate }

export function CreatePage() {
  const navigate = useNavigate()
  const confirmation = useConfirmationDialog()
  const templateHeading = useRef<HTMLHeadingElement>(null)
  const screen = useDeviceStore((state) => state.screen)
  const connection = useDeviceStore((state) => state.connection)
  const boardId = useDeviceStore((state) => state.boardId)
  const connected = connection === 'connected'
  const [kind, setKind] = useState<PageKind | 'all'>('all')
  const [pageModeFilter, setPageModeFilter] = useState<PageMode | 'all'>('all')
  const [source, setSource] = useState<'all' | 'preset' | 'mine'>('all')
  const [userTemplates, setUserTemplates] = useState<SavedTemplate[]>([])
  const [profileId, setProfileId] = useState('all')
  const [resolution, setResolution] = useState('all')
  const [layout, setLayout] = useState<TemplateLayout | 'all'>('all')
  const [query, setQuery] = useState('')
  const [page, setPage] = useState(1)
  const [userProfiles, setUserProfiles] = useState<ScreenProfile[]>([])
  const [blankCanvas, setBlankCanvas] = useState<{ width: number; height: number; color: number; pageMode: PageMode } | null>(null)
  const deviceSize = screen ? canvasSizeForScreen(screen) : null

  useEffect(() => {
    let cancelled = false
    void db.screenProfiles.toArray().then((records) => {
      if (cancelled) return
      setUserProfiles(records.map((record) => ({
        id: record.id,
        tag: 'user',
        group: 'user',
        name: record.name,
        model: record.model,
        device: record.device,
        size: record.size,
        config: record.config,
      })))
    })
    return () => { cancelled = true }
  }, [])
  useEffect(() => { void db.templates.orderBy('updatedAt').reverse().toArray().then(setUserTemplates) }, [])

  const profiles = useMemo(() => sortScreenProfiles([...builtInScreenProfiles, ...userProfiles]
    .filter((profile) => !connected || screenProfileAllowedOnBoard(profile, boardId))), [userProfiles, connected, boardId])
  const selectedProfile = findScreenProfileById(profiles, profileId)
  const profileGroups = useMemo(() => [
    ...profileFamilyOrder.map((family) => ({
      id: family,
      label: screenProfileFamilyLabels[family],
      profiles: profiles.filter((profile) => profile.tag === 'preset' && screenProfileFamily(profile) === family),
    })),
    { id: 'user', label: '用户 Profile', profiles: profiles.filter((profile) => profile.tag === 'user') },
  ].filter((group) => group.profiles.length), [profiles])

  const resolutionOptions = useMemo(() => {
    const options = new Map(templateResolutionOptions.map((item) => [item.id, item]))
    profiles.forEach((profile) => {
      const { width, height } = profile.config
      const long = Math.max(width, height), short = Math.min(width, height)
      options.set(`${long}x${short}`, { id: `${long}x${short}`, width: long, height: short, label: profile.name })
    })
    userTemplates.forEach(template => {
      const [width, height] = documentResolution(template).split('x').map(Number)
      if (!options.has(`${width}x${height}`)) options.set(`${width}x${height}`, { id: `${width}x${height}`, width, height, label: '自建模板' })
    })
    return [...options.values()].sort((left, right) =>
      left.width * left.height - right.width * right.height || left.width - right.width || left.height - right.height,
    )
  }, [profiles, userTemplates])

  useEffect(() => {
    if (!screen) return
    const match = findScreenProfileForDevice(profiles, screen)
    if (!match) return
    setProfileId(match.id)
    setResolution(`${Math.max(match.config.width, match.config.height)}x${Math.min(match.config.width, match.config.height)}`)
  }, [profiles, screen])

  useEffect(() => setPage(1), [kind, pageModeFilter, layout, profileId, query, resolution, source])

  const sourceTemplates = useMemo(() => {
    if (resolution === 'all') return templatesForScreens(resolutionOptions)
    const [width, height] = resolution.split('x').map(Number)
    return templatesForResolution(width, height)
  }, [resolution, resolutionOptions])
  const templates = useMemo(() => {
    const catalog: CatalogTemplate[] = [
      ...sourceTemplates.map(record => ({ source: 'preset' as const, record })),
      ...userTemplates.map(record => ({ source: 'mine' as const, record })),
    ]
    const order: readonly PageKind[] = creationTemplateKindOrder
    return catalog.filter(template => template.record.kind !== 'device-info' && (source === 'all' || template.source === source)
      && matchesDocumentFilters(template.record, { kind, pageMode:pageModeFilter, layout, query, resolution }))
      .sort((left, right) => Number(documentMode(left.record) !== 'static') - Number(documentMode(right.record) !== 'static')
        || order.indexOf(left.record.kind) - order.indexOf(right.record.kind)
        || left.record.width * left.record.height - right.record.width * right.record.height
        || right.record.width - left.record.width)
  }, [kind, pageModeFilter, layout, query, resolution, source, sourceTemplates, userTemplates])
  const pageCount = Math.max(1, Math.ceil(templates.length / TEMPLATES_PER_PAGE))
  const currentPage = Math.min(page, pageCount)
  const visibleTemplates = templates.slice((currentPage - 1) * TEMPLATES_PER_PAGE, currentPage * TEMPLATES_PER_PAGE)
  const previewColor = selectedProfile?.config.color ?? screen?.color

  const selectProfile = (nextId: string) => {
    setProfileId(nextId)
    const profile = profiles.find((item) => item.id === nextId)
    if (profile) setResolution(`${Math.max(profile.config.width, profile.config.height)}x${Math.min(profile.config.width, profile.config.height)}`)
  }

  const selectResolution = (nextResolution: string) => {
    setResolution(nextResolution)
    if (!selectedProfile || nextResolution === `${selectedProfile.config.width}x${selectedProfile.config.height}`) return
    setProfileId('all')
  }

  const newBlankCanvas = () => {
    if (connection === 'connected' && screen && deviceSize) {
      setBlankCanvas({ ...deviceSize, color: screen.color, pageMode: 'static' })
      return
    }
    const chosenSize = resolutionOptions.find(item => item.id === resolution)
    const size = chosenSize ?? (selectedProfile ? canvasSizeForScreen(selectedProfile.config) : deviceSize) ?? { width: 400, height: 300 }
    const long = Math.max(size.width, size.height), short = Math.min(size.width, size.height)
    const portrait = layout === 'portrait' || (layout === 'all' && size.width < size.height)
    setBlankCanvas({ width: portrait ? short : long, height: portrait ? long : short,
      color: colorModes.find(mode => mode.value === previewColor)?.value ?? 2, pageMode: 'static' })
  }
  const deleteTemplate = async (template: SavedTemplate) => {
    if (!await confirmation.confirm({ title: '删除自建模板', message: <>删除模板<strong>“{template.name}”</strong>？已从它创建的页面不会受影响。</>, confirmLabel: '删除', danger: true })) return
    await db.templates.delete(template.id)
    setUserTemplates(await db.templates.orderBy('updatedAt').reverse().toArray())
  }

  return (
    <div className="page create-page">
      <PageHeader title="创作" actions={<><button className="button primary" disabled={connection === 'connected' && !screen} title={connection === 'connected' ? '选择页面类型，使用当前设备的屏幕尺寸和色彩' : '选择页面类型、屏幕尺寸和色彩'} onClick={newBlankCanvas}><FilePlus2 />新建页面</button><button className="button secondary" onClick={() => { templateHeading.current?.scrollIntoView({ behavior: 'smooth', block: 'start' }); templateHeading.current?.focus({ preventScroll: true }) }}><LayoutTemplate />选择模板开始</button><Link className="button secondary" to="/library?tab=pages"><FolderOpen />我的页面</Link></>} />
      <div className="template-catalog-heading"><h2 ref={templateHeading} tabIndex={-1}>选择模板开始</h2><label className="template-filter-control"><LayoutTemplate /><select aria-label="模板来源" value={source} onChange={event => setSource(event.target.value as typeof source)}><option value="all">全部模板</option><option value="preset">预置模板</option><option value="mine">我的模板</option></select></label></div>
      <section className="template-toolbar" aria-label="模板筛选">
        <div className="segmented-control template-kind-filter">
          {(['all', ...creationTemplateKindOrder] as Array<PageKind | 'all'>).map((item) => (
            <button key={item} className={kind === item ? 'active' : ''} onClick={() => setKind(item)}>{documentKindLabels[item]}</button>
          ))}
        </div>
        <div className="template-filter-row">
          <PageModeFilter label="模板页面类型" value={pageModeFilter} change={setPageModeFilter} />
          <label className="template-filter-control template-profile-filter">
            <PanelTop /><span>Profile</span>
            <select aria-label="屏幕 Profile" value={profileId} onChange={(event) => selectProfile(event.target.value)}>
              <option value="all">全部屏幕 Profile</option>
              {profileGroups.map((group) => <optgroup key={group.id} label={group.label}>{group.profiles.map((profile) => (
                <option key={profile.id} value={profile.id}>{screenProfileTitle(profile)}{screen && findScreenProfileForDevice(profiles, screen)?.id === profile.id ? ' · 当前设备' : ''}</option>
              ))}</optgroup>)}
            </select>
          </label>
          <label className="template-filter-control template-resolution-filter">
            <Monitor /><span>分辨率</span>
            <select aria-label="模板分辨率" value={resolution} onChange={(event) => selectResolution(event.target.value)}>
              <option value="all">全部分辨率</option>
              {resolutionOptions.map((item) => <option key={item.id} value={item.id}>{item.width} × {item.height}{deviceSize?.width === item.width && deviceSize.height === item.height ? ' · 当前设备' : ''}</option>)}
            </select>
          </label>
          <div className="segmented-control compact orientation-filter" aria-label="屏幕方向">
            <button title="全部方向" aria-label="全部方向" className={layout === 'all' ? 'active' : ''} onClick={() => setLayout('all')}>全部</button>
            <button title="横屏模板" aria-label="横屏模板" className={layout === 'landscape' ? 'active' : ''} onClick={() => setLayout('landscape')}><RectangleHorizontal /><span>横屏</span></button>
            <button title="竖屏模板" aria-label="竖屏模板" className={layout === 'portrait' ? 'active' : ''} onClick={() => setLayout('portrait')}><RectangleVertical /><span>竖屏</span></button>
            <button title="方屏模板" aria-label="方屏模板" className={layout === 'square' ? 'active' : ''} onClick={() => setLayout('square')}><Square /><span>方屏</span></button>
          </div>
          <label className="search-field template-search"><Search /><input value={query} onChange={(event) => setQuery(event.target.value)} placeholder="搜索模板" /></label>
        </div>
      </section>
      <div className="template-results-bar">
        <span>共 {templates.length} 个模板{selectedProfile ? ` · ${selectedProfile.config.width} × ${selectedProfile.config.height}` : ''}</span>
        {templates.length > TEMPLATES_PER_PAGE && <TemplatePagination page={currentPage} pageCount={pageCount} setPage={setPage} />}
      </div>
      <section className="template-grid" aria-live="polite">
        {visibleTemplates.map(({ source: origin, record: template }) => { const mode=documentMode(template);return (
          <div className="template-card-pair" key={`${origin}:${template.id}`}>
            <button className="template-card" onClick={() => navigate(origin === 'preset' ? pageTemplateTarget(template as PageTemplate, { color: previewColor }) : `/create/canvas/blank?width=${template.width}&height=${template.height}&template=${encodeURIComponent(template.id)}`)}>
              {origin === 'preset' ? <TemplatePreview template={template as PageTemplate} screenColor={previewColor} /> : <div className="template-preview saved-template-preview">{template.preview ? <img src={(template as SavedTemplate).preview} alt="" /> : <LayoutTemplate />}</div>}
              <span className="template-copy"><strong>{template.name}</strong><small>{mode ? pageModeLabels[mode] : '原稿不可用'} · {template.width} × {template.height} · {documentLayout(template) === 'portrait' ? '竖屏' : documentLayout(template) === 'square' ? '方屏' : '横屏'} · {origin === 'preset' ? (template as PageTemplate).summary : `我的模板 · ${documentKindLabels[template.kind]}`}</small></span>
              <ChevronRight className="template-arrow" />
            </button>
            {origin === 'mine' && <button className="icon-button saved-template-delete" aria-label={`删除模板 ${template.name}`} title="删除自建模板" onClick={() => void deleteTemplate(template as SavedTemplate)}><Trash2 /></button>}
          </div>
        )})}
        {!templates.length && <div className="template-empty"><strong>没有匹配的模板</strong><span>调整 Profile、分辨率、方向或内容类型后再试。</span></div>}
      </section>
      {templates.length > TEMPLATES_PER_PAGE && <div className="template-pagination-footer"><TemplatePagination page={currentPage} pageCount={pageCount} setPage={setPage} /></div>}
      {confirmation.dialog}
      {blankCanvas && <div className="modal-backdrop" onMouseDown={event => { if (event.target === event.currentTarget) setBlankCanvas(null) }}>
        <form className="confirmation-dialog blank-canvas-dialog" role="dialog" aria-modal="true" aria-labelledby="blank-canvas-title"
          onKeyDown={event => { if (event.key === 'Escape') setBlankCanvas(null) }}
          onSubmit={event => { event.preventDefault(); navigate(`/create/canvas/blank?width=${blankCanvas.width}&height=${blankCanvas.height}&color=${blankCanvas.color}&pageMode=${blankCanvas.pageMode}`) }}>
          <header><div><FilePlus2 /><h2 id="blank-canvas-title">新建页面</h2></div><button className="icon-button" type="button" aria-label="关闭" onClick={() => setBlankCanvas(null)}><X /></button></header>
          <label>页面类型<select aria-label="页面类型" autoFocus value={blankCanvas.pageMode} onChange={event => setBlankCanvas({ ...blankCanvas, pageMode: event.target.value as PageMode })}>
            {Object.entries(pageModeLabels).map(([mode,label]) => <option key={mode} value={mode}>{label}</option>)}
          </select></label>
          <p className="blank-page-mode-hint" role="note">{pageModeNotices[blankCanvas.pageMode]}</p>
          {connected && screen && deviceSize ? <p>使用当前设备：{blankCanvas.width} × {blankCanvas.height} · {colorModes.find(mode => mode.value === blankCanvas.color)?.label}。尺寸与色彩无需重复填写。</p> : <>
          <p>选择实际屏幕尺寸。创建后尺寸固定，画板缩放不会改变导出像素。</p>
          <label>常用分辨率<select aria-label="画板分辨率" value={resolutionOptions.find(item => item.width === Math.max(blankCanvas.width, blankCanvas.height) && item.height === Math.min(blankCanvas.width, blankCanvas.height))?.id ?? 'custom'}
            onChange={event => { const size = resolutionOptions.find(item => item.id === event.target.value); if (size) setBlankCanvas({ ...blankCanvas, width: blankCanvas.width < blankCanvas.height ? size.height : size.width, height: blankCanvas.width < blankCanvas.height ? size.width : size.height }) }}>
            {resolutionOptions.map(item => <option key={item.id} value={item.id}>{item.width} × {item.height}</option>)}<option value="custom">自定义尺寸</option>
          </select></label>
          <div className="blank-canvas-size"><label>宽度（px）<input aria-label="画板宽度" required type="number" min={32} max={4096} value={blankCanvas.width || ''} onChange={event => setBlankCanvas({ ...blankCanvas, width: Number(event.target.value) })} /></label>
            <button className="icon-button" type="button" aria-label="交换宽高" title="交换宽高" onClick={() => setBlankCanvas({ ...blankCanvas, width: blankCanvas.height, height: blankCanvas.width })}><RectangleVertical /></button>
            <label>高度（px）<input aria-label="画板高度" required type="number" min={48} max={4096} value={blankCanvas.height || ''} onChange={event => setBlankCanvas({ ...blankCanvas, height: Number(event.target.value) })} /></label></div>
          <label>屏幕色彩<select aria-label="画板色彩" value={blankCanvas.color} onChange={event => setBlankCanvas({ ...blankCanvas, color: Number(event.target.value) })}>{colorModes.map(mode => <option key={mode.value} value={mode.value}>{mode.label}</option>)}</select></label>
          </>}
          <footer><button className="button secondary" type="button" onClick={() => setBlankCanvas(null)}>取消</button><button className="button primary" type="submit">创建页面</button></footer>
        </form>
      </div>}
    </div>
  )
}

function TemplatePagination({ page, pageCount, setPage }: { page: number; pageCount: number; setPage: (page: number) => void }) {
  return <nav className="template-pagination" aria-label="模板分页">
    <button title="上一页" aria-label="上一页" disabled={page <= 1} onClick={() => setPage(Math.max(1, page - 1))}><ChevronLeft /></button>
    <span>{page} / {pageCount}</span>
    <button title="下一页" aria-label="下一页" disabled={page >= pageCount} onClick={() => setPage(Math.min(pageCount, page + 1))}><ChevronRight /></button>
  </nav>
}
