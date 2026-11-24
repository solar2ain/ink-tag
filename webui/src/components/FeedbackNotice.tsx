import { isValidElement, useCallback, useEffect, useState, type Dispatch, type HTMLAttributes, type ReactNode, type SetStateAction } from 'react'
import { createPortal } from 'react-dom'
import { X } from 'lucide-react'

export function useFeedbackMessage(initial = ''): [string, Dispatch<SetStateAction<string>>, number] {
  const [state, setState] = useState({ message: initial, revision: 0 })
  const update = useCallback<Dispatch<SetStateAction<string>>>((next) => {
    setState(previous => ({ message: typeof next === 'function' ? next(previous.message) : next, revision: previous.revision + 1 }))
  }, [])
  return [state.message, update, state.revision]
}

type Props = HTMLAttributes<HTMLDivElement> & {
  children: ReactNode
  noticeKey?: number | string
  persistent?: boolean
}

function feedbackText(node: ReactNode): string {
  if (typeof node === 'string' || typeof node === 'number') return String(node)
  if (Array.isArray(node)) return node.map(feedbackText).join('')
  if (isValidElement<{ children?: ReactNode }>(node)) return feedbackText(node.props.children)
  return ''
}

export function FeedbackNotice({ children, noticeKey, persistent = false, className = 'library-message', role, ...props }: Props) {
  const [slot, setSlot] = useState<HTMLDivElement | null>(null)
  const [visible, setVisible] = useState(true)
  const [leaving, setLeaving] = useState(false)
  const [paused, setPaused] = useState(false)
  const dismiss = useCallback(() => {
    if (window.matchMedia?.('(prefers-reduced-motion: reduce)').matches) setVisible(false)
    else setLeaving(true)
  }, [])
  const contentKey = feedbackText(children)
  const error = role === 'alert' || className.split(/\s+/).includes('error') ||
    /失败|错误|拒绝|未完成|不匹配|Error|failed|denied|timeout/i.test(contentKey)
  useEffect(() => {
    let layer = document.getElementById('inktag-feedback-layer')
    if (!layer) {
      layer = document.createElement('div'); layer.id = 'inktag-feedback-layer'; document.body.append(layer)
    }
    const node = document.createElement('div'); node.className = 'feedback-slot'; layer.prepend(node); setSlot(node)
    return () => { node.remove(); if (!layer!.childElementCount) layer!.remove() }
  }, [])
  useEffect(() => { setVisible(true); setLeaving(false) }, [contentKey, noticeKey])
  useEffect(() => {
    if (!visible || leaving || paused || persistent) return
    const timer = window.setTimeout(dismiss, error ? 8000 : 5000)
    return () => window.clearTimeout(timer)
  }, [contentKey, noticeKey, visible, leaving, paused, persistent, error, dismiss])
  useEffect(() => {
    if (!leaving) return
    // animationend handles the ordinary path; bound cleanup if CSS is absent
    // or an animation is interrupted by browser preferences/backgrounding.
    const timer = window.setTimeout(() => setVisible(false), 250)
    return () => window.clearTimeout(timer)
  }, [leaving])
  if (!slot || !visible) return null
  return createPortal(<div {...props} className={`library-message ${className} feedback-toast${error ? ' error' : ''}${leaving ? ' is-leaving' : ''}`} role={role ?? (error ? 'alert' : 'status')}
    onAnimationEnd={(event) => { if (event.target === event.currentTarget && event.animationName === 'feedback-exit') setVisible(false) }}
    onMouseEnter={() => setPaused(true)} onMouseLeave={() => setPaused(false)}
    onFocus={() => setPaused(true)} onBlur={() => setPaused(false)}>
    <div className="feedback-text">{children}</div>
    <button type="button" className="feedback-close" aria-label="关闭提示" disabled={leaving} onClick={dismiss}><X aria-hidden="true" /></button>
  </div>, slot)
}
