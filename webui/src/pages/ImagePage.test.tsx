// @vitest-environment jsdom
import {act} from 'react'
import {createRoot,type Root} from 'react-dom/client'
import {MemoryRouter} from 'react-router-dom'
import {afterEach,beforeEach,expect,it,vi} from 'vitest'
import {ImagePage} from './ImagePage'
import {optimizeCanvas} from '../image/optimize'
import {imageEditorDraft} from '../image/editorDraft'
import {useDeviceStore} from '../store/device'
import {createEditionBadgeBitmap,editionBadgeGeometry} from '../device-gui/editionBadgeBitmap'
import {deviceScreenProfiles} from '../data/screenProfiles'

vi.mock('../components/DeviceImageSlots',()=>({DeviceImageSlots:()=> <div>设备图片区域</div>}))
vi.mock('../components/SendCanvasDialog',()=>({SendCanvasDialog:()=>null}))
vi.mock('../device-gui/editionBadgeBitmap',async importOriginal=>({...await importOriginal<typeof import('../device-gui/editionBadgeBitmap')>(),createEditionBadgeBitmap:vi.fn(()=>document.createElement('canvas'))}))
vi.mock('../image/optimize',()=>({algorithmGroups:[{label:'常用',options:[['auto','Hybrid 自适应（默认）']]}],processingPresets:[['balanced','不处理']],usesDirectStrength:()=>false,optimizeCanvas:vi.fn(async()=>{})}))

let host:HTMLDivElement,root:Root
const images:Array<{onload:(()=>void)|null;onerror:(()=>void)|null;src:string;naturalWidth:number;naturalHeight:number}>=[]
const oldDraft={...imageEditorDraft}
const previousDevice=useDeviceStore.getState()
beforeEach(()=>{
  vi.stubGlobal('IS_REACT_ACT_ENVIRONMENT',true);vi.useFakeTimers()
  images.length=0;Object.assign(imageEditorDraft,oldDraft,{source:null,sourceUrl:''})
  vi.stubGlobal('Image',class {onload=null;onerror=null;src='';naturalWidth=400;naturalHeight=300;constructor(){images.push(this)}})
  vi.stubGlobal('requestAnimationFrame',(callback:FrameRequestCallback)=>setTimeout(()=>callback(0),0))
  vi.stubGlobal('cancelAnimationFrame',(id:number)=>clearTimeout(id))
  vi.spyOn(URL,'createObjectURL').mockImplementation(()=>`blob:test-${images.length}`)
  vi.spyOn(URL,'revokeObjectURL').mockImplementation(()=>{})
  vi.spyOn(HTMLCanvasElement.prototype,'getContext').mockImplementation(()=>({drawImage:vi.fn(),fillRect:vi.fn(),clearRect:vi.fn(),save:vi.fn(),restore:vi.fn(),translate:vi.fn(),rotate:vi.fn(),getImageData:()=>({data:new Uint8ClampedArray(400*300*4),width:400,height:300})}) as never)
  useDeviceStore.setState({connection:'connected',deviceName:'IKT_TL_TEST',action:'idle',screen:null})
  host=document.createElement('div');document.body.append(host);root=createRoot(host)
})
afterEach(async()=>{
  await act(async()=>root.unmount());host.remove();vi.useRealTimers();vi.restoreAllMocks();vi.unstubAllGlobals()
  Object.assign(imageEditorDraft,oldDraft);useDeviceStore.setState(previousDevice,true)
})
const open=()=>act(async()=>root.render(<MemoryRouter><ImagePage /></MemoryRouter>))
const choose=()=>act(async()=>{
  const input=host.querySelector<HTMLInputElement>('input[type=file]')!
  Object.defineProperty(input,'files',{configurable:true,value:[new File(['fixture'],'fixture.png',{type:'image/png'})]})
  input.dispatchEvent(new Event('change',{bubbles:true}))
})
const clear=()=>act(async()=>[...host.querySelectorAll<HTMLButtonElement>('button')].find(b=>b.title==='清除图片')!.click())

it('describes E6 without a product brand or assuming a two-stage controller',async()=>{
  imageEditorDraft.simulatedColor=10
  await open()
  expect(host.textContent).toContain('E6 六色图片会按当前屏幕的传输格式自动编码并发送。')
  expect(host.textContent).not.toContain('图公主')
  expect(host.textContent).not.toContain('两个 2bpp')
  expect(host.textContent).not.toContain('E6 感知抖动')
  expect([...host.querySelectorAll('select')].find(s=>[...s.options].some(o=>o.value==='auto'))!.value).toBe('auto')
})

it('resets an unavailable in-memory algorithm selection without affecting the connection',async()=>{
  Object.assign(imageEditorDraft,{simulatedColor:10,algorithm:'removed-algorithm'})
  await open()
  expect(host.querySelector('option[value="e6Perceptual"]')).toBeNull()
  expect(imageEditorDraft.algorithm).toBe('auto')
  expect(useDeviceStore.getState().connection).toBe('connected')
})

for (const id of ['osprey_370_e6','huawei_398_a0','huawei_398_a1']) {
  for (const transform of [0,1,2,3]) {
    it(`turns the canvas, not the portrait photo, for ${id}, screen rotation ${transform}`,async()=>{
      const screen={...deviceScreenProfiles.find(p=>p.id===id)!.config,transform}
      useDeviceStore.setState({screen})
      await open();await choose()
      images[0].naturalWidth=480;images[0].naturalHeight=720
      await act(async()=>images[0].onload?.())
      const canvas=host.querySelector<HTMLCanvasElement>('.original-layer')!
      await act(async()=>vi.advanceTimersByTimeAsync(1))
      expect([canvas.width,canvas.height]).toEqual([Math.min(screen.visibleWidth,screen.visibleHeight),Math.max(screen.visibleWidth,screen.visibleHeight)])
      expect(host.querySelector<HTMLImageElement>('.image-crop-bleed img')!.style.transform).toContain('rotate(0deg)')
      expect(imageEditorDraft.rotationOffset).toBe(0)
      expect(useDeviceStore.getState().screen).toEqual(screen)
    })
  }
}

it('drops manual offsets from the previous screen before fitting a 3.7 E6 canvas to the photo',async()=>{
  useDeviceStore.setState({screen:deviceScreenProfiles.find(p=>p.id==='huawei_398_a0')!.config})
  await open();await choose()
  images[0].naturalWidth=480;images[0].naturalHeight=720
  await act(async()=>images[0].onload?.())
  await act(async()=>host.querySelector<HTMLButtonElement>('button[title="向右旋转"]')!.click())
  expect(imageEditorDraft.rotationOffset).toBe(90)
  const screen=deviceScreenProfiles.find(p=>p.id==='osprey_370_e6')!.config
  await act(async()=>useDeviceStore.setState({screen}))
  expect(imageEditorDraft.rotationOffset).toBe(0)
  expect(imageEditorDraft.pan).toEqual({x:0,y:0})
  expect(imageEditorDraft.imageScale).toBe(1)
  expect(host.querySelector<HTMLImageElement>('.image-crop-bleed img')!.style.transform).toContain('rotate(0deg)')
})

it('uses a landscape canvas for a landscape photo on a portrait-configured 3.7 E6 screen',async()=>{
  const screen={...deviceScreenProfiles.find(p=>p.id==='osprey_370_e6')!.config,transform:1}
  useDeviceStore.setState({screen})
  await open();await choose();await act(async()=>images[0].onload?.())
  await act(async()=>vi.advanceTimersByTimeAsync(1))
  const canvas=host.querySelector<HTMLCanvasElement>('.original-layer')!
  expect([canvas.width,canvas.height]).toEqual([720,480])
  expect(host.querySelector<HTMLImageElement>('.image-crop-bleed img')!.style.transform).toContain('rotate(0deg)')
  expect(useDeviceStore.getState().screen).toEqual(screen)
})

it('edits an upright bottom-right badge even before firmware orientation negotiation',async()=>{
  const screen=deviceScreenProfiles.find(p=>p.id==='osprey_370_e6')!.config
  useDeviceStore.setState({screen,activation:{phase:'inactive',edition:'base'},brandVisible:false})
  await open();await choose();images[0].naturalWidth=480;images[0].naturalHeight=720
  await act(async()=>images[0].onload?.())
  const badge=editionBadgeGeometry(480,720,'base')
  const layer=host.querySelector<HTMLCanvasElement>('.edition-preview-layer')!
  expect(layer.style.left).toBe(`${badge.x}px`)
  expect(layer.style.top).toBe(`${badge.y}px`)
  expect(layer.style.transform).toBe('rotate(0deg)')
  expect(createEditionBadgeBitmap).toHaveBeenLastCalledWith('base',10,480,720)
})

it('honors verified Pro hidden branding on the image page but never hides base',async()=>{
  useDeviceStore.setState({activation:{phase:'active',edition:'advanced'},brandVisible:false})
  await open();await choose();await act(async()=>images[0].onload?.())
  expect(host.querySelector('.edition-preview-layer')).toBeNull()
  await act(async()=>useDeviceStore.setState({activation:{phase:'inactive',edition:'base'}}))
  expect(host.querySelector('.edition-preview-layer')).not.toBeNull()
})

it('previews upright bottom-right badge for a live reading-orientation declaration',async()=>{
  const screen=deviceScreenProfiles.find(p=>p.id==='osprey_370_e6')!.config
  useDeviceStore.setState({screen,capabilities:{...useDeviceStore.getState().capabilities,imageReadingOrientation:true},
    activation:{phase:'inactive',edition:'base'},brandVisible:false})
  await open();await choose();images[0].naturalWidth=480;images[0].naturalHeight=720
  await act(async()=>images[0].onload?.())
  const badge=editionBadgeGeometry(480,720,'base')
  const layer=host.querySelector<HTMLCanvasElement>('.edition-preview-layer')!
  expect(layer.style.left).toBe(`${badge.x}px`);expect(layer.style.top).toBe(`${badge.y}px`)
  expect(layer.style.transform).toBe('rotate(0deg)')
  expect(createEditionBadgeBitmap).toHaveBeenLastCalledWith('base',10,480,720)
})

it.each(['connected','idle'] as const)('separates canvas and photo rotation while %s',async(connection)=>{
  const screen=deviceScreenProfiles.find(p=>p.id==='osprey_370_e6')!.config
  useDeviceStore.setState({connection,screen,activation:{phase:'inactive',edition:'base'}})
  await open()
  const turn=host.querySelector<HTMLButtonElement>('button[title="旋转画布"]')!
  expect(turn.disabled).toBe(true)
  expect(turn.previousElementSibling?.getAttribute('title')).toBe('放大预览')
  await choose();images[0].naturalWidth=480;images[0].naturalHeight=720
  await act(async()=>images[0].onload?.())
  const frame=host.querySelector<HTMLDivElement>('.image-device-frame')!
  const layer=host.querySelector<HTMLCanvasElement>('.edition-preview-layer')!
  const portrait=[frame.style.width,frame.style.height]
  const originalBadge=[layer.style.left,layer.style.top,layer.style.transform]
  expect(Number.parseFloat(portrait[0])).toBeLessThan(Number.parseFloat(portrait[1]))
  expect(layer.style.transform).toBe('rotate(0deg)')
  await act(async()=>host.querySelector<HTMLButtonElement>('button[title="向右旋转"]')!.click())
  expect(imageEditorDraft.rotationOffset).toBe(90)
  expect([frame.style.width,frame.style.height]).toEqual(portrait)
  expect([layer.style.left,layer.style.top,layer.style.transform]).toEqual(originalBadge)
  await act(async()=>turn.click())
  expect([frame.style.width,frame.style.height]).toEqual([...portrait].reverse())
  expect(imageEditorDraft.rotationOffset).toBe(90)
  expect(host.querySelector<HTMLImageElement>('.image-crop-bleed img')!.style.transform).toContain('rotate(90deg)')
  expect(layer.style.transform).toBe('rotate(0deg)')
  const landscapeBadge=editionBadgeGeometry(Number.parseFloat(frame.style.width),Number.parseFloat(frame.style.height),'base')
  expect([layer.style.left,layer.style.top]).toEqual([`${landscapeBadge.x}px`,`${landscapeBadge.y}px`])
  expect(imageEditorDraft.canvasTurned).toBe(true)
  // The send action must wait for fresh device pixels after a frame change.
  if(connection==='connected')expect(host.querySelector<HTMLButtonElement>('button[title="发送图片"]')!.disabled).toBe(true)
  expect(useDeviceStore.getState().screen).toEqual(screen)
  await act(async()=>root.render(<div>其他页面</div>));await open()
  expect(imageEditorDraft.canvasTurned).toBe(true)
  expect([host.querySelector<HTMLDivElement>('.image-device-frame')!.style.width,host.querySelector<HTMLDivElement>('.image-device-frame')!.style.height]).toEqual([...portrait].reverse())
  await act(async()=>host.querySelector<HTMLButtonElement>('button[title="旋转画布"]')!.click())
  expect(imageEditorDraft.canvasTurned).toBe(false)
  expect(imageEditorDraft.rotationOffset).toBe(90)
})

it('resets canvas orientation for a newly imported image, without changing the panel',async()=>{
  const screen=deviceScreenProfiles.find(p=>p.id==='osprey_370_e6')!.config
  useDeviceStore.setState({screen})
  await open();await choose();await act(async()=>images[0].onload?.())
  await act(async()=>host.querySelector<HTMLButtonElement>('button[title="旋转画布"]')!.click())
  expect(imageEditorDraft.canvasTurned).toBe(true)
  await choose();await act(async()=>images[1].onload?.())
  expect(imageEditorDraft.canvasTurned).toBe(false)
  const frame=host.querySelector<HTMLDivElement>('.image-device-frame')!
  expect([frame.style.width,frame.style.height]).toEqual(['720px','480px'])
  expect(useDeviceStore.getState().screen).toEqual(screen)
})

it('does not offer a no-op canvas turn for a square screen',async()=>{
  imageEditorDraft.targetResolution='200x200'
  await open();await choose();await act(async()=>images[0].onload?.())
  expect(host.querySelector<HTMLButtonElement>('button[title="旋转画布"]')!.disabled).toBe(true)
})

it('opens empty, imports, clears and imports again without touching the connection',async()=>{
  await open()
  expect(host.querySelector('h1')?.textContent).toBe('图片')
  expect(host.querySelector('.image-drop-zone')).not.toBeNull()
  await choose();await act(async()=>images[0].onload?.())
  expect(host.querySelector('.image-device-frame')).not.toBeNull()
  expect(createEditionBadgeBitmap).toHaveBeenCalled()
  await clear();await act(async()=>vi.runAllTimersAsync())
  expect(host.querySelector('.image-drop-zone')).not.toBeNull()
  expect(imageEditorDraft.source).toBeNull();expect(imageEditorDraft.sourceUrl).toBe('')
  expect(URL.revokeObjectURL).toHaveBeenCalledWith('blob:test-0')
  expect(useDeviceStore.getState().connection).toBe('connected');expect(useDeviceStore.getState().deviceName).toBe('IKT_TL_TEST')
  await choose();await act(async()=>images[1].onload?.())
  expect(host.querySelector('.image-device-frame')).not.toBeNull()
})

it('does not resurrect a slow image after clearing or unmounting',async()=>{
  await open();await choose();await clear();await act(async()=>images[0].onload?.())
  expect(imageEditorDraft.source).toBeNull();expect(host.querySelector('.image-drop-zone')).not.toBeNull()
  await choose();await act(async()=>root.render(<div>其他页面</div>));await act(async()=>images[1].onload?.())
  expect(imageEditorDraft.source).toBeNull()
})

it('shows decoding failure and releases its pending object URL',async()=>{
  await open();await choose();await act(async()=>images[0].onerror?.())
  expect(document.body.textContent).toContain('无法读取这张图片')
  expect(host.querySelector('.image-drop-zone')).not.toBeNull()
  expect(URL.revokeObjectURL).toHaveBeenCalledWith('blob:test-0')
})


it('passes the brightness slider into processing and retains it across page navigation',async()=>{
  await open();await choose();await act(async()=>images[0].onload?.())
  await act(async()=>vi.advanceTimersByTimeAsync(1))
  const input=host.querySelector<HTMLInputElement>('input[aria-label="图片亮度"]')!
  expect(input.value).toBe('0')
  await act(async()=>{
    Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value')!.set!.call(input,'25')
    input.dispatchEvent(new Event('input',{bubbles:true}))
  })
  await act(async()=>vi.advanceTimersByTimeAsync(1))
  await act(async()=>vi.advanceTimersByTimeAsync(80))
  expect(imageEditorDraft.brightness).toBe(25)
  expect(vi.mocked(optimizeCanvas).mock.calls.at(-1)?.[3]).toMatchObject({brightness:25})
  await act(async()=>root.render(<MemoryRouter><div>另一页面</div></MemoryRouter>))
  await open()
  expect(host.querySelector<HTMLInputElement>('input[aria-label="图片亮度"]')!.value).toBe('25')
})
