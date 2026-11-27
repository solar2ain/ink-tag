// @vitest-environment jsdom
import { act } from 'react'
import { createRoot, type Root } from 'react-dom/client'
import { MemoryRouter } from 'react-router-dom'
import { afterEach, beforeEach, expect, it, vi } from 'vitest'
import { DevicePage } from './DevicePage'
import { useDeviceStore } from '../store/device'
import { builtInScreenProfiles } from '../data/screenProfiles'
import { emptyDeviceSettingsCapabilities } from '../protocol/deviceSettings'
import { pinRoles, type PinConfig, type PinReport } from '../protocol/pinConfig'
import { ScreenWriteUnconfirmedError } from '../protocol/screen'
import { defaultGuiFonts } from '../protocol/guiAssets'
import { buildFontRoles } from '../protocol/fontRoles'

const data = vi.hoisted(() => ({ toArray: vi.fn() }))
const client = vi.hoisted(() => ({
  getDisplaySettings: vi.fn(async () => ({ supported: false, bootRefreshEnabled: false, brandVisibilitySupported: false, brandVisible: true })),
  setScreen: vi.fn(), clearStoredImages: vi.fn(), runAction: vi.fn(),
  getRuntimePinConfig: vi.fn(), setRuntimePinConfig: vi.fn(),
  readGuiAssetInfo:vi.fn(),writeGuiAssetPackage:vi.fn(),
}))
vi.mock('../protocol/client', () => ({ inktagClient: client }))
vi.mock('../data/db', () => ({ db: { screenProfiles: { orderBy: () => ({ reverse: () => ({ toArray: data.toArray }) }) },deviceGuiAssetPackages:{put:vi.fn()} } }))
vi.mock('../components/NativeFontPreview',()=>({NativeFontPreview:()=>null}))

let host: HTMLDivElement, root: Root, resolveProfiles: (items: unknown[]) => void
beforeEach(async () => {
  vi.stubGlobal('IS_REACT_ACT_ENVIRONMENT', true)
  vi.clearAllMocks()
  data.toArray.mockReturnValue(new Promise((resolve) => { resolveProfiles = resolve }))
  useDeviceStore.getState().resetDevice()
  const current = builtInScreenProfiles.find((profile) => profile.id === 'huawei_398_a0')!.config
  useDeviceStore.setState({ connection: 'connected', deviceId: 'screen-fixture-A', screen: { ...current, transform: 9, nativeUiScale: 1 } })
  host = document.createElement('div'); document.body.append(host); root = createRoot(host)
  await act(async () => root.render(<MemoryRouter initialEntries={['/device?tab=screen']}><DevicePage /></MemoryRouter>))
})
afterEach(async () => { await act(async () => root.unmount()); host.remove(); useDeviceStore.getState().resetDevice(); vi.unstubAllGlobals() })

it('shows the current preset without applying its default rotation or UI scale', async () => {
  expect(host.querySelector('.profile-select-trigger')?.textContent).toContain('适配huawei_a0')
  expect(host.querySelector('[aria-label="设备当前屏参"]')?.textContent).toContain('旋转 90°')
  expect(host.querySelector('[aria-label="设备当前屏参"]')?.textContent).toContain('UI scale 1')
  await act(async () => resolveProfiles([]))
  expect(client.setScreen).not.toHaveBeenCalled()
  expect(client.clearStoredImages).not.toHaveBeenCalled()
})

it('does not replace an unsaved form when browser user profiles finish loading', async () => {
  const size = host.querySelectorAll<HTMLSelectElement>('.form-grid select')[1]
  await act(async () => { size.value = '104x212'; size.dispatchEvent(new Event('change', { bubbles: true })) })
  expect(host.querySelector('.profile-select-trigger')?.textContent).toContain('自定义配置')
  await act(async () => resolveProfiles([]))
  expect(size.value).toBe('104x212')
  expect(host.querySelector('.profile-select-trigger')?.textContent).toContain('自定义配置')
  expect(client.setScreen).not.toHaveBeenCalled()
  expect(client.clearStoredImages).not.toHaveBeenCalled()
})

function clearButton() {
  return [...host.querySelectorAll<HTMLButtonElement>('button')].find((button) => button.textContent?.includes('测试清屏'))!
}

it('offers corrected E6 geometry normally without a migration prompt or automatic writes',async()=>{
  const config=builtInScreenProfiles.find(profile=>profile.id==='osprey_370_e6')!.config
  await act(async()=>useDeviceStore.setState({screen:{...config,width:480,height:720,visibleWidth:480,visibleHeight:720}}))
  expect(host.textContent).not.toContain('仍保存旧尺寸配置')
  expect(host.querySelector('option[value="720x480"]')).not.toBeNull()
  expect(client.setScreen).not.toHaveBeenCalled()
  expect(client.clearStoredImages).not.toHaveBeenCalled()
  expect(useDeviceStore.getState().connection).toBe('connected')
})

it.each(builtInScreenProfiles.filter(profile => profile.config.color === 10).map(profile => [profile.id, profile.config] as const))(
  'shows the E6 pin-16 warning below the selector for device profile %s without writes', async (_id, config) => {
    expect(host.querySelector('.e6-panel-warning')).toBeNull()
    await act(async () => useDeviceStore.setState({ screen: { ...config } }))
    const warning = host.querySelector('[role="note"][aria-label="E6 屏幕接线提醒"]')!
    expect(warning).not.toBeNull()
    expect(warning.previousElementSibling).toBe(host.querySelector('.profile-form-section'))
    expect(warning.textContent).toContain('普通设备主板')
    expect(warning.textContent).toContain('绝缘胶带贴住屏幕排线的第 16 脚，使其悬空')
    expect(warning.textContent).toContain('屏幕会发热，且非常耗电')
    expect(client.setScreen).not.toHaveBeenCalled()
    expect(client.setRuntimePinConfig).not.toHaveBeenCalled()
  },
)

it('follows the selected draft E6 profile offline and removes the warning for a non-E6 profile', async () => {
  await act(async () => useDeviceStore.setState({ connection: 'idle', deviceId: '', screen: null }))
  const select = async (e6: boolean) => {
    await act(async () => host.querySelector<HTMLButtonElement>('.profile-select-trigger')!.click())
    const options = [...host.querySelectorAll<HTMLButtonElement>('[role="option"]')]
    const option = options.find(button => e6 ? button.textContent?.includes('HT0154A10') : button.textContent?.includes('SSD1681'))!
    expect(option).toBeDefined()
    await act(async () => option.click())
  }
  await select(true)
  expect(host.querySelector('.e6-panel-warning')).not.toBeNull()
  await select(false)
  expect(host.querySelector('.e6-panel-warning')).toBeNull()
  expect(client.setScreen).not.toHaveBeenCalled()
  expect(client.setRuntimePinConfig).not.toHaveBeenCalled()
})

it('places profile saving in the title and device saving beside the selector', async () => {
  const screenTitle = host.querySelector('.panel-title')!
  expect(screenTitle.querySelector('button')?.textContent).toContain('保存新 Profile')
  const row = host.querySelector('.profile-form-section')!
  expect([...row.querySelectorAll('button')].map((button) => button.textContent))
    .toEqual(expect.arrayContaining(['保存到设备', '测试清屏']))
  await act(async () => screenTitle.querySelector<HTMLButtonElement>('button')!.click())
  expect(host.querySelector('.profile-editor')).not.toBeNull()
  expect(client.setScreen).not.toHaveBeenCalled()
})

it('uses the same section-title structure for device fonts and screen settings', () => {
  const fontTitle = host.querySelector('.gui-asset-settings > .panel-title h2')
  expect(fontTitle?.textContent).toBe('设备字体')
  expect(host.querySelector('.gui-asset-settings .behavior-section-heading')).toBeNull()
  expect(host.querySelector('.gui-asset-settings .font-asset-unavailable')?.textContent)
    .toBe('当前固件尚未声明动态字体资源能力。')
})

it.each([true,false])('uses an in-page modal before manual font upload, accepted=%s',async accepted=>{
  vi.spyOn(window,'confirm').mockImplementation(()=>{throw Error('Browser confirm must not be used')})
  const pkg=buildFontRoles(defaultGuiFonts(),{},1)
  client.readGuiAssetInfo.mockResolvedValue({version:1,schema:0,activeBank:null,storageMode:1,generation:0,activeSize:0,
    maximumSize:32768,activeCrc32:0,supportedSchemas:2,roleVersion:1,flags:3})
  client.writeGuiAssetPackage.mockResolvedValue({version:1,schema:1,activeBank:0,storageMode:1,generation:1,activeSize:pkg.totalSize,
    maximumSize:32768,activeCrc32:pkg.packageCrc32,supportedSchemas:2,roleVersion:1,flags:3})
  await act(async()=>useDeviceStore.setState({activation:{phase:'active',edition:'advanced'},
    settingsCapabilities:{...emptyDeviceSettingsCapabilities,guiAssetUpdate:true}}))
  const fontSection=host.querySelector('.gui-asset-settings')!
  const button=(text:string)=>[...fontSection.querySelectorAll<HTMLButtonElement>('button')].find(b=>b.textContent===text)!
  await act(async()=>button('生成默认包').click())
  await act(async()=>button('写入设备').click())
  const modal=fontSection.querySelector('.confirmation-dialog')!
  expect(modal.textContent).toContain('本设备只有一个用户字库区')
  expect(client.writeGuiAssetPackage).not.toHaveBeenCalled()
  expect(fontSection.textContent).not.toContain('正在写入')
  await act(async()=>modal.querySelector<HTMLButtonElement>(accepted?'button.primary':'button.secondary')!.click())
  if(accepted)expect(client.writeGuiAssetPackage).toHaveBeenCalledWith(pkg,expect.any(Function),{confirmSingleUser:true})
  else expect(client.writeGuiAssetPackage).not.toHaveBeenCalled()
  expect(window.confirm).not.toHaveBeenCalled()
  vi.mocked(window.confirm).mockRestore()
})

it('clears the effective profile without applying settings or deleting slots', async () => {
  expect(clearButton().disabled).toBe(false)
  await act(async () => clearButton().click())
  expect(client.runAction).toHaveBeenCalledExactlyOnceWith('clear')
  expect(client.setScreen).not.toHaveBeenCalled()
  expect(client.clearStoredImages).not.toHaveBeenCalled()
  expect(document.body.textContent).toContain('测试清屏完成。')
})

it.each([5, 0xffffffff])('imports installed serial %i as editable fonts and uploads a fresh package', async generation => {
  const installed = buildFontRoles(defaultGuiFonts(), {}, generation)
  const live = { version:1, schema:1, activeBank:0, storageMode:1, generation,
    activeSize:installed.totalSize, maximumSize:32768, activeCrc32:installed.packageCrc32,
    supportedSchemas:2, roleVersion:1, flags:3 }
  client.readGuiAssetInfo.mockResolvedValue(live)
  const next = buildFontRoles(defaultGuiFonts(), {}, (generation+1)>>>0)
  client.writeGuiAssetPackage.mockResolvedValue({ ...live, generation:next.generation,
    activeSize:next.totalSize, activeCrc32:next.packageCrc32 })
  await act(async () => useDeviceStore.setState({ activation:{phase:'active',edition:'advanced'},
    settingsCapabilities:{...emptyDeviceSettingsCapabilities,guiAssetUpdate:true} }))
  const section=host.querySelector('.gui-asset-settings')!
  const input=section.querySelector<HTMLInputElement>('input[accept=".itfa,.bin"]')!
  const file=new File([installed.bytes.slice().buffer], 'saved-fonts.itfa')
  Object.defineProperty(file, 'arrayBuffer', { value:async()=>installed.bytes.slice().buffer })
  Object.defineProperty(input, 'files', { configurable:true, value:[file] })
  await act(async()=>input.dispatchEvent(new Event('change',{bubbles:true})))
  expect(section.textContent).toContain('saved-fonts.itfa')
  const write=[...section.querySelectorAll<HTMLButtonElement>('button')].find(b=>b.textContent==='写入设备')!
  await act(async()=>write.click())
  const confirm=section.querySelector<HTMLButtonElement>('.confirmation-dialog button.primary')!
  expect(client.writeGuiAssetPackage).not.toHaveBeenCalled()
  await act(async()=>confirm.click())
  expect(client.writeGuiAssetPackage).toHaveBeenCalledWith(next,expect.any(Function),{confirmSingleUser:true})
})

it('requires applying draft screen settings before a clear test', async () => {
  const size = host.querySelectorAll<HTMLSelectElement>('.form-grid select')[1]
  await act(async () => { size.value = '104x212'; size.dispatchEvent(new Event('change', { bubbles: true })) })
  expect(clearButton().disabled).toBe(true)
  expect(clearButton().title).toContain('保存到设备')
  await act(async () => clearButton().click())
  expect(client.runAction).not.toHaveBeenCalled()
})

it('disables clearing while busy or disconnected and preserves failure detail', async () => {
  await act(async () => useDeviceStore.setState({ action: 'ota' }))
  expect(clearButton().disabled).toBe(true)
  await act(async () => useDeviceStore.setState({ action: 'idle', connection: 'idle' }))
  expect(clearButton().disabled).toBe(true)
  await act(async () => useDeviceStore.setState({ connection: 'connected' }))
  client.runAction.mockRejectedValueOnce(new Error('BUSY timeout'))
  await act(async () => clearButton().click())
  expect(document.body.textContent).toContain('测试清屏失败：BUSY timeout')
  expect(document.body.textContent).not.toContain('测试清屏完成。')
})

const pinReport = (): PinReport => {
  const effective: PinConfig = { activeLow: 32, pins: Object.fromEntries(pinRoles.map((role, i) => [role, i < 10 ? i : 255])) as PinConfig['pins'] }
  return { effective, default: effective, defaultValid: true, editableRoles: 0x3ff, overridePresent: false }
}
const button = (name: string) => [...host.querySelectorAll<HTMLButtonElement>('button')].find(b => b.textContent === name)!
async function openDriverDetails() {
  await act(async () => useDeviceStore.setState({ reportedPinConfig: pinReport(), settingsCapabilities: { ...emptyDeviceSettingsCapabilities, runtimePinConfig: true } }))
  await act(async () => button('高级设置').click())
}
it('removes the driver-owned BUSY polarity display and leaves the recipe/scale controls', async () => {
  await openDriverDetails()
  expect(host.querySelector('input[aria-label="BUSY 有效电平"]')).toBeNull()
  expect(host.querySelector('select[aria-label="BUSY 有效电平"]')).toBeNull()
  const labels = [...host.querySelectorAll('.advanced-grid > label')].map(label => label.firstChild?.textContent)
  expect(labels.slice(0, 2)).toEqual(['屏幕驱动方案', '原生内容缩放'])
  expect(client.getRuntimePinConfig).not.toHaveBeenCalled()
  expect(host.textContent).not.toMatch(/读取设备|保存 BUSY 极性/)
  expect(clearButton().disabled).toBe(false)
})
it('profile save writes the screen once and never writes routing or polarity GPIOs', async () => {
  await openDriverDetails()
  const pins=structuredClone(useDeviceStore.getState().reportedPinConfig)
  await act(async () => button('保存到设备').click())
  expect(client.setScreen).toHaveBeenCalledTimes(1)
  expect(client.getRuntimePinConfig).not.toHaveBeenCalled()
  expect(client.setRuntimePinConfig).not.toHaveBeenCalled()
  expect(useDeviceStore.getState().reportedPinConfig).toEqual(pins)
  expect(client.clearStoredImages).not.toHaveBeenCalled()
  expect(document.body.textContent).toContain('屏幕配置已保存。')
})

it.each([false,true])('E6 profile save checks Pro before any image deletion: %s',async pro=>{
  const config=builtInScreenProfiles.find(p=>p.id==='osprey_154_e6')!.config
  await act(async()=>useDeviceStore.setState({screen:config,imageStoreItems:[{id:1}] as never,
    activation:pro?{phase:'active',edition:'advanced'}:{phase:'inactive',edition:'base'}}))
  expect(host.querySelector('[aria-label="E6 Pro 说明"]')?.textContent).toContain('需要 Pro')
  await act(async()=>button('保存到设备').click())
  if(pro) {
    expect(client.setScreen).toHaveBeenCalledWith(config)
    expect(document.querySelector('[role="status"].library-message')?.textContent).toContain('屏幕配置已保存')
    expect(document.querySelector('.library-message.error')).toBeNull()
  }
  else {
    expect(client.setScreen).not.toHaveBeenCalled()
    expect(document.querySelector('[role="alert"].library-message.error')?.textContent)
      .toContain('未清空图片，也未保存屏幕配置')
  }
  expect(client.clearStoredImages).not.toHaveBeenCalled()
})

it('clears the previous error appearance after a successful screen save', async () => {
  const config=builtInScreenProfiles.find(p=>p.id==='osprey_154_e6')!.config
  await act(async()=>useDeviceStore.setState({screen:config,activation:{phase:'inactive',edition:'base'}}))
  await act(async()=>button('保存到设备').click())
  expect(document.querySelector('[role="alert"].library-message.error')).not.toBeNull()
  await act(async()=>useDeviceStore.setState({activation:{phase:'active',edition:'advanced'}}))
  await act(async()=>button('保存到设备').click())
  expect(document.querySelector('[role="alert"].library-message.error')).toBeNull()
  expect(document.querySelector('[role="status"].library-message')?.textContent).toContain('屏幕配置已保存')
})

it.each([false,true])('does not clear existing non-E6 images when Pro is absent or lost during confirmation: %s',async initiallyPro=>{
  const state=useDeviceStore.getState()
  await act(async()=>useDeviceStore.setState({capabilities:{...state.capabilities,imageStore:true},
    imageStoreItems:[{id:1}] as never,
    activation:initiallyPro?{phase:'active',edition:'advanced'}:{phase:'inactive',edition:'base'}}))
  await act(async()=>host.querySelector<HTMLButtonElement>('.profile-select-trigger')!.click())
  const e6=[...host.querySelectorAll<HTMLButtonElement>('[role="option"]')].find(b=>b.textContent?.includes('HT0154A10'))!
  await act(async()=>e6.click())
  await act(async()=>button('保存到设备').click())
  if(initiallyPro) {
    expect(host.querySelector('.confirmation-dialog')).not.toBeNull()
    await act(async()=>useDeviceStore.setState({activation:{phase:'inactive',edition:'base'}}))
    await act(async()=>button('保存并清空').click())
  }
  expect(client.clearStoredImages).not.toHaveBeenCalled()
  expect(client.setScreen).not.toHaveBeenCalled()
  expect(useDeviceStore.getState().imageStoreItems).toHaveLength(1)
  expect(document.body.textContent).toContain('未清空图片，也未保存屏幕配置')
})
it('reports an unknown screen ACK and blocks replay until the current device state is reread', async () => {
  await openDriverDetails()
  client.setScreen.mockImplementationOnce(async()=>{useDeviceStore.setState({ioWriteUnconfirmed:true,screen:null});throw new ScreenWriteUnconfirmedError()})
  await act(async () => button('保存到设备').click())
  expect(document.body.textContent).toContain('未确认')
  expect(button('保存到设备').disabled).toBe(true)
  expect(clearButton().disabled).toBe(true)
  await act(async()=>button('保存到设备').click())
  expect(client.setScreen).toHaveBeenCalledTimes(1)
  expect(client.setRuntimePinConfig).not.toHaveBeenCalled()
})
it('does not accept a successful profile ACK after the connected device has changed', async () => {
  await openDriverDetails()
  client.setScreen.mockImplementationOnce(async () => { useDeviceStore.setState({ deviceId: 'screen-fixture-B', reportedPinConfig: null }) })
  await act(async () => button('保存到设备').click())
  expect(client.setScreen).toHaveBeenCalledTimes(1)
  expect(document.body.textContent).toContain('设备已变化，未采用保存结果')
  expect(document.body.textContent).not.toContain('屏幕配置已保存。')
  expect(client.clearStoredImages).not.toHaveBeenCalled()
  expect(client.setRuntimePinConfig).not.toHaveBeenCalled()
})
it('does not apply an IC recommendation over device BUSY when a preset has no saved polarity', async () => {
  await act(async () => useDeviceStore.setState({ reportedPinConfig: pinReport(), settingsCapabilities: { ...emptyDeviceSettingsCapabilities, runtimePinConfig: true } }))
  await act(async () => host.querySelector<HTMLButtonElement>('.profile-select-trigger')!.click())
  await act(async () => host.querySelector<HTMLButtonElement>('[role="option"]')!.click())
  await act(async () => button('保存到设备').click())
  expect(client.setScreen).toHaveBeenCalledTimes(1)
  expect(client.getRuntimePinConfig).not.toHaveBeenCalled()
  expect(client.setRuntimePinConfig).not.toHaveBeenCalled()
})
