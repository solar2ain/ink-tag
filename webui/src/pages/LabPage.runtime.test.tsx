// @vitest-environment jsdom
import { act } from 'react'
import { createRoot, type Root } from 'react-dom/client'
import { afterEach, beforeEach, expect, it, vi } from 'vitest'
import { LabPanel } from './LabPage'
import { useDeviceStore } from '../store/device'

const client = vi.hoisted(() => ({ listTests: vi.fn().mockResolvedValue([]), simulateNfcTap: vi.fn(async () => {}) }))
vi.mock('../protocol/client', () => ({ inktagClient: client }))
let host: HTMLDivElement, root: Root
beforeEach(() => {
  vi.stubGlobal('IS_REACT_ACT_ENVIRONMENT', true)
  vi.clearAllMocks()
  useDeviceStore.getState().resetDevice()
  const state = useDeviceStore.getState()
  useDeviceStore.setState({ connection: 'connected', boardId: 'tlsr8359_hanshow',
    activation: { phase: 'active', edition: 'advanced' },
    capabilities: { ...state.capabilities, nfc: 'tap' },
    settingsCapabilities: { ...state.settingsCapabilities, protocolVersion: 1, nfcActionMask: 3 } })
  host = document.createElement('div'); document.body.append(host); root = createRoot(host)
})
afterEach(async () => { await act(async () => root.unmount()); host.remove(); useDeviceStore.getState().resetDevice(); vi.unstubAllGlobals() })
const render = async () => act(async () => root.render(<LabPanel />))
it.each([true,false])('does not offer a retired NFC tap simulation, hardware=%s', async present => {
 const state=useDeviceStore.getState()
 useDeviceStore.setState({capabilities:{...state.capabilities,nfc:present?'tap':'none'}})
 await render()
 expect([...host.querySelectorAll('.test-card')].some(n=>n.textContent?.includes('NFC模拟'))).toBe(false)
 expect(host.textContent).toContain('屏幕')
 expect(client.simulateNfcTap).not.toHaveBeenCalled()
})
