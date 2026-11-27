import { FeedbackNotice, useFeedbackMessage } from '../components/FeedbackNotice'
import {
  Copy, Download, Edit3, HardDrive, ImagePlus, Images, Monitor, RectangleHorizontal, RectangleVertical, Search, Send, Square, Trash2, Upload,
} from 'lucide-react'
import { useEffect, useMemo, useRef, useState } from 'react'
import { Link, useSearchParams } from 'react-router-dom'
import { PageHeader } from '../components/PageHeader'
import { PageModeFilter } from '../components/PageModeFilter'
import { useConfirmationDialog } from '../components/ConfirmationDialog'
import { db, type Asset, type SavedPage } from '../data/db'
import { documentKind, documentKindLabels, documentResolutions, emptyDocumentFilters, matchesDocumentFilters, type DocumentFilters } from '../data/documentLibrary'
import { creationTemplateKindOrder } from '../data/templates'
import { savedPageMode, canSendSavedPage, pageModeLabels, pageModeNotices, staticContentNotice } from '../data/pageMode'
import { SendCanvasDialog } from '../components/SendCanvasDialog'
import { useDeviceStore } from '../store/device'
import type { ImageSlotContentType } from '../types'
import { downloadCanvasPng } from '../download'

type LibraryTab = 'pages' | 'assets'

function dataUrlCanvas(dataUrl: string) {
  return new Promise<HTMLCanvasElement>((resolve, reject) => {
    const image = new Image()
    image.onload = () => {
      const canvas = document.createElement('canvas')
      canvas.width = image.naturalWidth
      canvas.height = image.naturalHeight
      canvas.getContext('2d')!.drawImage(image, 0, 0)
      resolve(canvas)
    }
    image.onerror = () => reject(new Error('页面预览无法读取'))
    image.src = dataUrl
  })
}

export function LibraryPage() {
  const confirmation = useConfirmationDialog()
  const { connection, capabilities, action } = useDeviceStore()
  const connected = connection === 'connected'
  const [params, setParams] = useSearchParams()
  const tab: LibraryTab = params.get('tab') === 'assets' ? 'assets' : 'pages'
  const setTab = (next: LibraryTab) => setParams({ tab: next })
  const [pages, setPages] = useState<SavedPage[]>([])
  const [assets, setAssets] = useState<Asset[]>([])
  const [message, setMessage, messageRevision] = useFeedbackMessage('')
  const [sendTarget, setSendTarget] = useState<{ page: SavedPage; store: boolean } | null>(null)
  const [filters, setFilters] = useState<DocumentFilters>(emptyDocumentFilters)
  const [sort, setSort] = useState('updated')
  const filteredPages = useMemo(() => pages.filter(page => matchesDocumentFilters(page, filters)).sort((a, b) =>
    sort === 'name' ? a.name.localeCompare(b.name, 'zh-CN') : sort === 'resolution' ? a.width * a.height - b.width * b.height || a.width - b.width : b.updatedAt - a.updatedAt,
  ), [pages, filters, sort])
  const resolutions = useMemo(() => documentResolutions(pages), [pages])
  const assetInput = useRef<HTMLInputElement>(null)

  const reload = async () => {
    const [nextPages, nextAssets] = await Promise.all([
      db.pages.orderBy('updatedAt').reverse().toArray(),
      db.assets.orderBy('createdAt').reverse().toArray(),
    ])
    setPages(nextPages)
    setAssets(nextAssets)
  }
  useEffect(() => { void reload() }, [])

  const importAssets = async (files: FileList | null) => {
    if (!files) return
    const now = Date.now()
    await Promise.all([...files].map((file, index) => db.assets.put({
      id: `asset-${now}-${index}`, name: file.name, type: file.type, blob: file, createdAt: now + index,
    })))
    setMessage(`已导入 ${files.length} 个素材`)
    await reload()
  }
  const duplicatePage = async (page: SavedPage) => {
    const now = Date.now()
    const id = `page-${crypto.randomUUID()}`, name = `${page.name} 副本`
    await db.pages.put({ ...page, id, name, createdAt: now, updatedAt: now })
    await reload()
  }
  const deletePage = async (page: SavedPage) => {
    if (!await confirmation.confirm({ title: '删除本地页面', message: <>将删除本地页面<strong>“{page.name}”</strong>。此操作不会修改设备内容。</>, confirmLabel: '删除', danger: true })) return
    await db.pages.delete(page.id)
    await reload()
  }
  const exportPage = async (page: SavedPage) => {
    if (!page.preview) return
    try { await downloadCanvasPng(await dataUrlCanvas(page.preview), `${page.name}.png`) }
    catch (error) { setMessage(error instanceof Error ? error.message : '页面导出失败，请重试。') }
  }
  const sendPage = async (page: SavedPage, saveToSlot: boolean) => {
    if (!canSendSavedPage(page)) { const mode=savedPageMode(page);setMessage(mode && mode !== 'static' ? pageModeNotices[mode] : staticContentNotice); return }
    if (!page.preview) return
    setSendTarget({ page, store: saveToSlot })
  }

  return <div className="page library-page">
    <PageHeader title="资源" />
    <div className="library-tabs" role="tablist">
      <button className={tab === 'pages' ? 'active' : ''} onClick={() => setTab('pages')}><Images />我的页面 <span>{pages.length}</span></button>
      <button className={tab === 'assets' ? 'active' : ''} onClick={() => setTab('assets')}><Upload />图片素材 <span>{assets.length}</span></button>
    </div>
    <p className="library-description">{tab === 'pages' ? '已保存的页面，可继续编辑或发送到设备。' : '图片素材可在画板的图片控件中插入。同一素材可以用于多个页面，不会修改原图。'}</p>
    {message && <FeedbackNotice className="library-message" noticeKey={messageRevision}>{message}</FeedbackNotice>}

    {tab === 'pages' && <>
      <section className="template-toolbar" aria-label="页面筛选">
        <div className="segmented-control template-kind-filter">{(['all', ...creationTemplateKindOrder, 'device-info'] as const).map(kind => <button key={kind} className={filters.kind === kind ? 'active' : ''} onClick={() => setFilters({ ...filters, kind })}>{documentKindLabels[kind]}</button>)}</div>
        <div className="template-filter-row page-filter-row">
          <PageModeFilter label="页面类型筛选" value={filters.pageMode} change={pageMode=>setFilters({...filters,pageMode})} />
          <label className="template-filter-control page-sort-control"><span>排序</span><select aria-label="页面排序" value={sort} onChange={event => setSort(event.target.value)}><option value="updated">最近保存</option><option value="name">名称</option><option value="resolution">分辨率从小到大</option></select></label>
          <label className="template-filter-control template-resolution-filter"><Monitor /><span>分辨率</span><select aria-label="页面分辨率" value={filters.resolution} onChange={event => setFilters({ ...filters, resolution: event.target.value })}><option value="all">全部分辨率</option>{resolutions.map(resolution => <option key={resolution} value={resolution}>{resolution.replace('x', ' × ')}</option>)}</select></label>
          <div className="segmented-control compact orientation-filter" aria-label="页面方向"><button aria-label="全部方向" title="全部方向" className={filters.layout === 'all' ? 'active' : ''} onClick={() => setFilters({ ...filters, layout: 'all' })}>全部</button>{([
            ['landscape', '横屏', RectangleHorizontal], ['portrait', '竖屏', RectangleVertical], ['square', '方屏', Square],
          ] as const).map(([layout, label, Icon]) => <button key={layout} aria-label={`${label}页面`} title={`${label}页面`} className={filters.layout === layout ? 'active' : ''} onClick={() => setFilters({ ...filters, layout })}><Icon /><span>{label}</span></button>)}</div>
          <label className="search-field template-search"><Search /><input aria-label="搜索页面" placeholder="搜索页面" value={filters.query} onChange={event => setFilters({ ...filters, query: event.target.value })} /></label>
        </div>
      </section>
      <div className="template-results-bar"><span>共 {filteredPages.length} 个页面</span>{Object.entries(filters).some(([key, value]) => value !== emptyDocumentFilters[key as keyof DocumentFilters]) && <button className="button secondary" onClick={() => setFilters(emptyDocumentFilters)}>清除筛选</button>}</div>
    </>}
    {tab === 'pages' && (pages.length ? filteredPages.length ? <section className="resource-grid">
        {filteredPages.map((page) => { const mode = savedPageMode(page), sendable = canSendSavedPage(page)
          const blocked = mode && mode !== 'static' ? pageModeNotices[mode] : '原稿不可用或含有动态内容，请先继续编辑。'
          return <article className="page-resource" key={page.id}>
        <div className="resource-preview">{page.preview ? <img src={page.preview} alt="" /> : <Images />}</div>
          <div className="resource-copy"><strong>{page.name}</strong><small>{page.width} × {page.height} · {new Date(page.updatedAt).toLocaleString()}</small><small>{mode ? pageModeLabels[mode] : '原稿不可用'} · {documentKindLabels[documentKind(page)]} · 保存在当前浏览器</small>{!sendable && <small>{mode === 'dynamic' ? '定制动态页面暂不能保存到设备' : mode === 'host-dynamic' ? '打开画板，导出给 CLI 定时推送' : '请先检查原稿'}</small>}</div>
        <div className="resource-actions">
          <Link className="button secondary" to={`/create/canvas/${page.templateId}?width=${page.width}&height=${page.height}&page=${encodeURIComponent(page.id)}`}><Edit3 />继续编辑</Link>
          <button className="icon-button" title="复制" onClick={() => void duplicatePage(page)}><Copy /></button>
          <button className="icon-button" title="导出 PNG" disabled={!page.preview} onClick={() => exportPage(page)}><Download /></button>
            <button className="icon-button" aria-label="发送到屏幕" title={sendable ? '发送到屏幕' : blocked} disabled={!sendable || !page.preview || !connected || action !== 'idle'} onClick={() => void sendPage(page, false)}><Send /></button>
            {capabilities.imageStore && <button className="icon-button" aria-label="发送并保存到设备槽" title={sendable ? '发送并保存到设备槽' : blocked} disabled={!sendable || !page.preview || !connected || action !== 'idle'} onClick={() => void sendPage(page, true)}><HardDrive /></button>}
          <button className="icon-button danger" title="删除" onClick={() => void deletePage(page)}><Trash2 /></button>
        </div>
        </article>})}
    </section> : <section className="template-empty"><strong>没有匹配的页面</strong><span>调整筛选条件，或清除筛选查看全部页面。</span><button className="button secondary" onClick={() => setFilters(emptyDocumentFilters)}>清除筛选</button></section> : <EmptyPages />)}

    {tab === 'assets' && <>
      <input ref={assetInput} hidden type="file" accept="image/*" multiple onChange={(event) => void importAssets(event.target.files)} />
      {assets.length ? <>
        <div className="asset-library-actions"><button className="button secondary" onClick={() => assetInput.current?.click()}><ImagePlus />导入素材</button></div>
        <section className="resource-grid asset-grid">{assets.map((asset) => <AssetItem key={asset.id} asset={asset} onDelete={async () => { await db.assets.delete(asset.id); await reload() }} />)}</section>
      </> : <section className="empty-state"><Upload /><h2>还没有素材</h2><p>导入图片后，它们会离线保存在当前浏览器中。</p><button className="button primary" onClick={() => assetInput.current?.click()}><ImagePlus />导入素材</button></section>}
    </>}

    {sendTarget && <SendCanvasDialog defaultName={sendTarget.page.name}
      initialDestination={sendTarget.store ? 'store' : 'display'}
      contentType={['schedule', 'wifi', 'todo', 'business'].includes(documentKind(sendTarget.page)) ? documentKind(sendTarget.page) as ImageSlotContentType : 'page'}
      prepareCanvas={() => dataUrlCanvas(sendTarget.page.preview!)} canSend={() => canSendSavedPage(sendTarget.page)}
      message={setMessage} close={() => setSendTarget(null)} />}
    {confirmation.dialog}
  </div>
}

function EmptyPages() {
  return <section className="empty-state"><div className="empty-visual" aria-hidden="true"><i /><i /><i /></div><h2>还没有保存的页面</h2><p>从模板或空白页面开始创作，选择「保存为页面」后即可在这里继续编辑。</p><Link className="button primary" to="/create"><ImagePlus />创建第一个页面</Link></section>
}

function AssetItem({ asset, onDelete }: { asset: Asset; onDelete: () => void }) {
  const [url, setUrl] = useState('')
  useEffect(() => {
    const next = URL.createObjectURL(asset.blob)
    setUrl(next)
    return () => URL.revokeObjectURL(next)
  }, [asset.blob])
  return <article className="page-resource"><div className="resource-preview">{url && <img src={url} alt="" />}</div><div className="resource-copy"><strong>{asset.name}</strong><small>{asset.type || '图片'} · {(asset.blob.size / 1024).toFixed(1)} KiB</small></div><div className="resource-actions"><button className="icon-button danger" title="删除" onClick={onDelete}><Trash2 /></button></div></article>
}
