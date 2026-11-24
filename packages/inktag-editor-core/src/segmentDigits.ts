/** The existing device-clock digit geometry, shared without font/raster dependencies. */
export function drawSegmentNumber<C>(gfx:{drawFastHLine:(x:number,y:number,w:number,c:C)=>void;drawFastVLine:(x:number,y:number,h:number,c:C)=>void},number:number,xLocation:number,yLocation:number,characterSize:number,foreground:C,background:C,digitCount:number,heightSize=characterSize) {
  const div=(n:number,d:number)=>Math.trunc(n/d)
  let value=Math.abs(Math.trunc(number))
  characterSize=Math.abs(Math.trunc(characterSize));heightSize=Math.abs(Math.trunc(heightSize))
  const segment2=5*characterSize,segment3=2*characterSize,segment4=7*heightSize
  const x1=characterSize+1,x2=segment3+segment2+1,y1=yLocation+heightSize+1,y3=yLocation+9*heightSize+1
  const segments=[[x1,yLocation,1],[x2,y1,0],[x2,y3+heightSize+1,0],[x1,2*y3-yLocation,1],[0,y3+heightSize+1,0],[0,y1,0],[x1,y3,1]] as const
  const numbers=[0x3f,0x06,0x5b,0x4f,0x66,0x6d,0x7d,0x07,0x7f,0x6f,0x00,0x40]
  let count=Math.max(1,Math.min(10,Math.abs(Math.trunc(digitCount))))
  const digitWidth=segment2+3*segment3+2;xLocation+=count*digitWidth
  while(count>0){
    const digit=value>9?value%10:digitCount<0&&value===0?10:value
    xLocation-=digitWidth;value=div(value,10)
    for(let i=0;i<7;i++){
      const [sx,sy,horizontal]=segments[i],color=(numbers[digit]&(1<<i))!==0?foreground:background
      let width:number,target:number,limit:number,a:number,b:number
      if(horizontal){
        width=segment2;target=sy+segment3;limit=sy+characterSize;a=xLocation+sx+characterSize;b=sy
        for(;b<limit;b++,a--,width+=2)gfx.drawFastHLine(a,b,width,color)
        for(;b<target;b++,a++,width-=2)gfx.drawFastHLine(a,b,width,color)
      }else{
        width=segment4;target=xLocation+sx+segment3;limit=xLocation+sx+characterSize;b=xLocation+sx;a=sy+characterSize
        for(;b<limit;b++,a--,width+=2)gfx.drawFastVLine(b,a,width,color)
        for(;b<target;b++,a++,width-=2)gfx.drawFastVLine(b,a,width,color)
      }
    }
    count--
  }
}
