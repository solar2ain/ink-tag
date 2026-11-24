// @vitest-environment jsdom
import { act } from 'react'
import { createRoot, type Root } from 'react-dom/client'
import { afterEach, beforeEach, expect, it, vi } from 'vitest'
import { FeedbackNotice, useFeedbackMessage } from './FeedbackNotice'

let root: Root, host: HTMLDivElement
beforeEach(() => { vi.useFakeTimers(); vi.stubGlobal('IS_REACT_ACT_ENVIRONMENT', true); host = document.createElement('div'); document.body.append(host); root = createRoot(host) })
afterEach(async () => { await act(async () => root.unmount()); host.remove(); vi.useRealTimers(); vi.unstubAllGlobals() })
it('shows above page content and dismisses an ordinary message after five seconds', async () => {
  await act(async () => root.render(<FeedbackNotice>保存成功</FeedbackNotice>))
  expect(host.textContent).toBe('')
  expect(document.querySelector('#inktag-feedback-layer [role=status]')?.textContent).toContain('保存成功')
  await act(async () => vi.advanceTimersByTime(5000))
  expect(document.querySelector('.feedback-toast')?.classList.contains('is-leaving')).toBe(true)
  await act(async () => vi.advanceTimersByTime(250))
  expect(document.querySelector('.feedback-toast')).toBeNull()
})
it('keeps uncertain results until explicitly dismissed and gives errors an alert role', async () => {
  await act(async () => root.render(<FeedbackNotice persistent className="library-message error">写入结果未确认</FeedbackNotice>))
  await act(async () => vi.advanceTimersByTime(60000))
  expect(document.querySelector('[role=alert]')?.textContent).toContain('未确认')
  await act(async () => document.querySelector<HTMLButtonElement>('[aria-label="关闭提示"]')!.click())
  await act(async () => vi.advanceTimersByTime(250))
  expect(document.querySelector('.feedback-toast')).toBeNull()
})
it('shows the same operation result again on a new explicit update', async () => {
  function Form() { const [message, setMessage, revision] = useFeedbackMessage(); return <><button onClick={() => setMessage('保存成功')}>保存</button>{message && <FeedbackNotice noticeKey={revision}>{message}</FeedbackNotice>}</> }
  await act(async () => root.render(<Form />))
  await act(async () => host.querySelector<HTMLButtonElement>('button')!.click())
  await act(async () => vi.advanceTimersByTime(5000))
  await act(async () => vi.advanceTimersByTime(250))
  expect(document.querySelector('.feedback-toast')).toBeNull()
  await act(async () => host.querySelector<HTMLButtonElement>('button')!.click())
  expect(document.querySelector('.feedback-toast')?.textContent).toContain('保存成功')
})

it('pauses dismissal while the user interacts with an error', async () => {
  await act(async () => root.render(<FeedbackNotice>保存失败，请重试</FeedbackNotice>))
  await act(async () => vi.advanceTimersByTime(5000))
  const notice = document.querySelector<HTMLDivElement>('.feedback-toast')!
  expect(notice.getAttribute('role')).toBe('alert')
  await act(async () => notice.dispatchEvent(new MouseEvent('mouseover', { bubbles: true })))
  await act(async () => vi.advanceTimersByTime(10000))
  expect(document.querySelector('.feedback-toast')).not.toBeNull()
  await act(async () => notice.dispatchEvent(new MouseEvent('mouseout', { bubbles: true })))
  await act(async () => vi.advanceTimersByTime(8000))
  await act(async () => vi.advanceTimersByTime(250))
  expect(document.querySelector('.feedback-toast')).toBeNull()
})
it('cleans portal nodes when leaving a page and keeps simultaneous notices independent', async () => {
  await act(async () => root.render(<><FeedbackNotice>第一条</FeedbackNotice><FeedbackNotice>第二条</FeedbackNotice></>))
  expect(document.querySelectorAll('.feedback-toast')).toHaveLength(2)
  await act(async () => document.querySelector<HTMLButtonElement>('[aria-label="关闭提示"]')!.click())
  await act(async () => vi.advanceTimersByTime(250))
  expect(document.querySelectorAll('.feedback-toast')).toHaveLength(1)
  await act(async () => root.render(null))
  expect(document.getElementById('inktag-feedback-layer')).toBeNull()
})

it('keeps a newer result when the previous exit has started', async () => {
  await act(async () => root.render(<FeedbackNotice noticeKey={1}>第一条</FeedbackNotice>))
  await act(async () => vi.advanceTimersByTime(5000))
  expect(document.querySelector('.is-leaving')).not.toBeNull()
  await act(async () => root.render(<FeedbackNotice noticeKey={2}>第二条</FeedbackNotice>))
  await act(async () => vi.advanceTimersByTime(250))
  expect(document.querySelector('.feedback-toast')?.textContent).toContain('第二条')
  expect(document.querySelector('.is-leaving')).toBeNull()
})

it('skips exit animation for reduced motion', async () => {
  vi.stubGlobal('matchMedia', () => ({ matches: true }))
  await act(async () => root.render(<FeedbackNotice>保存成功</FeedbackNotice>))
  await act(async () => document.querySelector<HTMLButtonElement>('[aria-label="关闭提示"]')!.click())
  expect(document.querySelector('.feedback-toast')).toBeNull()
})
