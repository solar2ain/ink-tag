// @vitest-environment jsdom
import { act } from 'react'
import { createRoot, type Root } from 'react-dom/client'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { useDeviceStore } from '../store/device'
import { OperationProgressDialog } from './OperationProgressDialog'

describe('operation progress dialog failures', () => {
  let host: HTMLDivElement
  let root: Root

  beforeEach(async () => {
    vi.stubGlobal('IS_REACT_ACT_ENVIRONMENT', true)
    vi.useFakeTimers()
    host = document.createElement('div')
    document.body.append(host)
    root = createRoot(host)
    useDeviceStore.getState().resetDevice()
    await act(async () => root.render(<OperationProgressDialog />))
  })

  afterEach(async () => {
    await act(async () => root.unmount())
    host.remove()
    vi.useRealTimers()
    vi.unstubAllGlobals()
  })

  it('keeps the progress surface open in an explicit error state', async () => {
    await act(async () => useDeviceStore.getState().setAction('calendar', null, '设备正在绘制日历布局。'))
    await act(async () => vi.advanceTimersByTime(600))
    expect(host.querySelector('.operation-dialog')).not.toBeNull()

    await act(async () => useDeviceStore.getState().failAction('calendar', '设备操作超时'))

    expect(useDeviceStore.getState().action).toBe('idle')
    expect(host.querySelector('.operation-dialog.failed')?.textContent).toContain('日历失败')
    expect(host.querySelector('.operation-dialog.failed')?.textContent).toContain('设备操作超时')

    await act(async () => (host.querySelector('.operation-dialog.failed button') as HTMLButtonElement).click())
    expect(useDeviceStore.getState().operationFailure).toBeNull()
    expect(host.querySelector('.operation-dialog')).toBeNull()
  })
})
