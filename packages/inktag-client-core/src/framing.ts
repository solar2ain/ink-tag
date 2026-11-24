import { encodeReleaseRequestFlags } from './generated/release-v1'

export const INKTAG_SERVICE_UUID = '62750001-d828-918d-fb46-b6c11c675aec'
export const INKTAG_COMMAND_UUID = '62750002-d828-918d-fb46-b6c11c675aec'
export const INKTAG_VERSION_UUID = '62750003-d828-918d-fb46-b6c11c675aec'

export type V2ResponseFrame = {
  kind: 'response'
  opcode: number
  transactionId: number
  status: number
  payload: Uint8Array
}

export type V2EventFrame = {
  kind: 'event'
  opcode: number
  transactionId: 0
  status: number
  originOpcode: number
  originTransactionId: number
  payload: Uint8Array
}

export type V2Frame = V2ResponseFrame | V2EventFrame

export function encodeV2Request(
  opcode: number,
  transactionId: number,
  flags = 0,
  payload: Uint8Array = new Uint8Array(0),
): Uint8Array {
  if (!Number.isInteger(opcode) || opcode < 0 || opcode > 0xff) throw new RangeError('opcode must be a byte')
  if (!Number.isInteger(transactionId) || transactionId < 1 || transactionId > 0xff) {
    throw new RangeError('transactionId must be between 1 and 255')
  }
  const wireFlags = encodeReleaseRequestFlags(flags)
  const packet = new Uint8Array(3 + payload.length)
  packet[0] = opcode
  packet[1] = transactionId
  packet[2] = wireFlags
  packet.set(payload, 3)
  return packet
}

export function decodeV2Frame(bytes: Uint8Array, firstEventOpcode: number): V2Frame | null {
  if (bytes.length < 3) return null
  const opcode = bytes[0]
  const transactionId = bytes[1]
  const status = bytes[2]
  if (opcode >= firstEventOpcode) {
    if (transactionId !== 0 || bytes.length < 5) return null
    return {
      kind: 'event', opcode, transactionId: 0, status,
      originOpcode: bytes[3], originTransactionId: bytes[4], payload: bytes.slice(5),
    }
  }
  if (transactionId === 0) return null
  return { kind: 'response', opcode, transactionId, status, payload: bytes.slice(3) }
}

export class V2TransactionRegistry<T> implements Iterable<[number, T]> {
  private nextId = 0
  private readonly entriesById = new Map<number, T>()

  allocate(isAlsoUnavailable: (transactionId: number) => boolean = () => false): number {
    for (let attempt = 0; attempt < 255; attempt += 1) {
      this.nextId = this.nextId >= 255 ? 1 : this.nextId + 1
      if (!this.entriesById.has(this.nextId) && !isAlsoUnavailable(this.nextId)) return this.nextId
    }
    throw new Error('no V2 transaction IDs are available')
  }

  set(transactionId: number, value: T): void {
    if (!Number.isInteger(transactionId) || transactionId < 1 || transactionId > 0xff) {
      throw new RangeError('transactionId must be between 1 and 255')
    }
    if (this.entriesById.has(transactionId)) throw new Error(`V2 transaction ${transactionId} is already pending`)
    this.entriesById.set(transactionId, value)
  }

  get(transactionId: number): T | undefined { return this.entriesById.get(transactionId) }
  has(transactionId: number): boolean { return this.entriesById.has(transactionId) }
  delete(transactionId: number): boolean { return this.entriesById.delete(transactionId) }
  clear(): void { this.entriesById.clear(); this.nextId = 0 }
  [Symbol.iterator](): IterableIterator<[number, T]> { return this.entriesById[Symbol.iterator]() }
}
