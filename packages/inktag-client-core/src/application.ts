import { CapabilityV2, DisplayModeV2 } from './generated/commands-v2'

export type ClientPageKey = 'overview' | 'image' | 'create' | 'library' | 'find' | 'device'
export type ClientUiIcon =
  | 'activity' | 'cpu' | 'edit-3' | 'file-up' | 'flask-conical' | 'image' | 'images' | 'key-round'
  | 'layout-dashboard' | 'logs' | 'map-pinned' | 'monitor-cog' | 'nfc' | 'panel-top'
  | 'panels-top-left' | 'upload' | 'sliders-horizontal' | 'circle-help'
export type ClientNavIcon = Extract<ClientUiIcon, 'layout-dashboard' | 'image' | 'panels-top-left' | 'images' | 'map-pinned' | 'monitor-cog'>
export type ClientDeviceTabKey = 'overview' | 'screen' | 'hardware' | 'behavior' | 'activation' | 'ota' | 'logs' | 'test' | 'preferences' | 'about'
export type ClientLibraryTabKey = 'pages' | 'assets' | 'drafts'

export interface ClientTabItem<Key extends string> {
  key: Key
  label: string
  icon: ClientUiIcon
}

export interface ClientNavigationItem {
  key: ClientPageKey
  path: string
  label: string
  title: string
  description: string
  icon: ClientNavIcon
  proOnly?: boolean
}

/** Product navigation metadata shared by the React and WeChat shells. */
export const CLIENT_NAVIGATION: readonly ClientNavigationItem[] = [
  { key: 'overview', path: '/', label: '总览', title: '工作台', description: '', icon: 'layout-dashboard' },
  { key: 'image', path: '/image', label: '图片', title: '图片', description: '裁切、预览并发送适合当前墨水屏的图片。', icon: 'image' },
  { key: 'create', path: '/create', label: '创作', title: '创作', description: '从横屏模板开始，保存草稿或直接发送。', icon: 'panels-top-left' },
  { key: 'library', path: '/library', label: '资源', title: '资源', description: '管理本地页面、图片素材和设备图槽。', icon: 'images' },
  { key: 'find', path: '/find', label: '查找', title: '查找', description: '查看已授权设备的位置记录。', icon: 'map-pinned', proOnly: true },
  { key: 'device', path: '/device', label: '设置', title: '设置', description: '查看设备、屏幕、硬件能力和常用操作。', icon: 'monitor-cog' },
]

/** Page structure shared by the React and native Mini Program renderers. */
export const CLIENT_DEVICE_TABS: readonly ClientTabItem<ClientDeviceTabKey>[] = [
  { key: 'overview', label: '概览', icon: 'activity' },
  { key: 'screen', label: '显示', icon: 'panel-top' },
  { key: 'hardware', label: '板卡', icon: 'cpu' },
  { key: 'behavior', label: '交互', icon: 'nfc' },
  { key: 'activation', label: '激活', icon: 'key-round' },
  { key: 'ota', label: '固件', icon: 'file-up' },
  { key: 'logs', label: '日志', icon: 'logs' },
  { key: 'test', label: '测试', icon: 'flask-conical' },
  { key: 'preferences', label: '偏好', icon: 'sliders-horizontal' },
  { key: 'about', label: '关于', icon: 'circle-help' },
]

export const CLIENT_LIBRARY_TABS: readonly ClientTabItem<ClientLibraryTabKey>[] = [
  { key: 'pages', label: '页面', icon: 'images' },
  { key: 'assets', label: '素材', icon: 'upload' },
  { key: 'drafts', label: '布局草稿', icon: 'edit-3' },
]

export function clientNavigation(includeProPages: boolean): ClientNavigationItem[] {
  return CLIENT_NAVIGATION.filter((item) => includeProPages || !item.proOnly)
}

export type ClientPlatform = 'static' | 'standalone' | 'server' | 'miniapp'

/** Find requires a backend and is deliberately absent in the native Mini Program. */
export function clientNavigationFor(platform: ClientPlatform, includeProPages = false): ClientNavigationItem[] {
  return clientNavigation(includeProPages).filter((item) => platform !== 'miniapp' || item.key !== 'find')
}

export function clientPageMetadata(key: ClientPageKey): ClientNavigationItem {
  const page = CLIENT_NAVIGATION.find((item) => item.key === key)
  if (!page) throw new RangeError(`unknown client page: ${key}`)
  return page
}

export interface V2ClientAvailability {
  directImage: boolean
  resources: boolean
  time: boolean
  calendar: boolean
  clock: boolean
  deviceInfo: boolean
  clear: boolean
}

/** Convert the negotiated V2 masks into UI-facing availability in one place. */
export function deriveV2ClientAvailability(capabilityMask: number, displayModeMask: number): V2ClientAvailability {
  const displayMode = (mode: number) => (displayModeMask & (1 << mode)) !== 0
  return {
    directImage: (capabilityMask & CapabilityV2.DIRECT_TRANSFER) !== 0,
    resources: (capabilityMask & CapabilityV2.RESOURCES) !== 0,
    time: (capabilityMask & CapabilityV2.TIME) !== 0,
    calendar: displayMode(DisplayModeV2.CALENDAR),
    clock: displayMode(DisplayModeV2.CLOCK),
    deviceInfo: displayMode(DisplayModeV2.DEVICE_INFO),
    clear: displayMode(DisplayModeV2.CLEAR),
  }
}
