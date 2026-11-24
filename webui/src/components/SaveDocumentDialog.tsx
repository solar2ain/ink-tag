import { FileText, LayoutTemplate, X } from 'lucide-react'
import { useRef, useState } from 'react'
import type { PageKind } from '../data/db'
import { documentKindLabels } from '../data/documentLibrary'
import { pageModeLabels, type PageMode } from '../data/pageMode'

export type SaveDocumentChoice = { target: 'page' | 'template'; name: string; kind: PageKind }
export function SaveDocumentDialog({ name, kind, pageMode = 'static', save, close }: {
  name: string; kind: PageKind; pageMode?: PageMode; save: (choice: SaveDocumentChoice) => Promise<boolean>; close: () => void
}) {
  const [choice, setChoice] = useState<SaveDocumentChoice>({ target: 'page', name, kind })
  const [saving, setSaving] = useState(false), [error, setError] = useState('')
  const pending = useRef(false)
  return <div className="modal-backdrop" onMouseDown={event => { if (event.target === event.currentTarget && !pending.current) close() }}>
    <form className="confirmation-dialog save-document-dialog" role="dialog" aria-modal="true" aria-labelledby="save-document-title"
      onKeyDown={event => {
        if (event.key === 'Escape' && !pending.current) { event.stopPropagation(); close() }
        if (event.key === 'Tab') {
          const fields = [...event.currentTarget.querySelectorAll<HTMLElement>('button:not(:disabled),input:not(:disabled),select:not(:disabled)')]
          const first = fields[0], last = fields.at(-1)
          if (event.shiftKey && document.activeElement === first) { event.preventDefault(); last?.focus() }
          else if (!event.shiftKey && document.activeElement === last) { event.preventDefault(); first?.focus() }
        }
      }} onSubmit={async event => {
        event.preventDefault()
        if (pending.current || !choice.name.trim()) return
        pending.current = true; setSaving(true); setError('')
        try { if (await save({ ...choice, name: choice.name.trim() })) close(); else setError('保存失败，请检查本机存储空间后重试。') }
        finally { pending.current = false; setSaving(false) }
      }}>
      <header><div><FileText /><h2 id="save-document-title">保存作品</h2></div><button className="icon-button" type="button" aria-label="关闭" disabled={saving} onClick={close}><X /></button></header>
      <p>{pageModeLabels[pageMode]} · {pageMode !== 'static' ? '仅保存到本机，动态功能暂不支持。' : '保存到本机后可继续编辑或发送到设备。'}</p>
      <fieldset className="save-targets" disabled={saving}><legend>保存为</legend>
        <label className={choice.target === 'page' ? 'active' : ''}><input type="radio" name="save-target" aria-label="页面" checked={choice.target === 'page'} onChange={() => setChoice({ ...choice, target: 'page' })} /><FileText /><span><strong>页面</strong><small>在「资源 · 我的页面」继续编辑</small></span></label>
        <label className={choice.target === 'template' ? 'active' : ''}><input type="radio" name="save-target" aria-label="模板" checked={choice.target === 'template'} onChange={() => setChoice({ ...choice, target: 'template' })} /><LayoutTemplate /><span><strong>模板</strong><small>在创作中复用，每次创建独立页面</small></span></label>
      </fieldset>
      <label>{choice.target === 'page' ? '页面名称' : '模板名称'}<input aria-label="作品名称" autoFocus required maxLength={80} disabled={saving} value={choice.name} onChange={event => setChoice({ ...choice, name: event.target.value })} /></label>
      <label>内容类型<select aria-label="作品类型" disabled={saving} value={choice.kind} onChange={event => setChoice({ ...choice, kind: event.target.value as PageKind })}>{Object.entries(documentKindLabels).filter(([key]) => key !== 'all' && (key !== 'device-info' || kind === 'device-info')).map(([key, label]) => <option key={key} value={key}>{label}</option>)}</select></label>
      {error && <p role="alert">{error}</p>}
      <footer><button className="button secondary" type="button" disabled={saving} onClick={close}>取消</button><button className="button primary" disabled={saving} type="submit">{saving ? '保存中' : choice.target === 'page' ? '保存页面' : '保存模板'}</button></footer>
    </form>
  </div>
}
