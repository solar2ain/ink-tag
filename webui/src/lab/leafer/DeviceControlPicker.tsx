import { useId, useRef, useState } from 'react'
import { BatteryMedium, Bluetooth, ChevronDown, Gauge, Monitor, Thermometer, Zap } from 'lucide-react'
import { deviceControls } from './deviceControls'

const icons = { 'battery-icon':BatteryMedium, bluetooth:Bluetooth, 'device-name':Monitor,
  temperature:Thermometer, voltage:Zap, 'battery-percent':Gauge }

export function DeviceControlPicker({ add }:{add:(id:string)=>void}) {
  const [open,setOpen] = useState(false)
  const id = useId(), trigger = useRef<HTMLButtonElement>(null)
  return <div className="device-control-picker" onKeyDown={event => {
    if(event.key === 'Escape' && open) { event.stopPropagation(); setOpen(false); trigger.current?.focus() }
  }}>
    <button ref={trigger} className="device-control-trigger" aria-expanded={open} aria-controls={id} onClick={() => setOpen(!open)}>
      <Monitor /><span>设备控件</span><ChevronDown aria-hidden="true" className={`device-control-chevron${open?' expanded':''}`} />
    </button>
    {open && <div id={id} role="group" aria-label="设备状态控件" className="device-control-menu">
      {deviceControls.map(item => {const Icon = icons[item.id as keyof typeof icons] ?? Monitor; return <button key={item.id} onClick={() => {add(item.id);setOpen(false)}}><Icon /><span>{item.name}</span></button>})}
      <p>预览数据仅作用于当前控件。动态页面暂不支持设备保存或自动推送。</p>
    </div>}
  </div>
}
