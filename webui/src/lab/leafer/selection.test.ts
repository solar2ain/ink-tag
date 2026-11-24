import { describe, expect, it } from 'vitest'
import { EditorCore, canSendDocument, clone, drawPlan, locked, parseDocument, type Alignment, type Document, type Widget } from '../../../../packages/inktag-editor-core/src/index'

const widget = (id: string, x: number, y: number, width = 40, height = 20): Widget => ({
  id, name: id, kind: 'text', x, y, width, height, cells: [[{ text: id }]], style: { fontSize: 9, color: '#191919', bold: false },
})
const make = () => new EditorCore({ version: 1, width: 400, height: 300, widgets: [widget('a',20,40),widget('b',120,60,60,30),widget('c',220,100,30,20)] })
describe('portable selection, alignment, groups and layer locks', () => {
  it('shift/toggle selection never starts a drag or edits a cell', () => {
    const e = make(), before = clone(e.doc)
    e.select('a'); e.down({x:125,y:65},4,true); e.move({x:170,y:100}); e.up({x:170,y:100})
    expect(e.selectedWidgets.map(w=>w.id)).toEqual(['a','b']); expect(e.doc).toEqual(before)
    e.down({x:25,y:45},4,true); e.up({x:25,y:45}); expect(e.selectedWidgets.map(w=>w.id)).toEqual(['b'])
    expect(e.canUndo).toBe(false)
  })
  it('marquee selects contained widgets and clears without creating history', () => {
    const e = make(); e.down({x:5,y:5}); e.move({x:190,y:95}); expect(e.marquee).toBeDefined(); e.up({x:190,y:95})
    expect(e.selectedWidgets.map(w=>w.id)).toEqual(['a','b']); expect(e.marquee).toBeUndefined(); expect(e.canUndo).toBe(false)
    e.down({x:300,y:200}); e.up({x:300,y:200}); expect(e.selectedWidgets).toHaveLength(0)
  })
  it('touch multiselect toggles whole controls instead of moving them', () => {
    const e = make(), before = clone(e.doc); e.multiSelect = true
    for (const p of [{x:25,y:45},{x:125,y:65}]) { e.down(p); e.move({x:p.x+15,y:p.y+15}); e.up(p) }
    expect(e.selectedWidgets).toHaveLength(2); expect(e.doc).toEqual(before)
  })
  it('moves multiple controls as one bounded unit and commits one history entry', () => {
    const e = make(); e.selectMany(['a','b']); const before = clone(e.doc)
    e.down({x:25,y:45}); e.move({x:40,y:55}); e.move({x:999,y:999}); e.up({x:999,y:999})
    expect(e.doc.widgets[1].x-e.doc.widgets[0].x).toBe(100)
    expect(e.selectionBounds!.x+e.selectionBounds!.width).toBe(400)
    expect(e.selectionBounds!.y+e.selectionBounds!.height).toBe(282)
    e.undo(); expect(e.doc).toEqual(before); expect(e.canUndo).toBe(false); expect(e.selectedWidgets).toHaveLength(2)
    e.redo(); expect(e.selectionBounds!.x+e.selectionBounds!.width).toBe(400)
  })
  it('cancelled multi-drag and marquee restore prior positions/selection', () => {
    const e = make(); e.selectMany(['a','b']); const before=clone(e.doc)
    e.down({x:25,y:45}); e.move({x:45,y:70}); e.cancel(); expect(e.doc).toEqual(before)
    e.down({x:5,y:5},4,true); e.move({x:300,y:200}); e.cancel()
    expect(e.selectedWidgets.map(w=>w.id)).toEqual(['a','b']); expect(e.canUndo).toBe(false)
  })
  it.each(['left','center','right','top','middle','bottom'] as Alignment[])('aligns %s within the selected range and undoes atomically', alignment => {
    const e = make(); e.selectMany(['a','b']); const before=clone(e.doc)
    e.align(alignment)
    const [a,b]=e.selectedWidgets
    const value=(w:Widget)=>alignment==='left'?w.x:alignment==='right'?w.x+w.width:alignment==='center'?w.x+w.width/2:alignment==='top'?w.y:alignment==='bottom'?w.y+w.height:w.y+w.height/2
    expect(value(a)).toBe(value(b)); expect(e.doc.widgets[2]).toEqual(before.widgets[2])
    e.undo(); expect(e.doc).toEqual(before); expect(e.canUndo).toBe(false)
  })
  it('aligns a single object to the canvas content area, keeping the mark strip clear', () => {
    const e=make();e.select('a');e.align('bottom');expect(e.selected!.y+e.selected!.height).toBe(282)
    e.align('right');expect(e.selected!.x+e.selected!.width).toBe(400)
    e.align('center');expect(e.selected!.x).toBe(180)
  })
  it('distributes with even gaps, refuses overlapping space and preserves the extremes', () => {
    const e=make();e.selectAll();expect(e.distribute('x')).toBeUndefined()
    const [a,b,c]=e.doc.widgets
    expect(b.x-a.x-a.width).toBe(c.x-b.x-b.width)
    expect([a.x,c.x+c.width]).toEqual([20,250])
    e.undo();e.doc.widgets[1].x=40;e.doc.widgets[2].x=60;const before=clone(e.doc)
    expect(e.distribute('x')).toContain('空间不足');expect(e.doc).toEqual(before)
  })
  it('grouping does not change geometry, paint order, glyph styles or draw plans', () => {
    const e=make();e.selectMany(['a','b']);const plan=drawPlan(e.doc), before=e.doc.widgets.map(w=>w.id)
    e.group();expect(drawPlan(e.doc)).toEqual(plan);expect(e.doc.widgets.map(w=>w.id)).toEqual(before)
    e.select('a');expect(e.selectedWidgets.map(w=>w.id)).toEqual(['a','b'])
    const relative=e.doc.widgets[1].x-e.doc.widgets[0].x
    e.align('right');expect(e.selectionBounds!.x+e.selectionBounds!.width).toBe(400)
    expect(e.doc.widgets[1].x-e.doc.widgets[0].x).toBe(relative)
    e.ungroup();e.select('a');expect(e.multiple).toBe(false)
    expect(parseDocument(JSON.stringify(e.doc))).toEqual(e.doc)
  })
  it('copies groups with new independent group and widget IDs', () => {
    const e=make();e.selectMany(['a','b']);e.group();const original=e.doc.widgets[0].groupId
    e.copy();e.paste();expect(e.selectedWidgets).toHaveLength(2)
    expect(e.selectedWidgets[0].groupId).not.toBe(original);expect(e.selectedWidgets[1].groupId).toBe(e.selectedWidgets[0].groupId)
    e.selectedWidgets[0].cells[0][0].text='changed';expect(e.doc.widgets[0].cells[0][0].text).toBe('a')
    expect(new Set(e.doc.widgets.map(w=>w.id)).size).toBe(5)
  })
  it('lets a selected group move from empty space inside its selection frame', () => {
    const e=make();e.selectMany(['a','b']);e.group();const before=clone(e.doc)
    e.down({x:80,y:50});e.move({x:90,y:60});e.up({x:90,y:60})
    expect(e.doc.widgets.slice(0,2).map(w=>w.x)).toEqual([30,130]);e.undo();expect(e.doc).toEqual(before)
  })
  it('multi-delete and layer ordering keep unrelated objects and undo once', () => {
    const e=make();e.selectMany(['a','b']);e.reorder(1);expect(e.doc.widgets.map(w=>w.id)).toEqual(['c','a','b'])
    e.undo();expect(e.doc.widgets.map(w=>w.id)).toEqual(['a','b','c'])
    e.remove();expect(e.doc.widgets.map(w=>w.id)).toEqual(['c']);e.undo();expect(e.doc.widgets).toHaveLength(3)
  })
  it('locks a complete group without changing paint order or geometry and can undo/unlock', () => {
    const e=make();e.selectMany(['a','b']);e.group();const before=clone(e.doc)
    e.setLocked(true,['a']);expect(e.doc.widgets.slice(0,2).every(locked)).toBe(true)
    const lockedDoc=clone(e.doc)
    e.nudge(10,10);e.align('left');e.remove();e.setFrame({x:200});e.updateStyle({fontSize:20});e.updateCellAt({id:'a',row:0,col:0},'bad')
    expect(e.doc).toEqual(lockedDoc);e.undo();expect(e.doc).toEqual(before)
    e.setLocked(true);e.setLocked(false);expect(e.doc).toEqual(before)
  })
  it('a locked ordinary widget is not misclassified as a device mark or raised in z-order', () => {
    const e=make();e.setLocked(true,['a']);e.select('b');e.setFrame({x:20,y:40})
    e.deselect();e.down({x:25,y:45});expect(e.selection?.id).toBe('b')
    expect(canSendDocument(e.doc)).toBe(true)
    e.doc.widgets[0].native={page:'calendar',role:'calendar',key:'calendar:0',source:{x:0,y:0,width:40,height:20}}
    expect(canSendDocument(e.doc)).toBe(false)
  })
  it('all editable selection skips locked layers and fixed branding cannot be unlocked, grouped or deleted', () => {
    const e=make();e.doc.widgets[2].native={page:'calendar',role:'graphic',key:'edition:free',source:{x:0,y:0,width:30,height:20}}
    e.setLocked(true,['a']);e.selectAll();expect(e.selectedWidgets.map(w=>w.id)).toEqual(['b'])
    e.select('c');e.setLocked(false);e.nudge(5,5);e.remove();expect(e.doc.widgets).toHaveLength(3);expect(locked(e.selected!)).toBe(true)
  })
  it('roundtrips old drafts and new editor-only metadata, and rejects malformed metadata', () => {
    const e=make();const original=clone(e.doc);expect(parseDocument(JSON.stringify(original))).toEqual(original)
    e.selectMany(['a','b']);e.group();e.setLocked(true);e.addGuide('x');e.addGuide('y')
    expect(parseDocument(JSON.stringify(e.doc))).toEqual(e.doc)
    for (const mutate of [(d:Document)=>{d.widgets[0].locked='yes' as never},(d:Document)=>{d.widgets[0].groupId=''},(d:Document)=>{d.guides![0].position=-1},(d:Document)=>{d.guides![0].position=1.5},(d:Document)=>{d.guides![1].position=290},(d:Document)=>{d.guides!.push(clone(d.guides![0]))}]) {
      const draft=clone(e.doc);mutate(draft);expect(()=>parseDocument(JSON.stringify(draft))).toThrow()
    }
  })
})

describe('smart snapping and persistent nonprinting guides', () => {
  it('snaps a resize edge to a guide without scaling glyphs', () => {
    const e=make();e.addGuide('x');e.setGuide(e.doc.guides![0].id,80);e.select('a')
    const style=clone(e.selected!.style)
    e.down({x:60,y:60});e.move({x:78,y:60},2,5);e.up({x:78,y:60})
    expect(e.selected!.width).toBe(60);expect(e.selected!.style).toEqual(style)
    e.undo();expect(e.selected!.width).toBe(40)
  })
  it('snaps to another edge and clears transient guides on release', () => {
    const e=make();e.select('a');e.down({x:25,y:45});e.move({x:103,y:45},2,5)
    expect(e.selected!.x+e.selected!.width/2).toBe(120);expect(e.snapLines).toContainEqual({axis:'x',position:120})
    e.up({x:103,y:45});expect(e.snapLines).toHaveLength(0);e.undo();expect(e.selected!.x).toBe(20)
  })
  it('uses screen-pixel tolerance from the adapter and supports Alt/global opt-out', () => {
    const e=make();e.select('a');e.down({x:25,y:45});e.move({x:103,y:45},2,1)
    expect(e.selected!.x).toBe(98);e.cancel()
    e.down({x:25,y:45});e.move({x:103,y:45},2,5,true);expect(e.selected!.x).toBe(98);expect(e.snapLines).toHaveLength(0);e.cancel()
    e.snapEnabled=false;e.down({x:25,y:45});e.move({x:103,y:45},2,5);expect(e.selected!.x).toBe(98)
  })
  it('snaps a collective selection without changing internal offsets', () => {
    const e=make();e.selectMany(['a','b']);e.down({x:25,y:45});e.move({x:44,y:45},2,5)
    expect(e.selectionBounds!.x+e.selectionBounds!.width/2).toBe(120)
    expect(e.doc.widgets[1].x-e.doc.widgets[0].x).toBe(100)
  })
  it('adds, edits, bounds and removes guides as separate undoable operations', () => {
    const e=make();e.addGuide('x');expect(e.doc.guides![0].position).toBe(200)
    const id=e.doc.guides![0].id;e.setGuide(id,999);expect(e.doc.guides![0].position).toBe(400)
    e.undo();expect(e.doc.guides![0].position).toBe(200)
    e.removeGuide(id);expect(e.doc.guides).toEqual([]);e.undo();expect(e.doc.guides![0].position).toBe(200)
    e.addGuide('y');e.setGuide(e.doc.guides![1].id,999);expect(e.doc.guides![1].position).toBe(282)
  })
  it('snaps to manual guides only while visible and never changes the draw plan', () => {
    const e=make(), plan=drawPlan(e.doc);e.addGuide('x');e.setGuide(e.doc.guides![0].id,87)
    expect(drawPlan(e.doc)).toEqual(plan)
    e.select('a');e.down({x:25,y:45});e.move({x:71,y:45},2,5)
    expect(e.selected!.x+e.selected!.width/2).toBe(87);e.cancel()
    e.showGuides=false;e.down({x:25,y:45});e.move({x:71,y:45},2,5);expect(e.selected!.x).toBe(66)
  })
})
