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


-- def runWithTimeout
--   (func : Unit → IO β)
--   (timeout : UInt32)
--   (prio : Task.Priority := Task.Priority.max) : IO (Except IO.Error β) :=
--   do
--     -- Launch a timer function to run in a separate thread
--     let timerFunc: IO (Except IO.Error β) := do
--         IO.sleep $ timeout
--         return Except.error $ IO.userError s!"error: lean server timeout after {timeout} milliseconds"
--     let timer <- IO.asTask timerFunc default -- make sure to use a separate thread, otherwise we get arbitrary delay
--
--     -- Launch the main task
--     let funcWrapper: IO (Except IO.Error β) := func () >>= fun b => return .ok b
--     let job <- IO.asTask funcWrapper prio
--
--     -- Wait for whichever finishes first
--     let result <- IO.waitAny [job, timer]
--
--     -- Cancel the timer manually if it's still going
--     if not (<- IO.hasFinished timer) then IO.cancel timer
--     if not (<- IO.hasFinished job) then IO.cancel job
--
--     -- Return the result
--     match result with
--       | Except.ok val => return val
--         -- TODO: Not sure how to throw error here properly; should basically never happen
--       | Except.error err => return (Except.error err.toString)



def runWithTimeout
  (func : Unit → IO β)
  (timeout : UInt32)
  (prio : Task.Priority := Task.Priority.dedicated) : IO (Except IO.Error β) := do

  let timer ← IO.asTask (cancellableTimer timeout) Task.Priority.dedicated
  let funcWrapper: IO (Except IO.Error β) := func () >>= fun b => return .ok b
  let job ← IO.asTask funcWrapper prio

  let result ← IO.waitAny [job, timer]

  -- cancel both tasks — no harm canceling the already-finished one
  IO.cancel job
  IO.cancel timer

  -- Return the result
  match result with
    | Except.ok val => return val
    | Except.error err => return (Except.error err.toString)
