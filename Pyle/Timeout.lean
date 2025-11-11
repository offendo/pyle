import Init.Prelude
import Lean

open Task Lean.Elab

def cancellableTimer (timeout : UInt32) : IO (Except IO.Error β) := do
  let mut remaining := timeout
  while remaining > 0 do
    if (← IO.checkCanceled) then
      return .error (IO.userError "timer cancelled - this is ok")
    let step := min remaining 1000
    IO.sleep step
    remaining := remaining - step
  throw <| IO.userError s!"error: lean server timeout after {timeout} milliseconds"

def runWithTimeout
  (func : Unit → IO β)
  (timeout : UInt32)
  (prio : Task.Priority := .dedicated)
  : IO (Except IO.Error β) := unsafe do

  -- spawn both tasks using Task.spawn
  let timer := Task.spawn (fun _ => unsafeIO (cancellableTimer timeout)) .dedicated
  let job := Task.spawn (fun _ => unsafeIO (do
    let result ← func ()
    pure (.ok result : Except IO.Error β)
  )) prio

  -- wait for whichever finishes first
  let result ← IO.waitAny [job, timer]

  -- cancel both
  IO.cancel job
  IO.cancel timer

  -- print result and return
  match result with
  | .ok val => return val
  | .error err => return .error err
