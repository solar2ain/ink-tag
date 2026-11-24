import { test, expect, type Page } from '@playwright/test'
import { readFile } from 'node:fs/promises'
import { parseDocument } from '../../packages/inktag-editor-core/src/index'

test.use({ locale: 'zh-CN' })
test.beforeEach(async ({ page }) => { await page.addInitScript(() => localStorage.setItem('inktag-getting-started-v1','seen')) })
async function openProperties(page: Page, mobile = false) {
  if (!mobile) return
  if (await page.getByRole('button',{name:'编辑所选图层',exact:true}).isVisible()) await page.getByRole('button',{name:'编辑所选图层',exact:true}).click()
  else if (await page.getByRole('button',{name:'打开编辑面板',exact:true}).isVisible()) await page.getByRole('button',{name:'打开编辑面板',exact:true}).click()
  await page.getByRole('button',{name:'属性',exact:true}).click()
}
async function addBox(page: Page, x: number, y: number, mobile = false) {
  if (mobile) {
    if (await page.getByRole('button',{name:'打开编辑面板',exact:true}).isVisible()) await page.getByRole('button',{name:'打开编辑面板',exact:true}).click()
    await page.getByRole('button',{name:'控件与图层',exact:true}).click()
  }
  await page.locator('.control-library').getByRole('button',{name:'图片框',exact:true}).click()
  await openProperties(page,mobile)
  for (const [label,value] of [['宽度',40],['高度',20],['X',x],['Y',y]] as const) {
    const field=page.getByRole('spinbutton',{name:label,exact:true});await field.fill(String(value));await field.press('Tab')
  }
}
async function saveScene(page: Page) {
  await page.getByRole('button',{name:'保存作品',exact:true}).click()
  const dialog=page.getByRole('dialog',{name:'保存作品'})
  await dialog.getByRole('button',{name:'保存页面',exact:true}).click()
  await expect(dialog).toHaveCount(0)
  return parseDocument(await page.evaluate(async()=>{
    const database=await new Promise<IDBDatabase>((resolve,reject)=>{const request=indexedDB.open('inktag-webui');request.onsuccess=()=>resolve(request.result);request.onerror=()=>reject(request.error)})
    try {return await new Promise<string>((resolve,reject)=>{const request=database.transaction('pages').objectStore('pages').getAll();request.onsuccess=()=>request.result.length ? resolve(request.result[0].document) : reject(new Error('page was not saved'));request.onerror=()=>reject(request.error)})} finally {database.close()}
  }))
}
async function exportPixels(page: Page) {
  const downloading=page.waitForEvent('download');await page.getByRole('button',{name:'下载 PNG',exact:true}).click()
  const file=await downloading, encoded=(await readFile(await file.path())).toString('base64')
  return page.evaluate(async data=>{
    const bytes=Uint8Array.from(atob(data),char=>char.charCodeAt(0)),bitmap=await createImageBitmap(new Blob([bytes],{type:'image/png'}))
    const canvas=document.createElement('canvas');canvas.width=bitmap.width;canvas.height=bitmap.height
    const context=canvas.getContext('2d')!;context.drawImage(bitmap,0,0);bitmap.close()
    const pixels=context.getImageData(0,0,canvas.width,canvas.height).data
    let text='';for(let i=0;i<pixels.length;i+=4096)text+=String.fromCharCode(...pixels.subarray(i,i+4096))
    return {width:canvas.width,height:canvas.height,pixels:btoa(text)}
  },encoded)
}

test('desktop aligns, groups, copies and locks without moving or restyling a group internally',async({page})=>{
  await page.setViewportSize({width:1280,height:900})
  await page.goto('/#/create/canvas/blank?width=400&height=300')
  await expect(page.getByLabel('创作画板')).toBeVisible({timeout:15000})
  await addBox(page,20,40);await addBox(page,120,80)
  await page.getByRole('button',{name:'选择',exact:true}).click()
  await page.keyboard.press('ControlOrMeta+a')
  await page.keyboard.press('ControlOrMeta+g')
  await expect(page.getByRole('button',{name:'取消组合',exact:true})).toBeEnabled()
  await page.keyboard.press('ControlOrMeta+Shift+g')
  await expect(page.getByRole('button',{name:'取消组合',exact:true})).toBeDisabled()
  await page.getByRole('button',{name:'顶对齐',exact:true}).click()
  let doc=await saveScene(page);expect(doc.widgets.map(w=>w.y)).toEqual([40,40])
  await page.getByRole('button',{name:'组合',exact:true}).click()
  if (!await page.getByRole('button',{name:'右对齐',exact:true}).isVisible()) await page.getByText('对齐与间距',{exact:true}).click()
  await page.getByRole('button',{name:'右对齐',exact:true}).click()
  doc=await saveScene(page);expect(doc.widgets.map(w=>w.x)).toEqual([260,360]);expect(doc.widgets[0].groupId).toBe(doc.widgets[1].groupId)
  await page.getByRole('button',{name:'锁定所选',exact:true}).click()
  await expect(page.getByRole('button',{name:'左对齐',exact:true})).toBeDisabled()
  doc=await saveScene(page);expect(doc.widgets.every(w=>w.locked)).toBe(true)
  await page.getByRole('button',{name:'解锁所选',exact:true}).click()
  await page.getByRole('button',{name:'复制所选',exact:true}).click()
  doc=await saveScene(page);expect(doc.widgets).toHaveLength(4);expect(doc.widgets[2].groupId).not.toBe(doc.widgets[0].groupId)
  await page.getByRole('button',{name:'撤销',exact:true}).click()
  await expect(page.locator('.layer-select')).toHaveCount(2)
  await page.getByRole('button',{name:'重做',exact:true}).click()
  await expect(page.locator('.layer-select')).toHaveCount(4)
  await page.locator('.layer-select').first().click()
  await page.screenshot({path:'../.local/test-results/editor-align-desktop-20261018.png'})
})

test('smart drag, marquee and draggable guides work at actual canvas coordinates, without entering PNG output',async({page})=>{
  await page.setViewportSize({width:1440,height:1000})
  await page.goto('/#/create/canvas/blank?width=400&height=300')
  await expect(page.getByLabel('创作画板')).toBeVisible({timeout:15000})
  await addBox(page,20,40);await addBox(page,120,80)
  await page.locator('.layer-select').last().click()
  await page.getByText('吸附与参考线',{exact:true}).click()
  await page.getByRole('button',{name:'垂直参考线',exact:true}).click()
  await page.getByRole('spinbutton',{name:'参考线 1 位置',exact:true}).fill('87')
  await page.getByRole('spinbutton',{name:'参考线 1 位置',exact:true}).press('Tab')
  await page.getByRole('button',{name:'原始大小',exact:true}).click()
  const stage=(await page.locator('.stage').boundingBox())!,origin={x:stage.x+(stage.width-400)/2,y:stage.y+(stage.height-300)/2}
  const drag=async(x1:number,y1:number,x2:number,y2:number)=>{await page.mouse.move(origin.x+x1,origin.y+y1);await page.mouse.down();await page.mouse.move(origin.x+x2,origin.y+y2,{steps:8})}
  // Drag the interior, not the newly supported top-left resize handle.
  await drag(40,50,86,50)
  await page.screenshot({path:'../.local/test-results/editor-smart-guide-20261018.png'})
  await page.mouse.up()
  let doc=await saveScene(page);expect(doc.widgets[0].x).toBe(67)
  const handle=page.getByRole('button',{name:'垂直参考线 87 像素',exact:true}),box=(await handle.boundingBox())!
  await page.mouse.move(box.x+box.width/2,box.y+box.height/2);await page.mouse.down();await page.mouse.move(box.x+box.width/2+10,box.y+box.height/2);await page.mouse.up()
  doc=await saveScene(page);expect(doc.guides?.[0].position).toBe(97)
  await drag(5,5,200,130);await page.mouse.up()
  await expect(page.getByRole('heading',{name:'已选 2 个图层',exact:true})).toBeVisible()
  const before=await exportPixels(page)
  await page.getByRole('button',{name:'组合',exact:true}).click()
  await page.getByRole('button',{name:'锁定所选',exact:true}).click()
  const after=await exportPixels(page);expect(after).toEqual(before)
  const showGuides=page.getByRole('checkbox',{name:'显示参考线',exact:true})
  if (!await showGuides.isVisible()) await page.getByText('吸附与参考线',{exact:true}).click()
  await showGuides.uncheck()
  expect(await exportPixels(page)).toEqual(before)
  // Template previews may include the device-brand placeholder, never editor aids.
  await page.getByRole('button',{name:'保存作品',exact:true}).click()
  const saveDialog=page.getByRole('dialog',{name:'保存作品'})
  await saveDialog.getByRole('radio',{name:'模板',exact:true}).check()
  await saveDialog.getByRole('button',{name:'保存模板',exact:true}).click()
  await expect(saveDialog).toHaveCount(0)
  const cleanPreview=await page.evaluate(async()=>{
    const database=await new Promise<IDBDatabase>((resolve,reject)=>{const request=indexedDB.open('inktag-webui');request.onsuccess=()=>resolve(request.result);request.onerror=()=>reject(request.error)})
    const preview=await new Promise<string>((resolve,reject)=>{const request=database.transaction('templates').objectStore('templates').getAll();request.onsuccess=()=>resolve(request.result[0].preview);request.onerror=()=>reject(request.error)});database.close()
    const bitmap=await createImageBitmap(await(await fetch(preview)).blob()),canvas=document.createElement('canvas');canvas.width=bitmap.width;canvas.height=bitmap.height
    const context=canvas.getContext('2d')!;context.drawImage(bitmap,0,0);bitmap.close()
    return [Array.from(context.getImageData(97,200,1,1).data),Array.from(context.getImageData(110,40,1,1).data)]
  })
  expect(cleanPreview).toEqual([[255,255,255,255],[255,255,255,255]])
})

for(const width of [320,390]) test(`phone selection, group alignment, layer lock and restore stay usable at ${width}px`,async({page})=>{
  await page.setViewportSize({width,height:820})
  await page.goto('/#/create/canvas/blank?width=400&height=300')
  await expect(page.getByLabel('创作画板')).toBeVisible({timeout:15000})
  await addBox(page,20,40,true);await addBox(page,120,80,true)
  await page.getByRole('button',{name:'控件与图层',exact:true}).click()
  await page.getByRole('button',{name:'多选图层',exact:true}).click()
  // The newly added box is selected already; tap the other layer to add it.
  await page.locator('.layer-select').last().click()
  await expect(page.locator('.multi-select-hint')).toContainText('已选 2 个图层')
  await page.getByRole('button',{name:'属性',exact:true}).click()
  await page.getByRole('button',{name:'顶对齐',exact:true}).click()
  await page.getByRole('button',{name:'组合',exact:true}).click()
  await page.getByRole('button',{name:'锁定所选',exact:true}).click()
  await expect(page.getByRole('button',{name:'左对齐',exact:true})).toBeDisabled()
  expect(await page.locator('.properties').evaluate(element=>element.scrollWidth<=element.clientWidth)).toBe(true)
  expect(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth)).toBe(true)
  const doc=await saveScene(page);expect(doc.widgets.every(w=>w.y===40&&w.locked)).toBe(true)
  await page.screenshot({path:`../.local/test-results/editor-align-phone-${width}-20261018.png`})
  await page.getByRole('button',{name:'解锁所选',exact:true}).click()
  await page.getByRole('button',{name:'取消组合',exact:true}).click()
  await page.getByRole('button',{name:'关闭编辑面板',exact:true}).click()
  await expect(page.getByRole('button',{name:'多选图层',exact:true})).toBeVisible()
  await expect(page.getByRole('button',{name:'编辑所选图层',exact:true})).toBeVisible()
  await page.getByRole('button',{name:'编辑所选图层',exact:true}).click()
  await expect(page.getByRole('button',{name:'左对齐',exact:true})).toBeEnabled()
  await page.goto('/#/library?tab=pages')
  await page.getByRole('link',{name:'继续编辑',exact:true}).click()
  await expect(page.getByLabel('创作画板')).toBeVisible({timeout:15000})
  await page.getByRole('button',{name:'打开编辑面板',exact:true}).click()
  await page.getByRole('button',{name:'控件与图层',exact:true}).click()
  await expect(page.locator('.layer-lock input:checked')).toHaveCount(2)
  await page.locator('.layer-select').first().click()
  await page.getByRole('button',{name:'属性',exact:true}).click()
  await expect(page.getByRole('button',{name:'解锁所选',exact:true})).toBeEnabled()
  await expect(page.getByRole('button',{name:'左对齐',exact:true})).toBeDisabled()
})

for(const width of [1280,320]) test(`template search type size and local WebUI help work without an external dead link at ${width}px`,async({page})=>{
  await page.setViewportSize({width,height:820})
  await page.goto('/#/create')
  const search=page.getByPlaceholder('搜索模板'),filter=page.getByLabel('模板分辨率',{exact:true})
  expect(await search.evaluate(element=>getComputedStyle(element).fontSize)).toBe(await filter.evaluate(element=>getComputedStyle(element).fontSize))
  await search.fill('课表');await expect(page.locator('.template-card').first()).toContainText('课表')
  await page.goto('/#/find')
  const help=page.getByRole('link',{name:'查看如何本地部署 WebUI 服务',exact:true})
  await expect(help).toHaveAttribute('href','#/help/local-webui');await help.click()
  await expect(page.getByRole('heading',{name:'本地部署 WebUI 服务',exact:true})).toBeVisible()
  await expect(page.locator('pre')).toContainText(['python -m pip install','inktag webui start --port 8787','inktag webui status','inktag webui log --follow'])
  const downloading=page.waitForEvent('download');await page.getByRole('link',{name:'下载 README',exact:true}).click()
  const file=await downloading,readme=await readFile(await file.path(),'utf8')
  expect(file.suggestedFilename()).toBe('InkTag-WebUI-README.md');expect(readme).toContain('http://127.0.0.1:8787')
  expect(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth)).toBe(true)
  await page.screenshot({path:`../.local/test-results/local-webui-help-${width}-20261018.png`})
})
