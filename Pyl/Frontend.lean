import Lean.Elab.Frontend
import Pyl.Timeout
import Pyl.JSON
import Pyl.Lean.InfoTree
import Pyl.Lean.InfoTree.ToJson
import Pyl.Lean.ContextInfo
import Pyl.Util.Path

open Lean Elab Pyl
open Lean.Elab Json

namespace Lean.Elab.IO

@[export run_lean_initialization]
def runLeanInitialization := unsafe do
  Lean.initSearchPath (← Lean.findSysroot)
  enableInitializersExecution

/- Structure which contains state, output messages (JSON), and info tree (JSON).
-/
structure EvalResponse where
  state : Command.State
  msgs : String
  tree : String

  /-- Evaluates Lean 4 code given an optional input state, and returns a new state.

  Arguments
  =========
  input : String
    Input Lean 4 code to evaluate
  initialState? : Option Command.State
    Optional initial state for continued computation.
  timeout? : Option UInt32
    Optional timeout (in seconds), to limit Lean computation time.

  Returns
  =======
  IO EvalResponse :
    Structure containing evaluation output and new command state.
  -/
@[export evaluate_from_state]
def evaluateFromState
  (input : String)
  (initialState? : Option Command.State)
  (timeout? : Option UInt32)
  : IO EvalResponse := do
  let fileName   := "<input>"
  let inputCtx   := Parser.mkInputContext input fileName

  /- First, initialize a state if needed-/
  let (initState, parserState) ← (match initialState? with
    | none => do
      /- Case 1: No state is given, so init an empty one. -/
      let opts : Options := {}
      let (header, parserState, messages) ← Parser.parseHeader inputCtx
      let (env, messages) ← processHeader header opts messages inputCtx
      let headerOnlyState := Command.mkState env messages opts
      return (headerOnlyState, parserState)
    | some cmdState => do
      /- Case 2: we were given a state, just use that! -/
      let parserState : Parser.ModuleParserState := {}
      return (cmdState, parserState)
  )
  /- Second, call with or without a timer, depending on what's needed.-/
  let (stateAfter, messages, trees) <- (match timeout? with
    | none => do
      return (<- processCommandsWithInfoTrees inputCtx parserState initState)
    | some timeout => unsafe do
        let func := fun () => processCommandsWithInfoTrees inputCtx parserState initState
        return (<- match (<-runWithTimeout func timeout) with
          | .inl val => pure val
          | .inr err => throw err
        )
  )

  /- Finally, return a parsed output -/
  let tree := Json.arr (← trees.toArray.mapM fun t => t.toJson none)
  let msgs := Json.arr (← messages.toArray.mapM fun m => m.toJson)
  return ⟨stateAfter, toString msgs, toString tree⟩

where
  processCommandsWithInfoTrees
      (inputCtx : Parser.InputContext)
      (parserState : Parser.ModuleParserState)
      (commandState : Command.State)
      : IO (Command.State × List Message × List InfoTree) := do
    let commandState := { commandState with infoState.enabled := true }
    let s ← IO.processCommands inputCtx parserState commandState <&> Frontend.State.commandState
    pure (s, s.messages.toList, s.infoState.trees.toList)
