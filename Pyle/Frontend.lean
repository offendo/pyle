import Lean.Elab.Frontend
import Pyle.Timeout
import Pyle.Lean.InfoTree
import Pyle.Lean.InfoTree.ToJson
import Pyle.JSON
import Lean.Data.Json

open Lean Elab

namespace Pyle
def ppTactic (ctx : ContextInfo) (stx : Syntax) : IO Format :=
  ctx.runMetaM {} try
    Lean.PrettyPrinter.ppTactic ⟨stx⟩
  catch _ =>
    pure "<failed to pretty print>"

/- FIX/TODO: change tactics so it gets tactics per-theorem instead of
 - concatenating them all together.
 -/
def tactics (trees : List InfoTree) : IO (List Pyle.Tactic) :=
  trees.flatMap InfoTree.tactics |>.mapM
    fun ⟨ctx, stx, _, goals, pos, endPos, ns⟩ => do
      -- let proofState := some (← ProofSnapshot.create ctx none env? goals rootGoals)
      let goals := s!"{(← ctx.ppGoals goals)}".trimAscii.toString
      let tactic := Format.pretty (← ppTactic ctx stx)
      --let proofStateId ← proofState.mapM recordProofSnapshot
      return Pyle.Tactic.of goals tactic pos endPos none ns
end Pyle


-- open Lean.Language.Lean in
-- def Pyle.processCommands (inputCtx : Parser.InputContext) (parserState : Parser.ModuleParserState)
--     (commandState : Command.State)
--     (old? : Option (Parser.InputContext × CommandParsedSnapshot) := none)
--     (cancelTk? : Option IO.CancelToken):
--     IO (Task CommandParsedSnapshot) := do
--   let prom ← IO.Promise.new
--   -- custom set cancel token
--   let cancelTk ← (match cancelTk? with
--     | none => return <-IO.CancelToken.new
--     | some tk => do
--       return tk)
--   process.parseCmd (old?.map (·.2)) parserState commandState prom (sync := true) cancelTk
--     |>.run (old?.map (·.1))
--     |>.run { inputCtx with }
--   return prom.result!
--
-- open Language in
-- /--
-- Variant of `IO.processCommands` that allows for potential incremental reuse. Pass in the result of a
-- previous invocation done with the same state (but usually different input context) to allow for
-- reuse.
-- -/
-- partial def Pyle.IO.processCommandsIncrementally (inputCtx : Parser.InputContext)
--     (parserState : Parser.ModuleParserState) (commandState : Command.State)
--     (old? : Option IncrementalState) (cancelTk? : Option IO.CancelToken) :
--     IO IncrementalState := do
--   let task ← Pyle.processCommands inputCtx parserState commandState (old?.map fun old => (old.inputCtx, old.initialSnap)) cancelTk?
--   go task.get task #[]
-- where
--   go initialSnap t commands := do
--     let snap := t.get
--     let commands := commands.push snap
--     match snap.nextCmdSnap? with
--     | some next => go initialSnap next.task commands
--     | none => do
--       -- Opting into reuse also enables incremental reporting, so make sure to collect messages from
--       -- all snapshots
--       let messages := toSnapshotTree initialSnap
--         |>.getAll.map (·.diagnostics.msgLog)
--         |>.foldl (· ++ ·) {}
--       -- In contrast to messages, we should collect info trees only from the top-level command
--       -- snapshots as they subsume any info trees reported incrementally by their children.
--       let trees := commands.map (·.finishedSnap.get.infoTree?) |>.filterMap id |>.toPArray'
--       return {
--         commandState := { snap.finishedSnap.get.cmdState with messages, infoState.trees := trees }
--         parserState := snap.parserState
--         cmdPos := snap.parserState.pos
--         commands := commands.map (·.stx)
--         inputCtx, initialSnap
--       }
--
-- def Pyle.IO.processCommands (inputCtx : Parser.InputContext) (parserState : Parser.ModuleParserState)
--     (commandState : Command.State) (cancelTk? : Option IO.CancelToken) : IO Frontend.State := do
--   let st ← Pyle.IO.processCommandsIncrementally inputCtx parserState commandState none cancelTk?
--   return st.toState

namespace Lean.Elab.IO
open Pyle Frontend

/--
Wrapper for `IO.processCommands` that enables info states, and returns
(new command state, messages, info trees)
-/
partial def processCommandsWithInfoTrees
    (inputCtx : Parser.InputContext) (parserState : Parser.ModuleParserState)
    (commandState : Command.State) (timeout : UInt32 := 0) :
    IO (Command.State × List Message × List InfoTree) := do
  let commandState := { commandState with infoState.enabled := true }
  -- Retain the snapshot tree so timeout cancellation reaches the parsing and
  -- elaboration tokens, rather than only canceling an outer collector task.
  let task ← Language.Lean.processCommands inputCtx parserState commandState
  let root : Language.SnapshotTask Language.Lean.CommandParsedSnapshot := {
    task, stx? := none, cancelTk? := none
  }
  let collect := fun () => collectCommands root.task.get root.task #[]
  let s ← if timeout == 0 then collect () else do
    match ← runWithTimeout collect timeout .dedicated root.cancelRec with
    | .ok s => pure s
    | .error e => throw e
  pure (s, s.messages.toList, s.infoState.trees.toList)
where
  collectCommands (initialSnap : Language.Lean.CommandParsedSnapshot)
      (task : Task Language.Lean.CommandParsedSnapshot)
      (commands : Array Language.Lean.CommandParsedSnapshot) : IO Command.State := do
    let snap := task.get
    let commands := commands.push snap
    if let some next := snap.nextCmdSnap? then
      collectCommands initialSnap next.task commands
    else
      let messages := Language.toSnapshotTree initialSnap
        |>.getAll.map (·.diagnostics.msgLog) |>.foldl (· ++ ·) {}
      let trees := commands.map (·.elabSnap.infoTreeSnap.get.infoTree?)
        |>.filterMap id |>.toPArray'
      return { snap.elabSnap.resultSnap.get.cmdState with messages, infoState.trees := trees }

end Lean.Elab.IO

@[export run_search_path_init]
def runSearchPathInit : IO Unit := unsafe do
  Lean.initSearchPath (← Lean.findSysroot)
  enableInitializersExecution

def runCancelTokenWithTimeout (cancelToken : IO.CancelToken) (timeout : UInt32) : IO Unit := do
  let _ ← IO.asTask do
    IO.sleep timeout
    cancelToken.set
  return ()

open Pyle
open Lean.Elab.IO
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
@[export lean_evaluate]
def evaluate_one
  (input : String)
  (env? : Option Environment)
  (timeout : UInt32 := 0)
  (returnInfoTree : Bool := True)
  (persistImports : Bool := False)
  (headerOnly : Bool := False)
  : IO $ String × Option Environment × Option Command.State := do
  let fileName   := "<input>"
  let inputCtx   := Parser.mkInputContext input fileName
  let (header, parserState, messages) ← Parser.parseHeader inputCtx

  let opts : Options := {}
  let (cmdStateBefore, headerState?) ← match env? with
  -- If we find it, go ahead and use it.
  | some env => do
    pure (Command.mkState env (opts := opts), none)
  | none => do
    -- Otherwise, process the header, and insert it into the cache.
    let (env, messages) ← processHeader header opts messages inputCtx (leakEnv := persistImports)
    let state := Command.mkState env messages opts
    pure (state, some state)

  -- Run commands
  let (newState, messages, trees, err) <- (if headerOnly then
    pure (none, [], [], "")
    else if timeout > 0 then
    do
      -- start a timer to cancel the job if needed
      let result : Except IO.Error (Command.State × List Lean.Message × List InfoTree) ← try
        pure (.ok (← processCommandsWithInfoTrees inputCtx parserState cmdStateBefore timeout))
        catch e => pure (.error e)
      (match result with
        | .error err => return (none, [], [], err.toString)
        | .ok (state, msgs, trees) => return (some state, msgs, trees, ""))
    else do
      let (state, msgs, trees) <- processCommandsWithInfoTrees inputCtx parserState cmdStateBefore
      pure (some state, msgs, trees, "")
  )
  -- Parse output
  let tree <- (if returnInfoTree
    then do
    return Json.arr (← trees.toArray.mapM fun t => t.toJson none)
    else
    return Json.null)
  let msgs := Json.arr (← (cmdStateBefore.messages.toList ++ messages).toArray.mapM fun m => m.toJson)
  let tacs := Json.arr ((<- Pyle.tactics trees).toArray.map fun m => toJson m)
  let errs := Json.str err

  -- Return response, header state, and new state
  let response : EvalResponse := ⟨msgs, tree, tacs, errs⟩
  -- A failed import must never become a reusable, apparently successful cache entry.
  let cacheEnv := if cmdStateBefore.messages.hasErrors then none else some cmdStateBefore.env
  return (toString $ toJson response, cacheEnv, headerState?)
