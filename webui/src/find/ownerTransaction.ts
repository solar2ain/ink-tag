/** Shared commit boundary for first registration and owner replacement. */
export async function runFindOwnerTransaction<Prepared, Proof, Result>(steps: {
  prepare: () => Promise<Prepared>
  stage: (prepared: Prepared) => Promise<void>
  commit: () => Promise<Proof>
  finalize: (prepared: Prepared, proof: Proof) => Promise<Result>
  abortDevice: () => Promise<unknown>
  abortLocal: (prepared: Prepared) => Promise<unknown>
  recoverAfterCommit?: (prepared: Prepared) => Promise<Result | undefined>
}): Promise<Result> {
  let prepared: Prepared | undefined
  let commitStarted = false
  try {
    prepared = await steps.prepare()
    await steps.stage(prepared)
    // Set this before dispatch: a rejected promise may mean a lost reply
    // after persistence, not a rejected device write.
    commitStarted = true
    const proof = await steps.commit()
    return await steps.finalize(prepared, proof)
  } catch (error) {
    if (!commitStarted) {
      await steps.abortDevice().catch(() => undefined)
      if (prepared !== undefined) await steps.abortLocal(prepared).catch(() => undefined)
    } else if (prepared !== undefined && steps.recoverAfterCommit) {
      const recovered = await steps.recoverAfterCommit(prepared)
      if (recovered !== undefined) return recovered
    }
    // Preserve the encrypted pending private owner after an uncertain commit.
    throw error
  }
}
