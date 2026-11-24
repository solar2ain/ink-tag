import { useId } from 'react'

type Props = {
  visible: boolean
  saving: boolean
  disabled: boolean
  onChange: (visible: boolean) => void
}

export function BrandVisibilitySetting({ visible, saving, disabled, onChange }: Props) {
  const hintId = useId()
  return <div className="display-setting">
    <div className="display-setting-head">
      <label className="setting-check">
        <input type="checkbox" checked={visible} disabled={disabled || saving}
          aria-describedby={hintId} onChange={(event) => onChange(event.target.checked)} />
        显示 InkTag Pro 标识
      </label>
      {saving && <span className="setting-saving" role="status">正在保存…</span>}
    </div>
    <p id={hintId}>关闭后不叠加全局标识，图片不变。</p>
  </div>
}
