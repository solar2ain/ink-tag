import { AlertTriangle, X } from 'lucide-react'
import { useCallback, useEffect, useRef, useState, type ReactNode } from 'react'

export interface ConfirmationOptions {
  title: string
  message: ReactNode
  confirmLabel?: string
  cancelLabel?: string
  danger?: boolean
}

export function useConfirmationDialog() {
  const [request, setRequest] = useState<ConfirmationOptions | null>(null)
  const resolver = useRef<((accepted: boolean) => void) | null>(null)

  const close = useCallback((accepted: boolean) => {
    resolver.current?.(accepted)
    resolver.current = null
    setRequest(null)
  }, [])

  const confirm = useCallback((options: ConfirmationOptions) => new Promise<boolean>((resolve) => {
    resolver.current?.(false)
    resolver.current = resolve
    setRequest(options)
  }), [])

  useEffect(() => () => resolver.current?.(false), [])
  useEffect(() => {
    if (!request) return
    const closeOnEscape = (event: KeyboardEvent) => {
      if (event.key === 'Escape') close(false)
    }
    window.addEventListener('keydown', closeOnEscape)
    return () => window.removeEventListener('keydown', closeOnEscape)
  }, [close, request])

  const dialog = request ? <div className="modal-backdrop" onMouseDown={(event) => { if (event.target === event.currentTarget) close(false) }}>
    <section className={`confirmation-dialog${request.danger ? ' confirmation-danger' : ''}`} role="dialog" aria-modal="true" aria-labelledby="confirmation-dialog-title" aria-describedby="confirmation-dialog-message">
      <header><div className={request.danger ? 'danger' : ''}><AlertTriangle /><h2 id="confirmation-dialog-title">{request.title}</h2></div><button className="icon-button" type="button" title="关闭" aria-label="关闭" onClick={() => close(false)}><X /></button></header>
      <p id="confirmation-dialog-message">{request.message}</p>
      <footer><button className="button secondary" type="button" onClick={() => close(false)}>{request.cancelLabel || '取消'}</button><button className={request.danger ? 'button danger' : 'button primary'} type="button" autoFocus onClick={() => close(true)}>{request.confirmLabel || '确认'}</button></footer>
    </section>
  </div> : null

  return { confirm, dialog }
}
