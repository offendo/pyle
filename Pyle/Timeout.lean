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

  -- spawn both tasks using Task.spawn
  let job <- IO.asTask (do
    let result ← func ()
    pure result
  ) prio
  let timer <- IO.asTask do
    IO.sleep timeout
    IO.cancel job

  -- wait for whichever finishes first
  let result ← IO.wait job

  -- cancel both
  IO.cancel job
  IO.cancel timer

  -- print result and return
  match result with
  | .ok val => return .ok val
  | .error err => return .error err
