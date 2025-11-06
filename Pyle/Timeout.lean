import Init.Prelude
import Lean

open Task Lean.Elab


def runWithTimeout
  (func : Unit → IO β)
  (timeout : UInt32)
  (prio : Task.Priority := Task.Priority.default) : IO (Except IO.Error β) :=
  do
    -- Launch a timer function to run in a separate thread
    let timerFunc: IO (Except IO.Error β) := do
        IO.sleep $ timeout
        return Except.error $ IO.userError s!"error: lean server timeout after {timeout} milliseconds"
    let timer <- IO.asTask timerFunc prio -- make sure to use a separate thread, otherwise we get arbitrary delay

    -- Launch the main task
    let funcWrapper: IO (Except IO.Error β) := func () >>= fun b => return .ok b
    let job <- IO.asTask funcWrapper prio

    -- Wait for whichever finishes first
    let result <- IO.waitAny [job, timer]

    -- Cancel the timer manually if it's still going
    if not (<- IO.hasFinished timer) then IO.cancel timer

    -- Return the result
    match result with
      | Except.ok val => return val
        -- TODO: Not sure how to throw error here properly; should basically never happen
      | Except.error err => return (Except.error err.toString)
