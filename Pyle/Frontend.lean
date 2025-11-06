import Lean.Elab.Frontend
import Pyle.Timeout
import Pyle.Lean.InfoTree
import Pyle.Lean.InfoTree.ToJson

open Lean Elab

namespace Lean.Elab.IO

structure EvalResponse where
  state : Command.State
  msgs : String
  tree : String

/--
Wrapper for `IO.processCommands` that enables info states, and returns
* the new command state
* messages
* info trees
-/
def processCommandsWithInfoTrees
    (inputCtx : Parser.InputContext) (parserState : Parser.ModuleParserState)
    (commandState : Command.State) : IO (Command.State × List Message × List InfoTree) := do
  let commandState := { commandState with infoState.enabled := true }
  let s ← IO.processCommands inputCtx parserState commandState <&> Frontend.State.commandState
  pure (s, s.messages.toList, s.infoState.trees.toList)

/--
Process some text input, with or without an existing command state.
If there is no existing environment, we parse the input for headers (e.g. import statements),
and create a new environment.
Otherwise, we add to the existing environment.

Returns:
1. The header-only command state (only useful when cmdState? is none)
2. The resulting command state after processing the entire input
3. List of messages
4. List of info trees
-/
def evaluate (input : String) (cmdState? : Option Command.State) (opts : Options := {}) (fileName : Option String := none) :
    IO EvalResponse := unsafe do
  Lean.initSearchPath (← Lean.findSysroot)
  enableInitializersExecution
  let fileName   := fileName.getD "<input>"
  let inputCtx   := Parser.mkInputContext input fileName

  match cmdState? with
  | none => do
    -- Split the processing into two phases to prevent self-reference in proofs in tactic mode
    let (header, parserState, messages) ← Parser.parseHeader inputCtx
    let (env, messages) ← processHeader header opts messages inputCtx (leakEnv := false)
    let headerOnlyState := Command.mkState env messages opts
    let (cmdState, messages, trees) ← processCommandsWithInfoTrees inputCtx parserState headerOnlyState
    -- return results
    let tree := Json.arr (← trees.toArray.mapM fun t => t.toJson none)
    let msgs := Json.arr (← messages.toArray.mapM fun m => m.toJson)
    return ⟨cmdState, toString msgs, toString tree⟩

  | some cmdStateBefore => do
    let parserState : Parser.ModuleParserState := {}
    let (cmdStateAfter, messages, trees) ← processCommandsWithInfoTrees inputCtx parserState cmdStateBefore
    let tree := Json.arr (← trees.toArray.mapM fun t => t.toJson none)
    let msgs := Json.arr (← messages.toArray.mapM fun m => m.toJson)
    return ⟨cmdStateAfter, toString msgs, toString tree⟩

@[export lean_evaluate]
def evaluate_with_timeout (input : String) (cmdState? : Option Command.State) (timeout : UInt32 := 0):
    IO (Except String EvalResponse) := do
    let opts : Options := {}
    let fileName : Option String := none
    -- Only call the timeout thread if we need to.
    if (timeout <= 0) then
      let output <- evaluate input cmdState? opts fileName
      return (.ok output)
    else
      let func := fun () => evaluate input cmdState? opts fileName
      let result <- runWithTimeout func timeout Task.Priority.dedicated
      match result with
        | .ok val => do
          return (.ok val)
        | .error err => do
          return (.error err.toString)
