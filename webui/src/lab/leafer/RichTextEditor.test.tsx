// @vitest-environment jsdom
import { act, useState } from 'react'
import { createRoot, type Root } from 'react-dom/client'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { RichTextEditor, RichTextSelectionProvider } from './RichTextEditor'
import { EditorCore } from '../../../../packages/inktag-editor-core/src/index'
import { nativeDocument } from './nativeBridge'
describe('single editable text surface',()=>{
  let host:HTMLDivElement,root:Root,core:EditorCore
  const textbox=()=>host.querySelector('[aria-label="富文本内容"]') as HTMLElement
  const select=async (from:number,to:number,editor=textbox())=>act(async()=>{
    editor.focus();const walker=document.createTreeWalker(editor,NodeFilter.SHOW_TEXT),nodes:Text[]=[];let node
    while((node=walker.nextNode()))nodes.push(node as Text)
    const point=(position:number):[Text,number]=>{let offset=0;for(const n of nodes){if(position<=offset+n.length)return [n,position-offset];offset+=n.length}return [nodes[nodes.length-1],nodes[nodes.length-1].length]}
    const range=document.createRange();range.setStart(...point(from));range.setEnd(...point(to));window.getSelection()!.removeAllRanges();window.getSelection()!.addRange(range)
    editor.dispatchEvent(new MouseEvent('mouseup',{bubbles:true}))
  })
  beforeEach(async()=>{
    vi.stubGlobal('IS_REACT_ACT_ENVIRONMENT',true)
    host=document.createElement('div');document.body.append(host);root=createRoot(host)
    core=new EditorCore(nativeDocument('calendar',400,300));core.select(core.doc.widgets.find(w=>w.name==='倒计时')!.id)
    function Harness(){const [,render]=useState(0);const update=(fn:()=>unknown)=>{fn();render(n=>n+1)};return <RichTextSelectionProvider><RichTextEditor core={core} act={update}/><RichTextEditor core={core} act={update} canvas/></RichTextSelectionProvider>}
    await act(async()=>root.render(<Harness/>))
  })
  afterEach(async()=>{await act(async()=>root.unmount());host.remove();vi.unstubAllGlobals()})
  it('has one source area without fragment management buttons',()=>{
    expect(textbox().textContent).toBe('离{{countdownName}}{{countdownDays}}天')
    expect(host.textContent).not.toContain('拆分选中文字')
    expect(host.querySelectorAll('[aria-label="富文本内容"]')).toHaveLength(1)
    expect(host.querySelector('.rich-live-preview')).toBeNull()
  })
  it('shows the applied preset, becomes custom after editing, and inserts from the nearby variable button',async()=>{
    const preset=host.querySelector('[aria-label="文字预置"]') as HTMLSelectElement
    await act(async()=>{preset.value='weekday';preset.dispatchEvent(new Event('change',{bubbles:true}))})
    expect(preset.value).toBe('weekday')
    await select(textbox().textContent!.length,textbox().textContent!.length)
    await act(async()=>{(host.querySelector('[aria-label="插入变量"]') as HTMLButtonElement).click()})
    const panel=host.querySelector('[aria-label="选择变量"]')!
    expect(panel).not.toBeNull()
    await act(async()=>{(Array.from(panel.querySelectorAll('button')).find(b=>b.textContent==='年份{{year}}') as HTMLButtonElement).click()})
    expect(core.selected!.cells[0][0].text).toBe('星期{{weekdayName}}{{year}}')
    expect(preset.value).toBe('');expect(preset.selectedOptions[0].textContent).toBe('自定义')
    expect(host.querySelector('[aria-label="选择变量"]')).toBeNull()
    await act(async()=>textbox().dispatchEvent(new KeyboardEvent('keydown',{key:'z',ctrlKey:true,bubbles:true,cancelable:true})))
    expect(preset.value).toBe('weekday')
  })
  it('changes font size immediately without blurring or replacing the input',async()=>{
    await select(1,18)
    const input=host.querySelector('[aria-label="选中文字字号"]') as HTMLInputElement
    const setValue=Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value')!.set!
    const edit=async(value:string)=>act(async()=>{input.focus();setValue.call(input,value);input.dispatchEvent(new Event('input',{bubbles:true}))})
    await edit('22')
    expect(core.selected!.cells[0][0].richText!.runs.find(r=>r.text==='{{countdownName}}')!.style!.fontSize).toBe(22)
    expect(document.activeElement).toBe(input)
    expect(host.querySelector('[aria-label="选中文字字号"]')).toBe(input)
    await edit('')
    expect(input.value).toBe('')
    await edit('18')
    expect(core.selected!.cells[0][0].richText!.runs.find(r=>r.text==='{{countdownName}}')!.style!.fontSize).toBe(18)
    expect(document.activeElement).toBe(input)
  })
  it('retains the selected variable when changing color via the sidebar',async()=>{
    await select(1,18)
    await act(async()=>{(host.querySelector('[aria-label="文字颜色 · 黑"]') as HTMLElement).click()})
    const runs=core.selected!.cells[0][0].richText!.runs
    expect(runs.find(r=>r.text==='{{countdownName}}')?.style?.color).toBe('#000000')
    expect(window.getSelection()!.toString()).toContain('{{countdownName}}')
    expect(core.doc.widgets.find(w=>w.name==='日期')!.cells[0][0].text).toContain('{{year}}')
  })
  it('uses the canvas selection when the sidebar applies formatting',async()=>{
    const canvas=host.querySelector('[aria-label="画布文字编辑"]') as HTMLElement
    await select(1,18,canvas)
    await act(async()=>{(host.querySelector('[aria-label="加粗文字"]') as HTMLElement).click()})
    expect(core.selected!.cells[0][0].richText!.runs.find(r=>r.text==='{{countdownName}}')!.style!.bold).toBe(true)
    expect(document.activeElement).toBe(canvas)
  })
  it('waits for Chinese composition to finish before changing the document',async()=>{
    const editor=textbox(),before=core.selected!.cells[0][0].text
    await act(async()=>{editor.focus();editor.dispatchEvent(new CompositionEvent('compositionstart',{bubbles:true}));editor.textContent='测试';editor.dispatchEvent(new InputEvent('input',{bubbles:true,isComposing:true}))})
    expect(core.selected!.cells[0][0].text).toBe(before)
    await act(async()=>editor.dispatchEvent(new CompositionEvent('compositionend',{bubbles:true,data:'测试'})))
    expect(core.selected!.cells[0][0].text).toBe('测试')
    expect(host.querySelector('[aria-label="画布文字编辑"]')!.textContent).toBe('测试')
  })
  it('pastes plain text safely and keeps explicit newlines',async()=>{
    await select(0,textbox().textContent!.length)
    const event=new Event('paste',{bubbles:true,cancelable:true})
    Object.defineProperty(event,'clipboardData',{value:{getData:()=>'<img src=x onerror=alert(1)>\n{{year}}'}})
    await act(async()=>textbox().dispatchEvent(event))
    expect(event.defaultPrevented).toBe(true);expect(textbox().querySelector('img')).toBeNull()
    expect(core.selected!.cells[0][0].text).toBe('<img src=x onerror=alert(1)>\n{{year}}')
  })
  it('undo/redo stays inside the selected control on both Ctrl and Meta',async()=>{
    const original=core.selected!.cells[0][0].text
    await select(0,original.length)
    await act(async()=>{textbox().textContent='测试';textbox().dispatchEvent(new InputEvent('input',{bubbles:true}))})
    await act(async()=>textbox().dispatchEvent(new KeyboardEvent('keydown',{key:'z',ctrlKey:true,bubbles:true,cancelable:true})))
    expect(core.selected!.cells[0][0].text).toBe(original)
    await act(async()=>textbox().dispatchEvent(new KeyboardEvent('keydown',{key:'z',metaKey:true,shiftKey:true,bubbles:true,cancelable:true})))
    expect(core.selected!.cells[0][0].text).toBe('测试')
  })
})
