import { renderToStaticMarkup } from 'react-dom/server'
import { beforeEach, describe, expect, it } from 'vitest'
import { DeviceLifecycleDialog } from './DeviceLifecycleDialog'
import { useDeviceStore } from '../store/device'
describe('two clearing scopes', () => {
  beforeEach(() => useDeviceStore.getState().resetDevice())
  it('does not describe a data-preservation scope for an ordinary reboot', () => {
    const html = renderToStaticMarkup(<DeviceLifecycleDialog action="reset" onClose={() => undefined} />)
    expect(html).toContain('重启设备')
    expect(html).not.toContain('保留范围')
    expect(html).not.toContain('保留全部设置')
  })
  it('clears images without clearing settings, credentials, Pro or Find', () => {
    const html = renderToStaticMarkup(<DeviceLifecycleDialog action="clear-user-data" onClose={() => undefined} />)
    expect(html).toContain('清除图槽')
    expect(html).toContain('不刷新屏幕')
    expect(html).toContain('保留设置、密码、设备二维码、NFC、Pro、Find 和板型数据')
    expect(html).not.toContain('恢复出厂')
  })
  it('describes fresh original-password verification and password removal', () => {
    const html = renderToStaticMarkup(<DeviceLifecycleDialog action="reset-settings" onClose={() => undefined} />)
    expect(html).toContain('关闭设备密码')
    expect(html).toContain('必须输入原密码')
    expect(html).toContain('保留图片、Pro、Find、板型、工厂默认及校准')
    expect(html).not.toContain('激活码')
  })
})
