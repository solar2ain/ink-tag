// @vitest-environment jsdom
import { act, type PointerEvent as ReactPointerEvent } from 'react'
import { createRoot, type Root } from 'react-dom/client'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { useViewport } from './useViewport'

describe('selection and pan workspace boundaries', () => {
  let host: HTMLDivElement, root: Root, viewport: ReturnType<typeof useViewport>
  const blank = { hit: vi.fn(() => true), click: vi.fn() }
  const capture = vi.fn()
  const pointer = (x: number, y: number, buttons = 0) => ({
    clientX: x, clientY: y, button: 0, buttons, pointerId: 1, type: 'pointerup',
    currentTarget: viewport.stage.current!, target: viewport.stage.current!,
    preventDefault: vi.fn(), stopPropagation: vi.fn(),
  }) as unknown as ReactPointerEvent
  const move = async (x: number, y: number, buttons = 0) => act(async () => viewport.handlers.onPointerMoveCapture(pointer(x, y, buttons)))
  beforeEach(async () => {
    vi.stubGlobal('IS_REACT_ACT_ENVIRONMENT', true)
    vi.stubGlobal('ResizeObserver', class { observe() {} disconnect() {} })
    vi.spyOn(HTMLElement.prototype, 'getBoundingClientRect').mockReturnValue({ left: 0, top: 0, width: 800, height: 600 } as DOMRect)
    capture.mockClear(); blank.hit.mockReset().mockReturnValue(true); blank.click.mockClear()
    host = document.createElement('div'); document.body.append(host); root = createRoot(host)
    function Harness() { viewport = useViewport(400, 300, blank); return <div ref={viewport.stage}/> }
    await act(async () => root.render(<Harness/>))
    Object.assign(viewport.stage.current!, { setPointerCapture: capture, hasPointerCapture: () => true, releasePointerCapture: vi.fn() })
  })
  afterEach(async () => { await act(async () => root.unmount()); host.remove(); vi.restoreAllMocks(); vi.unstubAllGlobals() })
  it('never pans or shows a hand over empty white page pixels in selection mode', async () => {
    const before = viewport.view
    for (const [x,y] of [[200,150],[201,151],[400,300],[599,449]]) {
      await move(x,y); expect(viewport.overBlank).toBe(false)
      await act(async () => viewport.handlers.onPointerDownCapture(pointer(x,y,1)))
      await move(x+12,y+12,1)
      expect(viewport.view).toEqual(before)
    }
    expect(capture).not.toHaveBeenCalled()
  })
  it('pans the gray workspace and treats its stationary click as deselection', async () => {
    await move(100,100); expect(viewport.overBlank).toBe(true)
    await act(async () => viewport.handlers.onPointerDownCapture(pointer(100,100,1)))
    await move(120,130,1)
    await act(async () => viewport.handlers.onPointerUpCapture(pointer(120,130)))
    expect(viewport.view).toEqual({x:220,y:180,zoom:1}); expect(blank.click).not.toHaveBeenCalled()
    await act(async () => viewport.handlers.onPointerDownCapture(pointer(100,100,1)))
    await act(async () => viewport.handlers.onPointerUpCapture(pointer(100,100)))
    expect(blank.click).toHaveBeenCalledOnce()
  })
  it('hand mode pans over controls inside the page without selecting or editing them', async () => {
    blank.hit.mockReturnValue(false)
    await act(async () => viewport.setHand(true))
    const event = pointer(400,300,1)
    await act(async () => viewport.handlers.onPointerDownCapture(event))
    await move(430,320,1)
    await act(async () => viewport.handlers.onPointerUpCapture(pointer(430,320)))
    expect(viewport.panning).toBe(true); expect(event.stopPropagation).toHaveBeenCalled()
    expect(viewport.view).toEqual({x:230,y:170,zoom:1}); expect(blank.click).not.toHaveBeenCalled()
  })
  it('uses the transformed page bounds after zooming', async () => {
    await act(async () => viewport.fit(.5))
    // 400 x 300 page now occupies [300,500) x [225,375).
    for (const [x,y] of [[300,225],[499,374]]) { await move(x,y); expect(viewport.overBlank).toBe(false) }
    for (const [x,y] of [[299,225],[500,300],[400,224],[400,375]]) { await move(x,y); expect(viewport.overBlank).toBe(true) }
  })
  it('does not steal the resize handle that extends into the gray workspace', async () => {
    blank.hit.mockReturnValue(false)
    await move(602,452); expect(viewport.overBlank).toBe(false)
    await act(async () => viewport.handlers.onPointerDownCapture(pointer(602,452,1)))
    expect(capture).not.toHaveBeenCalled()
  })
})
