// @vitest-environment jsdom
import { act } from 'react'
import { createRoot } from 'react-dom/client'
import { expect, it, vi } from 'vitest'
import { ToolRoute } from './ToolRoute'
import { useDeviceStore } from '../store/device'

it.each(['图片工具', '创作画板'])('contains %s failures and logs the actual cause without losing the shell', async name => {
  vi.stubGlobal('IS_REACT_ACT_ENVIRONMENT', true)
  const error = vi.spyOn(console, 'error').mockImplementation(() => {})
  const previous = useDeviceStore.getState()
  useDeviceStore.setState({ connection: 'connected', deviceName: 'IKT_GR_TEST' })
  const host = document.createElement('div'), root = createRoot(host)
  const load = vi.fn().mockRejectedValueOnce(new TypeError('Failed to fetch dynamically imported module: /assets/tool.js'))
    .mockResolvedValue({ default: () => <div>工具可用</div> })
  try {
    await act(async () => root.render(<><header>连接保留</header><ToolRoute name={name} load={load} /></>))
    expect(host.querySelector('[role=alert]')?.textContent).toContain(`${name}未能打开`)
    expect(useDeviceStore.getState().logs[0]?.message).toContain('/assets/tool.js')
    await act(async () => host.querySelector('button')!.click())
    expect(host.textContent).toContain('工具可用')
    expect(host.textContent).toContain('连接保留')
    expect(useDeviceStore.getState().connection).toBe('connected')
  } finally {
    await act(async () => root.unmount())
    useDeviceStore.setState(previous, true); error.mockRestore(); vi.unstubAllGlobals()
  }
})
