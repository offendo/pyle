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
  : IO (Except IO.Error β) := unsafe do

  let start <- IO.monoMsNow
  -- spawn both tasks using Task.spawn
  let job <- IO.asTask (do
    let result ← func ()
    return some result
  ) prio

  let timer <- IO.asTask do
    IO.sleep timeout
    return none

  -- wait for whichever finishes first
  let result ← IO.waitAny [job, timer]
  let stop <- IO.monoMsNow

  -- cancel both
  IO.cancel job
  IO.cancel timer

  -- print result and return
  match result with
  | .ok (some val) => do
    return .ok val
  | .ok (none) => do
    return .error (IO.userError s!"timeout after {timeout}ms")
  | .error err => do
    return .error err
