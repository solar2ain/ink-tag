import { expect,test,type Page } from '@playwright/test'
import { readFile } from 'node:fs/promises'
test.use({locale:'zh-CN'})
test.beforeEach(async({page})=>{
  await page.addInitScript(()=>localStorage.setItem('inktag-getting-started-v1','seen'))
  await page.setViewportSize({width:1440,height:1000})
  await page.goto('/#/create/canvas/blank?width=400&height=300&pageMode=static')
  await expect(page.getByLabel('创作画板')).toBeVisible({timeout:15000})
  await page.locator('.control-library').getByRole('button',{name:'图片框',exact:true}).click()
})
async function picture(page:Page) {
  const data=await page.evaluate(()=>{
    const canvas=document.createElement('canvas');canvas.width=160;canvas.height=100
    const ctx=canvas.getContext('2d')!,gradient=ctx.createLinearGradient(0,0,160,100)
    gradient.addColorStop(0,'#f18542');gradient.addColorStop(.5,'#7777aa');gradient.addColorStop(1,'#205a9c')
    ctx.fillStyle=gradient;ctx.fillRect(0,0,160,100);return canvas.toDataURL().split(',')[1]
  })
  await page.locator('input[data-canvas-image]').setInputFiles({name:'gradient.png',mimeType:'image/png',buffer:Buffer.from(data,'base64')})
  await expect(page.getByRole('button',{name:'替换本地图片',exact:true})).toBeVisible()
}
async function records(page:Page) {
  await page.getByRole('button',{name:'保存作品',exact:true}).click()
  await page.getByRole('dialog',{name:'保存作品'}).getByRole('button',{name:'保存页面',exact:true}).click()
  await expect(page.getByRole('dialog',{name:'保存作品'})).toHaveCount(0)
  return page.evaluate(async()=>{
    const db=await new Promise<IDBDatabase>((resolve,reject)=>{const r=indexedDB.open('inktag-webui');r.onsuccess=()=>resolve(r.result);r.onerror=()=>reject(r.error)})
    try {return await new Promise<any[]>((resolve,reject)=>{const r=db.transaction('pages').objectStore('pages').getAll();r.onsuccess=()=>resolve(r.result);r.onerror=()=>reject(r.error)})}finally{db.close()}
  })
}
test('imports, dithers, fills all pixels, exports and reopens without losing the source',async({page})=>{
  const errors:string[]=[];page.on('pageerror',e=>errors.push(e.message))
  await picture(page)
  await page.getByRole('button',{name:'铺满画板',exact:true}).click()
  await page.getByText('画板色彩',{exact:true}).click()
  await page.getByLabel('目标屏幕色彩',{exact:true}).selectOption('1')
  await page.getByLabel('图片抖动算法',{exact:true}).selectOption('floydSteinberg')
  const downloadPromise=page.waitForEvent('download');await page.getByRole('button',{name:'下载 PNG',exact:true}).click()
  const file=await downloadPromise, png=(await readFile(await file.path())).toString('base64')
  const result=await page.evaluate(async encoded=>{
    const image=await createImageBitmap(new Blob([Uint8Array.from(atob(encoded),c=>c.charCodeAt(0))],{type:'image/png'}))
    const canvas=document.createElement('canvas');canvas.width=image.width;canvas.height=image.height
    const ctx=canvas.getContext('2d')!;ctx.drawImage(image,0,0);image.close()
    const pixels=ctx.getImageData(0,0,canvas.width,canvas.height).data,colors=new Set<string>()
    for(let i=0;i<pixels.length;i+=4)colors.add(`${pixels[i]},${pixels[i+1]},${pixels[i+2]},${pixels[i+3]}`)
    return {width:canvas.width,height:canvas.height,colors:[...colors],bottom:[...ctx.getImageData(0,299,400,1).data].some((v,i)=>i%4<3&&v<255)}
  },png)
  expect(result).toEqual({width:400,height:300,colors:expect.arrayContaining(['0,0,0,255','255,255,255,255']),bottom:true})
  expect(result.colors).toHaveLength(2)
  const [saved]=await records(page), doc=JSON.parse(saved.document)
  expect(doc.footerInset).toBe(0)
  expect(doc.widgets[0]).toMatchObject({x:0,y:0,width:400,height:300,imageFit:'cover',imageDither:'floydSteinberg'})
  expect(doc.widgets[0].image).toMatch(/^data:image\/png;base64,/)
  await page.goto(`/#/create/canvas/blank?width=400&height=300&page=${saved.id}`)
  await expect(page.locator('footer [role=status]')).toContainText('已打开本地页面')
  await page.locator('.layer-select').first().click()
  await expect(page.getByLabel('图片抖动算法')).toHaveValue('floydSteinberg')
  await expect(page.getByRole('spinbutton',{name:'高度',exact:true})).toHaveValue('300')
  expect(errors).toEqual([])
})
test('double clicking an image frame opens its chooser and invalid input reports beside the controls',async({page})=>{
  await page.getByRole('button',{name:'原始大小',exact:true}).click()
  const stage=(await page.locator('.stage').boundingBox())!,x=stage.x+stage.width/2,y=stage.y+stage.height/2
  const choosing=page.waitForEvent('filechooser');await page.mouse.dblclick(x,y)
  await (await choosing).setFiles({name:'bad.svg',mimeType:'image/svg+xml',buffer:Buffer.from('<svg/>')})
  await expect(page.locator('.image-import-status')).toContainText('暂不支持')
  await expect(page.getByRole('button',{name:'添加本地图片',exact:true})).toBeVisible()
  await picture(page)
  await expect(page.locator('.image-import-status')).toContainText('图片已添加')
})
for(const corner of ['nw','ne','sw','se'])test(`drags ${corner} corner and undoes exactly one geometry change`,async({page})=>{
  for(const[label,value]of[['宽度','140'],['高度','100'],['X','60'],['Y','60']]){
    const field=page.getByRole('spinbutton',{name:label,exact:true});await field.fill(value);await field.press('Tab')
  }
  await page.getByRole('button',{name:'原始大小',exact:true}).click()
  const stage=(await page.locator('.stage').boundingBox())!,origin={x:stage.x+(stage.width-400)/2,y:stage.y+(stage.height-300)/2}
  const west=corner.endsWith('w'),north=corner.startsWith('n'),x=origin.x+(west?60:200),y=origin.y+(north?60:160)
  await page.mouse.move(x,y);await page.mouse.down();await page.mouse.move(x+20,y+15,{steps:6});await page.mouse.up()
  await expect(page.getByRole('spinbutton',{name:'宽度',exact:true})).toHaveValue(west?'120':'160')
  await expect(page.getByRole('spinbutton',{name:'高度',exact:true})).toHaveValue(north?'85':'115')
  await page.getByRole('button',{name:'撤销',exact:true}).click()
  await expect(page.getByRole('spinbutton',{name:'X',exact:true})).toHaveValue('60')
  await expect(page.getByRole('spinbutton',{name:'宽度',exact:true})).toHaveValue('140')
})

for(const control of ['文字','表格','待办事项','列表','二维码','分割线'])test(`${control} edits, copies, locks, exports and restores`,async({page})=>{
  const errors:string[]=[];page.on('pageerror',error=>errors.push(error.message))
  await page.getByRole('button',{name:'删除控件',exact:true}).click()
  await page.locator('.control-library').getByRole('button',{name:control,exact:true}).click()
  if(control==='文字') {
    await page.locator('.properties [contenteditable]').fill('中文静态图片 Ω 龘')
    await expect(page.locator('.properties .rich-warning')).toHaveCount(0)
  }
  if(control==='二维码') await page.getByLabel('二维码内容',{exact:true}).fill('https://solar2ain.github.io/ink-tag/')
  await page.getByRole('button',{name:'复制控件',exact:true}).click()
  await expect(page.locator('.layer-select')).toHaveCount(2)
  await page.getByRole('button',{name:'撤销',exact:true}).click()
  await expect(page.locator('.layer-select')).toHaveCount(1)
  await page.getByRole('button',{name:'重做',exact:true}).click()
  await expect(page.locator('.layer-select')).toHaveCount(2)
  await page.locator('.layer-select').first().click()
  await page.getByRole('button',{name:'锁定所选',exact:true}).click()
  await expect(page.getByRole('button',{name:'删除控件',exact:true})).toBeDisabled()
  await page.getByRole('button',{name:'解锁所选',exact:true}).click()
  const download=page.waitForEvent('download');await page.getByRole('button',{name:'下载 PNG',exact:true}).click()
  expect((await readFile(await (await download).path())).length).toBeGreaterThan(300)
  const [record]=await records(page),source=JSON.parse(record.document)
  expect(source.widgets).toHaveLength(2)
  await page.goto(`/#/create/canvas/blank?width=400&height=300&page=${record.id}`)
  await expect(page.locator('footer [role=status]')).toContainText('已打开本地页面')
  await expect(page.locator('.layer-select')).toHaveCount(2)
  expect(errors).toEqual([])
})

test('MCU font coverage only warns for device-dynamic pages, not static raster output',async({page})=>{
  for(const mode of ['static','host-dynamic','dynamic']) {
    await page.goto(`/#/create/canvas/blank?width=400&height=300&pageMode=${mode}`)
    await expect(page.getByLabel('创作画板')).toBeVisible()
    await page.locator('.control-library').getByRole('button',{name:'文字',exact:true}).click()
    await page.locator('.properties [contenteditable]').fill('中文测试')
    await page.getByLabel('选中文字字体',{exact:true}).selectOption(mode==='host-dynamic'?{label:'楷体'}:'helv-bold-14-num')
    if(mode==='dynamic')await expect(page.locator('.properties .rich-warning')).toContainText('所选字库缺少')
    else {
      await expect(page.locator('.properties .rich-warning')).toHaveCount(0)
      const download=page.waitForEvent('download');await page.getByRole('button',{name:'下载 PNG',exact:true}).click()
      expect((await readFile(await (await download).path())).length).toBeGreaterThan(300)
    }
  }
})

test('static pixel fonts have an offline device-effect preview with pixel zoom and no font gate',async({page})=>{
  await page.getByRole('button',{name:'删除控件',exact:true}).click()
  await page.locator('.control-library').getByRole('button',{name:'文字',exact:true}).click()
  await page.locator('.properties [contenteditable]').fill('纸上中文 Ω 123')
  await page.getByLabel('选中文字字体',{exact:true}).selectOption('InkTag Pixel Fusion')
  await page.getByRole('button',{name:'文字颜色 · 黑',exact:true}).click()
  await page.getByLabel('选中文字字号',{exact:true}).selectOption('24')
  await page.getByRole('button',{name:'设备效果预览',exact:true}).click()
  const dialog=page.getByRole('dialog',{name:'设备效果预览',exact:true})
  await expect(dialog.getByRole('status')).toContainText('按画板色板模拟')
  const pixels=dialog.getByLabel('屏幕像素预览',{exact:true})
  const frame=await pixels.evaluate((element:HTMLCanvasElement)=>{
    const data=element.getContext('2d')!.getImageData(0,0,element.width,element.height).data,colors=new Set<string>()
    for(let i=0;i<data.length;i+=4)colors.add([...data.slice(i,i+3)].join(','))
    return {width:element.width,height:element.height,colors:[...colors]}
  })
  expect(frame).toMatchObject({width:400,height:300})
  expect(frame.colors).toContain('0,0,0')
  expect(frame.colors).toContain('230,226,217')
  expect(frame.colors.length).toBeLessThanOrEqual(3)
  await expect.poll(async()=>{const box=await pixels.boundingBox();return box!.width}).toBeGreaterThan(400)
  const scale=dialog.getByLabel('当前预览缩放',{exact:true})
  await dialog.getByRole('button',{name:'1:1',exact:true}).click()
  await expect(pixels).toHaveCSS('width','400px')
  await expect(scale).toHaveText('100%')
  await dialog.getByRole('button',{name:'放大效果预览',exact:true}).click()
  await expect(pixels).toHaveCSS('width','500px')
  await expect(scale).toHaveText('125%')
  await dialog.getByRole('button',{name:'放大效果预览',exact:true}).click()
  await expect(pixels).toHaveCSS('width','600px')
  await dialog.getByRole('button',{name:'1:1',exact:true}).click()
  await dialog.getByRole('button',{name:'缩小效果预览',exact:true}).click()
  await expect(pixels).toHaveCSS('width','360px')
  await expect(scale).toHaveText('90%')
  await page.setViewportSize({width:390,height:780})
  await expect.poll(()=>page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth)).toBe(true)
  await dialog.getByRole('button',{name:'适应窗口',exact:true}).click()
  await expect.poll(async()=>{const box=await pixels.boundingBox();return box!.width}).toBeLessThan(350)
  await dialog.getByRole('button',{name:'1:1',exact:true}).click()
  await expect(pixels).toHaveCSS('width','400px')
  await dialog.getByRole('button',{name:'适应窗口',exact:true}).click()
  await page.screenshot({path:'../.local/canvas-device-effect-mobile.png'})
  await page.keyboard.press('Escape')
  await expect(dialog).toHaveCount(0)
  await expect(page.getByRole('button',{name:'设备效果预览',exact:true})).toBeFocused()
  const [saved]=await records(page),widget=JSON.parse(saved.document).widgets[0]
  expect(widget.style.fontId).toBeUndefined()
  expect(widget.cells[0][0].richText.runs[0].style.fontFamily).toBe('InkTag Pixel Fusion')
})
