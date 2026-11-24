// @vitest-environment jsdom
import {Blob as NodeBlob} from 'node:buffer'
import {act} from 'react'
import {createRoot,type Root} from 'react-dom/client'
import {afterEach,beforeEach,expect,it,vi} from 'vitest'
import {CalendarSettingsDialog} from './CalendarSettingsDialog'
import {useDeviceStore} from '../store/device'
import {defaultGuiFonts,type GuiAssetInfo,type GuiAssetPackage} from '../protocol/guiAssets'
import {buildFontRoles} from '../protocol/fontRoles'
import {catalogMissingGlyphs,missingChineseFontGlyphs} from '../device-gui/fontEditor'
import {packageFonts} from '../data/guiAssetPackages'

const mocks=vi.hoisted(()=>({read:vi.fn(),display:vi.fn(),upload:vi.fn(),save:vi.fn(),refresh:vi.fn(),
  editable:vi.fn(),remember:vi.fn(),preference:vi.fn()}))
vi.mock('../protocol/client',()=>({inktagClient:{readGuiAssetInfo:mocks.read,getDisplaySettings:mocks.display,
  writeGuiAssetPackage:mocks.upload,setDisplayConfiguration:mocks.save,runAction:mocks.refresh},ProtocolV2Error:class extends Error{}}))
vi.mock('../data/guiAssetPackages',async original=>({...await original<typeof import('../data/guiAssetPackages')>(),
  editableGuiFontPackage:mocks.editable,rememberGuiAssetPackage:mocks.remember}))
vi.mock('../data/devicePreferences',()=>({updateDevicePreference:mocks.preference}))
const initial=useDeviceStore.getState()
const info=(pkg?:GuiAssetPackage):GuiAssetInfo=>({version:1,schema:pkg?1:0,activeBank:pkg?0:null,storageMode:1,
  generation:pkg?.generation??0,activeSize:pkg?.totalSize??0,activeCrc32:pkg?.packageCrc32??0,
  maximumSize:32768,supportedSchemas:2,roleVersion:1,flags:3})
let host:HTMLDivElement,root:Root,close:ReturnType<typeof vi.fn<() => void>>
beforeEach(async()=>{
  vi.stubGlobal('IS_REACT_ACT_ENVIRONMENT',true);vi.stubGlobal('Blob',NodeBlob)
  await catalogMissingGlyphs('宝鹏')
  Object.values(mocks).forEach(m=>m.mockReset())
  mocks.read.mockResolvedValue(info());mocks.display.mockResolvedValue({complete:true,weekStart:1,countdownName:'',countdownDate:''})
  mocks.editable.mockResolvedValue({fonts:defaultGuiFonts(),chineseSource:'catalog'})
  mocks.upload.mockResolvedValue(info());mocks.remember.mockResolvedValue(undefined)
  mocks.save.mockResolvedValue(undefined);mocks.refresh.mockResolvedValue(undefined);mocks.preference.mockResolvedValue(undefined)
  vi.spyOn(window,'confirm').mockImplementation(()=>{throw Error('Browser confirm must not be used')})
  useDeviceStore.setState({...initial,connection:'connected',deviceId:'fixture',action:'idle',
    activation:{phase:'active',edition:'advanced'},settingsCapabilities:{...initial.settingsCapabilities,guiAssetUpdate:true}})
  close=vi.fn();host=document.createElement('div');document.body.append(host);root=createRoot(host)
})
afterEach(async()=>{await act(async()=>root.unmount());host.remove();useDeviceStore.setState(initial,true);vi.restoreAllMocks();vi.unstubAllGlobals()})
const open=()=>act(async()=>root.render(<CalendarSettingsDialog deviceId="fixture" countdownSupported onClose={close}/>))
const input=async(selector:string,value:string)=>act(async()=>{
  const field=host.querySelector<HTMLInputElement>(selector)!
  Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value')!.set!.call(field,value)
  field.dispatchEvent(new Event('input',{bubbles:true}));field.dispatchEvent(new Event('change',{bubbles:true}))
})
const fill=async(name='宝宝')=>{await input('input[placeholder="例如：国庆"]',name);await input('input[type=date]','2027-01-01')}
const save=async(settle=true,confirmation:'accept'|'cancel'|'none'='accept')=>{
  const submit=()=>host.querySelector<HTMLButtonElement>('.calendar-settings-dialog button.primary')!
  await act(async()=>submit().click())
  await vi.waitFor(async()=>{
    await act(async()=>Promise.resolve())
    expect(Boolean(host.querySelector('.confirmation-dialog')) || !submit().disabled).toBe(true)
  },{timeout:8000})
  const modal=host.querySelector('.confirmation-dialog')
  if(modal && confirmation!=='none')await act(async()=>modal.querySelector<HTMLButtonElement>(confirmation==='accept'?'button.primary':'button.secondary')!.click())
  if(settle)await vi.waitFor(async()=>{
    await act(async()=>Promise.resolve())
    expect(submit().disabled).toBe(false)
  },{timeout:8000})
}

it('adds 宝 to both real faces after confirmation, then saves and refreshes',async()=>{
  await open();await fill()
  expect(host.querySelector<HTMLButtonElement>('button.primary')!.disabled).toBe(false)
  await save(false,'none')
  const modal=host.querySelector('.confirmation-dialog')!
  expect(modal.textContent).toContain('宝')
  expect(modal.textContent).toContain('更新失败可能丢失原自定义字体')
  expect(modal.textContent).toContain('补字并上传')
  expect(mocks.upload).not.toHaveBeenCalled();expect(mocks.save).not.toHaveBeenCalled()
  expect(window.confirm).not.toHaveBeenCalled()
  await act(async()=>modal.querySelector<HTMLButtonElement>('button.primary')!.click())
  const pkg=mocks.upload.mock.calls[0][0] as GuiAssetPackage
  expect(missingChineseFontGlyphs(packageFonts(pkg),'宝宝')).toEqual([])
  expect(pkg.generation).toBe(1);expect(pkg.totalSize).toBeLessThanOrEqual(32768)
  expect(mocks.upload.mock.calls[0][2]).toEqual({confirmSingleUser:true})
  expect(mocks.remember).toHaveBeenCalledWith('fixture',pkg,'catalog')
  expect(mocks.save).toHaveBeenCalledWith(1,'宝宝','2027-01-01')
  expect(mocks.upload.mock.invocationCallOrder[0]).toBeLessThan(mocks.save.mock.invocationCallOrder[0])
  expect(mocks.remember.mock.invocationCallOrder[0]).toBeLessThan(mocks.save.mock.invocationCallOrder[0])
  expect(mocks.refresh).toHaveBeenCalledWith('calendar');expect(close).toHaveBeenCalledOnce()
})

it('cancelled confirmation performs no font/config/preference writes or refresh',async()=>{
  await open();await fill();await save(true,'cancel')
  for(const m of [mocks.upload,mocks.save,mocks.remember,mocks.preference,mocks.refresh,close])expect(m).not.toHaveBeenCalled()
  expect(host.querySelector<HTMLButtonElement>('button.primary')!.disabled).toBe(false)
})

it('does not upload or ask for confirmation when installed fonts already contain the name',async()=>{
  const {addCatalogGlyphs}=await import('../device-gui/fontEditor')
  mocks.editable.mockResolvedValue({fonts:await addCatalogGlyphs(defaultGuiFonts(),'宝'),chineseSource:undefined})
  await open();await fill();await save()
  expect(window.confirm).not.toHaveBeenCalled();expect(mocks.upload).not.toHaveBeenCalled()
  expect(mocks.save).toHaveBeenCalledOnce()
})

it.each([4, 0xffffffff])('supplementation at serial %i preserves glyphs and custom digit faces',async generation=>{
  const {addCatalogGlyphs}=await import('../device-gui/fontEditor')
  const compiler=(globalThis as unknown as {InkTagFontRoles:{compressNativeDigits(g:unknown[]):number[]}}).InkTagFontRoles
  const digit=(height:number)=>Uint8Array.from(compiler.compressNativeDigits(Array.from({length:11},(_,n)=>({
    codePoint:48+n,width:8,height,x:0,y:0,dx:8,pixels:Array(8*height).fill(1)}))))
  const base=await addCatalogGlyphs(defaultGuiFonts(),'鹏')
  const pkg=buildFontRoles(base,{},generation,32768,[digit(18),digit(24)])
  mocks.read.mockResolvedValue(info(pkg));mocks.editable.mockResolvedValue({fonts:packageFonts(pkg),chineseSource:'catalog'})
  await open();await fill();await save()
  const next=mocks.upload.mock.calls[0][0] as GuiAssetPackage
  const faces=packageFonts(next),old=packageFonts(pkg)
  expect(next.generation).toBe((generation+1)>>>0);expect(faces).toHaveLength(6)
  expect(missingChineseFontGlyphs(faces,'鹏宝')).toEqual([])
  expect(faces.slice(2)).toEqual(old.slice(2))
})

it.each(['base','unsupported','unknown-source','unavailable','capacity','read-error','upload-error','changed-device'])(
  'does not save countdown after failed supplementation: %s',async(kind)=>{
    if(kind==='base')useDeviceStore.setState({activation:{phase:'inactive',edition:'base'}})
    if(kind==='unsupported')useDeviceStore.setState({settingsCapabilities:{...initial.settingsCapabilities,guiAssetUpdate:false}})
    if(kind==='unknown-source')mocks.editable.mockResolvedValue({fonts:defaultGuiFonts()})
    if(kind==='capacity')mocks.read.mockResolvedValue({...info(),maximumSize:88})
    if(kind==='read-error')mocks.read.mockRejectedValue(Error('字库读取失败'))
    if(kind==='upload-error')mocks.upload.mockRejectedValue(Error('提交结果未确认'))
    await open();await fill(kind==='unavailable'?'😀':'宝宝')
    if(kind==='changed-device')await act(async()=>useDeviceStore.setState({deviceId:'another'}))
    await save()
    expect(mocks.save).not.toHaveBeenCalled();expect(mocks.preference).not.toHaveBeenCalled();expect(mocks.refresh).not.toHaveBeenCalled()
    expect(close).not.toHaveBeenCalled();expect(host.querySelector('[role=alert]')?.textContent).toBeTruthy()
    if(kind!=='upload-error')expect(mocks.upload).not.toHaveBeenCalled()
  })

it('checks fresh INFO instead of extending the font seen when the dialog opened',async()=>{
  await open();await fill()
  mocks.read.mockRejectedValue(Error('当前字库状态不可用'))
  await save()
  expect(mocks.upload).not.toHaveBeenCalled();expect(mocks.save).not.toHaveBeenCalled()
  expect(host.textContent).toContain('当前字库状态不可用')
})

it('rejects invalid target dates before any font upload',async()=>{
  await open();await fill();await input('input[type=date]','1969-12-31')
  expect(host.querySelector<HTMLButtonElement>('button.primary')!.disabled).toBe(true)
  expect(host.textContent).toContain('请选择有效的目标日期')
  expect(mocks.upload).not.toHaveBeenCalled()
})

it('never refreshes a different device if the connection changes after saving',async()=>{
  mocks.preference.mockImplementation(async()=>{useDeviceStore.setState({deviceId:'another'})})
  await open();await fill('国庆');await save()
  expect(mocks.save).toHaveBeenCalledOnce();expect(mocks.refresh).not.toHaveBeenCalled()
  expect(host.textContent).toContain('设备连接已变化')
})

it('rejects duplicate save and closing while upload is pending',async()=>{
  let complete!:()=>void
  mocks.upload.mockReturnValue(new Promise<void>(resolve=>{complete=resolve}))
  await open();await fill();await save(false)
  await act(async()=>host.querySelector<HTMLButtonElement>('.calendar-settings-dialog button.primary')!.click())
  await vi.waitFor(()=>expect(mocks.upload).toHaveBeenCalledOnce())
  expect(mocks.upload).toHaveBeenCalledOnce()
  expect(host.querySelector<HTMLButtonElement>('button[aria-label=关闭]')!.disabled).toBe(true)
  await act(async()=>window.dispatchEvent(new KeyboardEvent('keydown',{key:'Escape'})))
  expect(close).not.toHaveBeenCalled()
  await act(async()=>complete())
  expect(mocks.save).toHaveBeenCalledOnce()
})

it('Escape cancels only the nested font modal and keeps calendar editing open',async()=>{
  await open();await fill();await save(false,'none')
  await act(async()=>window.dispatchEvent(new KeyboardEvent('keydown',{key:'Escape'})))
  expect(host.querySelector('.confirmation-dialog')).toBeNull()
  expect(close).not.toHaveBeenCalled();expect(mocks.upload).not.toHaveBeenCalled();expect(mocks.save).not.toHaveBeenCalled()
  expect(host.querySelector('.calendar-settings-dialog')).not.toBeNull()
})

it('rechecks the device after a user accepts the asynchronous modal',async()=>{
  await open();await fill();await save(false,'none')
  await act(async()=>useDeviceStore.setState({deviceId:'another'}))
  await act(async()=>host.querySelector<HTMLButtonElement>('.confirmation-dialog button.primary')!.click())
  expect(mocks.upload).not.toHaveBeenCalled();expect(mocks.save).not.toHaveBeenCalled()
  expect(host.textContent).toContain('设备连接已变化')
})
