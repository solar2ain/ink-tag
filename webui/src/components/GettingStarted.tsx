import { ChevronLeft, ChevronRight, ExternalLink, Image, Leaf, Sun, X, ZoomIn } from 'lucide-react'
import { useEffect, useId, useRef, useState } from 'react'
import connectScreenshot from '../assets/onboarding/connect.png'
import screenScreenshot from '../assets/onboarding/screen.png'
import templatesScreenshot from '../assets/onboarding/schedule-template.png'
import sendScreenshot from '../assets/onboarding/schedule-editor.png'
import { useLanguage, useTranslation } from '../preferences'
import { ClientUpdates } from './ClientUpdates'
import { bluetoothNameFromDeviceUrl } from '../qr/qr'
import { EpaperColorGuide } from './EpaperColorGuide'

export const GUIDE_STORAGE_KEY = 'inktag-getting-started-v1'
export const PROJECT_URL = 'https://github.com/solar2ain/ink-tag'
export const WEBUI_URL = 'https://solar2ain.github.io/ink-tag/'
type GuideMark = {
  box: [number, number, number, number]
  label: [number, number, number]
  arrow: [number, number, number, number]
  text: string
}
type GuideFigure = {
  src: string; title: string; alt: string; text: string
  marks?: { width: number; height: number; items: GuideMark[] }
}
const slides: { title: string; lead: string; note: string; figures?: GuideFigure[]; colors?: boolean }[] = [
  { title: '认识墨水屏', lead: '像纸一样利用环境光阅读，适合显示不频繁变化的信息。',
    note: '不频繁刷新并保持低功耗待机时，纽扣电池也能长时间使用。实际续航取决于电池容量、刷新频率和蓝牙、Find 等活动，并非整机完全不耗电。' },
  { title:'墨水屏的颜色',lead:'先了解屏幕的实际色板，再选择合适的图片处理方式。',note:'抖动只改变图片的像素排列，不改变屏幕驱动、刷新方式或真实墨色。',colors:true },
  { title: '连接与核对屏幕', lead: '先找到自己的设备，再确认屏幕，避免发错设备或选错配置。',
    note: '需要支持 Web Bluetooth 的浏览器和系统蓝牙。首次选择需要浏览器授权；无法使用时，可改用 Windows、Linux 或 macOS CLI。',
    figures: [
      { src: connectScreenshot, title: '连接设备', alt: '连接菜单截图：框和箭头依次标出顶部连接菜单与选择附近设备按钮。',
        text: '打开顶部连接菜单，点“选择附近设备”，再在浏览器列表中选择设备的蓝牙名，例如 IKT_GR_3A57。' },
      { src: screenScreenshot, title: '核对当前屏幕', alt: '设备显示页截图：框和箭头标出显示页入口与当前屏幕 Profile。',
        text: '进入“设置 → 设备设置 → 显示”，核对当前选中的 Profile、分辨率和颜色。配置正确时无需重新保存；不要凭相似板名更换。' },
    ] },
  { title: '选择内容与发送', lead: '选模板、看预览、再发送；也可以从“图片”导入自己的图片。',
    note: '发送前请确认设备、分辨率和颜色。传输或刷新期间请保持供电；出现错误或结果未确认时，先查看提示，不要重复发送。',
    figures: [
      { src: templatesScreenshot, title: '选择课程表模板', alt: '课程表模板列表截图：编号红框和箭头标出分辨率筛选，以及可点击的“一周课表”模板卡片。',
        text: '打开“创作”，选择“课表”，按屏幕分辨率和方向筛选，再点击“一周课表”。创建后画板尺寸固定，换屏时需要重新创建。',
        marks: { width: 592, height: 746, items: [
          { box: [290, 101, 276, 47], label: [284, 206, 280], arrow: [440, 206, 440, 150], text: '按屏幕筛选' },
          { box: [10, 290, 556, 339], label: [240, 638, 320], arrow: [527, 638, 563, 607], text: '打开课程表模板' },
        ] } },
      { src: sendScreenshot, title: '编辑课程并发送', alt: '课程表画板截图：编号红框和箭头标出可编辑的课程表，以及顶部纸飞机“发送到设备”按钮；未连接时按钮不可用。',
        text: '先点击表格选中控件，再点击单元格修改课程和时间；可在“控件与属性”中调整样式。核对预览后，连接设备并点击顶部纸飞机按钮“发送到设备”，等待传输和屏幕刷新完成。课表按静态图片发送，内容不会自动更新。设备动态页面暂不支持保存到设备。',
        marks: { width: 592, height: 746, items: [
          { box: [104, 297, 369, 228], label: [24, 563, 250], arrow: [170, 563, 170, 527], text: '编辑课程内容' },
          { box: [535, 96, 37, 43], label: [285, 208, 281], arrow: [550, 208, 553, 141], text: '连接后发送' },
        ] } },
    ] },
]

function GuideImage({ figure, fullSize = false }: { figure: GuideFigure; fullSize?: boolean }) {
  const t = useTranslation()
  const en = useLanguage() === 'en'
  const arrowId = useId()
  const img = <img src={figure.src} alt={en ? `${t(figure.title)}: numbered boxes and arrows identify the controls.` : figure.alt} />
  if (!figure.marks) return img
  const { width, height, items } = figure.marks
  return <div className="guide-annotated" style={fullSize ? { width } : undefined}>
    {img}
    <svg className="guide-annotations" viewBox={`0 0 ${width} ${height}`} aria-hidden="true">
      <defs><marker id={arrowId} markerWidth="7" markerHeight="7" refX="6" refY="3.5" orient="auto"><path d="M0 0L7 3.5L0 7Z" fill="currentColor" /></marker></defs>
      {items.map((mark, index) => <g key={mark.text}>
        <rect className="guide-mark-box" x={mark.box[0]} y={mark.box[1]} width={mark.box[2]} height={mark.box[3]} rx="6" />
        <path className="guide-mark-arrow" d={`M${mark.arrow[0]} ${mark.arrow[1]}L${mark.arrow[2]} ${mark.arrow[3]}`} markerEnd={`url(#${arrowId})`} />
        <g transform={`translate(${mark.label[0]} ${mark.label[1]})`}>
          <rect className="guide-mark-label" width={mark.label[2]} height="40" rx="20" />
          <circle className="guide-mark-number" cx="20" cy="20" r="16" />
          <text className="guide-mark-index" x="20" y="27" textAnchor="middle">{index + 1}</text>
          <text className="guide-mark-text" x="44" y="27">{t(mark.text)}</text>
        </g>
      </g>)}
    </svg>
  </div>
}

export function GettingStartedDialog({ onClose }: { onClose: () => void }) {
  const t = useTranslation()
  const en = useLanguage() === 'en'
  const [step, setStep] = useState(0)
  const [zoom, setZoom] = useState<GuideFigure | null>(null)
  const dialog = useRef<HTMLDivElement>(null)
  const heading = useRef<HTMLHeadingElement>(null)
  const body = useRef<HTMLElement>(null)
  const zoomClose = useRef<HTMLButtonElement>(null)
  const touch = useRef<{ x: number; y: number } | null>(null)
  const slide = slides[step]
  useEffect(() => {
    const previous = document.activeElement as HTMLElement | null
    heading.current?.focus()
    return () => previous?.focus()
  }, [])
  useEffect(() => { body.current?.scrollTo?.(0, 0) }, [step])
  useEffect(() => {
    if (!zoom) return
    const previous = document.activeElement as HTMLElement | null
    zoomClose.current?.focus()
    return () => previous?.focus()
  }, [zoom])
  const move = (delta: number) => setStep((value) => Math.max(0, Math.min(slides.length - 1, value + delta)))
  return <div className="guide-backdrop">
    <div className="getting-started" role="dialog" aria-modal="true" aria-labelledby="guide-title" ref={dialog}
      onKeyDown={(event) => {
        if (zoom) {
          if (event.key === 'Escape') { event.preventDefault(); setZoom(null) }
          if (event.key === 'Tab') { event.preventDefault(); zoomClose.current?.focus() }
          return
        }
        if (event.key === 'Escape') { event.preventDefault(); onClose() }
        if (event.key === 'ArrowLeft') { event.preventDefault(); move(-1) }
        if (event.key === 'ArrowRight') { event.preventDefault(); move(1) }
        if (event.key === 'Tab') {
          const nodes = Array.from(dialog.current?.querySelectorAll<HTMLElement>('button:not(:disabled),a[href]') ?? [])
          const first = nodes[0], last = nodes[nodes.length - 1]
          if (event.shiftKey && (document.activeElement === first || document.activeElement === heading.current)) { event.preventDefault(); last?.focus() }
          else if (!event.shiftKey && document.activeElement === last) { event.preventDefault(); first?.focus() }
        }
      }}>
      <header className="guide-header"><span>{t('InkTag 入门教程')}</span><button className="icon-button" aria-label={t('关闭教程')} onClick={onClose}><X /></button></header>
      <nav className="guide-tabs" role="tablist" aria-label={en?'Guide topics':'教程主题'}>{slides.map((item,index)=><button key={item.title} id={`guide-tab-${index}`} role="tab" aria-selected={step===index} aria-controls="guide-panel" onClick={()=>setStep(index)}>{en?['E-paper','Connect & screen','Create & send','Color & images'][index]:item.title}</button>)}</nav>
      <article className="guide-slide" aria-live="polite" ref={body}
        id="guide-panel" role="tabpanel" aria-labelledby={`guide-tab-${step}`}
        onTouchStart={(event) => { touch.current = { x: event.touches[0].clientX, y: event.touches[0].clientY } }}
        onTouchEnd={(event) => {
          if (!touch.current || !event.changedTouches.length) return
          const dx = event.changedTouches[0].clientX - touch.current.x
          const dy = event.changedTouches[0].clientY - touch.current.y
          touch.current = null
          if (Math.abs(dx) > 48 && Math.abs(dx) > Math.abs(dy) * 1.5) move(dx < 0 ? 1 : -1)
        }}>
        <p className="guide-step">{en ? `Page ${step + 1} / ${slides.length} · Swipe or use arrow keys` : `第 ${step + 1} / ${slides.length} 页 · 左右滑动或使用方向键`}</p>
        <h2 id="guide-title" tabIndex={-1} ref={heading}>{slide.colors&&en?'Color & images':t(slide.title)}</h2>
        <p className="guide-lead">{slide.colors&&en?'Choose image processing for the inks your screen actually supports.':t(slide.lead)}</p>
        {slide.colors?<EpaperColorGuide/>:!slide.figures ? <>
          <div className="guide-paper-demo" aria-hidden="true"><div><span>InkTag</span><strong>Hello, e-paper.</strong><small>{t('内容停留，画面保留。')}</small></div></div>
          <div className="guide-facts">
            <div><Sun /><h3>{t('利用环境光')}</h3><p>{t('不是手机那样的发光屏。光线充足时容易阅读，暗处需要外部照明。')}</p><p>{t('纸张般的反射式显示，在合适照明下阅读更舒适，减少持续发光的刺激；护眼不代表医疗保证，仍需注意距离和休息。')}</p></div>
            <div><Leaf /><h3>{t('静态画面不需供电')}</h3><p>{t('刷新完成后，屏幕保持画面无需持续供电，断电后也能保图；设备的蓝牙、计时和刷新仍会消耗电量。')}</p></div>
            <div><Image /><h3>{t('刷新较慢，会闪烁')}</h3><p>{t('一次全刷通常需要几秒到十几秒，部分多色屏或低温环境会更久。闪烁通常是正常过程，请保持供电并等待完成，不要重复刷新。')}</p></div>
          </div>
        </> : <div className="guide-figures">{slide.figures.map((figure, index) => <figure key={figure.src}>
          <figcaption><span>{index + 1}</span><h3>{t(figure.title)}</h3></figcaption>
          <button className="guide-screenshot" onClick={() => setZoom(figure)} aria-label={`${en ? 'Enlarge screenshot' : '放大截图'}：${t(figure.title)}`}>
            <GuideImage figure={figure} /><span><ZoomIn />{t('点击放大')}</span>
          </button><p>{t(figure.text)}</p>
        </figure>)}</div>}
        <p className="guide-note">{slide.colors&&en?'Dithering changes image pixels, not the screen driver, refresh method or physical ink colors.':t(slide.note)}</p>
        {slide.figures && <p className="guide-example">{t('截图为示例设备界面；实际可用功能以连接的设备为准。')}</p>}
      </article>
      <footer className="guide-footer">
        <button className="button" onClick={() => move(-1)} disabled={step === 0}><ChevronLeft />{t('上一页')}</button>
        <div className="guide-dots" aria-label={t('教程分页')}>{slides.map((item, index) => <button key={item.title} aria-label={en ? `Page ${index + 1}` : `第 ${index + 1} 页`} aria-current={step === index ? 'step' : undefined} onClick={() => setStep(index)} />)}</div>
        <button className="button primary" onClick={() => step === slides.length - 1 ? onClose() : move(1)}>{t(step === slides.length - 1 ? '开始使用' : '下一页')}{step < slides.length - 1 && <ChevronRight />}</button>
      </footer>
      <p className="guide-reopen">{t('可随时在“设置 → 客户端设置 → 关于”中重新打开教程。')}<button onClick={onClose}>{t('暂时跳过')}</button></p>
      {zoom && <div className="guide-image-zoom" role="dialog" aria-modal="true" aria-labelledby="guide-image-title">
        <header><h3 id="guide-image-title">{t(zoom.title)}</h3><button className="icon-button" ref={zoomClose} aria-label={t('关闭大图')} onClick={() => setZoom(null)}><X /></button></header>
        <div className="guide-image-scroll"><GuideImage figure={zoom} fullSize /></div>
        <p>{t('可滚动查看完整截图。按 Esc 或点击右上角关闭。')}</p>
      </div>}
    </div>
  </div>
}

export function FirstVisitGuide() {
  const [open, setOpen] = useState(false)
  useEffect(() => {
    // A scanned device link prioritizes connection. The unread tutorial stays
    // available in About and on a later ordinary visit; do not mark it seen.
    if (bluetoothNameFromDeviceUrl(window.location.search)) return
    try { setOpen(window.localStorage.getItem(GUIDE_STORAGE_KEY) !== 'seen') }
    catch { setOpen(true) }
  }, [])
  const close = () => {
    try { window.localStorage.setItem(GUIDE_STORAGE_KEY, 'seen') } catch { /* Storage may be unavailable in a private browser. */ }
    setOpen(false)
  }
  return open ? <GettingStartedDialog onClose={close} /> : null
}

export function AboutInkTag() {
  const t = useTranslation()
  const language = useLanguage()
  const [guide, setGuide] = useState(false)
  return <section className="panel about-inktag">
    <div className="panel-heading"><h2>{t('关于 InkTag')}</h2></div>
    <p>{t('InkTag 为兼容设备提供纸张般的图片、日历与时钟展示，支持设备管理和无线更新。可用功能以当前设备为准。')}</p>
    <dl><dt>{t('项目与下载')}</dt><dd><a href={PROJECT_URL} target="_blank" rel="noopener noreferrer">github.com/solar2ain/ink-tag <ExternalLink /></a></dd>
      <dt>{t('Web 上位机')}</dt><dd><a href={WEBUI_URL} target="_blank" rel="noopener noreferrer">solar2ain.github.io/ink-tag/ <ExternalLink /></a></dd>
      <dt>{t('使用手册')}</dt><dd><a href={`${WEBUI_URL}manual/${language}/getting-started.html`} target="_blank" rel="noopener noreferrer">{t('首次使用与操作教程')} <ExternalLink /></a></dd></dl>
    <button className="button" onClick={() => setGuide(true)}>{t('查看入门教程')}</button>
    <ClientUpdates />
    {guide && <GettingStartedDialog onClose={() => setGuide(false)} />}
  </section>
}
