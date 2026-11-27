// @vitest-environment jsdom
import { act } from 'react'
import { createRoot, type Root } from 'react-dom/client'
import { MemoryRouter } from 'react-router-dom'
import { webcrypto } from 'node:crypto'
import { beforeEach, afterEach, expect, it, vi } from 'vitest'
import { FindPage } from '../pages/FindPage'
import { useRuntimeStore } from '../server/runtime'
import { useDeviceStore } from '../store/device'
import vector from '../../../tests/fixtures/find-portable-v1.json'
import { savedFindBackups, saveFindBackup, markFindBackupHistorical } from './offlineOwner'
import { readPortableBackup } from './portable'
import { setPreferences } from '../preferences'

const api = vi.hoisted(() => ({ query: vi.fn(), selectIdentity: vi.fn(), status: vi.fn(), exportBackup: vi.fn(), importBackup: vi.fn(),
  updateIdentity: vi.fn(), trackingHistory: vi.fn(), configureAllTracking: vi.fn(), deleteIdentity: vi.fn(), preparePublicOnlyReplacement: vi.fn(), abortPublicOnlyOwner: vi.fn() }))
const ble = vi.hoisted(() => ({ refreshFindDetails: vi.fn(), configureFind: vi.fn(), unregisterFindOwner: vi.fn(), beginPublicOnlyFindOwner: vi.fn(), readPublicOnlyFindOwnerStatus: vi.fn(), stagePublicOnlyFindOwnerReplacement: vi.fn(), abortFindOwnerRotation: vi.fn(), commitPublicOnlyFindOwner: vi.fn() }))
vi.mock('./client', () => ({ findClient: api }))
vi.mock('../protocol/client', () => ({ inktagClient: ble }))
let host: HTMLDivElement, root: Root
const button = (label: string) => Array.from(host.querySelectorAll('button')).find(b => b.textContent === label)!
async function input(index: number, value: string) {
  const field = host.querySelectorAll<HTMLInputElement>('[role=dialog] input[type=password]')[index]
  await act(async () => {
    Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value')!.set!.call(field, value)
    field.dispatchEvent(new Event('input', { bubbles: true }))
  })
}
beforeEach(async () => {
  const storage = new Map<string, string>()
  vi.stubGlobal('localStorage', { getItem: (k: string) => storage.get(k) ?? null,
    setItem: (k: string, v: string) => storage.set(k, v), removeItem: (k: string) => storage.delete(k),
    clear: () => storage.clear(), get length() { return storage.size } })
  vi.stubGlobal('IS_REACT_ACT_ENVIRONMENT', true); vi.stubGlobal('crypto', webcrypto); vi.clearAllMocks(); localStorage.clear()
  setPreferences({ findMapProvider: 'osm', findMapKeys: { baidu: '', amap: '', osm: '' } })
  localStorage.clear() // Keep export/privacy assertions independent of fixture preferences.
  vi.stubGlobal('ResizeObserver', class { observe() {} disconnect() {} })
  api.trackingHistory.mockResolvedValue({ ok: true, total: 0, points: [] })
  useRuntimeStore.setState({ mode: 'static', capabilities: null })
  useDeviceStore.getState().resetDevice()
  useDeviceStore.setState({ connection: 'connected', deviceId: 'synthetic-browser-id', deviceName: 'IKT_TEST_FAKE',
    activation: { ...useDeviceStore.getState().activation, phase: 'active', edition: 'advanced', activationId: '0123456789ABCDEF' },
    settingsCapabilities: { ...useDeviceStore.getState().settingsCapabilities, experimentalFind: true },
    findCapabilities: { flags: 256, normalWindowMs: 1, findWindowMs: 1, advertisementIntervalUnits: 1, publicOnlyP224: true },
    findConfig: { enabled: false, mode: 1, identityPresent: false, pausedForConnection: true } })
  host = document.createElement('div'); document.body.append(host); root = createRoot(host)
  await act(async () => root.render(<MemoryRouter><FindPage /></MemoryRouter>))
})
afterEach(async () => { if (root) await act(async () => root.unmount()); host?.remove(); localStorage.clear(); vi.unstubAllGlobals() })

it('places the read button before device export and re-reads missing Find details over BLE', async () => {
  const { findConfig, findCapabilities } = useDeviceStore.getState()
  const details = { findConfig: { ...findConfig!, identityPresent: true }, findCapabilities }
  await act(async () => useDeviceStore.setState({ findCapabilities: null, findConfig: null }))
  const refresh = button('重新读取')
  expect(refresh.nextElementSibling).toBe(button('导出'))
  ble.refreshFindDetails.mockResolvedValueOnce(details)
  await act(async () => refresh.click())
  expect(ble.refreshFindDetails).toHaveBeenCalledOnce()
  expect(useDeviceStore.getState()).toMatchObject(details)
  expect(api.status).not.toHaveBeenCalled()
  expect(api.query).not.toHaveBeenCalled()
  expect(ble.configureFind).not.toHaveBeenCalled()
  expect(document.body.textContent).toContain('Find 信息已重新读取')
})

it('disables reading without a connected Find device even with a local service', async () => {
  await act(async () => useDeviceStore.setState({ connection: 'idle' }))
  await localServer()
  expect(button('重新读取').disabled).toBe(true)
  await act(async () => useDeviceStore.setState({ connection: 'connected',
    settingsCapabilities: { ...useDeviceStore.getState().settingsCapabilities, experimentalFind: false },
  }))
  expect(button('重新读取').disabled).toBe(true)
  expect(api.status).toHaveBeenCalledOnce()
  expect(ble.refreshFindDetails).not.toHaveBeenCalled()
})

it('reads device Find details without touching the local library or selection', async () => {
  const state = await localServer()
  const { findConfig, findCapabilities, activation } = useDeviceStore.getState()
  await act(async () => useDeviceStore.setState({
    activation: { ...activation, activationId: state.identities[0].deviceId.slice(11) },
  }))
  const details = { findConfig: { ...findConfig!, enabled: true, identityPresent: true }, findCapabilities }
  ble.refreshFindDetails.mockResolvedValueOnce(details)
  await act(async () => button('重新读取').click())
  expect(useDeviceStore.getState()).toMatchObject(details)
  expect(api.status).toHaveBeenCalledOnce()
  expect(api.trackingHistory).toHaveBeenCalledOnce()
  expect(api.query).not.toHaveBeenCalled()
  expect(api.selectIdentity).not.toHaveBeenCalled()
  expect(api.updateIdentity).not.toHaveBeenCalled()
  ble.refreshFindDetails.mockRejectedValueOnce(new Error('设备暂时无响应'))
  await act(async () => button('重新读取').click())
  expect(document.body.textContent).toContain('读取设备 Find 信息失败')
  expect(useDeviceStore.getState()).toMatchObject(details)
})

it('lists encrypted browser records inline and clears only the confirmed local record', async () => {
  const fingerprint = vector.payload.identities[0].fingerprint
  await act(async () => {
    saveFindBackup({ fingerprint, deviceName:'办公桌标签', encryptedBackup: vector.encrypted })
    saveFindBackup({ fingerprint:'a'.repeat(32), encryptedBackup:vector.encrypted })
  })
  const list = host.querySelector('.find-browser-backups')!
  expect(list.closest('.find-device-config')).toBeNull()
  expect(list.classList.contains('find-panel')).toBe(true)
  expect(list.querySelectorAll('li')).toHaveLength(1)
  expect(list.querySelectorAll('option')).toHaveLength(2)
  expect(list.textContent).toContain('当前浏览器保存的设备信息')
  expect(list.querySelector('.notice.warning')?.textContent).toContain('已加密保存在当前浏览器')
  expect(list.querySelector('.notice.warning')?.textContent).toContain('查询设备位置并跟踪设备')
  expect(list.textContent).toContain('办公桌标签')
  expect(list.innerHTML).not.toContain(vector.encrypted)
  expect(list.innerHTML).not.toContain(vector.payload.identities[0].materialHex)
  const clear = list.querySelector('li button:last-child') as HTMLButtonElement
  await act(async () => clear.click())
  await act(async () => button('取消').click())
  expect(savedFindBackups()).toHaveLength(2)
  await act(async () => clear.click())
  await act(async () => button('跳过并继续').click())
  expect(savedFindBackups()).toHaveLength(2)
  expect(host.textContent).toContain('只能通过重新完整刷机')
  await act(async () => button('仍然删除').click())
  expect(savedFindBackups().map(x=>x.fingerprint)).toEqual(['a'.repeat(32)])
  expect(list.querySelectorAll('li')).toHaveLength(1)
  expect(ble.configureFind).not.toHaveBeenCalled()
  expect(api.configureAllTracking).not.toHaveBeenCalled()
})

it('hides browser backup lists when empty or in local service mode', async () => {
  expect(host.querySelector('.find-browser-backups')).toBeNull()
  await act(async () => saveFindBackup({ fingerprint:vector.payload.identities[0].fingerprint, encryptedBackup:vector.encrypted }))
  expect(host.querySelector('.find-browser-backups')).not.toBeNull()
  await act(async () => useRuntimeStore.setState({ mode:'server',capabilities:null }))
  expect(host.querySelector('.find-browser-backups')).toBeNull()
  expect(savedFindBackups()).toHaveLength(1)
})

it('selects one saved tag and applies copy and clear only to that tag', async () => {
  const first = vector.payload.identities[0].fingerprint, second='b'.repeat(32)
  const clipboard=vi.fn().mockResolvedValue(undefined)
  vi.stubGlobal('navigator',{clipboard:{writeText:clipboard}})
  await act(async () => {
    saveFindBackup({fingerprint:first,deviceName:'标签一',encryptedBackup:vector.encrypted})
    saveFindBackup({fingerprint:second,deviceName:'标签二',encryptedBackup:'IKTFIND1.synthetic-second'})
  })
  const select=host.querySelector<HTMLSelectElement>('[aria-label="选择已保存的标签"]')!
  await act(async () => { select.value=second;select.dispatchEvent(new Event('change',{bubbles:true})) })
  const list=host.querySelector('.find-browser-backups')!
  expect(list.querySelectorAll('li')).toHaveLength(1)
  expect(list.querySelector('li')?.textContent).toContain('标签二')
  expect(list.querySelector('li')?.textContent).not.toContain('标签一')
  await act(async () => (list.querySelector('li button') as HTMLButtonElement).click())
  expect(clipboard).toHaveBeenCalledExactlyOnceWith('IKTFIND1.synthetic-second')
  await act(async () => (list.querySelector('li button:last-child') as HTMLButtonElement).click())
  await act(async () => button('跳过并继续').click())
  await act(async () => button('仍然删除').click())
  expect(savedFindBackups().map(x=>x.fingerprint)).toEqual([first])
  expect(select.value).toBe(first)
  await act(async () => (list.querySelector('li button:last-child') as HTMLButtonElement).click())
  await act(async () => button('跳过并继续').click())
  await act(async () => button('仍然删除').click())
  expect(host.querySelector('.find-browser-backups')).toBeNull()
})

it('keeps the backup and reports storage deletion failure', async () => {
  await act(async () => saveFindBackup({ fingerprint:vector.payload.identities[0].fingerprint, encryptedBackup:vector.encrypted }))
  vi.spyOn(localStorage,'setItem').mockImplementation(() => { throw Error('storage denied') })
  await act(async () => (host.querySelector('.find-browser-backups li button:last-child') as HTMLButtonElement).click())
  await act(async () => button('跳过并继续').click())
  await act(async () => button('仍然删除').click())
  expect(host.querySelector('.find-browser-backups [role=alert]')?.textContent).toContain('未能清除')
  expect(savedFindBackups()).toHaveLength(1)
})

it('historical browser backups use ordinary confirmation without a connection or skip warning', async () => {
  const fp = vector.payload.identities[0].fingerprint
  await act(async () => { saveFindBackup({ fingerprint: fp, encryptedBackup: vector.encrypted }); markFindBackupHistorical(fp) })
  await act(async () => (host.querySelector('.find-browser-backups li button:last-child') as HTMLButtonElement).click())
  expect(host.textContent).not.toContain('跳过并继续')
  await act(async () => button('确认清除').click())
  expect(savedFindBackups()).toHaveLength(0)
})

it('keeps replacement visible with no local identity and asks for the previous backup', async () => {
  const state = await localServer()
  api.status.mockResolvedValue({ ...state, identities: [] })
  await act(async () => {
    useRuntimeStore.setState({ mode: 'static', capabilities: null })
  })
  await act(async () => useRuntimeStore.setState({ mode: 'server', capabilities: {
    apiVersion: 1, serverVersion: 'synthetic', features: ['find-query'], collections: [], limits: { jsonBytes: 1, assetBytes: 1 } } }))
  await act(async () => useDeviceStore.setState({
    findConfig: { ...useDeviceStore.getState().findConfig!, identityPresent: true },
    findCapabilities: { ...useDeviceStore.getState().findCapabilities!, publicOnlyOwnerReplacement: true },
  }))
  expect(button('替换身份').disabled).toBe(false)
  await act(async () => button('替换身份').click())
  expect(host.querySelector('[role=dialog]')?.textContent).toContain('恢复原身份后替换')
  expect(host.textContent).toContain('仅轨迹备份无法替换身份')
  const field = host.querySelector<HTMLTextAreaElement>('textarea')!
  await act(async () => { Object.getOwnPropertyDescriptor(HTMLTextAreaElement.prototype, 'value')!.set!.call(field, vector.encrypted); field.dispatchEvent(new Event('input', { bubbles: true })) })
  await input(0, vector.password)
  ble.readPublicOnlyFindOwnerStatus.mockResolvedValue({ ...vector.proofs[1], identityPresent: true, timeValid: true })
  api.importBackup.mockResolvedValue(state)
  api.updateIdentity.mockResolvedValue(state)
  await act(async () => button('解密并导入').click())
  await vi.waitFor(() => expect(button('确认更新')).toBeDefined())
  expect(api.importBackup).toHaveBeenCalledExactlyOnceWith(vector.encrypted, vector.password, false)
  expect(ble.beginPublicOnlyFindOwner).not.toHaveBeenCalled()
  await act(async () => button('取消').click())
})

it('protects deletion of a current server identity and opens export without treating it as a completed backup', async () => {
  const state = await localServer()
  api.deleteIdentity.mockResolvedValue({ ...state, identities: [] })
  await act(async () => useDeviceStore.setState({ connection: 'idle' }))
  const remove = Array.from(host.querySelectorAll('.find-identity-list article button')).find(b => b.textContent === '删除') as HTMLButtonElement
  await act(async () => remove.click())
  expect(host.querySelector('[role=dialog]')?.textContent).toContain('连接对应标签或备份')
  expect(api.deleteIdentity).not.toHaveBeenCalled()
  await act(async () => button('导出备份').click())
  expect(host.querySelectorAll('[role=dialog]')).toHaveLength(1)
  expect(host.querySelector('[role=dialog]')?.textContent).toContain('导出设备信息')
  expect(Array.from(host.querySelectorAll<HTMLInputElement>('.find-export-devices input[type=checkbox]'), e => e.checked)).toEqual([true])
  expect(button('全部勾选')).toBeUndefined()
  await act(async () => button('关闭').click())
  expect(button('解除登记并删除').disabled).toBe(true)
  await act(async () => button('仅删除本机资料').click())
  expect(api.deleteIdentity).not.toHaveBeenCalled()
  await act(async () => button('仍然删除').click())
  expect(api.deleteIdentity).toHaveBeenCalledExactlyOnceWith(state.identities[0].keyFingerprint)
  expect(ble.unregisterFindOwner).not.toHaveBeenCalled()
  expect(ble.configureFind).not.toHaveBeenCalled()
})

it('only removes local identity after the matching device confirms unregister', async () => {
  const state = await localServer()
  const identity = state.identities[0]
  await act(async () => useDeviceStore.setState({
    activation: { ...useDeviceStore.getState().activation, activationId: identity.deviceId.slice(11) },
    findCapabilities: { ...useDeviceStore.getState().findCapabilities!, flags: 0x500 },
  }))
  api.deleteIdentity.mockResolvedValue({ ...state, identities: [] })
  ble.unregisterFindOwner.mockRejectedValueOnce(new Error('设备拒绝解除登记'))
  await act(async () => button('删除').click())
  await act(async () => button('解除登记并删除').click())
  expect(host.querySelector('[role=alert]')?.textContent).toContain('设备拒绝解除登记')
  expect(api.deleteIdentity).not.toHaveBeenCalled()
  ble.unregisterFindOwner.mockImplementationOnce(async () => {
    expect(api.deleteIdentity).not.toHaveBeenCalled()
  })
  await act(async () => button('解除登记并删除').click())
  expect(ble.unregisterFindOwner).toHaveBeenLastCalledWith(identity.deviceId, identity.keyFingerprint)
  expect(api.deleteIdentity).toHaveBeenCalledExactlyOnceWith(identity.keyFingerprint)
})

it('deletes only the connected device identity and leaves local records through cancel, failure, and success', async () => {
  const state = await localServer(false, true)
  const [identity, other] = state.identities
  other.selected = true
  identity.selected = false
  expect(button('删除身份').disabled).toBe(true)
  await act(async () => useDeviceStore.setState({
    activation: { ...useDeviceStore.getState().activation, activationId: identity.deviceId.slice(11) },
    localAlias: identity.deviceName,
    deviceName: identity.bluetoothName,
    findConfig: { ...useDeviceStore.getState().findConfig!, identityPresent: true },
  }))
  expect(button('删除身份').disabled).toBe(true)
  await act(async () => useDeviceStore.setState({
    findCapabilities: { ...useDeviceStore.getState().findCapabilities!, flags: 0x500 },
  }))
  expect(button('删除身份').disabled).toBe(false)

  await act(async () => button('删除身份').click())
  expect(host.querySelector('[role=dialog]')?.textContent).toContain('本机的设备资料和历史轨迹会保留')
  await act(async () => button('取消').click())
  expect(ble.unregisterFindOwner).not.toHaveBeenCalled()

  ble.unregisterFindOwner.mockRejectedValueOnce(new Error('设备拒绝解除登记'))
  await act(async () => button('删除身份').click())
  await act(async () => button('确认删除身份').click())
  expect(document.querySelector('[role=alert]')?.textContent).toContain('设备拒绝解除登记')
  expect(button('删除身份').disabled).toBe(false)
  expect(api.deleteIdentity).not.toHaveBeenCalled()

  ble.unregisterFindOwner.mockImplementationOnce(async () => {
    useDeviceStore.getState().setFindConfig({ ...useDeviceStore.getState().findConfig!, identityPresent: false })
  })
  await act(async () => button('删除身份').click())
  await act(async () => button('确认删除身份').click())

  expect(ble.unregisterFindOwner).toHaveBeenLastCalledWith(identity.deviceId, identity.keyFingerprint)
  expect(api.deleteIdentity).not.toHaveBeenCalled()
  expect(host.querySelectorAll('.find-identity-list article')).toHaveLength(2)
  expect(host.querySelector('.find-device-status')?.textContent).toContain('未登记')
  expect(button('删除身份').disabled).toBe(true)
})

it.each(['older', 'tracks-only'])('deletes %s historical identities locally without an unregister action', async kind => {
  const state = await localServer(true)
  const [historical, current] = state.identities
  historical.createdAt = '2025-01-01T00:00:00Z'
  historical.historyOnly = kind === 'tracks-only'
  api.deleteIdentity.mockResolvedValue({ ...state, identities: [current] })
  await act(async () => useDeviceStore.setState({ connection: 'idle' }))
  const card = Array.from(host.querySelectorAll('.find-identity-list article')).find(
    row => row.querySelector('.find-identity-usage.history'),
  )!
  const remove = Array.from(card.querySelectorAll('button')).find(b => b.textContent === '删除')!
  await act(async () => remove.click())
  const actions = Array.from(host.querySelectorAll('[role=dialog] footer button'), b => b.textContent)
  expect(actions).toEqual(['取消', '删除本机资料'])
  expect(api.deleteIdentity).not.toHaveBeenCalled()
  await act(async () => button('删除本机资料').click())
  expect(api.deleteIdentity).toHaveBeenCalledExactlyOnceWith(historical.keyFingerprint)
  expect(ble.unregisterFindOwner).not.toHaveBeenCalled()
})

it.each(['static', 'standalone'] as const)('keeps replacement visible and restores old material in %s', async mode => {
  await act(async () => { useRuntimeStore.setState({ mode }); useDeviceStore.setState({
    findConfig: { ...useDeviceStore.getState().findConfig!, identityPresent: true },
    findCapabilities: { ...useDeviceStore.getState().findCapabilities!, publicOnlyOwnerReplacement: true } }) })
  await act(async () => button('替换身份').click())
  expect(host.querySelector('[role=dialog]')?.textContent).toContain('原先保存的设备信息')
  expect(ble.beginPublicOnlyFindOwner).not.toHaveBeenCalled()
})

it('does not stage, commit or abort on a different tag selected during replacement preparation', async () => {
  const state = await localServer()
  api.updateIdentity.mockResolvedValue(state)
  api.abortPublicOnlyOwner.mockResolvedValue(state)
  await act(async () => useDeviceStore.setState({ findConfig: { ...useDeviceStore.getState().findConfig!, identityPresent: true },
    findCapabilities: { ...useDeviceStore.getState().findCapabilities!, publicOnlyOwnerReplacement: true } }))
  ble.readPublicOnlyFindOwnerStatus.mockResolvedValue({ ...vector.proofs[1], identityPresent: true, timeValid: true })
  ble.beginPublicOnlyFindOwner.mockResolvedValue({ nonceHex: '0102030405060708' })
  api.preparePublicOnlyReplacement.mockImplementation(async () => {
    useDeviceStore.setState({ activation: { ...useDeviceStore.getState().activation, activationId: 'FFFFFFFFFFFFFFFF' } })
    return { transactionId: 'synthetic-pending', deviceId: state.identities[0].deviceId }
  })
  await act(async () => button('替换身份').click())
  await act(async () => button('确认更新').click())
  expect(ble.stagePublicOnlyFindOwnerReplacement).not.toHaveBeenCalled()
  expect(ble.commitPublicOnlyFindOwner).not.toHaveBeenCalled()
  expect(ble.abortFindOwnerRotation).not.toHaveBeenCalled()
  expect(api.abortPublicOnlyOwner).toHaveBeenCalledExactlyOnceWith('synthetic-pending')
})

it.each([vector.password, '123456'])('allows first static registration with %s, preserving ciphertext and confirmation before BLE', async password => {
  const toggle = host.querySelector<HTMLInputElement>('input[role=switch]')!
  expect(toggle.disabled).toBe(false)
  await act(async () => toggle.click())
  expect(host.querySelector('[role=dialog]')?.textContent).toContain('保存设备信息并开启 Find')
  await input(0, password); await input(1, password)
  await act(async () => button('生成加密资料').click())
  await vi.waitFor(async () => {
    await act(async () => { await new Promise(resolve => setTimeout(resolve, 25)) })
    expect(host.querySelector<HTMLTextAreaElement>('textarea[readonly]')?.value).toMatch(/^IKTFIND1\./)
  }, { timeout: 4000 })
  expect(button('登记并开启Find').disabled).toBe(true)
  expect(ble.beginPublicOnlyFindOwner).not.toHaveBeenCalled(); expect(api.status).not.toHaveBeenCalled()
  const stored = localStorage.getItem('inktag.find.encrypted-backups.v1')!
  expect(stored).toContain('IKTFIND1.'); expect(stored).not.toContain('materialHex')
  expect(Object.keys(JSON.parse(stored)[0]).sort()).toEqual(['deviceId', 'deviceName', 'encryptedBackup', 'fingerprint'])
  expect(JSON.parse(stored)[0].deviceName).toBe('IKT_TEST_FAKE')
  await act(async () => host.querySelector<HTMLButtonElement>('[role=dialog] button[aria-label=关闭]')!.click())
  expect(host.querySelector('[role=dialog]')).toBeNull()
  expect(localStorage.getItem('inktag.find.encrypted-backups.v1')).toBe(stored)
})
it('autofills, copies and retains a generated password only until its dialog closes', async () => {
  await localServer(); api.exportBackup.mockResolvedValue({ encryptedBackup: vector.encrypted, identityCount: 1 })
  const clipboard = vi.fn().mockResolvedValue(undefined)
  vi.stubGlobal('navigator', { clipboard: { writeText: clipboard } })
  await act(async () => button('导出标签').click())
  await act(async () => button('全部勾选').click())
  await act(async () => button('自动生成口令').click())
  const generated = host.querySelector<HTMLInputElement>('[aria-label="生成的迁移口令"]')!.value
  expect(generated).toMatch(/^[A-HJ-NP-Z2-9]{12}$/)
  expect(Array.from(host.querySelectorAll<HTMLInputElement>('[role=dialog] input[type=password]'),e=>e.value)).toEqual([generated,generated])
  await act(async () => button('复制口令').click())
  expect(clipboard).toHaveBeenCalledExactlyOnceWith(generated)
  expect(localStorage.getItem('inktag.find.encrypted-backups.v1') || '').not.toContain(generated)
  await act(async () => button('生成加密资料').click())
  expect(api.exportBackup).toHaveBeenCalledExactlyOnceWith(generated,true,[vector.payload.identities[0].fingerprint],true,true,false)
  expect(host.querySelector<HTMLInputElement>('[aria-label="生成的迁移口令"]')?.value).toBe(generated)
  await act(async () => button('关闭').click())
  await act(async () => button('导出标签').click())
  expect(host.querySelector('[aria-label="生成的迁移口令"]')).toBeNull()
  expect(Array.from(host.querySelectorAll<HTMLInputElement>('[role=dialog] input[type=password]'),e=>e.value)).toEqual(['',''])
})
it('rejects mismatched passwords before registration or persistence', async () => {
  await act(async () => host.querySelector<HTMLInputElement>('input[role=switch]')!.click())
  await input(0, vector.password); await input(1, 'different-password')
  await act(async () => button('生成加密资料').click())
  expect(host.querySelector('[role=alert]')?.textContent).toContain('不一致')
  expect(localStorage.length).toBe(0); expect(ble.beginPublicOnlyFindOwner).not.toHaveBeenCalled()
})
it('does not unlock query or register unsupported/non-Pro devices', async () => {
  await act(async () => useDeviceStore.setState({ activation: { ...useDeviceStore.getState().activation, phase: 'inactive', edition: 'base' } }))
  await act(async () => host.querySelector<HTMLInputElement>('input[role=switch]')!.click())
  expect(host.querySelector('[role=dialog]')).toBeNull(); expect(ble.configureFind).not.toHaveBeenCalled()
  expect(host.textContent).toContain('当前模式不支持 Find 查询')
  expect(api.status).not.toHaveBeenCalled()
  expect(host.textContent).toContain('不可用，请先激活 Pro')
})

function checkbox(label: string) {
  return Array.from(host.querySelectorAll('label')).find(e => e.textContent?.includes(label))!.querySelector<HTMLInputElement>('input[type=checkbox]')!
}
it('requires an encrypted browser backup for enrollment and stops when storage is unavailable', async () => {
  await act(async () => host.querySelector<HTMLInputElement>('input[role=switch]')!.click())
  expect(host.querySelector('[role=dialog] input[type=checkbox]')).toBeNull()
  expect(button('生成未加密资料')).toBeUndefined()
  await input(0, vector.password); await input(1, vector.password)
  const write = vi.spyOn(localStorage, 'setItem').mockImplementation(() => { throw Error('storage denied') })
  await act(async () => {
    button('生成加密资料').click()
    await vi.waitFor(() => expect(write).toHaveBeenCalled())
  })
  expect(host.querySelector('[role=alert]')?.textContent).toContain('无法保存加密迁移资料')
  expect(localStorage.length).toBe(0)
  expect(button('登记并开启Find')).toBeUndefined()
  expect(ble.beginPublicOnlyFindOwner).not.toHaveBeenCalled()
  write.mockRestore()
})

it('can re-enable a device with only a legacy plaintext copy after saving a new encrypted backup', async () => {
  const owner = vector.payload.identities[0]
  ble.readPublicOnlyFindOwnerStatus.mockResolvedValue({ ...vector.proofs[1], identityPresent: true, timeValid: true })
  await act(async () => useDeviceStore.setState({ findConfig: { ...useDeviceStore.getState().findConfig!, identityPresent: true } }))
  await act(async () => host.querySelector<HTMLInputElement>('input[role=switch]')!.click())
  const field = host.querySelector<HTMLTextAreaElement>('[role=dialog] textarea')!
  await act(async () => {
    Object.getOwnPropertyDescriptor(HTMLTextAreaElement.prototype, 'value')!.set!.call(field, JSON.stringify(vector.payload))
    field.dispatchEvent(new Event('input', { bubbles: true }))
  })
  await act(async () => checkbox('我了解并接受').click())
  await act(async () => {
    button('读取并核对资料').click()
    await vi.waitFor(() => expect(ble.readPublicOnlyFindOwnerStatus).toHaveBeenCalled())
  })
  await vi.waitFor(async () => {
    await act(async () => { await Promise.resolve() })
    expect(button('生成加密资料')).toBeDefined()
  })
  expect(savedFindBackups()).toEqual([])
  expect(ble.configureFind).not.toHaveBeenCalled()
  await input(0, vector.password); await input(1, vector.password)
  await act(async () => {
    button('生成加密资料').click()
    await vi.waitFor(() => expect(savedFindBackups()).toHaveLength(1))
  })
  const [backup] = savedFindBackups()
  expect(backup).toMatchObject({ fingerprint: owner.fingerprint, deviceId: owner.deviceId })
  expect(await readPortableBackup(backup.encryptedBackup, vector.password)).toMatchObject({
    identities: [{ fingerprint: owner.fingerprint, materialHex: owner.materialHex }],
  })
  await act(async () => checkbox('我已另存备份').click())
  await act(async () => {
    button('开启Find').click()
    await vi.waitFor(() => expect(ble.configureFind).toHaveBeenCalled())
  })
  expect(ble.configureFind).toHaveBeenCalledExactlyOnceWith(true)
  expect(ble.beginPublicOnlyFindOwner).not.toHaveBeenCalled()
})

it.each(['static', 'standalone'] as const)('requires export and saved acknowledgement before re-enabling an enrolled %s tag', async mode => {
  const entry = vector.payload.identities[0]
  saveFindBackup({ fingerprint: entry.fingerprint, encryptedBackup: vector.encrypted })
  ble.readPublicOnlyFindOwnerStatus.mockResolvedValue({ ...vector.proofs[1], identityPresent: true, timeValid: true })
  await act(async () => {
    useRuntimeStore.setState({ mode })
    useDeviceStore.setState({ findConfig: { ...useDeviceStore.getState().findConfig!, identityPresent: true } })
  })
  await act(async () => host.querySelector<HTMLInputElement>('input[role=switch]')!.click())
  expect(host.querySelector('[role=dialog]')?.textContent).toContain('否则后续无法获取该设备的位置')
  expect(ble.configureFind).not.toHaveBeenCalled()
  await input(0, vector.password)
  await act(async () => button('读取并核对资料').click())
  await act(async () => { await new Promise(resolve => setTimeout(resolve, 350)) })
  await vi.waitFor(() => expect(host.querySelector<HTMLTextAreaElement>('textarea[readonly]')?.value).toMatch(/^IKTFIND1\./), {timeout:5000})
  expect(button('开启Find').disabled).toBe(true)
  expect(ble.configureFind).not.toHaveBeenCalled()
  await act(async () => checkbox('我已另存备份').click())
  await act(async () => button('开启Find').click())
  await act(async () => { await vi.waitFor(() => expect(ble.configureFind).toHaveBeenCalled()) })
  expect(ble.configureFind).toHaveBeenCalledExactlyOnceWith(true)
  expect(ble.beginPublicOnlyFindOwner).not.toHaveBeenCalled()
  expect(ble.readPublicOnlyFindOwnerStatus).toHaveBeenCalledTimes(2)
  expect(host.querySelector('[role=dialog]')).toBeNull()
})

it('cannot re-enable an existing identity without its query material or with a wrong public proof', async () => {
  saveFindBackup({ fingerprint: vector.payload.identities[0].fingerprint, encryptedBackup: vector.encrypted })
  ble.readPublicOnlyFindOwnerStatus.mockResolvedValue({ ...vector.proofs[1], identityPresent: true, publicKeyHex: '00'.repeat(28) })
  await act(async () => useDeviceStore.setState({ findConfig: { ...useDeviceStore.getState().findConfig!, identityPresent: true } }))
  await act(async () => host.querySelector<HTMLInputElement>('input[role=switch]')!.click())
  await input(0, vector.password)
  await act(async () => button('读取并核对资料').click())
  await act(async () => { await new Promise(resolve => setTimeout(resolve, 350)) })
  expect(host.querySelector('[role=alert]')?.textContent).toContain('不匹配')
  expect(ble.configureFind).not.toHaveBeenCalled()
})

it('selects devices and can export only tracks without keys, using plaintext only after consent', async () => {
  await localServer()
  api.exportBackup.mockResolvedValue({ backupText: '{"format":"synthetic-output"}', encrypted: false })
  await act(async () => button('导出标签').click())
  await act(async () => button('全部勾选').click())
  await act(async () => { checkbox('设备信息（含查询私钥）').click(); checkbox('使用口令加密').click() })
  expect(button('生成未加密资料').disabled).toBe(true)
  await act(async () => checkbox('我了解并接受').click())
  await act(async () => button('生成未加密资料').click())
  expect(api.exportBackup).toHaveBeenCalledExactlyOnceWith('', true, [vector.payload.identities[0].fingerprint], false, false, true)
  expect(localStorage.length).toBe(0)
})

it('moves tracks and notes to the device group and requests merged history for windows and notes', async () => {
  const state = await localServer(true)
  const second = state.identities[1]
  api.trackingHistory.mockResolvedValue({ ok: true, points: [], total: 0, retentionDays: 90 })
  const heading = host.querySelector('.find-device-group-heading')!
  expect(heading.querySelector('.find-device-group-title')?.textContent).toContain('2 个身份')
  expect(host.querySelectorAll('.find-identity-list-actions button').length).toBe(8)
  expect(host.querySelectorAll('button.find-identity-list-main').length).toBe(2)
  const action = (text: string) => Array.from(heading.querySelectorAll('button')).find(b => b.textContent === text)!
  await act(async () => action('轨迹').click())
  expect(api.trackingHistory).toHaveBeenCalledWith(second.keyFingerprint, 5000, 24, undefined, undefined, true)
  await act(async () => button('全部').click())
  expect(api.trackingHistory).toHaveBeenLastCalledWith(second.keyFingerprint, 30000, 0, undefined, undefined, true)
  await act(async () => host.querySelector<HTMLButtonElement>('[aria-label=关闭轨迹]')!.click())
  await act(async () => action('备注').click())
  const note = host.querySelector<HTMLInputElement>('#find-device-note')!
  await act(async () => { Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value')!.set!.call(note, 'Shared note'); note.dispatchEvent(new Event('input', { bubbles: true })) })
  api.updateIdentity.mockResolvedValue(state)
  await act(async () => host.querySelector('form.find-note-editor')!.dispatchEvent(new Event('submit', { bubbles: true, cancelable: true })))
  expect(api.updateIdentity).toHaveBeenCalledWith(second.keyFingerprint, { findNote: 'Shared note' }, true)
})

it('provides separate start-all and stop-all controls without invoking devices', async () => {
  const state = await localServer()
  api.configureAllTracking.mockResolvedValue({ ...state, identities: state.identities.map(e => ({ ...e, trackingEnabled: true })) })
  await act(async () => button('开启全部').click())
  expect(api.configureAllTracking).not.toHaveBeenCalled()
  await act(async () => Array.from(host.querySelectorAll<HTMLButtonElement>('[role=dialog] button')).find(b => b.textContent === '开启记录')!.click())
  expect(api.configureAllTracking).toHaveBeenCalledExactlyOnceWith(true)
  await act(async () => button('暂停全部').click())
  expect(api.configureAllTracking).toHaveBeenLastCalledWith(false)
  expect(ble.configureFind).not.toHaveBeenCalled()
})

it('requires acknowledgement before plaintext import and does not request a password', async () => {
  const state = await localServer(); api.importBackup.mockResolvedValue({ ...state, importedCount: 1, existingCount: 0 })
  await act(async () => button('导入标签').click())
  const field = host.querySelector<HTMLTextAreaElement>('[role=dialog] textarea')!
  const plain = JSON.stringify(vector.payload)
  await act(async () => { Object.getOwnPropertyDescriptor(HTMLTextAreaElement.prototype, 'value')!.set!.call(field, plain); field.dispatchEvent(new Event('input', { bubbles: true })) })
  expect(host.querySelector('[role=dialog] input[type=password]')).toBeNull()
  expect(button('解密并导入').disabled).toBe(true)
  await act(async () => checkbox('我了解并接受').click())
  await act(async () => button('解密并导入').click())
  expect(api.importBackup).toHaveBeenCalledExactlyOnceWith(plain, '', true)
  expect(localStorage.length).toBe(0)
})

async function localServer(withHistory = false, withOtherDevice = false, ready = false) {
  const e = vector.payload.identities[0]
  const state = { ok: true, loggedIn: ready, queryReady: ready, needs2fa: false, methods: [],
    selectedKeyFingerprint: e.fingerprint, identities: [{ ...e, keyFingerprint: e.fingerprint,
      selected: true, historyOnly: false, trackingEnabled: false, trackPointCount: 0, lastReportAt: null, lastQueriedAt: null,
      lastAutoQueryAt: null, lastAutoError: null, currentSlot: 0, address: '', advertisementKey: '' }] }
  if (withHistory) state.identities.push({ ...state.identities[0], keyFingerprint: 'ab'.repeat(16), createdAt: '2026-01-02T00:00:00Z', selected: false })
  if (withOtherDevice) state.identities.push({ ...state.identities[0], deviceId: 'activation:other-tag', keyFingerprint: 'cd'.repeat(16), selected: false })
  api.status.mockResolvedValue(state)
  await act(async () => useRuntimeStore.setState({ mode: 'server', capabilities: {
    apiVersion: 1, serverVersion: 'synthetic', features: ['find-query'], collections: [], limits: { jsonBytes: 1, assetBytes: 1 },
  } }))
  return state
}
it('exports the local library as a downloadable file without mounting its large string, then clears passphrase inputs', async () => {
  const state = await localServer(false, true); api.exportBackup.mockResolvedValue({ encryptedBackup: vector.encrypted, identityCount: 1 })
  await act(async () => button('导出标签').click())
  await act(async () => button('全部勾选').click())
  await input(0, vector.password); await input(1, vector.password)
  await act(async () => button('生成加密资料').click())
  expect(api.exportBackup).toHaveBeenCalledExactlyOnceWith(vector.password, true, expect.arrayContaining(state.identities.map(e => e.keyFingerprint)), true, true, false)
  expect(host.querySelector('[role=dialog] textarea')).toBeNull()
  expect(button('保存文件').disabled).toBe(false)
  expect(button('复制')).toBeUndefined()
  expect(host.querySelector('[role=dialog] input[type=password]')).toBeNull()
})
it('imports without BLE or Apple login and clears the transient passphrase/ciphertext on success', async () => {
  const state = await localServer(); api.importBackup.mockResolvedValue({ ...state, importedCount: 0, existingCount: 1, importedTrackCount: 0 })
  await act(async () => button('导入标签').click())
  const field = host.querySelector<HTMLTextAreaElement>('[role=dialog] textarea')!
  await act(async () => { Object.getOwnPropertyDescriptor(HTMLTextAreaElement.prototype, 'value')!.set!.call(field, vector.encrypted); field.dispatchEvent(new Event('input', { bubbles: true })) })
  await input(0, vector.password)
  await act(async () => button('解密并导入').click())
  expect(api.importBackup).toHaveBeenCalledExactlyOnceWith(vector.encrypted, vector.password, false)
  expect(host.querySelector('[role=dialog]')).toBeNull(); expect(ble.configureFind).not.toHaveBeenCalled()
  expect(document.body.textContent).toContain('已导入资料')
})


it('starts bulk export empty and supports select-all, clear-all and a fresh selection on reopening', async () => {
  const state = await localServer(true, true)
  api.exportBackup.mockResolvedValue({ encryptedBackup: vector.encrypted, identityCount: 3 })
  await act(async () => button('导出标签').click())
  const devices = () => Array.from(host.querySelectorAll<HTMLInputElement>('.find-export-devices input[type=checkbox]'))
  expect(devices()).toHaveLength(2)
  expect(devices().map(e => e.checked)).toEqual([false, false])
  expect(button('全部清除').disabled).toBe(true)
  expect(button('生成加密资料').disabled).toBe(true)
  await act(async () => button('全部勾选').click())
  expect(devices().every(e => e.checked)).toBe(true)
  expect(button('全部勾选').disabled).toBe(true)
  await act(async () => button('全部清除').click())
  expect(devices().some(e => e.checked)).toBe(false)
  expect(button('生成加密资料').disabled).toBe(true)
  expect(api.exportBackup).not.toHaveBeenCalled()
  await act(async () => devices()[0].click())
  expect(devices().map(e => e.checked)).toEqual([true, false])
  await input(0, vector.password); await input(1, vector.password)
  await act(async () => button('生成加密资料').click())
  expect(api.exportBackup).toHaveBeenCalledExactlyOnceWith(vector.password, true,
    expect.arrayContaining(state.identities.slice(0, 2).map(e => e.keyFingerprint)), true, true, false)
  expect(api.exportBackup.mock.calls[0][2]).toHaveLength(2)
  await act(async () => button('关闭').click())
  await act(async () => button('导出标签').click())
  expect(devices().every(e => !e.checked)).toBe(true)
})

it.each([
  [false, true, true],
  [true, true, false],
  [true, false, true],
] as const)('offers string copy with other-device=%s history=%s when permitted', async (otherDevice, history, canCopy) => {
  await localServer(false, otherDevice)
  api.exportBackup.mockResolvedValue({ encryptedBackup: vector.encrypted })
  const clipboard = vi.fn().mockResolvedValue(undefined)
  vi.stubGlobal('navigator', { clipboard: { writeText: clipboard } })
  await act(async () => button('导出标签').click())
  await act(async () => button('全部勾选').click())
  if (!history) await act(async () => checkbox('历史轨迹').click())
  await input(0, vector.password); await input(1, vector.password)
  await act(async () => button('生成加密资料').click())
  expect(Boolean(host.querySelector('[role=dialog] textarea[readonly]'))).toBe(canCopy)
  expect(button('保存文件')).toBeDefined()
  if (canCopy) {
    await act(async () => button('复制').click())
    expect(clipboard).toHaveBeenCalledExactlyOnceWith(vector.encrypted)
  } else expect(button('复制')).toBeUndefined()
})

it('saves manual latest queries even when periodic tracking is paused and refreshes the point count', async () => {
  const state = await localServer(false, false, true)
  const report = { found: true, timestamp: '2026-11-05T08:00:00Z', latitude: 30, longitude: 110,
    horizontalAccuracy: 20, confidence: 1, status: 0, savedPointCount: 1 }
  api.selectIdentity.mockResolvedValue(state)
  api.query.mockResolvedValue(report)
  api.status.mockResolvedValue({ ...state, identities: state.identities.map(e => ({ ...e, trackPointCount: 1 })) })
  expect(host.textContent).toContain('轨迹 0 点')
  await act(async () => button('查询最新').click())
  expect(api.query).toHaveBeenCalledExactlyOnceWith({ fingerprint: state.identities[0].keyFingerprint,
    saveHistory: true, queryWindow: 'latest' })
  expect(host.textContent).toContain('轨迹 1 点')
  expect(host.textContent).not.toContain('轨迹记录中')
  expect(ble.configureFind).not.toHaveBeenCalled()
})

it.each([true, false])('shows the selected tag latest saved point without querying Apple (logged-in=%s)', async (ready) => {
  const point = { timestamp: '2026-11-05T08:00:00Z', receivedAt: '2026-11-05T08:01:00Z',
    latitude: 30, longitude: 110, horizontalAccuracy: 20, confidence: 1, status: 0 }
  api.trackingHistory.mockResolvedValue({ ok: true, total: 7, points: [point] })
  const state = await localServer(false, true, ready)
  expect(api.trackingHistory).toHaveBeenCalledExactlyOnceWith(state.identities[0].keyFingerprint, 1, 0, undefined, undefined, true)
  expect(host.querySelector('.find-map-card')).not.toBeNull()
  expect(host.querySelector('.find-map-frame .find-osm-track-map')).not.toBeNull()
  expect(host.querySelector('.find-map-frame iframe')).toBeNull()
  expect(host.textContent).toContain('30.0000000')
  expect(api.query).not.toHaveBeenCalled()
  expect(ble.configureFind).not.toHaveBeenCalled()
  // A different selection with no points must not inherit this tag's location.
  if (!ready) return
  const next = { ...state, selectedKeyFingerprint: state.identities[1].keyFingerprint,
    identities: state.identities.map((entry, i) => ({ ...entry, selected: i === 1 })) }
  api.trackingHistory.mockResolvedValue({ ok: true, total: 0, points: [] })
  api.selectIdentity.mockResolvedValue(next)
  api.status.mockResolvedValue(next)
  api.query.mockResolvedValue({ found: false })
  await act(async () => Array.from(host.querySelectorAll('button')).find(b => b.textContent === '查询最新' && b !== button('查询最新'))!.click())
  expect(host.querySelector('.find-map-card')).toBeNull()
})

it('hides the default position map when the selected tag has no saved location', async () => {
  await localServer(false, false, true)
  expect(host.querySelector('.find-map-card')).toBeNull()
  expect(api.query).not.toHaveBeenCalled()
})
