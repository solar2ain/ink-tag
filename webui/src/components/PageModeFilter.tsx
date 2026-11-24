import { Layers } from 'lucide-react'
import { pageModeLabels, type PageMode } from '../data/pageMode'

export function PageModeFilter({label,value,change}:{label:string;value:PageMode|'all';change:(value:PageMode|'all')=>void}) {
  return <label className="template-filter-control"><Layers /><span>页面类型</span>
    <select aria-label={label} value={value} onChange={event=>change(event.target.value as PageMode|'all')}>
      <option value="all">全部页面类型</option>
      {Object.entries(pageModeLabels).map(([mode,text])=><option key={mode} value={mode}>{text}</option>)}
    </select>
  </label>
}
