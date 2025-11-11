import Init.Prelude
import Lean

open Task Lean.Elab

def cancellableTimer (timeout : UInt32) : IO (Except IO.Error β) := do
  let mut remaining := timeout
  while remaining > 0 do
    match (<-IO.checkCanceled) with
      | true => return (.error $ IO.userError "timer cancelled - this is ok")
      | false => do
        let step := min remaining 1000
        IO.sleep step
        remaining := remaining - step
  throw <| IO.userError s!"error: lean server timeout after {timeout} milliseconds"

def runWithTimeout
  (func : Unit → IO β)
  (timeout : UInt32)
  (prio : Task.Priority := Task.Priority.dedicated) : IO (Except IO.Error β) := do

  let timer ← IO.asTask (cancellableTimer timeout) Task.Priority.dedicated
  let funcWrapper: IO (Except IO.Error β) := func () >>= fun b => return .ok b
  let job ← IO.asTask funcWrapper prio

  let result ← IO.waitAny [job, timer]
  match result with
    | .error err => IO.println s!"Got error: {err.toString}"
    | .ok val => IO.println s!"Finished result!"

  -- cancel both tasks
  IO.cancel job
  IO.cancel timer

  -- Return the result
  match result with
    | Except.ok val => return val
    | Except.error err => return (Except.error err.toString)
