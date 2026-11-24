import { clone, documentPageMode, type Document, type Widget, type TextRun } from '../../../../packages/inktag-editor-core/src/index'
import { deviceBatteryPercent } from '../../device-gui/deviceGuiComponents'
import { previewDefaultTimestamp } from '../../design/layoutPreviewTime'
import { nativeDocument } from './nativeBridge'
import { richTextLayout } from './richTextRenderer'
import { rememberTextPreset } from './textPresetState'
import presets from './textPresets.json'

const textPresets = presets.filter(preset => 'group' in preset && preset.group === 'device')
export const deviceControls = [
  { id: 'battery-icon', name: '电池图标' },
  ...textPresets.map(({ id, name }) => ({ id, name })),
]

/** Existing text/native scene data only: no new document kind or device format. */
export function createDeviceControl(id:string,doc:Document):Widget {
  if(documentPageMode(doc)==='static') throw new Error('设备控件仅限动态页面')
  const availableHeight = doc.height - (doc.footerInset ?? 18)
  let w:Widget
  if(id === 'battery-icon') {
    const source = nativeDocument('calendar',doc.width,doc.height).widgets.find(widget => widget.native?.key === 'battery:0')
    if(!source) throw new Error('此尺寸暂不支持电池图标')
    w = clone(source)
    w.native!.sample = { voltage: 3000 }
  } else {
    const preset = textPresets.find(item => item.id === id)
    if(!preset) throw new Error('未知设备控件')
    w = { id: '', name: preset.name, kind: 'text', x: 0, y: 0, width: doc.width, height: availableHeight,
      style: {fontSize:12,fontId:'wqy9-lunar',bold:false,color:'#000000',align:'left'},
      sample: {timestamp:previewDefaultTimestamp.v1,bluetoothName:'IKT_GR_3A57',deviceName:'InkTag',temperature:24,voltage:3000,batteryPercent:deviceBatteryPercent(3000)},
      cells: [[{text:preset.runs.map(run => run.text).join(''),richText:{runs:clone(preset.runs) as TextRun[],padding:2,verticalAlign:'middle'}}]] }
    const content = w.cells[0][0].richText!
    const layout = richTextLayout(w,content)
    w.width = Math.max(1,Math.min(doc.width,Math.ceil(layout.width)))
    w.height = Math.max(1,Math.min(availableHeight,Math.ceil(layout.height)))
    w.cells[0][0].richText = rememberTextPreset(content,w.style,preset.id)
  }
  let index = 1
  while(doc.widgets.some(widget => widget.id === `device-${id}-${index}`)) index++
  w.id = `device-${id}-${index}`
  w.x = Math.floor((doc.width-w.width)/2)
  w.y = Math.floor((availableHeight-w.height)/2)
  return w
}
