import Lean.Elab.Frontend
import Pyle.Timeout
import Pyle.JSON
import Pyle.Lean.InfoTree
import Pyle.Lean.InfoTree.ToJson
import Pyle.Lean.ContextInfo

open Lean Elab Pyle
open Lean.Elab Json

namespace Lean.Elab.IO

@[export run_lean_initialization]
def runLeanInitialization := unsafe do
  Lean.initSearchPath (← Lean.findSysroot)
  enableInitializersExecution


/-- Wrapper around IO.processCommands to enable info tree output. -/
def processCommandsWithInfoTrees
      (inputCtx : Parser.InputContext)
      (parserState : Parser.ModuleParserState)
      (commandState : Command.State)
      : IO (Command.State × List Message × List InfoTree) := do
    let commandState := { commandState with infoState.enabled := true }
    let s ← IO.processCommands inputCtx parserState commandState <&> Frontend.State.commandState
    pure (s, s.messages.toList, s.infoState.trees.toList)

/- Structure which contains state, output messages (JSON), and info tree (JSON).
-/
structure EvalResponse where
  state : Command.State
  msgs : String
  tree : String
  error : Option String

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


@[export evaluate]
def evaluate
  (input : String)
  : IO EvalResponse := do
  let fileName   := "<input>"
  let inputCtx   := Parser.mkInputContext input fileName

  let opts : Options := {}
  let (header, parserState, messages) ← Parser.parseHeader inputCtx
  let (env, messages) ← processHeader header opts messages inputCtx
  let initialState := Command.mkState env messages opts

  let (stateAfter, messages, trees, error) <- (
    try
      let (stateAfter, messages, trees) <- processCommandsWithInfoTrees inputCtx parserState initialState
      return (stateAfter, messages, trees, none)
    catch err =>
      return (initialState, [], [], some err.toString)
  )

  /- Finally, return a parsed output -/
  let tree := Json.arr (← trees.toArray.mapM fun t => t.toJson none)
  let msgs := Json.arr (← messages.toArray.mapM fun m => m.toJson)
  return ⟨stateAfter, toString msgs, toString tree, error⟩

@[export evaluate_from_state]
def evaluateFromState
  (input : String)
  (initialState : Command.State)
  : IO EvalResponse := do
  let fileName   := "<input>"
  let inputCtx   := Parser.mkInputContext input fileName

  let parserState : Parser.ModuleParserState := {}
  let (stateAfter, messages, trees, error) <- (
    try
      let (stateAfter, messages, trees) <- processCommandsWithInfoTrees inputCtx parserState initialState
      return (stateAfter, messages, trees, none)
    catch err =>
      return (initialState, [], [], some err.toString)
  )

  /- Finally, return a parsed output -/
  let tree := Json.arr (← trees.toArray.mapM fun t => t.toJson none)
  let msgs := Json.arr (← messages.toArray.mapM fun m => m.toJson)
  return ⟨stateAfter, toString msgs, toString tree, error⟩

@[export evaluate_with_timeout]
def evaluateWithTimeout
    (input : String)
    (timeout : UInt32)
    : IO EvalResponse := unsafe do

    let fileName   := "<input>"
    let inputCtx   := Parser.mkInputContext input fileName

    let opts : Options := {}
    let (header, parserState, messages) ← Parser.parseHeader inputCtx
    let (env, messages) ← processHeader header opts messages inputCtx
    let initialState := Command.mkState env messages opts

    let func := fun () => processCommandsWithInfoTrees inputCtx parserState initialState
    IO.println s!"timeout: {timeout}"
    let (stateAfter, messages, trees, error) := (<- match (<-runWithTimeout func timeout) with
      | .inl (stateAfter, messages, trees) => return (stateAfter, messages, trees, none)
      | .inr err => return (initialState, [], [], some err.toString)
    )
    /- Finally, return a parsed output -/
    let tree := Json.arr (← trees.toArray.mapM fun t => t.toJson none)
    let msgs := Json.arr (← messages.toArray.mapM fun m => m.toJson)
    return ⟨stateAfter, toString msgs, toString tree, error⟩

@[export evaluate_from_state_with_timeout]
def evaluateFromStateWithTimeout
  (input : String)
  (initialState : Command.State)
  (timeout : UInt32)
  : IO EvalResponse := unsafe do

  let fileName   := "<input>"
  let inputCtx   := Parser.mkInputContext input fileName

  let parserState : Parser.ModuleParserState := {}
  let func := fun () => processCommandsWithInfoTrees inputCtx parserState initialState
  let (stateAfter, messages, trees, error) := (<- match (<-runWithTimeout func timeout) with
    | .inl (stateAfter, messages, trees) => return (stateAfter, messages, trees, none)
    | .inr err => return (initialState, [], [], some err.toString)
  )
  /- Finally, return a parsed output -/
  let tree := Json.arr (← trees.toArray.mapM fun t => t.toJson none)
  let msgs := Json.arr (← messages.toArray.mapM fun m => m.toJson)
  return ⟨stateAfter, toString msgs, toString tree, error⟩
