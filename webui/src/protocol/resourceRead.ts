import { CommandV2 } from './generatedCommandsV2'

export const RESOURCE_READ_BUDGET_MS = 60000
export const RESOURCE_POST_COMMIT_READ_BUDGET_MS = 120000
export const RESOURCE_READ_RETRY_DELAY_MS = 2000
export const RESOURCE_READ_MAX_BUSY_RETRIES = 30

export type ResourceReadBudget = { deadline: number; retries: number; requestTimeoutMs: number; signal?: AbortSignal }
export function resourceReadBudget(afterCommit = false): ResourceReadBudget {
  // A scheduled native-page waveform can postpone the first metadata reply
  // after an accepted COMMIT. Extend only that readback, never replay uploads.
  const milliseconds = afterCommit ? RESOURCE_POST_COMMIT_READ_BUDGET_MS : RESOURCE_READ_BUDGET_MS
  return { deadline: performance.now() + milliseconds, retries: 0, requestTimeoutMs: afterCommit ? milliseconds : 10000 }
}

function assertActive(signal: AbortSignal) {
  if (signal.aborted) throw new Error('图槽读取已取消')
}

function delay(milliseconds: number, signal: AbortSignal) {
  assertActive(signal)
  return new Promise<void>((resolve, reject) => {
    const cancelled = () => {
      clearTimeout(timer)
      signal.removeEventListener('abort', cancelled)
      reject(new Error('图槽读取已取消'))
    }
    const timer = setTimeout(() => {
      signal.removeEventListener('abort', cancelled)
      resolve()
    }, milliseconds)
    signal.addEventListener('abort', cancelled, { once: true })
  })
}

/** BUSY is a rejected snapshot, not an ACK. Never apply this to mutations. */
export async function retryResourceRead<T>(
  opcode: number, request: (timeout: number) => Promise<T>,
  isBusy: (error: unknown) => boolean, signal: AbortSignal,
  budget = resourceReadBudget(),
) {
  if (opcode !== CommandV2.GET_RESOURCE_INFO && opcode !== CommandV2.LIST_RESOURCES) {
    throw new Error('此命令不允许自动重试')
  }
  // All pages belong to the first connection, not whichever device is current
  // when the next page starts.
  const active = budget.signal ??= signal
  for (;;) {
    assertActive(active)
    const remaining = budget.deadline - performance.now()
    if (remaining <= 0) throw new Error('图槽读取等待已达上限，请稍后刷新')
    try {
      const result = await request(Math.min(budget.requestTimeoutMs, remaining))
      assertActive(active)
      if (performance.now() >= budget.deadline) throw new Error('图槽读取等待已达上限，请稍后刷新')
      return result
    } catch (error) {
      assertActive(active)
      if (!isBusy(error)) throw error
      if (budget.retries >= RESOURCE_READ_MAX_BUSY_RETRIES ||
          budget.deadline - performance.now() <= RESOURCE_READ_RETRY_DELAY_MS) {
        throw new Error('图槽仍未就绪，请稍后刷新', { cause: error })
      }
      budget.retries++
      await delay(RESOURCE_READ_RETRY_DELAY_MS, active)
    }
  }
}
