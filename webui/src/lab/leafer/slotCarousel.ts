import { documentPageMode, type Document, type Widget } from '../../../../packages/inktag-editor-core/src/index'

/** Authoring-only image widget: references device slots, never copies their pixels. */
export function createSlotCarousel(doc: Document): Widget {
  if (documentPageMode(doc) === 'static') throw new Error('图槽轮播仅限动态页面')
  const width = Math.min(240,doc.width-16), height = Math.min(160,doc.height-(doc.footerInset??18)-16)
  let index=1;while(doc.widgets.some(widget=>widget.id===`slot-carousel-${index}`))index++
  return { id:`slot-carousel-${index}`, name:'图槽轮播', kind:'image',
    x:Math.floor((doc.width-width)/2),y:Math.floor((doc.height-(doc.footerInset??18)-height)/2),width,height,
    cells:[[{text:''}]],style:{fontSize:12,color:'#dc3026',bold:false},imageFit:'fit',
    imageSlots:{slotIds:[],intervalSeconds:300} }
}

/** Users see 1-based slot numbers; the persisted model uses existing zero-based IDs. */
export function parseSlotNumbers(text: string): number[] | undefined {
  if(!text.trim())return []
  const values=text.trim().split(/[，,\s]+/).filter(Boolean)
  if(values.length>255 || values.some(value=>!/^\d+$/.test(value)||Number(value)<1||Number(value)>255))return
  return values.map(value=>Number(value)-1)
}
