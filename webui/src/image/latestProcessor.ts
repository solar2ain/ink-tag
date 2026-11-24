export interface LatestProcessorOptions<Result> {
  onBusy?: () => void
  onResult: (result: Result) => void
  onError?: (error: unknown) => void
}

export class LatestProcessor<Request, Result> {
  private generation = 0
  private pending: { generation: number; request: Request } | null = null
  private running = false
  private timer: ReturnType<typeof setTimeout> | null = null
  private disposed = false

  constructor(
    private readonly process: (request: Request) => Promise<Result>,
    private readonly options: LatestProcessorOptions<Result>,
  ) {}

  get busy() {
    return this.running || this.pending !== null
  }

  activate() {
    this.disposed = false
  }

  schedule(request: Request, delay = 80) {
    if (this.disposed) return
    this.pending = { generation: ++this.generation, request }
    if (this.timer !== null) clearTimeout(this.timer)
    this.options.onBusy?.()
    this.timer = setTimeout(() => void this.flush(), delay)
  }

  dispose() {
    this.disposed = true
    this.cancel()
  }

  /** Drop queued work and invalidate an in-flight result without unmounting. */
  cancel() {
    ++this.generation
    this.pending = null
    if (this.timer !== null) clearTimeout(this.timer)
    this.timer = null
  }

  private async flush() {
    this.timer = null
    if (this.disposed || this.running || !this.pending) return
    const current = this.pending
    this.running = true
    try {
      const result = await this.process(current.request)
      if (!this.disposed && this.pending?.generation === current.generation) {
        this.pending = null
        this.options.onResult(result)
      }
    } catch (error) {
      if (!this.disposed && this.pending?.generation === current.generation) {
        this.pending = null
        this.options.onError?.(error)
      }
    } finally {
      this.running = false
      if (!this.disposed && this.pending) {
        if (this.timer !== null) clearTimeout(this.timer)
        this.timer = setTimeout(() => void this.flush(), 0)
      }
    }
  }
}
