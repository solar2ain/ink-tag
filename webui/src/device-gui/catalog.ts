import {
  defaultDeviceGuiStyle,
  type DeviceGuiDocumentKind,
  type DeviceGuiAppearance,
  type DeviceGuiFrame,
  type DeviceGuiSizeClass,
  type DeviceGuiWidgetData,
  type DeviceGuiWidgetKind,
} from './model'

export interface DeviceGuiWidgetDefinition {
  kind: DeviceGuiWidgetKind
  label: string
  description: string
  category: 'date-time' | 'device' | 'structure' | 'content'
  documentKinds: DeviceGuiDocumentKind[]
  textOnly: boolean
  numericOnly: boolean
  delivery: 'native' | 'raster'
  defaultData: DeviceGuiWidgetData
  appearances: ReadonlyArray<{ id: DeviceGuiAppearance; label: string }>
  sizes: Record<DeviceGuiSizeClass, Pick<DeviceGuiFrame, 'width' | 'height'>>
}

export const deviceGuiWidgetCatalog: DeviceGuiWidgetDefinition[] = [
  widget('inktag-brand', 'InkTag 标识', '随屏幕颜色变化的品牌色块与字标', 'content', false, false, {}, [96, 22], [170, 40], [250, 56]),
  widget('device-info-details', '设备信息', '蓝牙名、屏幕信息、版本与产品等级', 'device', false, false, { showTitle: true, showBluetooth: true, showScreen: true, showActivation: true }, [76, 82], [130, 150], [220, 240]),
  widget('device-info-footer', '信息页脚', 'InkTag 显示类型与当前面板颜色', 'device', false, false, {}, [112, 18], [220, 28], [360, 36]),
  widget('qr-code', '二维码', '固件、WebUI 或自定义地址二维码', 'content', false, false, { qrPurpose: 'firmware', showLabel: true }, [76, 90], [120, 138], [190, 208]),
  widget('status-bar', '紧凑状态栏', '日期、电压、温度和电池状态', 'device', false, false, { dateFormat: 'ymd', showValue: true }, [112, 22], [220, 28], [360, 36]),
  widget('date-header', '日期页头', '年份、月份、农历、周数和电源状态', 'date-time', false, false, { showLunar: true, showValue: true }, [112, 28], [380, 60], [720, 78]),
  widget('clock-footer', '时钟页脚', '星期、农历、周数、节气和倒计时', 'date-time', false, false, { appearance: 'default', countdownDate: '2026-10-14' }, [112, 28], [320, 58], [620, 74], [['default', '双栏'], ['pill', '胶囊']]),
  widget('board-name', '板卡型号', '设备回传的板卡型号', 'device', true, false, {}, [72, 20], [130, 28], [200, 36]),
  widget('activation-status', '激活状态', 'InkTag 或 Pro 固件状态', 'device', false, false, {}, [44, 15], [64, 20], [96, 24]),
  widget('calendar', '月历', '日期网格、农历与节假日', 'date-time', false, false, { appearance: 'grid', weekStart: 1, showLunar: true, showHolidays: true, showWeekday: true }, [120, 150], [320, 210], [600, 350], [['grid', '网格'], ['minimal', '无框']]),
  widget('digital-clock', '数字时钟', '设备时间，12/24 小时制', 'date-time', false, true, { timeFormat: '24h' }, [112, 38], [220, 72], [400, 116]),
  widget('analog-clock', '表盘时钟', '低成本表盘与指针', 'date-time', false, false, {}, [88, 88], [160, 160], [260, 260]),
  widget('date', '日期', '年月日、月日、星期或大日期', 'date-time', true, false, { appearance: 'default', dateFormat: 'ymd' }, [112, 22], [180, 30], [260, 42], [['default', '设备字库'], ['seven-segment', '七段大日期']]),
  widget('year', '年份', '四位年份', 'date-time', true, true, {}, [56, 22], [88, 30], [130, 42]),
  widget('lunar', '农历', '农历日期与生肖信息', 'date-time', true, false, { lunarFormat: 'date' }, [112, 22], [180, 30], [280, 42]),
  widget('week-number', '周数', 'ISO 周数', 'date-time', true, false, {}, [70, 20], [96, 28], [140, 38]),
  widget('countdown', '倒计时', '距离目标日期的天数', 'date-time', true, false, { appearance: 'default', countdownDate: '2027-01-01' }, [112, 22], [180, 56], [280, 104], [['default', '文字'], ['seven-segment', '七段数字']]),
  widget('weekday-strip', '星期条', '独立星期标题，可组合月历与侧栏', 'date-time', false, false, { appearance: 'band', weekStart: 0 }, [112, 20], [220, 26], [360, 34], [['band', '反色条'], ['minimal', '文字']]),
  widget('battery', '电池', '电池图标与可选百分比', 'device', false, false, { showValue: true }, [58, 20], [84, 28], [120, 38]),
  widget('voltage', '电池电压', '实时电压值', 'device', true, false, {}, [64, 20], [94, 28], [130, 38]),
  widget('temperature', '温度', '设备温度值', 'device', true, false, {}, [60, 20], [88, 28], [124, 38]),
  widget('device-name', '设备名称', '用户为设备设置的名称', 'device', true, false, {}, [112, 22], [180, 30], [280, 42]),
  widget('bluetooth-name', '蓝牙名称', '设备广播名称', 'device', true, false, {}, [112, 22], [180, 30], [280, 42]),
  widget('divider', '分隔线', '横向或纵向结构线', 'structure', false, false, { orientation: 'horizontal' }, [112, 12], [220, 12], [360, 12]),
  widget('agenda-list', '待办列表', '有界固定事项，可编号或勾选', 'structure', false, false, { appearance: 'numbered', titleText: '待办事项', subtitleText: '7月5日  星期六', text: '09:00|接见客户\n12:00|安排午餐\n15:00|项目会议', footerText: '宜：接口出错' }, [112, 120], [220, 180], [360, 260], [['numbered', '编号'], ['checkbox', '勾选框']], 'raster'),
  widget('schedule-table', '课表', '有界固定表格，使用竖线分隔单元格', 'structure', false, false, { appearance: 'grid', titleText: '课程表', text: '节次|周一|周二|周三|周四|周五\n第1节|语文|数学|英语|物理|体育\n第2节|数学|英语|语文|历史|美术', columns: 6, headerRows: 1 }, [112, 120], [320, 190], [600, 320], [['grid', '网格']], 'raster'),
  widget('image-panel', '固定图片', '固件资源中的固定图片区域', 'content', false, false, { resourceId: 0 }, [96, 64], [180, 120], [300, 200]),
  widget('canvas-region', 'Canvas 区域', '由受限绘图命令填充的区域', 'content', false, false, {}, [112, 72], [220, 140], [360, 240]),
  widget('text', '文本', '固定文字与日期、农历、温度等变量可自由组合；WQY 缺字时会提示', 'content', true, false, { text: 'InkTag' }, [112, 22], [180, 30], [280, 42]),
  widget('raster-text', '固定栅格文字', '使用 Web 字体渲染到静态背景，不由设备动态排版', 'content', false, false, { text: '固定文字', rasterFontId: 'system-sans' }, [112, 28], [180, 44], [280, 64], [['default', '默认']], 'raster'),
]

function widget(
  kind: DeviceGuiWidgetKind,
  label: string,
  description: string,
  category: DeviceGuiWidgetDefinition['category'],
  textOnly: boolean,
  numericOnly: boolean,
  defaultData: DeviceGuiWidgetData,
  compact: [number, number],
  normal: [number, number],
  large: [number, number],
  appearances: ReadonlyArray<readonly [DeviceGuiAppearance, string]> = [['default', '默认']],
  delivery: DeviceGuiWidgetDefinition['delivery'] = 'native',
): DeviceGuiWidgetDefinition {
  return {
    kind,
    label,
    description,
    category,
    documentKinds: ['device-info', 'calendar', 'clock', 'agenda', 'schedule'],
    textOnly,
    numericOnly,
    delivery,
    defaultData,
    appearances: appearances.map(([id, label]) => ({ id, label })),
    sizes: {
      compact: { width: compact[0], height: compact[1] },
      normal: { width: normal[0], height: normal[1] },
      large: { width: large[0], height: large[1] },
    },
  }
}

export function widgetDefinition(kind: DeviceGuiWidgetKind) {
  const definition = deviceGuiWidgetCatalog.find((item) => item.kind === kind)
  if (!definition) throw new Error(`Unknown device GUI widget: ${kind}`)
  return definition
}

export function defaultStyleForWidget(kind: DeviceGuiWidgetKind, variant: DeviceGuiSizeClass) {
  const numeric = widgetDefinition(kind).numericOnly
  return {
    ...defaultDeviceGuiStyle,
    fontId: numeric
      ? variant === 'large' ? 'helv-bold-18-num' as const : 'helv-bold-14-num' as const
      : variant === 'compact' ? 'wqy9-lunar' as const : 'wqy12-lunar' as const,
    align: kind === 'digital-clock' || kind === 'date' || kind === 'year' || kind === 'inktag-brand' || kind === 'qr-code'
      ? 'center' as const
      : defaultDeviceGuiStyle.align,
  }
}

export const deviceGuiCategoryLabels: Record<DeviceGuiWidgetDefinition['category'], string> = {
  'date-time': '日期与时间',
  device: '设备状态',
  structure: '结构与列表',
  content: '内容区域',
}
