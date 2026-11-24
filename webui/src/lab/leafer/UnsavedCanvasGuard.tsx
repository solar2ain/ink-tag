import { useEffect, useRef, useState } from 'react'
import { createPortal } from 'react-dom'
import { useBlocker } from 'react-router-dom'

/** Router-owned blocking also covers browser Back, without losing the BLE session. */
export function UnsavedCanvasGuard({ dirty, save, draft, discard }: {
  dirty: boolean; save: () => Promise<boolean>; draft: () => Promise<boolean>; discard: () => Promise<boolean>
}) {
  const blocker = useBlocker(dirty), dialog = useRef<HTMLDivElement>(null)
  const pending = useRef(false), [busy, setBusy] = useState(false), [error, setError] = useState('')
  useEffect(() => {
    if (!dirty) return
    const unload = (event: BeforeUnloadEvent) => { event.preventDefault(); event.returnValue = '' }
    window.addEventListener('beforeunload', unload)
    return () => window.removeEventListener('beforeunload', unload)
  }, [dirty])
  useEffect(() => {
    if (blocker.state !== 'blocked') return
    const previous = document.activeElement as HTMLElement | null
    setError(''); dialog.current?.querySelector<HTMLButtonElement>('button')?.focus()
    return () => { if (previous?.isConnected) previous.focus() }
  }, [blocker.state])
  if (blocker.state !== 'blocked') return null
  const leave = async (operation: () => Promise<boolean>) => {
    if (pending.current) return
    pending.current = true; setBusy(true); setError('')
    try {
      if (await operation()) blocker.proceed()
      else setError('未能保存当前修改，请重试或继续编辑。')
    } catch { setError('操作失败，当前画面仍保留，请重试。') }
    finally { pending.current = false; setBusy(false) }
  }
  return createPortal(<div className="modal-backdrop"><div ref={dialog} className="confirmation-dialog unsaved-canvas-dialog" role="dialog" aria-modal="true" aria-labelledby="unsaved-canvas-title"
    onKeyDown={event => {
      if (event.key === 'Escape' && !pending.current) { event.stopPropagation(); blocker.reset() }
      if (event.key === 'Tab') {
        const fields = [...event.currentTarget.querySelectorAll<HTMLButtonElement>('button:not(:disabled)')]
        if (event.shiftKey && document.activeElement === fields[0]) { event.preventDefault(); fields.at(-1)?.focus() }
        else if (!event.shiftKey && document.activeElement === fields.at(-1)) { event.preventDefault(); fields[0]?.focus() }
      }
    }}>
    <h2 id="unsaved-canvas-title">保存画板修改？</h2><p>当前修改尚未保存。草稿仅暂存在此浏览器，下次打开同一画板时恢复，不会加入页面库或发送到设备。</p>
    {error && <p role="alert">{error}</p>}
    <footer><button className="button secondary" disabled={busy} onClick={() => blocker.reset()}>继续编辑</button>
      <button className="button secondary" disabled={busy} onClick={() => void leave(discard)}>不保存</button>
      <button className="button secondary" disabled={busy} onClick={() => void leave(draft)}>暂存草稿</button>
      <button className="button primary" disabled={busy} onClick={() => void leave(save)}>保存页面</button></footer>
  </div></div>, document.body)
}
