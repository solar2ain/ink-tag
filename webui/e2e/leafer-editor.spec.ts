import { test, expect, type Page } from '@playwright/test'
import { readFile } from 'node:fs/promises'
import { fileURLToPath } from 'node:url'
import { parseDocument } from '../../packages/inktag-editor-core/src/index'

test.use({ locale: 'zh-CN' })

test.beforeEach(async ({ page }) => {
  // Onboarding has its own suite; these cases exercise an established editor session.
  await page.addInitScript(() => localStorage.setItem('inktag-getting-started-v1', 'seen'))
})

async function savePage(page: Page) {
  await page.getByRole('button', { name: '保存作品', exact: true }).click()
  const dialog = page.getByRole('dialog', { name: '保存作品' })
  await dialog.getByRole('button', { name: '保存页面', exact: true }).click()
  await expect(dialog).toHaveCount(0)
}

async function libraryRecords(page: Page, table: 'pages' | 'templates') {
  return page.evaluate(async table => {
    const database = await new Promise<IDBDatabase>((resolve, reject) => {
      const request = indexedDB.open('inktag-webui'); request.onsuccess = () => resolve(request.result); request.onerror = () => reject(request.error)
    })
    try {
      return await new Promise<Array<{ id: string; name: string; kind: string; document: string }>>((resolve, reject) => {
        const request = database.transaction(table).objectStore(table).getAll(); request.onsuccess = () => resolve(request.result); request.onerror = () => reject(request.error)
      })
    } finally { database.close() }
  }, table)
}

for (const width of [1280, 320]) test(`static page shares named slot sending with the picture page at ${width}px`, async ({ page }) => {
  await page.setViewportSize({ width, height: 800 })
  await page.addInitScript({ path: fileURLToPath(new URL('./virtual-bluetooth.js', import.meta.url)) })
  await page.goto('/?image-fixture=1&fixed-slots=15#/create/canvas/blank?width=400&height=300&pageMode=static')
  await expect(page.getByLabel('创作画板')).toBeVisible({ timeout: 15000 })
  await page.getByRole('button', { name: /^(打开浏览器记住的设备|连接其他设备)$/ }).click()
  await page.getByRole('button', { name: /选择附近设备/ }).click()
  await expect(page.locator('.device-summary strong')).toHaveText('E2E Fixture')
  const trigger = page.getByRole('button', { name: '发送到设备', exact: true })
  await trigger.click()
  const dialog = page.getByRole('dialog', { name: '发送页面', exact: true })
  await expect(dialog.getByRole('option')).toHaveCount(15)
  await expect(dialog.getByLabel('图片名称', { exact: true })).toHaveValue('空白页面')
  await dialog.getByRole('option', { name: '槽 8，已使用，课程表', exact: true }).click()
  await expect(dialog).toContainText('将替换槽 8')
  await expect(dialog).toContainText('写入中断可能丢失旧图片')
  await dialog.getByRole('option', { name: '槽 3，空槽', exact: true }).click()
  await dialog.getByLabel('图片名称', { exact: true }).fill('课程表')
  await expect(dialog.getByRole('button', { name: '开始发送', exact: true })).toBeDisabled()
  await dialog.getByLabel('图片名称', { exact: true }).fill('桌面课表')
  await expect(dialog.getByRole('button', { name: '开始发送', exact: true })).toBeEnabled()
  await dialog.getByRole('checkbox', { name: /保存后刷新屏幕/ }).uncheck()
  await expect(dialog).toContainText('不会修改 NFC 配置')
  const bounds = (await dialog.boundingBox())!
  expect(bounds.x).toBeGreaterThanOrEqual(0)
  expect(bounds.x + bounds.width).toBeLessThanOrEqual(width)
  expect(bounds.y).toBeGreaterThanOrEqual(0)
  expect(bounds.y + bounds.height).toBeLessThanOrEqual(800)
  expect(await dialog.evaluate(element => element.scrollWidth <= element.clientWidth)).toBe(true)
  const slots = dialog.getByRole('listbox', { name: '选择保存图槽' })
  expect(await slots.evaluate(element => getComputedStyle(element).display)).toBe('grid')
  await page.screenshot({ path: `../.local/test-results/canvas-send-slots-${width}-20261018.png` })
  await dialog.getByRole('radio', { name: /仅显示一次/ }).check()
  await expect(dialog.getByLabel('图片名称', { exact: true })).toHaveCount(0)
  await dialog.press('Escape')
  await expect(dialog).toHaveCount(0)
  await expect(trigger).toBeFocused()
  // Opening/cancelling the chooser is not an upload or an NFC settings write.
  expect(await page.evaluate(() => (window as any).__inktagImageCommands)).not.toContain(0x10)
  await savePage(page)
  await page.goto('/?image-fixture=1&fixed-slots=15#/library?tab=pages')
  await expect(page.locator('.device-summary strong')).toHaveText('E2E Fixture')
  const card = page.locator('.page-resource')
  await card.getByRole('button', { name: '发送并保存到设备槽', exact: true }).click()
  await expect(dialog.getByRole('listbox', { name: '选择保存图槽' })).toBeVisible()
  await expect(dialog.getByLabel('图片名称', { exact: true })).toHaveValue('空白页面')
  await dialog.getByRole('button', { name: '取消', exact: true }).click()
  await card.getByRole('button', { name: '发送到屏幕', exact: true }).click()
  await expect(dialog.getByRole('radio', { name: /仅显示一次/ })).toBeChecked()
})

test('creation has blank, template and My pages entrances without the discarded explanation', async ({ page }) => {
  await page.goto('/#/create')
  await expect(page.getByText('从模板或空白画板开始创作，保存后进入「资源 · 我的页面」继续编辑。图片素材可在画板中插入；模板本身不会被修改。')).toHaveCount(0)
  await expect(page.getByRole('button', { name: '新建页面', exact: true })).toBeVisible()
  await expect(page.getByRole('region', { name: '模板筛选' }).getByRole('button',{name:'设备',exact:true})).toHaveCount(0)
  await page.getByPlaceholder('搜索模板').fill('设备信息')
  await expect(page.locator('.template-card')).toHaveCount(0)
  await page.getByPlaceholder('搜索模板').fill('')
  await expect(page.getByRole('button', { name: '选择模板开始', exact: true })).toHaveCount(0)
  for (const width of [320, 390, 799, 800, 1280]) {
    await page.setViewportSize({ width, height: 800 })
    const title = (await page.getByRole('heading', { name: '创作', exact: true }).boundingBox())!
    const create = (await page.getByRole('button', { name: '新建页面', exact: true }).boundingBox())!
    const pages = (await page.getByRole('link', { name: '我的页面', exact: true }).boundingBox())!
    expect(create.x).toBeGreaterThan(title.x + title.width)
    expect(pages.x).toBeGreaterThan(create.x + create.width)
    expect(pages.x + pages.width).toBeLessThanOrEqual(width)
    if (width <= 799) expect(Math.abs(title.y + title.height / 2 - pages.y - pages.height / 2)).toBeLessThan(2)
    const source = (await page.getByLabel('模板来源').boundingBox())!
    const resolution = (await page.getByLabel('模板分辨率').boundingBox())!
    const search = (await page.getByPlaceholder('搜索模板').boundingBox())!
    expect(source.width).toBeLessThan(96)
    expect(source.y).toBeGreaterThan(resolution.y + resolution.height)
    expect(search.x).toBeGreaterThan(source.x + source.width)
    expect(Math.abs(source.y + source.height / 2 - search.y - search.height / 2)).toBeLessThan(2)
    expect(search.x + search.width).toBeLessThanOrEqual(width)
  }
  await page.getByLabel('模板来源').selectOption('mine')
  await expect(page.locator('.template-card')).toHaveCount(0)
  await page.getByLabel('模板来源').selectOption('all')
  await expect(page.locator('.template-card').first()).toBeVisible()
  await page.getByRole('link', { name: '我的页面', exact: true }).click()
  await expect(page).toHaveURL(/library\?tab=pages/)
  await expect(page.getByRole('region', { name: '页面筛选' })).toBeVisible()
  const importButton = page.getByRole('button', { name: '导入素材', exact: true })
  await expect(importButton).toHaveCount(0)
  await page.getByRole('button', { name: /图片素材/ }).click()
  const chooser = page.waitForEvent('filechooser')
  await importButton.click()
  await (await chooser).setFiles(fileURLToPath(new URL('../../assets/image-slots/sources/bamboo-integrity.png', import.meta.url)))
  await expect(page.getByText('bamboo-integrity.png')).toBeVisible()
  await expect(importButton).toHaveCount(1)
  await expect(page.locator('.page-header').getByRole('button', { name: '导入素材' })).toHaveCount(0)
})

test('device controls are collapsed and insert independent editable, undoable samples',async({page})=>{
  await page.goto('/#/create/canvas/blank?width=400&height=300&pageMode=dynamic')
  await expect(page.getByLabel('创作画板')).toBeVisible({timeout:15000})
  const picker=page.getByRole('button',{name:'设备控件',exact:true})
  await expect(picker).toHaveAttribute('aria-expanded','false')
  await expect(page.getByRole('group',{name:'设备状态控件'})).toHaveCount(0)
  await expect(page.locator('.control-library button')).toHaveText(['日历','时钟','设备控件','图槽轮播','文字','图片框','表格','待办事项','列表','二维码','分割线'])
  const clockBox=await page.locator('.control-library').getByRole('button',{name:'时钟',exact:true}).boundingBox()
  const deviceBox=await picker.boundingBox()
  expect(Math.abs(deviceBox!.width-clockBox!.width)).toBeLessThan(1)
  expect(deviceBox?.height).toBe(clockBox?.height)
  await picker.click()
  await page.getByRole('group',{name:'设备状态控件'}).getByRole('button',{name:'蓝牙名称',exact:true}).click()
  await expect(picker).toHaveAttribute('aria-expanded','false')
  await expect(page.locator('.layer-list button')).toHaveCount(1)
  await savePage(page)
  let [record]=await libraryRecords(page,'pages')
  expect(JSON.parse(record.document).widgets[0].cells[0][0].text).toBe('[{{bluetoothName}}]')
  await page.getByRole('button',{name:'撤销',exact:true}).click()
  await expect(page.locator('.layer-list button')).toHaveCount(0)
  await page.getByRole('button',{name:'重做',exact:true}).click()
  await expect(page.locator('.layer-list button')).toHaveCount(1)
  await picker.click()
  await page.getByRole('group',{name:'设备状态控件'}).getByRole('button',{name:'电池图标',exact:true}).click()
  await page.getByLabel('电池图标预览电压').fill('2300')
  await savePage(page)
  ;[record]=await libraryRecords(page,'pages')
  const widgets=JSON.parse(record.document).widgets
  expect(widgets).toHaveLength(2)
  expect(widgets[0].sample.bluetoothName).toBe('IKT_GR_3A57')
  expect(widgets[1].native.sample.voltage).toBe(2300)
})

test('device controls fit the mobile drawer and close it after choosing a control',async({page})=>{
  await page.setViewportSize({width:320,height:740})
  await page.goto('/#/create/canvas/blank?width=200&height=200&pageMode=dynamic')
  await expect(page.getByLabel('创作画板')).toBeVisible({timeout:15000})
  await page.getByRole('button',{name:'打开编辑面板',exact:true}).click()
  await page.getByRole('button',{name:'设备控件',exact:true}).click()
  const controls=page.getByRole('group',{name:'设备状态控件'})
  await expect(controls.getByRole('button')).toHaveCount(6)
  expect(await controls.evaluate(element=>element.scrollWidth<=element.clientWidth)).toBe(true)
  await controls.getByRole('button',{name:'电池电压',exact:true}).click()
  await expect(page.getByRole('button',{name:'打开编辑面板',exact:true})).toBeVisible()
  await expect(page.locator('footer [role=status]')).toContainText('已添加电池电压')
  expect(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth)).toBe(true)
})

test('static pages exclude runtime controls and block manually pasted device variables',async({page})=>{
  await page.goto('/#/create/canvas/blank?width=400&height=300&pageMode=static')
  await expect(page.getByLabel('创作画板')).toBeVisible({timeout:15000})
  await expect(page.locator('.native-template-title span')).toContainText('静态页面')
  for(const name of ['日历','时钟','设备控件','图槽轮播']) await expect(page.locator('.control-library').getByRole('button',{name,exact:true})).toHaveCount(0)
  await page.locator('.control-library').getByRole('button',{name:'文字',exact:true}).click()
  await expect(page.getByLabel('文字预置')).toHaveCount(0)
  await expect(page.getByRole('button',{name:'插入变量',exact:true})).toHaveCount(0)
  await page.getByRole('textbox',{name:'富文本内容',exact:true}).fill('{{temperature}}')
  await expect(page.getByRole('note')).toContainText('静态页面不能包含动态控件或变量')
  await expect(page.getByRole('button',{name:'发送到设备',exact:true})).toBeDisabled()
  await page.getByRole('textbox',{name:'富文本内容',exact:true}).fill('普通文字')
  await expect(page.getByRole('note')).toHaveCount(0)
  await savePage(page)
  expect(JSON.parse((await libraryRecords(page,'pages'))[0].document).pageMode).toBe('static')
})

for(const mode of ['dynamic','host-dynamic']) test(`phone creation selects ${mode} type, persists it locally and restores its controls`,async({page})=>{
  await page.setViewportSize({width:320,height:740})
  await page.goto('/#/create')
  await page.getByRole('button',{name:'新建页面',exact:true}).click()
  const dialog=page.getByRole('dialog',{name:'新建页面'})
  await dialog.getByLabel('页面类型').selectOption(mode)
  await expect(dialog.getByLabel('页面类型').locator('option')).toHaveText(['静态页面','设备动态页面','主机动态页面'])
  await expect(dialog.getByRole('note')).toContainText(mode==='dynamic'?'由设备自行渲染与刷新':'设备须在主机蓝牙范围内')
  expect(await dialog.evaluate(element=>element.scrollWidth<=element.clientWidth)).toBe(true)
  await dialog.getByRole('button',{name:'创建页面',exact:true}).click()
  await expect(page).toHaveURL(new RegExp(`pageMode=${mode}`))
  await expect(page.getByLabel('创作画板')).toBeVisible({timeout:15000})
  await expect(page.getByRole('note')).toContainText(mode==='dynamic'?'日历、时钟及设备控件仅用于预览':'导出主机任务')
  await savePage(page)
  const [record]=await libraryRecords(page,'pages')
  expect(JSON.parse(record.document).pageMode).toBe(mode)
  // URL parameters cannot downgrade a saved dynamic source to a static page.
  await page.goto(`/#/create/canvas/blank?width=400&height=300&pageMode=static&page=${record.id}`)
  await expect(page.locator('footer [role=status]')).toContainText('已打开本地页面')
  await expect(page.locator('.native-template-title span')).toContainText(mode==='dynamic'?'设备动态页面':'主机动态页面')
  await page.getByRole('button',{name:'打开编辑面板',exact:true}).click()
  await expect(page.getByRole('button',{name:mode==='dynamic'?'设备控件':'天气数据控件',exact:true})).toBeVisible()
  await expect(page.getByRole('button',{name:'发送到设备',exact:true})).toBeDisabled()
})

for(const kind of ['calendar','clock']) test(`${kind} cannot send or store a custom dynamic page, even with a connected device`,async({page})=>{
  await page.addInitScript({path:fileURLToPath(new URL('./virtual-bluetooth.js',import.meta.url))})
  // A misleading static query never changes a native dynamic template's type.
  await page.goto(`/?image-fixture=empty#/create/canvas/${kind}-native?width=400&height=300&pageMode=static`)
  await expect(page.getByLabel('创作画板')).toBeVisible({timeout:15000})
  await page.getByRole('button',{name:/^(打开浏览器记住的设备|连接其他设备)$/}).click()
  await page.getByRole('button',{name:/选择附近设备/}).click()
  await expect(page.locator('.device-summary strong')).toHaveText('E2E Fixture')
  await expect(page.locator('.native-template-title span')).toContainText('设备动态页面')
  await expect(page.getByRole('note')).toContainText('定制动态页面暂不支持保存到设备')
  await expect(page.getByRole('button',{name:'发送到设备',exact:true})).toBeDisabled()
  await expect(page.getByLabel('保存到槽位',{exact:true})).toHaveCount(0)
  await expect(page.getByLabel('发送时保存到设备槽',{exact:true})).toHaveCount(0)
  const before=await page.evaluate(()=>(window as any).__inktagImageCommands.slice())
  await savePage(page)
  const [record]=await libraryRecords(page,'pages')
  expect(JSON.parse(record.document).pageMode).toBe('dynamic')
  expect(await page.evaluate(()=>(window as any).__inktagImageCommands)).toEqual(before)
  await page.goto('/?image-fixture=empty#/library?tab=pages')
  // The existing browser grant reconnects the same virtual device on navigation.
  await expect(page.locator('.device-summary strong')).toHaveText('E2E Fixture')
  const card=page.locator('.page-resource')
  await expect(card).toContainText('定制动态页面暂不能保存到设备')
  await expect(card.getByRole('button',{name:'发送到屏幕',exact:true})).toBeDisabled()
  await expect(card.getByRole('button',{name:'发送并保存到设备槽',exact:true})).toBeDisabled()
})

for(const [mode,width] of [['dynamic',1280],['dynamic',320]] as const) test(`slot carousel in ${mode} edits ordered references, undoes and persists at ${width}px`,async({page})=>{
  await page.setViewportSize({width,height:740})
  await page.goto(`/#/create/canvas/blank?width=400&height=300&pageMode=${mode}`)
  await expect(page.getByLabel('创作画板')).toBeVisible({timeout:15000})
  if(width===320)await page.getByRole('button',{name:'打开编辑面板',exact:true}).click()
  await page.locator('.control-library').getByRole('button',{name:'图槽轮播',exact:true}).click()
  if(width===320){
    await page.getByRole('button',{name:'打开编辑面板',exact:true}).click()
    await page.getByRole('navigation',{name:'编辑面板',exact:true}).getByRole('button',{name:'属性',exact:true}).click()
  }
  const numbers=page.getByLabel('轮播图槽编号',{exact:true})
  await numbers.fill('0')
  await expect(numbers).toHaveAttribute('aria-invalid','true')
  await numbers.fill('1, 3, 2')
  await expect(numbers).toHaveAttribute('aria-invalid','false')
  await page.getByLabel('轮播间隔（秒）',{exact:true}).fill('120')
  await numbers.press('Tab')
  await page.getByRole('button',{name:'撤销',exact:true}).click()
  await expect(page.getByLabel('轮播间隔（秒）',{exact:true})).toHaveValue('300')
  await page.getByRole('button',{name:'重做',exact:true}).click()
  await expect(page.getByLabel('轮播间隔（秒）',{exact:true})).toHaveValue('120')
  await expect(page.getByRole('button',{name:'添加本地图片',exact:true})).toHaveCount(0)
  expect(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth)).toBe(true)
  await savePage(page)
  const [record]=await libraryRecords(page,'pages'),doc=parseDocument(record.document)
  expect(doc.pageMode).toBe(mode)
  expect(doc.widgets[0].imageSlots).toEqual({slotIds:[0,2,1],intervalSeconds:120})
  expect(doc.widgets[0].image).toBeUndefined()
  await page.goto('/#/library?tab=pages')
  const card=page.locator('.page-resource')
  await expect(card).toContainText(mode==='dynamic'?'定制动态页面暂不能保存到设备':'主机动态页面暂不支持自动推送')
  await expect(card.getByRole('button',{name:'发送到屏幕',exact:true})).toBeDisabled()
  await card.getByRole('link',{name:'继续编辑',exact:true}).click()
  await expect(page.locator('footer [role=status]')).toContainText('已打开本地页面')
})

for(const source of ['page','template'] as const) test(`removed device-info starter does not lose a saved ${source}`,async({page})=>{
  await page.goto('/#/create/canvas/blank?width=400&height=300')
  await expect(page.getByLabel('创作画板')).toBeVisible({timeout:15000})
  const table=source==='page'?'pages':'templates'
  const document=JSON.stringify({version:1,width:400,height:300,widgets:[{id:'saved-label',name:'保留的标签',kind:'text',x:10,y:20,width:180,height:30,
    cells:[[{text:'Saved device label'}]],style:{fontSize:12,color:'#000000',bold:false}}]})
  const record={id:`saved-device-${source}`,templateId:'device-info-native',name:'我的设备页面',kind:'device-info',width:400,height:300,document,updatedAt:123}
  await page.evaluate(async({table,record})=>{
    const database=await new Promise<IDBDatabase>((resolve,reject)=>{const request=indexedDB.open('inktag-webui');request.onsuccess=()=>resolve(request.result);request.onerror=()=>reject(request.error)})
    try {await new Promise<void>((resolve,reject)=>{const transaction=database.transaction(table,'readwrite');transaction.objectStore(table).put(record);transaction.oncomplete=()=>resolve();transaction.onabort=()=>reject(transaction.error)})}finally{database.close()}
  },{table,record})
  await page.goto(`/#/create/canvas/device-info-native?width=400&height=300&${source}=${record.id}`)
  await expect(page.locator('footer [role=status]')).toContainText(source==='page'?'已打开本地页面':'已从模板创建独立页面')
  await expect(page.locator('.layer-list button')).toHaveText('保留的标签')
  expect(await libraryRecords(page,table)).toEqual([record])
  await savePage(page)
  const [saved]=await libraryRecords(page,'pages')
  expect(saved.document).toBe(document)
  if(source==='template') expect(await libraryRecords(page,'templates')).toEqual([record])
  await page.goto('/#/create/canvas/device-info-native?width=400&height=300')
  await expect(page.getByRole('heading',{name:'模板与分辨率不匹配'})).toBeVisible()
})

test('saving as a template creates reusable fixed-size copies and never changes the source', async ({ page }) => {
  await page.goto('/#/create/canvas/blank?width=212&height=104&color=10')
  await expect(page.getByLabel('创作画板')).toBeVisible({ timeout: 15_000 })
  await page.locator('.control-library').getByRole('button', { name: '文字', exact: true }).click()
  await page.getByRole('button', { name: '保存作品' }).click()
  let dialog = page.getByRole('dialog', { name: '保存作品' })
  await dialog.getByRole('radio', { name: '模板', exact: true }).check()
  await dialog.getByLabel('作品名称').fill('我的任务模板')
  await dialog.getByLabel('作品类型').selectOption('todo')
  await dialog.getByRole('button', { name: '保存模板', exact: true }).click()
  await expect(dialog).toHaveCount(0)
  await expect(page.locator('footer [role="status"]')).toContainText('模板已保存')
  const [source] = await libraryRecords(page, 'templates')
  expect(()=>parseDocument(source.document),source.document).not.toThrow()
  expect(source.kind).toBe('todo')
  expect(await libraryRecords(page, 'pages')).toHaveLength(0)

  await page.goto('/#/create')
  await page.getByLabel('模板来源').selectOption('mine')
  await expect(page.locator('.template-card')).toHaveCount(1)
  await expect(page.locator('.template-card img')).toBeVisible()
  await page.locator('.template-card').click()
  await expect(page.locator('footer [role="status"]')).toContainText('已从模板创建独立页面')
  await expect(page.locator('.native-template-title span')).toContainText('212 × 104 · 六色')
  await expect(page.locator('.layer-list button')).toHaveCount(1)
  await page.locator('.control-library').getByRole('button', { name: '分割线', exact: true }).click()
  await page.getByRole('button', { name: '保存作品' }).click()
  dialog = page.getByRole('dialog', { name: '保存作品' })
  await expect(dialog.getByLabel('作品类型')).toHaveValue('todo')
  await dialog.getByLabel('作品名称').fill('今天的任务')
  await dialog.getByRole('button', { name: '保存页面', exact: true }).click()
  await expect(dialog).toHaveCount(0)
  expect(await libraryRecords(page, 'templates')).toEqual([source])
  expect(await libraryRecords(page, 'pages')).toHaveLength(1)

  // Saving a derived template also creates a new record, not a write to the source.
  await page.getByRole('button', { name: '保存作品' }).click()
  await dialog.getByRole('radio', { name: '模板', exact: true }).check()
  await dialog.getByLabel('作品名称').fill('第二个模板')
  await dialog.getByRole('button', { name: '保存模板', exact: true }).click()
  await expect(dialog).toHaveCount(0)
  const templates = await libraryRecords(page, 'templates')
  expect(templates).toHaveLength(2)
  expect(templates.find(template => template.id === source.id)).toEqual(source)
  await page.goto('/#/create')
  await page.getByLabel('模板来源').selectOption('mine')
  await page.getByRole('button', { name: '删除模板 我的任务模板', exact: true }).click()
  await page.getByRole('dialog', { name: '删除自建模板' }).getByRole('button', { name: '删除', exact: true }).click()
  await expect(page.locator('.template-card')).toHaveCount(1)
  await page.getByRole('link', { name: '我的页面', exact: true }).click()
  await expect(page.locator('.page-resource')).toHaveCount(1)
  await page.getByRole('link', { name: '继续编辑' }).click()
  await expect(page.locator('footer [role="status"]')).toContainText('已打开本地页面')
  await expect(page.locator('.layer-list button')).toHaveCount(2)
})

test('My pages filters by type, fixed resolution, orientation and name and can clear filters', async ({ page }) => {
  await page.goto('/#/library')
  await expect(page.getByRole('region', { name: '页面筛选' })).toBeVisible()
  await page.evaluate(async () => {
    const database = await new Promise<IDBDatabase>((resolve, reject) => {
      const request = indexedDB.open('inktag-webui'); request.onsuccess = () => resolve(request.result); request.onerror = () => reject(request.error)
    })
    const records = [
      { id: 'tasks-wide', name: '今日待办', kind: 'todo', width: 400, height: 300, updatedAt: 3 },
      { id: 'tasks-tall', name: '明日待办', kind: 'todo', width: 300, height: 400, updatedAt: 2 },
      { id: 'calendar-square', name: '桌面日历', kind: 'calendar', width: 152, height: 152, updatedAt: 1 },
    ]
    await new Promise<void>((resolve, reject) => {
      const tx = database.transaction('pages', 'readwrite')
      for (const record of records) tx.objectStore('pages').put({ ...record, templateId: 'blank', document: JSON.stringify({ version: 1, width: record.width, height: record.height, widgets: [], colorMode: 2 }) })
      tx.oncomplete = () => resolve(); tx.onerror = () => reject(tx.error)
    })
    database.close()
  })
  await page.reload()
  await expect(page.locator('.page-resource')).toHaveCount(3)
  await page.getByRole('button', { name: '待办', exact: true }).click()
  await page.getByLabel('页面分辨率').selectOption('400x300')
  await expect(page.locator('.page-resource')).toHaveCount(2)
  await page.getByRole('button', { name: '竖屏页面', exact: true }).click()
  await expect(page.locator('.page-resource')).toHaveCount(1)
  await expect(page.locator('.page-resource')).toContainText('明日待办')
  await page.getByLabel('搜索页面').fill('今日')
  await expect(page.getByText('没有匹配的页面')).toBeVisible()
  await page.getByRole('button', { name: '清除筛选', exact: true }).first().click()
  await expect(page.locator('.page-resource')).toHaveCount(3)
  await page.getByRole('button', { name: '方屏页面', exact: true }).click()
  await expect(page.locator('.page-resource')).toHaveCount(1)
  await expect(page.locator('.page-resource')).toContainText('桌面日历')
})

for(const width of [320,1280]) test(`templates and saved pages filter all three rendering types at ${width}px`,async({page})=>{
  await page.setViewportSize({width,height:740})
  await page.goto('/#/create')
  await expect(page.getByLabel('模板页面类型')).toBeVisible()
  const assertSizeFiltersShareRow=async(root:string)=>{
    const resolution=await page.locator(`${root} .template-resolution-filter`).boundingBox()
    const orientation=await page.locator(`${root} .orientation-filter`).boundingBox()
    expect(Math.abs(resolution!.y-orientation!.y)).toBeLessThan(1)
    expect(resolution!.x+resolution!.width).toBeLessThanOrEqual(orientation!.x)
    expect(await page.locator(`${root} .orientation-filter`).evaluate(element=>element.scrollWidth<=element.clientWidth)).toBe(true)
  }
  await assertSizeFiltersShareRow('.create-page')
  await page.evaluate(async()=>{
    const database=await new Promise<IDBDatabase>((resolve,reject)=>{const request=indexedDB.open('inktag-webui');request.onsuccess=()=>resolve(request.result);request.onerror=()=>reject(request.error)})
    try {
      await new Promise<void>((resolve,reject)=>{
        const tx=database.transaction(['pages','templates'],'readwrite')
        for(const table of ['pages','templates'])for(const [mode,name] of [['static','静态作品'],['dynamic','设备作品'],['host-dynamic','主机作品']]) {
          tx.objectStore(table).put({id:`${table}-${mode}`,name,kind:'custom',templateId:'blank',width:400,height:300,updatedAt:1,
            document:JSON.stringify({version:1,width:400,height:300,widgets:[],pageMode:mode})})
        }
        tx.oncomplete=()=>resolve();tx.onabort=()=>reject(tx.error)
      })
    } finally {database.close()}
  })
  await page.reload()
  await page.getByLabel('模板来源').selectOption('mine')
  await expect(page.locator('.template-card')).toHaveCount(3)
  const templateFilter=page.getByLabel('模板页面类型')
  await expect(templateFilter.locator('option')).toHaveText(['全部页面类型','静态页面','设备动态页面','主机动态页面'])
  for(const [mode,name] of [['static','静态作品'],['dynamic','设备作品'],['host-dynamic','主机作品']]) {
    await templateFilter.selectOption(mode)
    await expect(page.locator('.template-card')).toHaveCount(1)
    await expect(page.locator('.template-card')).toContainText(name)
  }
  await page.getByLabel('模板分辨率').selectOption('400x300')
  await page.getByPlaceholder('搜索模板').fill('主机')
  await expect(page.locator('.template-card')).toHaveCount(1)
  await templateFilter.selectOption('static')
  await expect(page.getByText('没有匹配的模板',{exact:true})).toBeVisible()
  expect(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth)).toBe(true)
  await page.getByRole('link',{name:'我的页面',exact:true}).click()
  await expect(page.locator('.page-resource')).toHaveCount(3)
  const pageFilter=page.getByLabel('页面类型筛选')
  await assertSizeFiltersShareRow('.library-page')
  for(const [mode,name] of [['static','静态作品'],['dynamic','设备作品'],['host-dynamic','主机作品']]) {
    await pageFilter.selectOption(mode)
    await expect(page.locator('.page-resource')).toHaveCount(1)
    await expect(page.locator('.page-resource')).toContainText(name)
  }
  await page.getByLabel('搜索页面').fill('主机')
  await pageFilter.selectOption('static')
  await expect(page.getByText('没有匹配的页面',{exact:true})).toBeVisible()
  await page.getByRole('button',{name:'清除筛选',exact:true}).first().click()
  await expect(pageFilter).toHaveValue('all')
  await expect(page.locator('.page-resource')).toHaveCount(3)
  expect(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth)).toBe(true)
})

for (const width of [320, 390]) {
  test(`save choice dialog fits ${width}px phone and supports keyboard cancellation`, async ({ page }) => {
    await page.setViewportSize({ width, height: 700 })
    await page.goto('/#/create/canvas/blank?width=400&height=300')
    await expect(page.getByLabel('创作画板')).toBeVisible({ timeout: 15_000 })
    await page.keyboard.press('ControlOrMeta+s')
    const dialog = page.getByRole('dialog', { name: '保存作品' })
    await expect(dialog.getByRole('radio', { name: '页面', exact: true })).toBeChecked()
    expect(await dialog.evaluate(element => { const bounds = element.getBoundingClientRect(); return element.scrollWidth <= element.clientWidth && bounds.left >= 0 && bounds.right <= innerWidth && bounds.top >= 0 && bounds.bottom <= innerHeight })).toBe(true)
    await dialog.getByRole('radio', { name: '模板', exact: true }).check()
    await dialog.getByLabel('作品名称').fill('手机模板')
    await page.keyboard.press('Escape')
    await expect(dialog).toHaveCount(0)
    await expect(page.getByRole('button', { name: '保存作品', exact: true })).toBeFocused()
    expect(await libraryRecords(page, 'templates')).toHaveLength(0)
  })
}

test('creation opens the canvas by default and saves an editable page with a preview', async ({ page }) => {
  await page.goto('/#/create')
  await page.getByRole('button', { name: '课表', exact: true }).click()
  await page.locator('.template-card').first().click()
  await expect(page).toHaveURL(/#\/create\/canvas\//)
  await expect(page.getByLabel('创作画板')).toBeVisible({ timeout: 15_000 })
  await expect(page.locator('.leafer-lab .native-template-title strong')).toHaveText('一周课表')
  await expect(page.locator('.lab-mode-tools').getByRole('button', { name: '画笔' }).locator('span')).toBeVisible()
  await savePage(page)
  await expect(page.locator('.leafer-lab footer [role="status"]')).toContainText('页面已保存到本机资源库')

  await page.goto('/#/library')
  await expect(page.getByRole('button', { name: /我的页面/ })).toBeVisible()
  await expect(page.getByRole('button', { name: /布局草稿/ })).toHaveCount(0)
  await expect(page.getByText('一周课表')).toBeVisible()
  await expect(page.locator('.resource-preview img')).toBeVisible()
  await expect(page.getByRole('link', { name: '继续编辑' })).toBeVisible()
  await page.getByRole('link', { name: '继续编辑' }).click()
  await expect(page.getByLabel('创作画板')).toBeVisible({ timeout: 15_000 })
  await expect(page.locator('.leafer-lab .native-template-title strong')).toHaveText('一周课表')
})

for (const failure of ['invalid-json', 'invalid-document', 'geometry-mismatch', 'read-failure', 'missing', 'invalid-template'] as const) {
  test(`failed restore protects the source and disables output: ${failure}`, async ({ page }) => {
    const table = failure === 'invalid-template' ? 'templates' : 'pages'
    const id = `protected-${failure}`
    if (failure === 'read-failure') await page.addInitScript(() => {
      const original = IDBObjectStore.prototype.get
      IDBObjectStore.prototype.get = function (query) {
        if (this.name === 'pages' && query === 'protected-read-failure' && sessionStorage.getItem('restore-recovered') !== '1') {
          throw new DOMException('Isolated read failure fixture', 'UnknownError')
        }
        return original.call(this, query)
      }
    })
    await page.goto('/#/create/canvas/blank?width=400&height=300')
    await expect(page.getByLabel('创作画板')).toBeVisible({ timeout: 15_000 })
    const document = failure === 'invalid-json' || failure === 'invalid-template' ? '{invalid'
      : JSON.stringify(failure === 'invalid-document' ? { version: 999 } : {
        version: 1, width: failure === 'geometry-mismatch' ? 200 : 400, height: 300, colorMode: 2, widgets: [],
      })
    const record = { id, name: '需要保护的原稿', templateId: 'blank', kind: 'custom', width: 400, height: 300, document, updatedAt: 123 }
    if (failure !== 'missing') await page.evaluate(async ({ table, record }) => {
      const database = await new Promise<IDBDatabase>((resolve, reject) => {
        const request = indexedDB.open('inktag-webui'); request.onsuccess = () => resolve(request.result); request.onerror = () => reject(request.error)
      })
      try {
        await new Promise<void>((resolve, reject) => {
          const transaction = database.transaction(table, 'readwrite')
          transaction.objectStore(table).put(record)
          transaction.oncomplete = () => resolve(); transaction.onabort = () => reject(transaction.error)
        })
      } finally { database.close() }
    }, { table, record })
    await page.goto(`/#/create/canvas/blank?width=400&height=300&${table === 'pages' ? 'page' : 'template'}=${id}`)
    await expect(page.locator('footer [role="status"]')).toContainText('原稿不会被覆盖')
    await expect(page.getByRole('button', { name: '保存作品', exact: true })).toBeDisabled()
    await expect(page.getByRole('button', { name: '下载 PNG', exact: true })).toBeDisabled()
    await expect(page.getByRole('button', { name: '发送到设备', exact: true })).toBeDisabled()
    await page.keyboard.press('ControlOrMeta+s')
    await expect(page.getByRole('dialog', { name: '保存作品' })).toHaveCount(0)
    expect(await libraryRecords(page, table)).toEqual(failure === 'missing' ? [] : [record])
    expect(await libraryRecords(page, table === 'pages' ? 'templates' : 'pages')).toEqual([])
    if (failure === 'read-failure') {
      await page.evaluate(() => sessionStorage.setItem('restore-recovered', '1'))
      await page.reload()
      await expect(page.locator('footer [role="status"]')).toContainText('已打开本地页面')
      await expect(page.getByRole('button', { name: '保存作品', exact: true })).toBeEnabled()
      expect(await libraryRecords(page, 'pages')).toEqual([record])
    }
  })
}

test('new blank canvas uses selected size, orientation and color on a phone', async ({ page }) => {
  await page.setViewportSize({ width: 320, height: 700 })
  await page.goto('/#/create')
  const actions = page.locator('.create-page .page-actions')
  expect(await actions.evaluate(element => {
    const boxes = [...element.querySelectorAll('a,button')].map(item => item.getBoundingClientRect())
    return element.scrollWidth <= element.clientWidth && boxes.every(box => box.left >= 0 && box.right <= window.innerWidth)
  })).toBe(true)
  await page.getByLabel('模板分辨率').selectOption('212x104')
  await page.getByRole('button', { name: '竖屏模板' }).click()
  await page.getByRole('button', { name: '新建页面', exact: true }).click()
  const dialog = page.getByRole('dialog', { name: '新建页面' })
  await expect(dialog.getByLabel('页面类型')).toHaveValue('static')
  await expect(dialog.getByLabel('画板宽度')).toHaveValue('104')
  await expect(dialog.getByLabel('画板高度')).toHaveValue('212')
  await dialog.getByLabel('画板色彩').selectOption('3')
  expect(await dialog.evaluate(element => element.scrollWidth <= element.clientWidth)).toBe(true)
  await dialog.getByRole('button', { name: '创建页面' }).click()
  await expect(page).toHaveURL(/create\/canvas\/blank\?width=104&height=212&color=3/)
  await expect(page.getByLabel('创作画板')).toBeVisible({ timeout: 15_000 })
  await expect(page.locator('.native-template-title strong')).toHaveText('空白页面')
  await expect(page.locator('.native-template-title span')).toContainText('104 × 212 · 四色')
  await expect(page.locator('.layer-list button')).toHaveCount(0)
})

test('connected device asks only for page type and uses its screen, ignoring template filters', async ({ page }) => {
  await page.addInitScript({ path: fileURLToPath(new URL('./virtual-bluetooth.js', import.meta.url)) })
  await page.goto('/#/create')
  await page.getByRole('button', { name: /^(打开浏览器记住的设备|连接其他设备)$/ }).click()
  await page.getByRole('button', { name: /选择附近设备/ }).click()
  await expect(page.locator('.device-summary strong')).toHaveText('E2E Fixture')
  await page.getByLabel('模板分辨率').selectOption('212x104')
  await page.getByRole('button', { name: '竖屏模板' }).click()
  await page.getByRole('button', { name: '新建页面', exact: true }).click()
  const dialog=page.getByRole('dialog',{name:'新建页面'})
  await expect(dialog.getByLabel('页面类型')).toHaveValue('static')
  await expect(dialog.getByLabel('画板宽度')).toHaveCount(0)
  await dialog.getByRole('button',{name:'创建页面',exact:true}).click()
  await expect(page).toHaveURL(/create\/canvas\/blank\?width=400&height=300&color=2/)
  await expect(page.getByRole('dialog')).toHaveCount(0)
  await expect(page.getByLabel('创作画板')).toBeVisible({ timeout: 15_000 })
  await expect(page.locator('.native-template-title span')).toContainText('400 × 300 · 三色')
  await expect(page.locator('.native-template-title span')).toContainText('静态页面')
  await expect(page.getByRole('button',{name:'发送到设备',exact:true})).toBeEnabled()
})

test('storage upgrade retires old editor documents but preserves current pages, assets and device records', async ({ page }) => {
  await page.route('**/__storage_fixture', route => route.fulfill({ contentType: 'text/html', body: '<html><body></body></html>' }))
  await page.goto('/__storage_fixture')
  await page.evaluate(async () => {
    const connection = await new Promise<IDBDatabase>((resolve, reject) => {
      const request = indexedDB.open('inktag-webui', 70)
      request.onupgradeneeded = () => {
        const schema: Record<string, string[]> = { pages: ['name', 'templateId', 'updatedAt'], assets: ['name', 'type', 'createdAt'],
          screenProfiles: ['name', 'updatedAt'], devicePreferences: ['updatedAt'], nativeTemplateDrafts: ['updatedAt'],
          leaferDrafts: ['templateId', 'updatedAt'], deviceSlotPreviews: ['deviceId', 'updatedAt'], deviceGuiAssetPackages: ['deviceId', 'updatedAt'] }
        for (const [name, indexes] of Object.entries(schema)) {
          const table = request.result.createObjectStore(name, { keyPath: 'id' })
          for (const index of indexes) table.createIndex(index, index)
          if (name === 'deviceSlotPreviews') table.createIndex('[deviceId+slotId]', ['deviceId', 'slotId'])
        }
      }
      request.onsuccess = () => resolve(request.result); request.onerror = () => reject(request.error)
    })
    await new Promise<void>((resolve, reject) => {
      const tx = connection.transaction([...connection.objectStoreNames], 'readwrite')
      tx.objectStore('pages').put({ id: 'historical-page', name: '历史图片页面', templateId: 'schedule-week', width: 400, height: 300, document: '{}', createdAt: 1, updatedAt: 1 })
      tx.objectStore('nativeTemplateDrafts').put({ id: 'device-gui:calendar:400x300:2:default', settings: JSON.stringify({ schemaVersion: 1, name: '历史原生页面', kind: 'calendar', width: 400, height: 300, widgets: [] }), updatedAt: 2 })
      tx.objectStore('leaferDrafts').put({ id: 'current-page', name: '新画板页面', templateId: 'blank', width: 400, height: 300,
        document: JSON.stringify({ version: 1, width: 400, height: 300, colorMode: 2, widgets: [] }), updatedAt: 3 })
      tx.objectStore('assets').put({ id: 'source-image', name: '原图', type: 'image/png', blob: new Blob(), createdAt: 1 })
      tx.objectStore('devicePreferences').put({ id: 'remembered-device', localAlias: '书桌设备', updatedAt: 1 })
      tx.objectStore('screenProfiles').put({ id: 'screen', name: '自建屏幕', updatedAt: 1 })
      tx.objectStore('deviceSlotPreviews').put({ id: 'slot', deviceId: 'remembered-device', slotId: 7, updatedAt: 1 })
      tx.objectStore('deviceGuiAssetPackages').put({ id: 'gui-assets', deviceId: 'remembered-device', updatedAt: 1 })
      tx.oncomplete = () => resolve(); tx.onerror = () => reject(tx.error)
    })
    connection.close()
  })
  await page.goto('/#/library')
  await expect(page.locator('.page-resource')).toHaveCount(1)
  await expect(page.getByText('新画板页面', { exact: true })).toBeVisible()
  await expect(page.getByText(/历史图片页面|历史原生页面/)).toHaveCount(0)
  await expect(page.getByRole('link', { name: '继续编辑' })).toHaveAttribute('href', /create\/canvas\/blank.*page=current-page/)
  await expect(page.getByRole('button', { name: /布局草稿/ })).toHaveCount(0)
  const counts = await page.evaluate(async () => {
    const database = await new Promise<IDBDatabase>((resolve, reject) => {
      const request = indexedDB.open('inktag-webui'); request.onsuccess = () => resolve(request.result); request.onerror = () => reject(request.error)
    })
    const names = ['assets', 'devicePreferences', 'screenProfiles', 'deviceSlotPreviews', 'deviceGuiAssetPackages']
    const counts = await Promise.all(names.map(name => new Promise<number>((resolve, reject) => {
      const request = database.transaction(name).objectStore(name).count(); request.onsuccess = () => resolve(request.result); request.onerror = () => reject(request.error)
    })))
    const retired = ['nativeTemplateDrafts', 'leaferDrafts'].some(name => database.objectStoreNames.contains(name))
    database.close(); return { counts, retired }
  })
  expect(counts).toEqual({ counts: [1, 1, 1, 1, 1], retired: false })
  await page.getByRole('link', { name: '继续编辑' }).click()
  await expect(page.locator('footer [role="status"]')).toContainText('已打开本地页面')
})

test('separate pages from one blank size never overwrite each other and editing updates only that page', async ({ page }) => {
  const createAndSave = async () => {
    await page.goto('/#/create/canvas/blank?width=400&height=300')
    await expect(page.getByLabel('创作画板')).toBeVisible({ timeout: 15_000 })
    await savePage(page)
    await expect(page.locator('footer [role="status"]')).toContainText('页面已保存')
    await page.goto('/#/library')
  }
  await createAndSave()
  await expect(page.locator('.page-resource')).toHaveCount(1)
  await createAndSave()
  await expect(page.locator('.page-resource')).toHaveCount(2)
  await page.locator('.page-resource').first().getByRole('link', { name: '继续编辑' }).click()
  await expect(page).toHaveURL(/create\/canvas\/blank.*page=canvas-/)
  await expect(page.locator('footer [role="status"]')).toContainText('已打开本地页面')
  await page.locator('.control-library').getByRole('button', { name: '文字', exact: true }).click()
  await savePage(page)
  await expect(page.locator('footer [role="status"]')).toContainText('页面已保存')
  await page.goto('/#/library')
  await expect(page.locator('.page-resource')).toHaveCount(2)
  await page.locator('.page-resource').first().getByRole('button', { name: '复制', exact: true }).click()
  await expect(page.locator('.page-resource')).toHaveCount(3)
  const copy = page.locator('.page-resource').filter({ hasText: '空白页面 副本' })
  await copy.getByRole('link', { name: '继续编辑' }).click()
  await expect(page.locator('.native-template-title strong')).toHaveText('空白页面 副本')
  await expect(page.locator('.layer-list button')).toHaveCount(1)
  await page.goto('/#/library')
  await page.locator('.page-resource').filter({ hasText: '空白页面 副本' }).getByRole('button', { name: '删除', exact: true }).click()
  await page.getByRole('dialog', { name: '删除本地页面' }).getByRole('button', { name: '删除', exact: true }).click()
  await expect(page.locator('.page-resource')).toHaveCount(2)
})

test('resource images insert into the new canvas without modifying the original asset', async ({ page }) => {
  await page.goto('/#/library?tab=assets')
  const png = await page.evaluate(() => {
    const canvas = document.createElement('canvas'); canvas.width = 2; canvas.height = 2
    const ctx = canvas.getContext('2d')!; ctx.fillStyle = '#dc3026'; ctx.fillRect(0, 0, 2, 2)
    return canvas.toDataURL('image/png').split(',')[1]
  })
  await page.locator('input[type="file"]').setInputFiles({ name: '红色素材.png', mimeType: 'image/png', buffer: Buffer.from(png, 'base64') })
  await expect(page.locator('.page-resource')).toHaveCount(1)
  await page.goto('/#/create/canvas/blank?width=400&height=300')
  await expect(page.getByLabel('创作画板')).toBeVisible({ timeout: 15_000 })
  await page.locator('.control-library').getByRole('button', { name: '图片框', exact: true }).click()
  await page.getByRole('button', { name: '从素材库选择' }).click()
  const picker = page.getByRole('dialog', { name: '插入图片素材' })
  await picker.getByRole('button', { name: '红色素材.png' }).click()
  await expect(picker).toHaveCount(0)
  await expect(page.getByRole('button', { name: '替换本地图片' })).toBeVisible()
  await savePage(page)
  await expect(page.locator('footer [role="status"]')).toContainText('页面已保存')
  await page.goto('/#/library')
  await expect(page.locator('.resource-preview img')).toBeVisible()
  await page.getByRole('button', { name: /图片素材/ }).click()
  await expect(page.locator('.page-resource')).toHaveCount(1)
  await expect(page.getByText('红色素材.png')).toBeVisible()
})

test('template preview uses the same scene pixels as the opened canvas', async ({ page }) => {
  await page.goto('/#/create')
  await page.getByRole('button', { name: '课表', exact: true }).click()
  await page.getByLabel('模板分辨率').selectOption('400x300')
  const card = page.locator('.template-card').first()
  const thumbnail = card.locator('canvas')
  await expect.poll(() => thumbnail.evaluate(canvas => canvas.width)).toBe(400)
  const preview = await thumbnail.evaluate(canvas => canvas.toDataURL('image/png'))
  await card.click()
  await expect(page.getByLabel('创作画板')).toBeVisible({ timeout: 15_000 })
  const download = page.waitForEvent('download')
  await page.getByRole('button', { name: '下载 PNG' }).click()
  const encoded = (await readFile(await (await download).path())).toString('base64')
  const identical = await page.evaluate(async ({ preview, encoded }) => {
    const images = await Promise.all([preview, `data:image/png;base64,${encoded}`].map(async url => createImageBitmap(await (await fetch(url)).blob())))
    const pixels = images.map(image => {
      const canvas = document.createElement('canvas'); canvas.width = 400; canvas.height = 300
      const ctx = canvas.getContext('2d')!; ctx.drawImage(image, 0, 0); image.close()
      return ctx.getImageData(0, 0, 400, 282).data // Device-owned mark is omitted from the send/export image.
    })
    return pixels[0].every((value, i) => value === pixels[1][i])
  }, { preview, encoded })
  expect(identical).toBe(true)
})

test('brush stroke is an undoable editable layer', async ({ page }) => {
  await page.goto('/#/create/canvas/schedule-week-212x104?width=212&height=104')
  await expect(page.getByLabel('创作画板')).toBeVisible({ timeout: 15_000 })
  await page.getByRole('button', { name: '画笔', exact: true }).click()
  const canvas = page.getByLabel('自由画笔画布')
  await expect(canvas).toBeVisible()
  const bounds = await canvas.boundingBox()
  expect(bounds).not.toBeNull()
  await page.mouse.move(bounds!.x + 20, bounds!.y + 45)
  await page.mouse.down()
  await page.mouse.move(bounds!.x + 48, bounds!.y + 55, { steps: 5 })
  await page.mouse.up()
  await expect(page.locator('.layer-list button').first()).toContainText('自由画笔')
  await page.getByRole('button', { name: '撤销', exact: true }).click()
  await expect(page.locator('.layer-list button').filter({ hasText: '自由画笔' })).toHaveCount(0)
  await page.getByRole('button', { name: '重做', exact: true }).click()
  await expect(page.locator('.layer-list button').first()).toContainText('自由画笔')
})

test('compact timetable exposes in-canvas row and column editing without wrapping', async ({ page }) => {
  await page.goto('/#/create/canvas/schedule-week-212x104?width=212&height=104')
  await expect(page.getByLabel('创作画板')).toBeVisible({ timeout: 15_000 })
  await page.locator('.layer-list button').first().click()
  const actions = page.getByRole('toolbar', { name: '课表快捷操作' })
  await expect(actions).toBeVisible()
  for (const name of ['编辑格子', '＋行', '−行', '＋列', '−列']) {
    await expect(actions.getByRole('button', { name, exact: true })).toBeVisible()
  }
  expect(await actions.evaluate(element => element.scrollWidth <= element.clientWidth)).toBe(true)
  await actions.getByRole('button', { name: '＋行' }).click()
  await expect(page.getByRole('button', { name: '撤销', exact: true })).toBeEnabled()
  await actions.getByRole('button', { name: '−行' }).click()
  await expect(page.getByRole('button', { name: '重做', exact: true })).toBeDisabled()
})

test('mobile editor keeps the canvas foremost and opens controls or properties on demand', async ({ page }) => {
  await page.setViewportSize({ width: 390, height: 844 })
  await page.goto('/#/create/canvas/schedule-week?width=400&height=300')
  await expect(page.getByLabel('创作画板')).toBeVisible({ timeout: 15_000 })
  const header = page.locator('.leafer-lab > .native-template-header')
  await expect(header.getByRole('link', { name: '返回模板' })).toBeVisible()
  await expect(header.getByRole('button', { name: '模板库' })).toHaveCount(0)
  await expect(header.getByRole('textbox', { name: '页面名称' })).toHaveCount(0)
  await expect(header.locator('.native-template-title strong')).toHaveText('一周课表')
  await expect(header.locator('.native-template-title span')).toContainText('400 × 300')
  await expect(header.getByRole('button', { name: '发送到设备' })).toBeVisible()
  await expect(header.getByRole('button', { name: '发送到设备' })).toBeDisabled()
  await expect(header.getByRole('button', { name: '载入草稿' })).toHaveCount(0)
  await expect(page.getByRole('button', { name: '载入草稿' })).toHaveCount(0)
  const headerGeometry = await header.evaluate(element => {
    const back = element.querySelector('a.icon-button')!.getBoundingClientRect()
    const title = element.querySelector('.native-template-title')!.getBoundingClientRect()
    const titleSize = Number.parseFloat(getComputedStyle(element.querySelector('.native-template-title strong')!).fontSize)
    const history = element.querySelector('.native-template-history')!.getBoundingClientRect()
    const send = element.querySelector('button[aria-label="发送到设备"]')!.getBoundingClientRect()
    return { oneRow: [title, history, send].every(box => Math.abs(box.top + box.height / 2 - back.top - back.height / 2) <= 2), order: back.right <= title.left && title.right <= history.left && history.right <= send.left, fits: element.scrollWidth <= window.innerWidth, backSize: back.width, titleSize, backTitleGap: Math.round(title.left - back.right) }
  })
  expect(headerGeometry).toEqual({ oneRow: true, order: true, fits: true, backSize: 44, titleSize: 16, backTitleGap: 10 })
  await expect(header).not.toContainText('InkTag')
  await expect(page.locator('.lab-toolbar-options')).toHaveCount(0)
  await expect(page.locator('.lab-mode-tools').getByRole('button')).toHaveCount(4)
  await expect(page.locator('.lab-mode-tools').getByRole('button', { name: '画笔' }).locator('span')).toBeHidden()
  expect(await page.locator('.lab-toolbar').evaluate(element => element.scrollWidth <= window.innerWidth)).toBe(true)
  const controls = page.locator('.leafer-lab .lab-tools')
  const properties = page.locator('.leafer-lab .properties')
  const dock = page.getByRole('navigation', { name: '编辑面板', exact: true })
  await expect(page.locator('.mobile-editor-tabs')).toHaveCount(0)
  await expect(controls).toBeHidden()
  await expect(properties).toBeHidden()
  await dock.getByRole('button', { name: '打开编辑面板' }).click()
  await expect(controls).toBeVisible()
  await expect(properties).toBeHidden()
  await expect(dock.getByRole('button', { name: '控件与图层' })).toHaveAttribute('aria-pressed', 'true')
  await dock.getByRole('button', { name: '属性', exact: true }).click()
  await expect(properties).toBeVisible()
  await expect(controls).toBeHidden()
  const storeToggle = properties.getByRole('checkbox', { name: '发送时保存到设备槽' })
  await expect(storeToggle).toHaveCount(0)
  await dock.getByRole('button', { name: '关闭编辑面板' }).click()
  await expect(properties).toBeHidden()
  await dock.getByRole('button', { name: '打开编辑面板' }).click()
  await expect(properties).toBeVisible()
  await dock.getByRole('button', { name: '控件与图层' }).click()
  await controls.getByRole('button', { name: '文字', exact: true }).click()
  await expect(controls).toBeHidden()
  await expect(dock.getByRole('button', { name: '打开编辑面板' })).toBeVisible()
  expect(await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth)).toBe(true)
})

for (const width of [320, 390]) {
  test(`mobile bottom sheet refits and centers the whole canvas at ${width}px without changing pixels`, async ({ page }) => {
    await page.setViewportSize({ width, height: width === 320 ? 700 : 844 })
    await page.goto('/#/create/canvas/blank?width=400&height=300')
    await expect(page.getByLabel('创作画板')).toBeVisible({ timeout: 15_000 })
    await page.getByRole('button', { name: '画笔', exact: true }).click()
    const stage = page.locator('.leafer-lab .stage')
    const dock = page.getByRole('navigation', { name: '编辑面板', exact: true })
    const canvas = page.getByLabel('自由画笔画布')
    const geometry = () => canvas.evaluate(element => {
      const board = element.getBoundingClientRect()
      const stage = element.closest('.stage')!.getBoundingClientRect()
      const fullHeight = board.width * 300 / 400 // Drawing excludes only the protected device footer.
      return { scale: board.width / 400, centered: Math.abs(board.left + board.width / 2 - stage.left - stage.width / 2) < 1 && Math.abs(board.top + fullHeight / 2 - stage.top - stage.height / 2) < 1,
        fits: board.left >= stage.left + 9 && board.right <= stage.right - 9 && board.top >= stage.top + 9 && board.top + fullHeight <= stage.bottom - 9 }
    })
    await expect.poll(async () => (await geometry()).centered).toBe(true)
    const fullStage = await stage.boundingBox()
    const fullScale = (await geometry()).scale
    await dock.getByRole('button', { name: '打开编辑面板' }).click()
    await expect(page.locator('.lab-tools')).toBeVisible()
    await expect.poll(async () => (await stage.boundingBox())!.height).toBeLessThan(fullStage!.height - 100)
    await expect.poll(async () => { const view = await geometry(); return view.centered && view.fits && view.scale < fullScale }).toBe(true)
    const openStage = await stage.boundingBox()
    const dockBox = await dock.boundingBox()
    const panelBox = await page.locator('.lab-tools').boundingBox()
    expect(Math.abs(openStage!.y + openStage!.height - dockBox!.y)).toBeLessThan(1)
    expect(Math.abs(dockBox!.y + dockBox!.height - panelBox!.y)).toBeLessThan(1)
    await dock.getByRole('button', { name: '属性', exact: true }).click()
    await expect(page.locator('.properties')).toBeVisible()
    expect((await stage.boundingBox())!.height).toBe(openStage!.height)
    await expect.poll(async () => (await geometry()).centered).toBe(true)
    await expect(canvas).toHaveAttribute('width', '400')
    await expect(canvas).toHaveAttribute('height', '300')
    await expect(page.locator('.native-template-title span')).toContainText('400 × 300')
    await dock.getByRole('button', { name: '关闭编辑面板' }).click()
    await expect.poll(async () => (await stage.boundingBox())!.height).toBe(fullStage!.height)
    await expect.poll(async () => { const view = await geometry(); return view.centered && view.fits && view.scale === fullScale }).toBe(true)
  })
}

test('mobile drawing controls wrap, use ink and size icons, and keep separate eraser width', async ({ page }) => {
  await page.setViewportSize({ width: 390, height: 844 })
  await page.goto('/#/create/canvas/blank?width=400&height=300')
  await expect(page.getByLabel('创作画板')).toBeVisible({ timeout: 15_000 })
  await expect(page.locator('.native-template-title strong')).toHaveText('空白页面')
  await expect(page.locator('.lab-toolbar-options')).toHaveCount(0)
  const primaryHeight = await page.locator('.lab-toolbar-primary').evaluate(element => element.getBoundingClientRect().height)
  await page.getByRole('button', { name: '画笔', exact: true }).click()
  expect(await page.locator('.lab-toolbar-primary').evaluate(element => element.getBoundingClientRect().height)).toBe(primaryHeight)
  await expect(page.locator('.lab-toolbar-options')).toBeVisible()
  await expect(page.getByRole('group', { name: '画笔颜色' }).getByRole('button')).toHaveCount(3)
  await page.getByRole('button', { name: '画笔 4 像素' }).click()
  await expect(page.getByRole('button', { name: '画笔 4 像素' })).toHaveAttribute('aria-pressed', 'true')
  await page.getByRole('button', { name: '画笔颜色 红' }).click()
  await page.getByRole('button', { name: '橡皮', exact: true }).click()
  await expect(page.getByRole('button', { name: '橡皮 8 像素' })).toHaveAttribute('aria-pressed', 'true')
  await page.getByRole('button', { name: '橡皮 16 像素' }).click()
  await expect(page.getByRole('button', { name: '橡皮 16 像素' })).toHaveAttribute('aria-pressed', 'true')
  await page.getByRole('button', { name: '画笔', exact: true }).click()
  await expect(page.getByRole('button', { name: '画笔 4 像素' })).toHaveAttribute('aria-pressed', 'true')
  const layout = await page.locator('.lab-toolbar').evaluate(element => ({
    fits: element.scrollWidth <= window.innerWidth,
    separated: element.querySelector('.lab-toolbar-options')!.getBoundingClientRect().top >= element.querySelector('.lab-toolbar-primary')!.getBoundingClientRect().bottom,
  }))
  expect(layout.fits).toBe(true)
  expect(layout.separated).toBe(true)
  await expect(page.getByLabel('自由画笔画布')).toBeVisible()
  await page.getByRole('button', { name: '选择', exact: true }).click()
  await expect(page.locator('.lab-toolbar-options')).toHaveCount(0)
  expect(await page.locator('.lab-toolbar-primary').evaluate(element => element.getBoundingClientRect().height)).toBe(primaryHeight)
  expect(await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth)).toBe(true)
})

test('narrow six-color phone keeps the tool groups and title actions within the viewport', async ({ page }) => {
  await page.setViewportSize({ width: 320, height: 700 })
  await page.goto('/#/create/canvas/blank?width=212&height=104&color=10')
  await expect(page.getByLabel('创作画板')).toBeVisible({ timeout: 15_000 })
  await page.getByRole('button', { name: '画笔', exact: true }).click()
  await expect(page.getByRole('group', { name: '画笔颜色' }).getByRole('button')).toHaveCount(6)
  await expect(page.locator('.lab-mode-tools').getByRole('button', { name: '画笔' }).locator('span')).toBeHidden()
  const geometry = await page.evaluate(() => ({
    pageFits: document.documentElement.scrollWidth <= window.innerWidth,
    headerFits: document.querySelector('.native-template-header')!.scrollWidth <= window.innerWidth,
    toolbarFits: document.querySelector('.lab-toolbar')!.scrollWidth <= window.innerWidth,
  }))
  expect(geometry).toEqual({ pageFits: true, headerFits: true, toolbarFits: true })
})

test('canvas exports exact pixels and visible table borders without the base placeholder', async ({ page }) => {
  await page.goto('/#/create/canvas/schedule-week?width=400&height=300')
  await expect(page.getByLabel('创作画板')).toBeVisible({ timeout: 15_000 })
  await page.locator('.layer-list button').first().click()
  const download = page.waitForEvent('download')
  await page.getByRole('button', { name: '下载 PNG' }).click()
  const file = await download
  const encoded = (await readFile(await file.path())).toString('base64')
  const raster = await page.evaluate(async data => {
    const bytes = Uint8Array.from(atob(data), char => char.charCodeAt(0))
    const bitmap = await createImageBitmap(new Blob([bytes], { type: 'image/png' }))
    const canvas = document.createElement('canvas')
    canvas.width = bitmap.width; canvas.height = bitmap.height
    const context = canvas.getContext('2d')!
    context.drawImage(bitmap, 0, 0); bitmap.close()
    const pixels = context.getImageData(350, 284, 46, 14).data
    const border = context.getImageData(16, 48, 1, 1).data
    return { size: [canvas.width, canvas.height], white: Array.from(pixels).every((value, index) => index % 4 === 3 || value === 255), border: border[0] < 64 && border[1] < 64 && border[2] < 64 }
  }, encoded)
  expect(raster).toEqual({ size: [400, 300], white: true, border: true })
  await expect(page.locator('.leafer-lab footer [role="status"]')).toContainText('设备标识占位不包含')
})
