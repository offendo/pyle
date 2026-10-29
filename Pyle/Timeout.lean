import Init.Prelude
import Lean

open Task Lean.Elab

def timer (timeout : UInt32) : IO (Except IO.Error β) := do
  IO.sleep timeout
  throw <| IO.userError s!"error: lean server timeout after {timeout} milliseconds"

def runWithTimeout
  (func : Unit → IO β)
  (timeout : UInt32)
  (prio : Task.Priority := .max)
  (onTimeout : BaseIO Unit := pure ())
  : IO (Except IO.Error β) := unsafe do

  let start ← IO.monoMsNow
  let job ← IO.asTask (func ()) prio
  -- Poll on the caller instead of putting sleeping timer tasks in Lean's pool.
  while !(← IO.hasFinished job) do
    if (← IO.monoMsNow) - start ≥ timeout.toNat then
      onTimeout
      -- IO.cancel alone does not cancel elaboration: its snapshot tokens must
      -- be set. Join the collector so batches cannot accumulate orphan jobs.
      let _ ← IO.wait job
      return .error (IO.userError s!"timeout after {timeout}ms")
    IO.sleep 1
  return ← IO.wait job
