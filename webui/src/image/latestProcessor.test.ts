import { describe, expect, it, vi } from 'vitest'
import { LatestProcessor } from './latestProcessor'

describe('LatestProcessor', () => {
  it('cancels an in-flight result and still accepts a new image',async()=>{
    vi.useFakeTimers()
    try{
      let complete:((value:number)=>void)|undefined
      const results:number[]=[],errors:unknown[]=[]
      const processor=new LatestProcessor<number,number>(value=>value===1?new Promise(resolve=>complete=resolve):Promise.resolve(value),{onResult:value=>results.push(value),onError:error=>errors.push(error)})
      processor.schedule(1,0);await vi.advanceTimersByTimeAsync(0)
      processor.cancel();processor.schedule(2,0);complete?.(1)
      await vi.runAllTimersAsync()
      expect(results).toEqual([2]);expect(errors).toEqual([]);expect(processor.busy).toBe(false)
      processor.schedule(3);processor.cancel();await vi.runAllTimersAsync()
      expect(results).toEqual([2])
    }finally{vi.useRealTimers()}
  })
  it('collapses rapid requests to the latest pending value', async () => {
    vi.useFakeTimers()
    const work = vi.fn(async (value: number) => value * 2)
    const results: number[] = []
    const processor = new LatestProcessor(work, { onResult: (result) => results.push(result) })

    processor.schedule(1)
    processor.schedule(2)
    processor.schedule(3)
    await vi.runAllTimersAsync()

    expect(work).toHaveBeenCalledTimes(1)
    expect(work).toHaveBeenCalledWith(3)
    expect(results).toEqual([6])
    vi.useRealTimers()
  })

  it('discards an obsolete running result and processes the newest request next', async () => {
    vi.useFakeTimers()
    let releaseFirst: (() => void) | undefined
    const work = vi.fn((value: number) => value === 1
      ? new Promise<number>((resolve) => { releaseFirst = () => resolve(1) })
      : Promise.resolve(value))
    const results: number[] = []
    const processor = new LatestProcessor(work, { onResult: (result) => results.push(result) })

    processor.schedule(1, 0)
    await vi.advanceTimersByTimeAsync(0)
    processor.schedule(2, 0)
    releaseFirst?.()
    await vi.runAllTimersAsync()

    expect(work.mock.calls.map(([value]) => value)).toEqual([1, 2])
    expect(results).toEqual([2])
    expect(processor.busy).toBe(false)
    vi.useRealTimers()
  })
})
