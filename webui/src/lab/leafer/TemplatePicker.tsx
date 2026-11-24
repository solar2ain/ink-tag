import { useState } from 'react'
import { X, CalendarDays, Clock3, Contact, Table2, ListTodo, Image, Wifi, CloudSun } from 'lucide-react'
import { labTemplates, templateKindLabels, templateKindOrder } from './templateLibrary'
import { templateResolutionOptions, type TemplateKind } from '../../data/templates'
const icons = { calendar: CalendarDays, clock: Clock3, 'device-info': Contact, schedule: Table2, todo: ListTodo, business: Contact, wifi: Wifi, photo: Image, weather: CloudSun, 'date-card': CalendarDays }
export function TemplatePicker({ width, height, choose, close }: { width: number; height: number; choose: (id: string) => void; close: () => void }) {
  const [kind,setKind] = useState<TemplateKind | 'all'>('all')
  const [resolution,setResolution] = useState(`${Math.max(width,height)}x${Math.min(width,height)}`)
  const [query,setQuery] = useState('')
  const items = labTemplates.filter(t => (kind === 'all' || kind === t.kind) && (resolution === 'all' || `${Math.max(t.width,t.height)}x${Math.min(t.width,t.height)}` === resolution) && `${t.name} ${t.width}x${t.height}`.includes(query.trim()))
  return <div className="lab-dialog-backdrop"><section role="dialog" aria-modal="true" aria-labelledby="lab-library-title" className="lab-template-dialog" onKeyDown={e => {
    if (e.key === 'Escape') close()
    if (e.key === 'Tab') { const nodes = [...e.currentTarget.querySelectorAll<HTMLElement>('button,input,select')]; const first=nodes[0], last=nodes.at(-1); if (e.shiftKey && document.activeElement === first) { e.preventDefault(); last?.focus() } else if (!e.shiftKey && document.activeElement === last) { e.preventDefault(); first?.focus() } }
  }}>
    <header><div><h2 id="lab-library-title">模板库</h2><p>与 InkTag 共用模板目录 · 创建固定尺寸的新画板</p></div><button onClick={() => choose('blank')}>空白画板 · {width} × {height}</button><button autoFocus aria-label="关闭模板库" onClick={close}><X size={18}/></button></header>
    <div className="template-filters"><input aria-label="搜索模板" placeholder="搜索模板名称" value={query} onChange={e => setQuery(e.target.value)} /><select aria-label="模板屏幕尺寸" value={resolution} onChange={e => setResolution(e.target.value)}><option value="all">全部尺寸</option>{templateResolutionOptions.map(r => <option key={r.id} value={r.id}>{r.width} × {r.height} · {r.label}</option>)}</select></div>
    <nav className="template-categories" aria-label="模板类型">{(['all',...templateKindOrder] as const).map(k => <button key={k} aria-pressed={kind === k} onClick={() => setKind(k)}>{templateKindLabels[k]}</button>)}</nav>
    <div className="lab-template-grid">{items.map(t => { const Icon = icons[t.kind]; return <button key={t.id} onClick={() => choose(t.id)} className="lab-template-card"><Icon size={24}/><strong>{t.name}</strong><span>{t.width} × {t.height} · {t.layout === 'portrait' ? '竖屏' : t.layout === 'square' ? '方屏' : '横屏'}</span><small>{t.summary}</small></button> })}{!items.length && <p>当前筛选没有模板，请选择其他尺寸或类型。</p>}</div>
  </section></div>
}
