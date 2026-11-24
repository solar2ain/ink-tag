import { FeedbackNotice, useFeedbackMessage } from '../components/FeedbackNotice'
import { AlignHorizontalDistributeCenter, CalendarClock, CaseSensitive, CircleDot, Eraser, GalleryHorizontalEnd, Gauge, Grid3X3, Lightbulb, MonitorCheck, Palette, Pause, Play, Square, X } from 'lucide-react'
import { useEffect, useRef, useState } from 'react'
import { PageHeader } from '../components/PageHeader'
import { paletteForColor } from '../image/processing'
import { inktagClient } from '../protocol/client'
import { DisplayActionV2, DisplayModeV2, TestOperationV2 } from '../protocol/generatedCommandsV2'
import { canvasSizeForScreen } from '../protocol/screen'
import { localDateTimeInputValue } from '../protocol/timeTest'
import { TEST_SLIDESHOW_MINUTES, slideshowIntervalSeconds } from '../slideshow/interval'
import { useDeviceStore } from '../store/device'

type TestKind = 'screen' | 'time' | 'slideshow' | 'ghost' | 'button' | 'led'
type ScreenTestKind = 'comprehensive' | 'palette' | 'font' | 'bounds' | 'grid'
type ButtonGestureTest = 'buttonSingle' | 'buttonDouble' | 'buttonLong'

const screenTests = [
  { id: 'comprehensive', icon: MonitorCheck, name: '综合测试屏', description: '一次检查颜色、字体、方向、边界、细线与像素图案。' },
  { id: 'palette', icon: Palette, name: '颜色与灰阶', description: '输出当前屏幕支持的全部颜色和渐变级别。' },
  { id: 'font', icon: CaseSensitive, name: '字体样张', description: '中文、英文、数字与符号的字号组合。' },
  { id: 'bounds', icon: AlignHorizontalDistributeCenter, name: '方向与边界', description: '四边标记、坐标原点、旋转和镜像检查。' },
  { id: 'grid', icon: Grid3X3, name: '像素网格', description: '棋盘格、细线、反色块与坏点定位。' },
] as const

const tests = [
  { id: 'screen', icon: MonitorCheck, name: '屏幕测试', description: '选择综合、颜色、字体、边界或像素网格测试。' },
  { id: 'ghost', icon: Eraser, name: '残影消除', description: '执行有界的完整黑白清洁循环，最终恢复白屏。' },
  { id: 'time', icon: CalendarClock, name: '日期与时间', description: '调整测试时间，并刷新设备上的日历或时钟页面。' },
  { id: 'slideshow', icon: GalleryHorizontalEnd, name: '轮播测试', description: '用短间隔验证设备图槽轮播；日常轮播仍限制为 30 分钟以上。' },
  { id: 'button', icon: CircleDot, name: '按键模拟', description: '模拟单击、双击或长按，执行已保存的按键动作。' },
  { id: 'led', icon: Lightbulb, name: '灯效测试', description: '测试红、绿、蓝通道以及快闪、慢闪和三色轮闪。' },
] as const

function createCanvas(width: number, height: number) {
  const canvas = document.createElement('canvas')
  canvas.width = width
  canvas.height = height
  const context = canvas.getContext('2d')!
  context.fillStyle = '#fff'
  context.fillRect(0, 0, width, height)
  context.fillStyle = '#111'
  context.strokeStyle = '#111'
  return { canvas, context }
}

export function testPatternPalette(color: number) {
  return paletteForColor(color).map(({ rgb }) => `rgb(${rgb.join(',')})`)
}

function paletteCanvas(width: number, height: number, color: number) {
  const { canvas, context } = createCanvas(width, height)
  const colors = testPatternPalette(color)
  colors.forEach((value, index) => {
    const x = Math.floor(index * width / colors.length)
    const next = Math.floor((index + 1) * width / colors.length)
    context.fillStyle = value
    context.fillRect(x, 0, next - x, height * .72)
    context.fillStyle = index === 0 ? '#fff' : '#111'
    context.font = `bold ${Math.max(10, Math.floor(width / 28))}px sans-serif`
    context.fillText(String(index + 1), x + 8, 24)
  })
  context.fillStyle = '#fff'
  context.fillRect(0, height * .72, width, height * .28)
  context.fillStyle = '#111'
  context.font = `bold ${Math.max(12, Math.floor(width / 18))}px sans-serif`
  context.fillText('INKTAG COLOR TEST', width * .05, height * .87)
  return canvas
}

function fontCanvas(width: number, height: number) {
  const { canvas, context } = createCanvas(width, height)
  const lines = ['InkTag 字体测试', '天地玄黄  ABC xyz', '0123456789  12:34', '，。！？ @ # % &']
  const sizes = [Math.max(20, width * .085), Math.max(15, width * .055), Math.max(12, width * .045), Math.max(10, width * .035)]
  let y = height * .15
  lines.forEach((line, index) => {
    context.font = `${index === 0 ? 'bold ' : ''}${Math.floor(sizes[index])}px "Microsoft YaHei", sans-serif`
    context.fillText(line, width * .06, y)
    y += height * .22
  })
  return canvas
}

function boundsCanvas(width: number, height: number) {
  const { canvas, context } = createCanvas(width, height)
  const margin = Math.max(3, Math.floor(Math.min(width, height) * .025))
  context.lineWidth = Math.max(2, margin / 2)
  context.strokeRect(margin, margin, width - margin * 2, height - margin * 2)
  context.font = `bold ${Math.max(12, Math.floor(width / 18))}px sans-serif`
  context.fillText('0,0  TL', margin * 2, margin * 3)
  const labels = [['TR', width - margin * 5, margin * 3], ['BL', margin * 2, height - margin * 2], ['BR', width - margin * 5, height - margin * 2]] as const
  labels.forEach(([label, x, y]) => context.fillText(label, x, y))
  context.beginPath(); context.moveTo(width / 2, margin); context.lineTo(width / 2, height - margin); context.moveTo(margin, height / 2); context.lineTo(width - margin, height / 2); context.stroke()
  context.fillStyle = '#d92e2e'
  context.fillRect(width * .46, height * .46, width * .08, height * .08)
  return canvas
}

function gridCanvas(width: number, height: number) {
  const { canvas, context } = createCanvas(width, height)
  const cell = Math.max(4, Math.floor(Math.min(width, height) / 18))
  for (let y = 0; y < height; y += cell) for (let x = 0; x < width; x += cell) {
    if (((x / cell) + (y / cell)) % 2 === 0) context.fillRect(x, y, cell, cell)
  }
  context.fillStyle = '#fff'; context.fillRect(width * .18, height * .36, width * .64, height * .28)
  context.strokeStyle = '#111'; context.lineWidth = 1
  for (let x = width * .2; x <= width * .8; x += Math.max(2, width / 80)) { context.beginPath(); context.moveTo(x, height * .38); context.lineTo(x, height * .62); context.stroke() }
  return canvas
}

function comprehensiveCanvas(width: number, height: number, color: number) {
  const { canvas, context } = createCanvas(width, height)
  const colors = testPatternPalette(color)
  const margin = Math.max(3, Math.round(Math.min(width, height) * .018))
  const top = Math.max(30, Math.round(height * .23))
  const bottom = Math.round(height * .76)
  const swatchWidth = (width - margin * 2) / colors.length

  context.lineWidth = Math.max(2, Math.round(Math.min(width, height) / 120))
  context.strokeRect(margin, margin, width - margin * 2, height - margin * 2)
  colors.forEach((value, index) => {
    context.fillStyle = value
    context.fillRect(Math.round(margin + index * swatchWidth), margin, Math.ceil(swatchWidth), top - margin)
  })
  context.fillStyle = '#111'
  context.textBaseline = 'top'
  context.font = `bold ${Math.max(12, Math.floor(width / 18))}px "Microsoft YaHei", sans-serif`
  context.fillText('InkTag 综合测试', margin * 3, top + margin)
  context.font = `${Math.max(10, Math.floor(width / 28))}px "Microsoft YaHei", sans-serif`
  context.fillText('天地玄黄  ABC xyz  0123456789', margin * 3, top + margin + Math.max(18, width / 15))
  context.font = `bold ${Math.max(11, Math.floor(width / 22))}px sans-serif`
  context.fillText('12:34  Aa  #&%  1px', margin * 3, top + margin + Math.max(36, width / 8.5))

  const patternTop = Math.max(top + Math.round(height * .36), bottom - Math.round(height * .13))
  context.lineWidth = 1
  for (let x = margin * 2; x < width * .52; x += Math.max(2, Math.round(width / 90))) {
    context.beginPath(); context.moveTo(x, patternTop); context.lineTo(x, bottom); context.stroke()
  }
  const cell = Math.max(3, Math.round(Math.min(width, height) / 35))
  for (let y = patternTop; y < bottom; y += cell) for (let x = width * .57; x < width - margin * 2; x += cell) {
    if ((Math.floor(x / cell) + Math.floor(y / cell)) % 2 === 0) context.fillRect(x, y, cell, cell)
  }
  context.lineWidth = Math.max(1, Math.round(Math.min(width, height) / 150))
  context.beginPath()
  context.moveTo(width / 2, top); context.lineTo(width / 2, bottom)
  context.moveTo(margin, height / 2); context.lineTo(width - margin, height / 2)
  context.stroke()
  context.font = `bold ${Math.max(9, Math.floor(width / 34))}px sans-serif`
  context.fillText('TL', margin * 2, bottom + margin)
  context.fillText('TR', width - margin * 6, bottom + margin)
  context.fillText('BL', margin * 2, height - margin * 5)
  context.fillText('BR', width - margin * 6, height - margin * 5)
  return canvas
}

export function LabPage() {
  return <div className="page lab-page"><PageHeader title="测试" /><LabPanel /></div>
}

export function LabPanel() {
  const { connection, deviceId, screen, action, capabilities, settingsCapabilities, imageStoreInfo } = useDeviceStore()
  const [message, setMessage, messageRevision] = useFeedbackMessage('')
  const [timeView, setTimeView] = useState<'calendar' | 'clock'>('clock')
  const [testDateTime, setTestDateTime] = useState(() => localDateTimeInputValue())
  const [timeBusy, setTimeBusy] = useState(false)
  const [selectedTest, setSelectedTest] = useState<TestKind | null>(null)
  const [screenTest, setScreenTest] = useState<ScreenTestKind>('comprehensive')
  const [screenBusy, setScreenBusy] = useState(false)
  const [slideshowMinutes, setSlideshowMinutes] = useState(TEST_SLIDESHOW_MINUTES)
  const [ghostCycles, setGhostCycles] = useState(5)
  const [ghostBusy, setGhostBusy] = useState(false)
  const [ledRunning, setLedRunning] = useState(false)
  const ledSequence = useRef(0)
  const timeMode = timeView === 'calendar' ? DisplayModeV2.CALENDAR : DisplayModeV2.CLOCK
  const baseReady = connection === 'connected' && action === 'idle'
  const [testInventory, setTestInventory] = useState<{ deviceId: string; operations: number[]; error: string } | null>(null)
  const inventory = connection === 'connected' && testInventory?.deviceId === deviceId ? testInventory : null
  const hasTest = (operation: number) => Boolean(inventory && !inventory.error && inventory.operations.includes(operation))
  const timeTestReady = baseReady && settingsCapabilities.protocolVersion === 1 && !timeBusy
  const timeShiftReady = timeTestReady && hasTest(TestOperationV2.TIME_SHIFT)
  const timeSetReady = timeTestReady && hasTest(TestOperationV2.TIME_SET)

  useEffect(() => {
    if (connection !== 'connected') { setTestInventory(null); return }
    if (action !== 'idle' || testInventory?.deviceId === deviceId) return
    let current = true
    void inktagClient.listTests().then(
      operations => { if (current) setTestInventory({ deviceId, operations, error: '' }) },
      error => { if (current) setTestInventory({ deviceId, operations: [], error: error instanceof Error ? error.message : String(error) }) },
    )
    return () => { current = false }
  }, [connection, deviceId, action, testInventory?.deviceId])

  useEffect(() => () => { ledSequence.current += 1 }, [])

  const writeLed = async (mask: number) => {
    await inktagClient.testLed(mask)
  }

  const runLedSolid = async (mask: number, name: string) => {
    const sequence = ++ledSequence.current
    setLedRunning(true)
    setMessage(`正在点亮${name}。`)
    try {
      await writeLed(mask)
      if (ledSequence.current === sequence) setMessage(`${name}已点亮；设备的有界测试保护可能自动熄灯。`)
    } catch (error) {
      if (ledSequence.current === sequence) {
        setLedRunning(false)
        setMessage(error instanceof Error ? error.message : String(error))
      }
    }
  }

  const runLedPattern = async (kind: 'fast' | 'slow' | 'rgb') => {
    const sequence = ++ledSequence.current
    setLedRunning(true)
    const halfPeriod = kind === 'fast' ? 120 : 240
    const masks = kind === 'rgb'
      ? [1, 0, 2, 0, 4, 0, 1, 0, 2, 0, 4, 0]
      : Array.from({ length: kind === 'fast' ? 8 : 6 }, (_, index) => index % 2 === 0 ? 4 : 0)
    setMessage(kind === 'fast' ? '正在测试蓝灯快闪。' : kind === 'slow' ? '正在测试蓝灯慢闪。' : '正在测试三色轮闪。')
    try {
      for (const mask of masks) {
        if (ledSequence.current !== sequence) return
        await writeLed(mask)
        await new Promise((resolve) => window.setTimeout(resolve, halfPeriod))
      }
      if (ledSequence.current === sequence) {
        await writeLed(0)
        setLedRunning(false)
        setMessage('状态灯测试完成，输出已停止。')
      }
    } catch (error) {
      if (ledSequence.current === sequence) {
        setLedRunning(false)
        setMessage(error instanceof Error ? error.message : String(error))
      }
    }
  }

  const stopLed = async () => {
    ledSequence.current += 1
    setLedRunning(false)
    try {
      await writeLed(0)
      setMessage('状态灯输出已停止。')
    } catch (error) {
      setMessage(error instanceof Error ? error.message : String(error))
    }
  }

  const runTimeShift = async (deltaSeconds: number) => {
    if (!timeShiftReady) return
    setTimeBusy(true)
    setMessage(`正在${deltaSeconds > 0 ? '前进' : '后退'}${Math.abs(deltaSeconds) === 86400 ? ' 1 天' : ' 1 分钟'}并刷新${timeView === 'calendar' ? '日历' : '时钟'}。`)
    try {
      await inktagClient.testTimeShift(deltaSeconds, timeMode)
      setMessage('测试时间已更新，设备刷新完成。')
    } catch (error) {
      setMessage(error instanceof Error ? error.message : String(error))
    } finally {
      setTimeBusy(false)
    }
  }

  const runTimeSet = async () => {
    if (!timeSetReady) return
    const value = new Date(testDateTime)
    if (Number.isNaN(value.getTime())) {
      setMessage('请输入有效的测试日期时间。')
      return
    }
    setTimeBusy(true)
    setMessage(`正在设置测试时间并刷新${timeView === 'calendar' ? '日历' : '时钟'}。`)
    try {
      await inktagClient.testTimeSet(value, timeMode)
      setMessage('测试日期时间已设置，设备刷新完成。')
    } catch (error) {
      setMessage(error instanceof Error ? error.message : String(error))
    } finally {
      setTimeBusy(false)
    }
  }

  const run = async (kind: ScreenTestKind | ButtonGestureTest) => {
    try {
      if (kind === 'buttonSingle' || kind === 'buttonDouble' || kind === 'buttonLong') {
        const gesture = kind === 'buttonSingle' ? 'single' : kind === 'buttonDouble' ? 'double' : 'long'
        await inktagClient.simulateButton(gesture)
        setMessage(`已模拟按键${gesture === 'single' ? '单击' : gesture === 'double' ? '双击' : '长按'}，请观察设备动作和日志。`)
        return
      }
      if (!screen) throw new Error('请先读取设备屏幕配置。')
      const { width, height } = canvasSizeForScreen(screen)
      const canvas = kind === 'comprehensive' ? comprehensiveCanvas(width, height, screen.color)
        : kind === 'palette' ? paletteCanvas(width, height, screen.color)
        : kind === 'font' ? fontCanvas(width, height)
          : kind === 'bounds' ? boundsCanvas(width, height) : gridCanvas(width, height)
      setMessage(`正在发送${screenTests.find((item) => item.id === kind)?.name}`)
      await inktagClient.sendCanvas(canvas, `test_${kind}`)
      setMessage('测试内容已发送，设备正在刷新。')
    } catch (error) {
      setMessage(error instanceof Error ? error.message : String(error))
    }
  }
  const needsScreen = (kind: TestKind) => kind === 'screen'
  const hasButtonProtocol = settingsCapabilities.buttonGestureMask !== 0
  const hasGhostClean = (settingsCapabilities.displayActionMask & (1 << DisplayActionV2.GHOST_CLEAN)) !== 0
  const testSupported = (kind: TestKind) => {
    if (kind === 'slideshow') return capabilities.imageStore
    if (kind === 'time') return hasTest(TestOperationV2.TIME_SHIFT) || hasTest(TestOperationV2.TIME_SET)
    if (kind === 'button') return hasButtonProtocol && hasTest(TestOperationV2.INPUT_SIMULATE)
    if (kind === 'led') return capabilities.led === 'rgb' && settingsCapabilities.protocolVersion === 1 && hasTest(TestOperationV2.LED_CHANNEL)
    if (kind === 'ghost') return hasGhostClean
    return true
  }
  const selected = selectedTest ? tests.find((test) => test.id === selectedTest) : null
  const SelectedIcon = selected?.icon

  return <section>
    <div className="panel-title"><div><h2>屏幕与硬件测试</h2><p>屏幕测试在弹窗中选择并启动；只读运行诊断与走时诊断位于设备日志。</p></div></div>
    {message && <FeedbackNotice className="library-message" noticeKey={messageRevision}>{message}</FeedbackNotice>}
    {inventory?.error && <div className="library-message" role="status">无法读取设备测试项目。<button className="button secondary" type="button" disabled={!baseReady} onClick={() => setTestInventory(null)}>重新读取</button></div>}
    <div className="test-grid">{tests.map(({ id, icon: Icon, name, description }) => {
      const supported = testSupported(id)
      const ready = supported && (id === 'time' ? timeTestReady : id === 'slideshow' ? baseReady && Boolean(imageStoreInfo?.playable) : baseReady && (!needsScreen(id) || !!screen))
      const imageHint = supported && id === 'slideshow' && baseReady
        ? imageStoreInfo == null ? '设备图槽信息尚未读到。'
          : imageStoreInfo.playable === 0 ? '请先上传当前屏幕可播放的图片。' : undefined
        : undefined
      const usesTestList = ['time', 'button', 'led'].includes(id)
      const listHint = usesTestList && !inventory ? '正在读取设备测试项目'
        : usesTestList && inventory?.error ? '未能读取设备测试项目，请重试' : undefined
      const unavailableReason = connection !== 'connected' ? '请先连接设备'
        : action !== 'idle' ? '设备操作进行中，请稍后'
          : !supported ? listHint ?? '当前设备不支持此测试'
            : needsScreen(id) && !screen ? '请先读取或配置屏幕'
              : id === 'time' && timeBusy ? '时间测试进行中，请稍后'
                : id === 'time' && settingsCapabilities.protocolVersion !== 1 ? '当前设备不支持日期与时间测试'
                  : imageHint
      return <button key={name} type="button" className={`test-card ${id === 'screen' ? 'featured' : ''}`} disabled={!ready} title={unavailableReason} aria-label={unavailableReason ? `${name}：${unavailableReason}` : name} onClick={() => setSelectedTest(id)}>
        <Icon /><div><h2>{name}</h2><p>{description}</p>{imageHint && <p className="test-card-hint">{imageHint}</p>}</div><span className="test-card-run" aria-hidden="true"><Play /></span>
      </button>
    })}</div>
    <div className="notice warning"><Gauge /><div><strong>屏幕测试会刷新设备</strong><span>综合、颜色、字体、边界和网格测试前请确认屏幕配置；硬件模拟不会改写板卡 GPIO。</span></div></div>
    {selected?.id === 'screen' && <div className="modal-backdrop" onMouseDown={(event) => { if (event.target === event.currentTarget && !screenBusy) setSelectedTest(null) }}>
      <section className="test-dialog" role="dialog" aria-modal="true" aria-labelledby="screen-test-title">
        <header><MonitorCheck /><div><h2 id="screen-test-title">屏幕测试</h2><p>选择测试图，点击开始后才会发送并刷新屏幕。</p></div><button className="icon-button" type="button" disabled={screenBusy} title="关闭" onClick={() => setSelectedTest(null)}><X /></button></header>
        <div className="screen-test-options">{screenTests.map(({ id, icon: Icon, name, description }) => <label key={id} className={screenTest === id ? 'selected' : ''}><input type="radio" name="screen-test" value={id} checked={screenTest === id} disabled={screenBusy} onChange={() => setScreenTest(id)} /><Icon /><span><strong>{name}</strong><small>{description}</small></span></label>)}</div>

        <footer><button className="button secondary" type="button" disabled={screenBusy} onClick={() => setSelectedTest(null)}>关闭</button><button className="button primary" type="button" disabled={!baseReady || !screen || screenBusy} onClick={() => void (async () => { setScreenBusy(true); try { await run(screenTest) } finally { setScreenBusy(false) } })()}><Play />{screenBusy ? '发送中' : '开始测试'}</button></footer>
      </section>
    </div>}
    {selected?.id === 'time' && <div className="modal-backdrop" onMouseDown={(event) => { if (event.target === event.currentTarget && !timeBusy) setSelectedTest(null) }}>
      <section className="test-dialog" role="dialog" aria-modal="true" aria-labelledby="test-dialog-title" aria-describedby="test-dialog-description">
        <header>{SelectedIcon && <SelectedIcon />}<div><h2 id="test-dialog-title">{selected.name}</h2><p id="test-dialog-description">{selected.description}</p></div><button className="icon-button" type="button" disabled={timeBusy} title="关闭" onClick={() => setSelectedTest(null)}><X /></button></header>
        <div className="time-test-dialog-body">
          <div className="segmented-control"><button type="button" className={timeView === 'calendar' ? 'active' : ''} onClick={() => setTimeView('calendar')}>日历</button><button type="button" className={timeView === 'clock' ? 'active' : ''} onClick={() => setTimeView('clock')}>时钟</button></div>
          <div className="time-shift-grid"><button disabled={!timeShiftReady} onClick={() => void runTimeShift(-86400)}>-1 天</button><button disabled={!timeShiftReady} onClick={() => void runTimeShift(86400)}>+1 天</button><button disabled={!timeShiftReady} onClick={() => void runTimeShift(-60)}>-1 分钟</button><button disabled={!timeShiftReady} onClick={() => void runTimeShift(60)}>+1 分钟</button></div>
          <label>指定测试日期与时间<input type="datetime-local" value={testDateTime} onChange={(event) => setTestDateTime(event.target.value)} /></label>
          <small>会修改设备时间并刷新页面，不会修改电脑系统时间。测试后请重新校时；烤机或走时测量期间请勿使用。</small>
        </div>
        <footer><button className="button secondary" type="button" disabled={timeBusy} onClick={() => setSelectedTest(null)}>关闭</button><button className="button primary" type="button" disabled={!timeSetReady} onClick={() => void runTimeSet()}><Play />设置并刷新</button></footer>
      </section>
    </div>}
    {selected?.id === 'slideshow' && <div className="modal-backdrop" onMouseDown={(event) => { if (event.target === event.currentTarget && action === 'idle') setSelectedTest(null) }}>
      <section className="test-dialog" role="dialog" aria-modal="true" aria-labelledby="slideshow-test-title" aria-describedby="slideshow-test-description">
        <header>{SelectedIcon && <SelectedIcon />}<div><h2 id="slideshow-test-title">{selected.name}</h2><p id="slideshow-test-description">{selected.description}</p></div><button className="icon-button" type="button" disabled={action !== 'idle'} title="关闭" onClick={() => setSelectedTest(null)}><X /></button></header>
        <div className="time-test-dialog-body">
          <label>测试间隔<input type="number" inputMode="numeric" min={TEST_SLIDESHOW_MINUTES} max="29" step="1" value={slideshowMinutes} onChange={(event) => setSlideshowMinutes(Number(event.target.value))} /></label>
          <small>测试间隔为 1–29 分钟。设备需要至少两张兼容的轮播图片。</small>
        </div>
        <footer><button className="button secondary" type="button" disabled={action !== 'slideshow'} onClick={() => void (async () => { try { await inktagClient.stopSlideshow(); setMessage('测试轮播已停止。') } catch (error) { setMessage(error instanceof Error ? error.message : String(error)) } })()}><Pause />停止轮播</button><button className="button primary" type="button" disabled={action !== 'idle' || !Number.isInteger(slideshowMinutes) || slideshowMinutes < 1 || slideshowMinutes > 29} onClick={() => void (async () => { try { await inktagClient.startSlideshow(slideshowIntervalSeconds(slideshowMinutes, true)); setMessage(`测试轮播已启动，间隔 ${slideshowMinutes} 分钟。`) } catch (error) { setMessage(error instanceof Error ? error.message : String(error)) } })()}><Play />开始测试</button></footer>
      </section>
    </div>}
    {selected?.id === 'ghost' && <div className="modal-backdrop" onMouseDown={(event) => { if (event.target === event.currentTarget && !ghostBusy) setSelectedTest(null) }}>
      <section className="test-dialog" role="dialog" aria-modal="true" aria-labelledby="ghost-test-title" aria-describedby="ghost-test-description">
        <header>{SelectedIcon && <SelectedIcon />}<div><h2 id="ghost-test-title">{selected.name}</h2><p id="ghost-test-description">{selected.description}</p></div><button className="icon-button" type="button" disabled={ghostBusy} title="关闭" onClick={() => setSelectedTest(null)}><X /></button></header>
        <div className="time-test-dialog-body">
          <label>清洁循环次数<input type="number" inputMode="numeric" min="1" max="10" step="1" value={ghostCycles} onChange={(event) => setGhostCycles(Number(event.target.value))} /></label>
          <small>每轮执行完整白→黑→白刷新；耗时、耗电并影响屏幕寿命，仅在确有残影时使用。</small>
        </div>
        <footer><button className="button secondary" type="button" disabled={!ghostBusy} onClick={() => void (async () => { try { await inktagClient.stop(); setMessage('已请求停止残影清洁。') } catch (error) { setMessage(error instanceof Error ? error.message : String(error)) } finally { setGhostBusy(false) } })()}><Square />停止</button><button className="button primary" type="button" disabled={!baseReady || ghostBusy || !Number.isInteger(ghostCycles) || ghostCycles < 1 || ghostCycles > 10} onClick={() => void (async () => { setGhostBusy(true); setMessage(`正在执行 ${ghostCycles} 次残影清洁循环。`); try { await inktagClient.testGhostClean(ghostCycles); setMessage('残影清洁完成，屏幕已恢复白色。') } catch (error) { setMessage(error instanceof Error ? error.message : String(error)) } finally { setGhostBusy(false) } })()}><Play />开始清洁</button></footer>
      </section>
    </div>}
    {selected?.id === 'button' && <div className="modal-backdrop" onMouseDown={(event) => { if (event.target === event.currentTarget && action === 'idle') setSelectedTest(null) }}>
      <section className="test-dialog" role="dialog" aria-modal="true" aria-labelledby="button-test-title" aria-describedby="button-test-description">
        <header>{SelectedIcon && <SelectedIcon />}<div><h2 id="button-test-title">按键模拟</h2><p id="button-test-description">选择一种按键手势，设备会执行对应的已保存动作。</p></div><button className="icon-button" type="button" disabled={action !== 'idle'} title="关闭" onClick={() => setSelectedTest(null)}><X /></button></header>
        <div className="time-test-dialog-body">
          <div className="time-shift-grid">
            <button disabled={!baseReady || !(settingsCapabilities.buttonGestureMask & 1)} onClick={() => void run('buttonSingle')}>单击</button>
            <button disabled={!baseReady || !(settingsCapabilities.buttonGestureMask & 2)} onClick={() => void run('buttonDouble')}>双击</button>
            <button disabled={!baseReady || !(settingsCapabilities.buttonGestureMask & 4)} onClick={() => void run('buttonLong')}>长按</button>
          </div>
          <small>模拟操作使用与实体按键相同的动作映射，不会修改按键配置。</small>
        </div>
        <footer><button className="button secondary" type="button" disabled={action !== 'idle'} onClick={() => setSelectedTest(null)}>关闭</button></footer>
      </section>
    </div>}
    {selected?.id === 'led' && <div className="modal-backdrop" onMouseDown={(event) => { if (event.target === event.currentTarget && !ledRunning) setSelectedTest(null) }}>
      <section className="test-dialog" role="dialog" aria-modal="true" aria-labelledby="led-test-title" aria-describedby="led-test-description">
        <header>{SelectedIcon && <SelectedIcon />}<div><h2 id="led-test-title">{selected.name}</h2><p id="led-test-description">{selected.description}</p></div><button className="icon-button" type="button" disabled={ledRunning} title="关闭" onClick={() => setSelectedTest(null)}><X /></button></header>
        <div className="time-test-dialog-body">
          <div className="time-shift-grid led-test-grid">
            <button disabled={!baseReady} onClick={() => void runLedSolid(1, '红灯')}><span className="led-test-dot red" />红灯</button>
            <button disabled={!baseReady} onClick={() => void runLedSolid(2, '绿灯')}><span className="led-test-dot green" />绿灯</button>
            <button disabled={!baseReady} onClick={() => void runLedSolid(4, '蓝灯')}><span className="led-test-dot blue" />蓝灯</button>
            <button disabled={!baseReady} onClick={() => void runLedPattern('fast')}>快闪</button>
            <button disabled={!baseReady} onClick={() => void runLedPattern('slow')}>慢闪</button>
            <button disabled={!baseReady} onClick={() => void runLedPattern('rgb')}>三色闪</button>
          </div>
          <small>快闪使用 120ms、慢闪使用 240ms；测试命令不修改已保存的灯效设置。</small>
        </div>
        <footer><button className="button secondary" type="button" disabled={connection !== 'connected'} onClick={() => void stopLed()}><Square />停止</button><button className="button primary" type="button" disabled={ledRunning} onClick={() => setSelectedTest(null)}>完成</button></footer>
      </section>
    </div>}
  </section>
}
