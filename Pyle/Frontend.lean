import Lean.Elab.Frontend
import Pyle.Timeout
import Pyle.Lean.InfoTree
import Pyle.Lean.InfoTree.ToJson
import Pyle.JSON

open Lean Elab
open Pyle

namespace Lean.Elab.IO

structure EvalResponse where
  state : Command.State
  messages : String
  tree : String
  tactics : String


def ppTactic (ctx : ContextInfo) (stx : Syntax) : IO Format :=
  ctx.runMetaM {} try
    Lean.PrettyPrinter.ppTactic ⟨stx⟩
  catch _ =>
    pure "<failed to pretty print>"

def tactics (trees : List InfoTree) : IO (List Tactic) :=
  trees.flatMap InfoTree.tactics |>.mapM
    fun ⟨ctx, stx, rootGoals, goals, pos, endPos, ns⟩ => do
      -- let proofState := some (← ProofSnapshot.create ctx none env? goals rootGoals)
      let goals := s!"{(← ctx.ppGoals goals)}".trim
      let tactic := Format.pretty (← ppTactic ctx stx)
      --let proofStateId ← proofState.mapM recordProofSnapshot
      return Tactic.of goals tactic pos endPos none ns

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

def evaluate (input : String) (cmdState? : Option Command.State) (opts : Options := {}) (fileName : Option String := none)
  : IO EvalResponse := unsafe do
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
    let jsontree := Json.arr (← trees.toArray.mapM fun t => t.toJson none)
    let jsonmsgs := Json.arr (← messages.toArray.mapM fun m => m.toJson)
    let tacs <- tactics trees
    let jsontactics := Json.arr (tacs.toArray.map fun m => toJson m)
    return ⟨cmdState, toString jsonmsgs, toString jsontree, toString jsontactics⟩
  | some cmdStateBefore => do
    let parserState : Parser.ModuleParserState := {}
    let (cmdStateAfter, messages, trees) ← processCommandsWithInfoTrees inputCtx parserState cmdStateBefore
    let tree := Json.arr (← trees.toArray.mapM fun t => t.toJson none)
    let msgs := Json.arr (← messages.toArray.mapM fun m => m.toJson)
    let tacs <- tactics trees
    let jsontactics := Json.arr (tacs.toArray.map fun m => toJson m)
    return ⟨cmdStateAfter, toString msgs, toString tree, toString jsontactics⟩

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
      let result <- runWithTimeout func timeout Task.Priority.max
      match result with
        | .ok val => do
          return (.ok val)
        | .error err => do
          return (.error err.toString)

-- def runInThread (func : Unit → IO β) (prio : Task.Priority := Task.Priority.max) : IO $ Task (Except IO.Error β) :=
--   do
--     let funcWrapper: IO β := func () >>= fun b => return b
--     let task <- (IO.asTask funcWrapper prio)
--     return task
--
-- def collate (input : List (Except IO.Error EvalResponse)) : List (Except String EvalResponse) := List.map unpack input
--   where
--     unpack (item : Except IO.Error EvalResponse) : Except String EvalResponse := match item with
--       | .error err => .error err.toString
--       | .ok val => .ok val
--
-- @[export lean_evaluate_batch]
-- def evaluate_batch (inputs : List String) (cmdState? : Option Command.State) :
--     IO (List (Except String EvalResponse)) := do
--     let opts : Options := {}
--     let fileName : Option String := none
--     let tasks <- List.mapM
--         (fun inp => do
--           return (<- runInThread (fun () => evaluate inp cmdState? opts fileName ))) inputs
--
--     let results <- IO.wait $ Task.mapList collate tasks
--     return results
