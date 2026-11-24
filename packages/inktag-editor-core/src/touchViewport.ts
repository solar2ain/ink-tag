import { contains, zoomAt, type EditorCore, type Point } from './index'

/** Coordinates are canvas-local CSS pixels, independent of device pixel ratio. */
export class TouchViewport {
  view = { x: 12, y: 12, zoom: 1 }
  hand = false
  private gesture?: { mode: 'pan' | 'edit' | 'pinch'; point: Point; view: {x:number;y:number;zoom:number}; distance: number; moved: boolean }
  constructor(readonly core: EditorCore) {}
  private local(p: Point) { return {x:(p.x-this.view.x)/this.view.zoom,y:(p.y-this.view.y)/this.view.zoom} }
  private center(p: Point[]) { return p.length>1?{x:(p[0].x+p[1].x)/2,y:(p[0].y+p[1].y)/2}:p[0] }
  start(points: Point[]) {
    if(!points.length)return
    const point=this.center(points)
    if(points.length>1) {
      this.core.cancel()
      this.gesture={mode:'pinch',point,view:{...this.view},distance:Math.max(1,Math.hypot(points[0].x-points[1].x,points[0].y-points[1].y)),moved:false}
      return
    }
    const p=this.local(point),outside=!contains({x:0,y:0,width:this.core.doc.width,height:this.core.doc.height},p)
    const mode=this.hand||(outside&&this.core.isBlank(p,10/this.view.zoom))?'pan':'edit'
    this.gesture={mode,point,view:{...this.view},distance:0,moved:false}
    if(mode==='edit')this.core.down(p,10/this.view.zoom)
  }
  move(points: Point[]) {
    const g=this.gesture
    if(!g||!points.length)return
    if(points.length>1&&g.mode!=='pinch'){this.start(points);return}
    const p=this.center(points)
    if(g.mode==='pinch') {
      if(points.length<2)return
      const distance=Math.hypot(points[0].x-points[1].x,points[0].y-points[1].y)
      const next=zoomAt(g.view,g.point,g.view.zoom*distance/g.distance)
      this.view={...next,x:next.x+p.x-g.point.x,y:next.y+p.y-g.point.y}
    } else if(g.mode==='pan') {
      g.moved ||= Math.hypot(p.x-g.point.x,p.y-g.point.y)>3
      if(g.moved)this.view={...g.view,x:g.view.x+p.x-g.point.x,y:g.view.y+p.y-g.point.y}
    } else this.core.move(this.local(p),5/this.view.zoom,5/this.view.zoom)
  }
  end(point?: Point) {
    const g=this.gesture;this.gesture=undefined
    if(!g)return
    if(g.mode==='edit'&&point)this.core.up(this.local(point))
    else if(g.mode==='pan'&&!g.moved&&!this.hand)this.core.deselect()
  }
  cancel() {this.core.cancel();this.gesture=undefined}
  fit(width:number,height:number,zoom?:number) {
    this.cancel()
    const z=zoom??Math.max(.25,Math.min(3,(width-24)/this.core.doc.width,(height-24)/this.core.doc.height))
    this.view={x:(width-this.core.doc.width*z)/2,y:(height-this.core.doc.height*z)/2,zoom:z}
  }
  scale(factor:number,width:number,height:number) {this.cancel();this.view=zoomAt(this.view,{x:width/2,y:height/2},this.view.zoom*factor)}
}
