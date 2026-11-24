import { useSyncExternalStore } from 'react'
import type { FindMapProvider } from './find/map'

export type LanguageMode = 'browser' | 'zh-CN' | 'en'
export type FindMapPreference = 'system' | FindMapProvider
export const PREFERENCES_KEY = 'inktag-web-preferences-v1'
type Preferences = { language: LanguageMode; findMapProvider: FindMapPreference; findMapKeys: Record<FindMapProvider, string> }
const defaults: Preferences = { language: 'browser', findMapProvider: 'system', findMapKeys: { baidu: '', amap: '', osm: '' } }
let current: Preferences = defaults
let browserLanguage: 'zh-CN' | 'en' = 'zh-CN'
const listeners = new Set<() => void>()

export function parsePreferences(value: string | null): Preferences {
  try {
    const parsed = JSON.parse(value || 'null')
    return {
      language: parsed?.language === 'zh-CN' || parsed?.language === 'en' ? parsed.language : 'browser',
      findMapProvider: ['baidu', 'amap', 'osm'].includes(parsed?.findMapProvider) ? parsed.findMapProvider : 'system',
      findMapKeys: {
        baidu: typeof parsed?.findMapKeys?.baidu === 'string' ? parsed.findMapKeys.baidu.trim().slice(0, 256) : '',
        amap: typeof parsed?.findMapKeys?.amap === 'string' ? parsed.findMapKeys.amap.trim().slice(0, 256) : '',
        osm: '',
      },
    }
  } catch { return { ...defaults } }
}

export function resolveBrowserLanguage(languages: readonly string[]) {
  for (const language of languages) {
    if (/^zh(?:-|$)/i.test(language)) return 'zh-CN' as const
    if (/^en(?:-|$)/i.test(language)) return 'en' as const
  }
  return 'en' as const
}

export function getPreferences() { return current }
export function getLanguage() { return current.language === 'browser' ? browserLanguage : current.language }
export function getFindMapProvider(): FindMapProvider {
  return current.findMapProvider === 'system' ? (browserLanguage === 'zh-CN' ? 'baidu' : 'osm') : current.findMapProvider
}
export function subscribePreferences(listener: () => void) {
  listeners.add(listener)
  return () => { listeners.delete(listener) }
}

function apply() {
  document.documentElement.lang = getLanguage()
  delete document.documentElement.dataset.motion
  listeners.forEach((listener) => listener())
}

export function initializePreferences() {
  try { current = parsePreferences(window.localStorage.getItem(PREFERENCES_KEY)) }
  catch { current = { ...defaults } }
  const readBrowser = () => {
    browserLanguage = resolveBrowserLanguage(navigator.languages?.length ? navigator.languages : [navigator.language])
    apply()
  }
  readBrowser()
  window.addEventListener('languagechange', readBrowser)
  return () => window.removeEventListener('languagechange', readBrowser)
}

export function setPreferences(patch: Partial<Preferences>) {
  current = parsePreferences(JSON.stringify({ ...current, ...patch }))
  let saved = true
  try { window.localStorage.setItem(PREFERENCES_KEY, JSON.stringify(current)) }
  catch { saved = false }
  apply()
  return saved
}

export function useLanguage() { return useSyncExternalStore(subscribePreferences, getLanguage, getLanguage) }
export function usePreferences() { return useSyncExternalStore(subscribePreferences, getPreferences, getPreferences) }
export function useFindMapProvider() { return useSyncExternalStore(subscribePreferences, getFindMapProvider, getFindMapProvider) }

const english: Record<string, string> = {
  '蓝牙广播方式': 'Advertising mode', '持续广播': 'Always on', '延时关闭': 'Timed off', '延时（秒）': 'Delay (s)',
  '写入设备': 'Save to device', '请稍候…': 'Please wait…', '重新读取': 'Read again',
  '连接设备后可读取广播设置。': 'Connect a device to read its advertising settings.',
  '正在读取广播设置…': 'Reading advertising settings…',
  '广播设置未读到，请重新读取。': 'Advertising settings could not be read. Please try again.',
  '当前固件不支持调整广播方式。': 'This firmware does not support advertising settings.',
  '设备没有按键或 NFC，无法调整广播方式。': 'A button or NFC is required to change advertising mode.',
  '修改广播设置需要激活 Pro。': 'Activate Pro to change advertising settings.',
  '广播设置已保存。': 'Advertising settings saved.',
  '请重新读取设置后再试。': 'Read the settings again before retrying.',
  '未连接时每秒广播一次。Find My 不受此设置影响。': 'Advertises every second when disconnected. Find My is unaffected.',
  '延时需为 30–65535 秒的整数，请调整后写入设备。': 'Enter a whole number from 30 to 65535 seconds before saving to the device.',
  '延时至少 30 秒。启动或断开连接后开始计时；按键或 NFC 触碰可重新开放连接。日历、时钟、轮播和 Find My 继续运行。': 'Use a delay of at least 30 seconds. The timer starts after startup or disconnection. A button or NFC tap makes the device connectable again. Calendar, clock, slideshow and Find My keep running.',
  '墨水屏的颜色': 'E-paper colors',
  'NFC 切换页面（Pro）': 'NFC page switching (Pro)',
  '碰一碰连接（base）：手机读取标签链接，首次连接仍需浏览器蓝牙授权。': 'Tap to connect (base): read the tag link; the first connection still requires browser Bluetooth authorization.',
  '当前设备无可用 NFC。': 'NFC is unavailable on this device.', '设备动作需激活 Pro。': 'Device actions require Pro activation.',
  '客户端更新': 'Client updates', '构建': 'Build',
  '尚无已批准发布的客户端': 'No approved client release is available yet',
  '正在检查…': 'Checking…', '检查客户端更新': 'Check client updates', '可更新 WebUI': 'WebUI update available',
  '当前 WebUI 无需更新': 'Your WebUI is up to date',
  '不影响设备连接与其他功能。': 'Device connections and other features remain available.', '本地服务': 'Local server',
  '下载附件后按手册安装；网页检查不会自动安装或刷写设备。': 'Download the package and follow the manual. Checking does not install software or flash a device.',
  '未能自动连接，请选择设备。首次授权需要点击下方按钮。': 'Automatic connection was unavailable. Click below to select and authorize a device.',
  '当前浏览器不支持 Web Bluetooth，请使用支持蓝牙的浏览器或 CLI。': 'This browser does not support Web Bluetooth. Use a compatible browser or the CLI.',
  '选择蓝牙设备': 'Choose Bluetooth device', '暂不连接': 'Not now',
  '设置': 'Settings', '客户端设置': 'Client settings',
  '总览': 'Overview', '图片': 'Pictures', '创作': 'Create', '资源': 'Resources', '查找': 'Find', '设备': 'Device',
  '偏好': 'Preferences', '概览': 'Overview', '显示': 'Display', '板卡': 'Hardware', '交互': 'Interaction',
  '激活': 'Activation', '固件': 'Firmware', '日志': 'Logs', '测试': 'Tests', '关于': 'About',
  '主导航': 'Main navigation', '移动端主导航': 'Mobile navigation', '设备设置': 'Device settings',
  '认识墨水屏': 'Meet e-paper', '连接与核对屏幕': 'Connect and check the panel', '选择内容与发送': 'Choose and send content',
  'InkTag 入门教程': 'Getting started with InkTag', '关闭教程': 'Close tutorial', '上一页': 'Previous', '下一页': 'Next',
  '开始使用': 'Get started', '暂时跳过': 'Skip for now', '教程分页': 'Tutorial pages', '点击放大': 'Enlarge',
  '关闭大图': 'Close image', '连接设备': 'Connect a device', '核对当前屏幕': 'Check the current panel',
  '选择课程表模板': 'Choose a timetable template', '编辑课程并发送': 'Edit classes and send', '利用环境光': 'Reflective, paper-like reading',
  '按屏幕筛选': 'Filter by panel', '打开课程表模板': 'Open timetable', '编辑课程内容': 'Edit classes', '连接后发送': 'Connect, then send',
  '静态画面不需供电': 'Image retention without power', '刷新较慢，会闪烁': 'Slow refresh with flashing',
  '关于 InkTag': 'About InkTag', '项目与下载': 'Project and downloads', 'Web 上位机': 'Web console',
  '使用手册': 'User manual', '首次使用与操作教程': 'Getting started and usage', '查看入门教程': 'Open tutorial',
  '像纸一样利用环境光阅读，适合显示不频繁变化的信息。': 'Read using ambient light, like paper. Best for information that changes infrequently.',
  '不是手机那样的发光屏。光线充足时容易阅读，暗处需要外部照明。': 'Unlike a phone, the panel does not emit light. Use adequate lighting; external light is needed in the dark.',
  '纸张般的反射式显示，在合适照明下阅读更舒适，减少持续发光的刺激；护眼不代表医疗保证，仍需注意距离和休息。': 'Reflective, paper-like display can be comfortable under suitable lighting, without a continuously luminous screen. Eye-friendly is not a medical guarantee; keep a suitable distance and take breaks.',
  '刷新完成后，屏幕保持画面无需持续供电，断电后也能保图；设备的蓝牙、计时和刷新仍会消耗电量。': 'The panel retains its finished image without sustaining power, including after power-off. Bluetooth, timekeeping and refresh still consume device energy.',
  '一次全刷通常需要几秒到十几秒，部分多色屏或低温环境会更久。闪烁通常是正常过程，请保持供电并等待完成，不要重复刷新。': 'A full refresh usually takes several to over ten seconds; color panels and cold conditions can take longer. Flashing is usually normal. Keep power stable and wait instead of repeating refreshes.',
  '不频繁刷新并保持低功耗待机时，纽扣电池也能长时间使用。实际续航取决于电池容量、刷新频率和蓝牙、Find 等活动，并非整机完全不耗电。': 'With infrequent refresh and low-power standby, a coin cell can last a long time. Battery life depends on capacity, refresh frequency, Bluetooth and Find activity. The complete device is not power-free.',
  '先找到自己的设备，再确认屏幕，避免发错设备或选错配置。': 'Choose your own device and confirm its panel before sending content.',
  '需要支持 Web Bluetooth 的浏览器和系统蓝牙。首次选择需要浏览器授权；无法使用时，可改用 Windows、Linux 或 macOS CLI。': 'A Web Bluetooth-capable browser and system Bluetooth are required. Grant permission on first selection, or use the Windows, Linux or macOS CLI.',
  '打开顶部连接菜单，点“选择附近设备”，再在浏览器列表中选择设备的蓝牙名，例如 IKT_GR_3A57。': 'Open the top connection menu, choose a nearby device, and select its Bluetooth name, for example IKT_GR_3A57.',
  '进入“设置 → 设备设置 → 显示”，核对当前选中的 Profile、分辨率和颜色。配置正确时无需重新保存；不要凭相似板名更换。': 'Open Settings → Device settings → Display and check the selected profile, resolution and colors. Do not resave a correct setting or choose by a similar board name.',
  '选模板、看预览、再发送；也可以从“图片”导入自己的图片。': 'Choose a template, preview it, then send. You can also import your own pictures.',
  '发送前请确认设备、分辨率和颜色。传输或刷新期间请保持供电；出现错误或结果未确认时，先查看提示，不要重复发送。': 'Confirm the device, resolution and colors before sending. Keep power stable during transfer and refresh. If an error appears or the outcome is unconfirmed, read the message before sending again.',
  '打开“创作”，选择“课表”，按屏幕分辨率和方向筛选，再点击“一周课表”。创建后画板尺寸固定，换屏时需要重新创建。': 'Open Create, select Timetable, filter by panel resolution and orientation, then choose Weekly timetable. Canvas size is fixed after creation; create a new canvas after changing panels.',
  '先点击表格选中控件，再点击单元格修改课程和时间；可在“控件与属性”中调整样式。核对预览后，连接设备并点击顶部纸飞机按钮“发送到设备”，等待传输和屏幕刷新完成。课表按静态图片发送，内容不会自动更新。设备动态页面暂不支持保存到设备。': 'Select the table, then click a cell to edit a class or time. Adjust styling in Controls and properties. Check the preview, connect your device, click the paper-plane Send to device button at the top, and wait for transfer and refresh. Timetables are sent as static images and do not update automatically. Custom dynamic pages cannot yet be saved to the device.',
  '新建页面': 'New page', '页面类型': 'Page type', '静态页面': 'Static page', '设备动态页面': 'Device dynamic page', '主机动态页面': 'Host dynamic page',
  '由设备自行渲染与刷新，无需主机持续在线。定制动态页面暂不支持保存到设备。日历、时钟及设备控件仅用于预览，可保存到本机继续编辑或导出 PNG。': 'The device renders and refreshes the page without a continuously connected host. Custom device-dynamic pages cannot yet be saved to the device. Controls are preview-only; save locally to keep editing or export a PNG.',
  '由 CLI 本地服务定期渲染，通过蓝牙推送到设备。服务需持续运行，设备须在主机蓝牙范围内。主机动态页面暂不支持自动推送，仅可保存到本机或导出预览 PNG。': 'A local CLI service periodically renders and sends the page over Bluetooth. The service must stay running and the device must stay within Bluetooth range. Automatic delivery of host-dynamic pages is not yet supported; only local saving and PNG preview export are available.',
  '图槽轮播': 'Image-slot carousel', '全部页面类型': 'All page types',
  '静态页面不能包含动态控件或变量，请改用普通文字。': 'Static pages cannot contain dynamic controls or variables. Use plain text instead.',
  '截图为示例设备界面；实际可用功能以连接的设备为准。': 'Screenshots show an example device. Actual functions depend on the connected device.',
  '内容停留，画面保留。': 'Content stays. Image remains.',
  '可随时在“设置 → 客户端设置 → 关于”中重新打开教程。': 'Reopen the tutorial from Settings → Client settings → About.',
  '可滚动查看完整截图。按 Esc 或点击右上角关闭。': 'Scroll to view the full screenshot. Press Esc or close from the top right.',
  'InkTag 为兼容电子价签提供纸张般的图片、日历与时钟展示，支持设备管理和无线更新。可用功能以当前设备为准。': 'InkTag turns compatible electronic shelf labels into paper-like picture, calendar and clock displays, with device management and wireless updates. Features depend on the device.',
}

export function translate(text: string, language: 'zh-CN' | 'en') { return language === 'en' ? english[text] ?? text : text }
export function useTranslation() {
  const language = useLanguage()
  return (text: string) => translate(text, language)
}
