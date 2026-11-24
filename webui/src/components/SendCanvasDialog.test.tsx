// @vitest-environment jsdom
import { act } from 'react'
import { createRoot, type Root } from 'react-dom/client'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { SendCanvasDialog } from './SendCanvasDialog'
import { useDeviceStore } from '../store/device'
import { makeScreenConfig } from '../protocol/screen'
import type { ImageStoreInfo } from '../types'

const client = vi.hoisted(() => ({ sendCanvas: vi.fn(), listStoredImages: vi.fn() }))
vi.mock('../protocol/client', () => ({ inktagClient: client }))
const initial = useDeviceStore.getState()
const screen = makeScreenConfig({ driver: 0x13, color: 2, transform: 0,
  width: 400, height: 300, visibleX: 0, visibleY: 0, visibleWidth: 400, visibleHeight: 300 })
const storeInfo: ImageStoreInfo = { width: 400, height: 300, bytesPerImage: 15000, requiredSectors: 4,
  totalSectors: 60, freeSectors: 52, largestFreeRun: 52, fit: 13, contiguousFit: 13,
  physicalContiguousFit: 13, occupiedIds: [0, 7], fixedSlotCount: 15, saved: 2, compatible: 2,
  playable: 2, incompatible: 0, slideshow: false, intervalSeconds: 300 }

describe('shared canvas send dialog (no real device)', () => {
  let host: HTMLDivElement, root: Root, canvas: HTMLCanvasElement
  let close = vi.fn<() => void>(), message = vi.fn<(text: string) => void>()
  beforeEach(() => {
    vi.stubGlobal('IS_REACT_ACT_ENVIRONMENT', true)
    vi.stubGlobal('ImageData',class {constructor(public data:Uint8ClampedArray,public width:number,public height:number){}})
    vi.spyOn(HTMLCanvasElement.prototype,'getContext').mockImplementation(()=>({
      getImageData:(_x:number,_y:number,width:number,height:number)=>({width,height,data:new Uint8ClampedArray(width*height*4).fill(255)}),putImageData:vi.fn(),
    }) as never)
    useDeviceStore.setState({ ...initial, connection: 'connected', deviceId: 'virtual-device', screen,
      capabilities: { ...initial.capabilities, imageStore: true, imageMetadata: true },
      imageStoreInfo: { ...storeInfo }, imageStoreItems: [0, 7].map(id => ({ id, name: id ? '课程表' : '今日待办',
        width: 400, height: 300, bytes: 15000, createdAt: 1, format: 1, segments: 1, compatible: true, contentType: 'page' })) })
    host = document.createElement('div'); document.body.append(host); root = createRoot(host)
    canvas = document.createElement('canvas'); canvas.width = 400; canvas.height = 300
    close = vi.fn(); message = vi.fn(); client.sendCanvas.mockReset().mockResolvedValue(2); client.listStoredImages.mockReset()
  })
  afterEach(async () => { await act(async () => root.unmount()); host.remove(); useDeviceStore.setState(initial); vi.restoreAllMocks(); vi.unstubAllGlobals() })
  const render = async (extra: Partial<Parameters<typeof SendCanvasDialog>[0]> = {}) => act(async () => {
    root.render(<SendCanvasDialog defaultName="我的页面" contentType="page" prepareCanvas={() => canvas} close={close} message={message} {...extra} />)
  })
  const button = (text: string) => [...document.querySelectorAll<HTMLButtonElement>('button')].find(node => node.textContent === text)!
  const click = async (text: string) => act(async () => button(text).click())
  const radio = async (label: string) => act(async () => [...document.querySelectorAll('label')].find(node => node.querySelector('strong')?.textContent === label)!.querySelector<HTMLInputElement>('input')!.click())
  const inputName = async (value: string) => act(async () => {
    const input = document.querySelector<HTMLInputElement>('[aria-label="图片名称"]')!
    Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value')!.set!.call(input, value)
    input.dispatchEvent(new Event('input', { bubbles: true }))
  })
  it('sends once without storing, even when the default name is a reserved NFC command', async () => {
    await render({ defaultName: '日历' }); await radio('仅显示一次'); await click('开始发送')
    expect(client.sendCanvas).toHaveBeenCalledExactlyOnceWith(canvas, '日历', false, 'page', undefined, true)
    expect(message).toHaveBeenCalledWith('画面已发送，屏幕刷新完成。')
    expect(close).toHaveBeenCalledOnce()
  })
  it('saves a named page to a selected empty slot, optionally without refreshing', async () => {
    await render()
    await radio('保存到图槽')
    await act(async () => document.querySelector<HTMLButtonElement>('[aria-label="槽 3，空槽"]')!.click())
    await inputName('  测试页面  ')
    await act(async () => document.querySelector<HTMLInputElement>('input[type=checkbox]')!.click())
    await click('开始发送')
    expect(client.sendCanvas).toHaveBeenCalledExactlyOnceWith(canvas, '测试页面', true, 'page', 2, false)
    expect(message).toHaveBeenCalledWith('已保存到设备图槽 3，屏幕保持不变。名称“测试页面”已保存。')
    expect(document.body.textContent).toContain('Pro 设备')
    expect(document.body.textContent).toContain('不会修改 NFC 配置')
  })
  it('defaults to display-only even when free image slots are available',async()=>{
    await render();await click('开始发送')
    expect(client.sendCanvas.mock.calls[0].slice(1)).toEqual(['我的页面',false,'page',undefined,true])
  })
  it.each([0, 3])('permits choosing an existing slot when append is full (physical fit %i)', async physicalContiguousFit => {
    useDeviceStore.setState({ imageStoreInfo: { ...storeInfo, fit: 0, contiguousFit: 0, physicalContiguousFit } })
    await render()
    const stored = [...document.querySelectorAll('label')].find(node => node.querySelector('strong')?.textContent === '保存到图槽')!.querySelector<HTMLInputElement>('input')!
    expect(stored.disabled).toBe(false)
    await radio('保存到图槽')
    expect(document.body.textContent).toContain('图槽已满，可选择已有图片覆盖')
    expect(button('开始发送').disabled).toBe(true)
    expect(document.querySelector<HTMLButtonElement>('[aria-label="槽 3，空槽"]')!.disabled).toBe(true)
    await act(async () => document.querySelector<HTMLButtonElement>('[aria-label="槽 8，已使用，课程表"]')!.click())
    await click('开始发送')
    expect(client.sendCanvas).toHaveBeenCalledExactlyOnceWith(canvas, '我的页面', true, 'page', 7, true)
  })
  it('opens store mode at full capacity without silently switching to display-only', async () => {
    useDeviceStore.setState({ imageStoreInfo: { ...storeInfo, fit: 0, contiguousFit: 0, physicalContiguousFit: 0 } })
    await render({ initialDestination: 'store' })
    expect(document.querySelector('[aria-label="选择保存图槽"]')).not.toBeNull()
    expect(button('开始发送').disabled).toBe(true)
    expect(client.sendCanvas).not.toHaveBeenCalled()
  })
  it('explains old firmware badge orientation without sending unsupported flags',async()=>{
    canvas.width=300;canvas.height=400
    await render({readingCanvasSize:{width:300,height:400}})
    expect(document.body.textContent).toContain('当前固件的 InkTag 标识仍按设备屏幕方向显示')
    await click('开始发送')
    expect(client.sendCanvas.mock.calls[0]).toHaveLength(6)
  })
  it('does not show the badge fallback for current firmware or matching canvas aspect',async()=>{
    await render({readingCanvasSize:{width:400,height:300}})
    expect(document.body.textContent).not.toContain('当前固件的 InkTag 标识仍按设备屏幕方向显示')
    await act(async()=>useDeviceStore.setState({capabilities:{...useDeviceStore.getState().capabilities,imageReadingOrientation:true}}))
    await render({readingCanvasSize:{width:300,height:400}})
    expect(document.body.textContent).not.toContain('当前固件的 InkTag 标识仍按设备屏幕方向显示')
  })
  it('allows keeping the target name on replacement, but refuses another slot name', async () => {
    await render({ defaultName: '课程表',initialDestination:'store' })
    expect(button('开始发送').disabled).toBe(true)
    await act(async () => document.querySelector<HTMLButtonElement>('[aria-label="槽 8，已使用，课程表"]')!.click())
    expect(document.body.textContent).toContain('写入中断可能丢失旧图片')
    expect(button('开始发送').disabled).toBe(false)
    await inputName('今日待办'); expect(button('开始发送').disabled).toBe(true)
    await inputName('课程表'); await click('开始发送')
    expect(client.sendCanvas).toHaveBeenCalledExactlyOnceWith(canvas, '课程表', true, 'page', 7, true)
  })
  it.each(['SLOT-1', '一'.repeat(12), '今日待办'])('refuses an invalid stored name: %s', async value => {
    await render({initialDestination:'store'}); await inputName(value); await click('开始发送')
    expect(document.querySelector('[aria-invalid=true]')).not.toBeNull()
    expect(client.sendCanvas).not.toHaveBeenCalled()
  })
  it('does not invent empty targets without an occupancy snapshot; legacy auto allocation still works', async () => {
    useDeviceStore.setState({ imageStoreInfo: { ...storeInfo, occupiedIds: undefined, fixedSlotCount: undefined } })
    await render({initialDestination:'store'})
    expect(document.querySelector<HTMLButtonElement>('[aria-label="槽 3，尚未读取"]')!.disabled).toBe(true)
    await click('开始发送')
    expect(client.sendCanvas).toHaveBeenCalledExactlyOnceWith(canvas, '我的页面', true, 'page', undefined, true)
  })
  it.each(['loading', 'error'] as const)('blocks storage with a %s list but permits direct display', async resourceReadState => {
    useDeviceStore.setState({ resourceReadState })
    await render()
    const stored = [...document.querySelectorAll('label')].find(node => node.querySelector('strong')?.textContent === '保存到图槽')!
    expect(stored.querySelector<HTMLInputElement>('input')!.disabled).toBe(true)
    await click('开始发送'); expect(client.sendCanvas.mock.calls[0][2]).toBe(false)
  })
  it('preserves direct sending on a device without image storage', async () => {
    useDeviceStore.setState({ capabilities: { ...initial.capabilities, imageStore: false } })
    await render(); await click('开始发送')
    expect(client.sendCanvas.mock.calls[0].slice(1)).toEqual(['我的页面', false, 'page', undefined, true])
  })
  it('reads missing slots once when opened, with no storage write or automatic retry', async () => {
    useDeviceStore.setState({ imageStoreInfo: null })
    client.listStoredImages.mockResolvedValue(undefined)
    await render()
    expect(client.listStoredImages).toHaveBeenCalledOnce()
    expect(client.sendCanvas).not.toHaveBeenCalled()
    await act(async()=>useDeviceStore.setState({action:'idle'}))
    expect(client.listStoredImages).toHaveBeenCalledOnce()
  })
  it('accepts approximate templates and automatically pads/crops to the device', async () => {
    const prepareCanvas=vi.fn(()=>canvas)
    canvas.width=200;canvas.height=200
    await render({prepareCanvas})
    expect(document.body.textContent).not.toContain('不一致')
    expect(button('开始发送').disabled).toBe(false)
    await click('开始发送');expect(prepareCanvas).toHaveBeenCalledOnce()
    expect(client.sendCanvas.mock.calls[0][0]).toMatchObject({width:400,height:300})
    expect(canvas).toMatchObject({width:200,height:200})
  })
  it.each([[250,128],[128,250]])('sends a ZKC21V %i×%i template through its 250×122 window after orientation alignment',async(width,height)=>{
    useDeviceStore.setState({screen:makeScreenConfig({driver:0x23,recipe:1,color:2,transform:9,width:128,height:250,visibleX:0,visibleY:0,visibleWidth:122,visibleHeight:250})})
    canvas.width=width;canvas.height=height
    await render({initialDestination:'display'});await click('开始发送')
    expect(client.sendCanvas.mock.calls[0][0]).toMatchObject({width:250,height:122})
    expect(close).toHaveBeenCalledOnce()
    expect(document.body.textContent).not.toContain('可视区')
  })
  it('refreshes a failed list inside the chooser without sending or losing the page', async () => {
    useDeviceStore.setState({ resourceReadState: 'error', imageStoreInfo: null })
    client.listStoredImages.mockImplementation(async () => useDeviceStore.setState({ resourceReadState: 'idle', imageStoreInfo: storeInfo }))
    await render(); await click('刷新图槽'); await radio('保存到图槽')
    expect(client.listStoredImages).toHaveBeenCalledOnce()
    expect(document.querySelector('[aria-label="选择保存图槽"]')).not.toBeNull()
    expect(client.sendCanvas).not.toHaveBeenCalled()
  })
  it.each(['display','store'] as const)('keeps opposite reading orientation metadata before adapting %s canvas',async initialDestination=>{
    useDeviceStore.setState({capabilities:{...useDeviceStore.getState().capabilities,imageReadingOrientation:true}})
    canvas.width=300;canvas.height=400
    await render({initialDestination});await click('开始发送')
    expect(client.sendCanvas.mock.calls[0][0]).toMatchObject({width:400,height:300})
    expect(client.sendCanvas.mock.calls[0][6]).toBe(1)
    expect(useDeviceStore.getState().screen).toEqual(screen)
  })
  it('automatically pads a differently sized export and does not retry its send', async () => {
    await render({ prepareCanvas: () => { canvas.width = 300; return canvas } })
    await click('开始发送')
    expect(client.sendCanvas).toHaveBeenCalledOnce(); expect(close).toHaveBeenCalledOnce()
    expect(client.sendCanvas.mock.calls[0][0]).toMatchObject({width:400,height:300})
  })
  it.each(['device', 'occupied', 'dynamic'] as const)('rechecks %s state after asynchronous export', async changed => {
    let resolve!: (value: HTMLCanvasElement) => void
    let allowed = true
    await render({ initialDestination:'store',prepareCanvas: () => new Promise(done => { resolve = done }), canSend: () => allowed })
    await act(async () => document.querySelector<HTMLButtonElement>('[aria-label="槽 3，空槽"]')!.click())
    await click('开始发送'); expect(button('开始发送')).toBeUndefined()
    await act(async () => {
      if (changed === 'device') useDeviceStore.setState({ deviceId: 'other-device' })
      else if (changed === 'occupied') useDeviceStore.setState({ imageStoreInfo: { ...storeInfo, occupiedIds: [0, 2, 7] } })
      else allowed = false
      resolve(canvas)
    })
    expect(client.sendCanvas).not.toHaveBeenCalled(); expect(close).not.toHaveBeenCalled()
    expect(document.querySelector('[role=alert]')).not.toBeNull()
  })
  it('serializes double submit and keeps explicit failure visible', async () => {
    let fail!: (cause: Error) => void
    client.sendCanvas.mockImplementation(() => new Promise((_resolve, reject) => { fail = reject }))
    await render(); await click('开始发送')
    await act(async () => document.querySelector('form')!.dispatchEvent(new Event('submit', { bubbles: true, cancelable: true })))
    expect(client.sendCanvas).toHaveBeenCalledOnce()
    await act(async () => fail(new Error('设备拒绝写入')))
    expect(close).not.toHaveBeenCalled(); expect(document.body.textContent).toContain('设备拒绝写入')
  })
  it('closes on Escape and restores the initiating control focus', async () => {
    const trigger = document.createElement('button'); document.body.append(trigger); trigger.focus()
    await render()
    await act(async () => document.querySelector('form')!.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true })))
    expect(close).toHaveBeenCalledOnce()
    // The parent removes the dialog after close in the app.
    await act(async () => root.render(null))
    expect(document.activeElement).toBe(trigger)
    trigger.remove()
  })
})
