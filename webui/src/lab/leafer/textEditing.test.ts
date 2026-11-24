import { describe, expect, it } from 'vitest'
import { editText, formatText, templateText } from '../../../../packages/inktag-editor-core/src/textEditing'
import type { TextRun } from '../../../../packages/inktag-editor-core/src/index'
const example:TextRun[]=[{text:'离',fontId:'wqy9-lunar'},{text:'{{countdownName}}',fontId:'wqy12-lunar',gap:3,style:{color:'#dc3026'}},{text:'{{countdownDays}}天',gap:3,fontId:'wqy9-lunar'}]
describe('range-based rich text editing',()=>{
  it('styles partial literal selections without exposing fragments',()=>{
    const result=formatText([{text:'今天晴天'}],{start:2,end:4},r=>({...r,style:{color:'#dc3026'}}))
    expect(result.map(r=>[r.text,r.style?.color])).toEqual([['今天',undefined],['晴天','#dc3026']])
  })
  it('expands style selection to a whole variable without changing source text',()=>{
    const result=formatText(example,{start:4,end:8},r=>({...r,style:{color:'#000000'}}))
    expect(templateText(result)).toBe(templateText(example))
    expect(result[1]).toMatchObject({text:'{{countdownName}}',gap:3,style:{color:'#000000'}})
    expect(result[2]).toEqual(example[2])
  })
  it('replacement inherits the replaced run font/color, not the preceding literal',()=>{
    const result=editText(example,'离国庆{{countdownDays}}天')
    expect(result[1]).toMatchObject({text:'国庆',fontId:'wqy12-lunar',gap:3,style:{color:'#dc3026'}})
    expect(result[2]).toEqual(example[2])
  })
  it('supports formatting an empty text before typing',()=>{
    const formatted=formatText([],{start:0,end:0},r=>({...r,style:{bold:true}}))
    expect(editText(formatted,'你好')).toMatchObject([{text:'你好',style:{bold:true}}])
  })
  it('coalesces normal typing rather than accumulating one run per keystroke',()=>{
    let runs:TextRun[]=[{text:'',style:{color:'#dc3026'}}]
    for(let i=1;i<=300;i++)runs=editText(runs,'字'.repeat(i))
    expect(runs).toHaveLength(1);expect(runs[0].text.length).toBe(300)
  })
  it('preserves emoji and explicit line breaks during replacement',()=>{
    expect(templateText(editText([{text:'🙂早上好'}],'🙃早上好\n你好'))).toBe('🙃早上好\n你好')
  })
})
