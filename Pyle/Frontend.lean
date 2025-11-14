import Lean.Elab.Frontend
import Pyle.Timeout
import Pyle.Lean.InfoTree
import Pyle.Lean.InfoTree.ToJson
import Pyle.JSON
import Pyle.LruCache
import Lean.Data.Json

open Lean Elab

namespace Pyle
def ppTactic (ctx : ContextInfo) (stx : Syntax) : IO Format :=
  ctx.runMetaM {} try
    Lean.PrettyPrinter.ppTactic ⟨stx⟩
  catch _ =>
    pure "<failed to pretty print>"

def tactics (trees : List InfoTree) : IO (List Pyle.Tactic) :=
  trees.flatMap InfoTree.tactics |>.mapM
    fun ⟨ctx, stx, rootGoals, goals, pos, endPos, ns⟩ => do
      IO.println s!"{<-IO.getTID} [in tactics map func]"
      -- let proofState := some (← ProofSnapshot.create ctx none env? goals rootGoals)
      let goals := s!"{(← ctx.ppGoals goals)}".trim
      let tactic := Format.pretty (← ppTactic ctx stx)
      --let proofStateId ← proofState.mapM recordProofSnapshot
      return Pyle.Tactic.of goals tactic pos endPos none ns
end Pyle


namespace Lean.Elab.IO

namespace Pyle.Frontend
open Lean Elab
open Pyle
open Frontend

structure Context where
  inputCtx : Parser.InputContext
  cancelTk? : Option IO.CancelToken

abbrev FrontendM := ReaderT Context $ StateRefT State IO

def setCommandState (commandState : Command.State) : FrontendM Unit :=
  modify fun s => { s with commandState := commandState }

def updateCmdPos : FrontendM Unit := do
  modify fun s => { s with cmdPos := s.parserState.pos }

def getParserState : FrontendM Parser.ModuleParserState := do pure (← get).parserState
def getCommandState : FrontendM Command.State := do pure (← get).commandState
def setParserState (ps : Parser.ModuleParserState) : FrontendM Unit := modify fun s => { s with parserState := ps }
def setMessages (msgs : MessageLog) : FrontendM Unit := modify fun s => { s with commandState := { s.commandState with messages := msgs } }
def getInputContext : FrontendM Parser.InputContext := do pure (← read).inputCtx

@[inline] def runCommandElabM (x : Command.CommandElabM α) : FrontendM α := do
  let ctx ← read
  let s ← get
  let cmdCtx : Command.Context := {
    cmdPos       := s.cmdPos
    fileName     := ctx.inputCtx.fileName
    fileMap      := ctx.inputCtx.fileMap
    snap?        := none
    cancelTk?    := ctx.cancelTk?
  }
  match (← liftM <| EIO.toIO' <| (x cmdCtx).run s.commandState) with
  | Except.error e      => throw <| IO.Error.userError s!"unexpected internal error: {← e.toMessageData.toString}"
  | Except.ok (a, sNew) => setCommandState sNew; return a

def elabCommandAtFrontend (stx : Syntax) : FrontendM Unit := do
  runCommandElabM do
    let initMsgs ← modifyGet fun st => (st.messages, { st with messages := {} })
    Command.elabCommandTopLevel stx
    let mut msgs := (← get).messages
    modify ({ · with messages := initMsgs ++ msgs })

def processCommand : FrontendM Bool := do
  updateCmdPos
  let cmdState ← getCommandState
  let ictx ← getInputContext
  let pstate ← getParserState
  let scope := cmdState.scopes.head!
  let pmctx := {
    env := cmdState.env,
    options := scope.opts,
    currNamespace := scope.currNamespace,
    openDecls := scope.openDecls
  }
  let msgs := Json.arr (← cmdState.messages.toArray.mapM fun m => m.toJson)
  IO.println s!"({<-IO.getTID}): About to run profiler: Previous messages: {msgs}"
  let profileOutput := profileit "parsing" scope.opts fun _ => Parser.parseCommand ictx pmctx pstate cmdState.messages
  IO.println s!"({<-IO.getTID}): Ran profiler!"
  match profileOutput with
  | (cmd, ps, messages) => do
    modify fun s => { s with commands := s.commands.push cmd }
    setParserState ps
    setMessages messages
    elabCommandAtFrontend cmd
    pure (Parser.isTerminalCommand cmd)

partial def processCommands : FrontendM Unit := do
  let done ← processCommand
  unless done do
    processCommands

/--
Wrapper for `Frontend.processCommands` that enables info states, and returns
(new command state, messages, info trees)
-/
def processCommandsWithInfoTrees : Frontend.FrontendM (Command.State × List Message × List InfoTree) := do
  let _ <- processCommands
  let s <- getCommandState
  pure (s, s.messages.toList, s.infoState.trees.toList)

end Pyle.Frontend
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
open Frontend
open Lean.Elab.IO
open Pyle.Frontend
def go
  (cache : LRU String Environment)
  (input : String)
  (timeout : UInt32)
  : IO EvalResponse := do

  let fileName   := "<input>"
  let inputCtx   := Parser.mkInputContext input fileName
  let (header, parserState, messages) ← Parser.parseHeader inputCtx

  let env? <- cache.get (toString header)
  let opts : Options := {}
  let cmdStateBefore := (<-match env? with
  -- If we find it, go ahead and use it.
  | some env => return Command.mkState env messages opts
  | none => do
    -- Otherwise, process the header, and insert it into the cache.
    let (env, messages) ← processHeader header opts messages inputCtx
    cache.put (toString header) env
    return Command.mkState env messages opts)

  -- Execute the FrontendM monad by splitting it into a ReaderT -> StateRefT
  -- Doing this inside the thread should ensure that we don't have states
  -- getting in each other's ways. Furthermore, we should be able to use CancelToken now!
  /- NOTE not sure if this init -> call .run kind of paradigm is the smartest thing to do.
  -- Might be something cleaner. -/
  let initFrontendState := Frontend.State.mk cmdStateBefore parserState parserState.pos Array.empty
  let initContext <- (if timeout > 0 then do
    let cancelTk := (<- IO.CancelToken.new)
    let initContext := Context.mk inputCtx (some cancelTk)
    let _ <- runCancelTokenWithTimeout cancelTk timeout
    return initContext
  else
    return (Context.mk inputCtx none))
  -- TODO maybe return this finalState for iterative computation
  let (response, _) <- (process.run initContext).run initFrontendState
  return response
where
  process : Pyle.Frontend.FrontendM EvalResponse := do
    -- Run commands
    let (messages, trees, err) <- (try
      let (_, messages, trees) ← processCommandsWithInfoTrees
      return (messages, trees, "")
    catch _ : IO.Error => do
      let m : List Lean.Message := []
      let t : List InfoTree := []
      let e := s!"error: timeout after {timeout}ms"
      return (m, t, e))

    -- Parse output
    let tree := Json.arr (← trees.toArray.mapM fun t => t.toJson none)
    let msgs := Json.arr (← messages.toArray.mapM fun m => m.toJson)
    let tacs := Json.arr ((<- Pyle.tactics trees).toArray.map fun m => toJson m)
    let errs := Json.str err
    -- return
    return ⟨msgs, tree, tacs, errs⟩
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
  (cache? : Option (LRU String Environment))
  (timeout : UInt32 := 0)
  : IO $ (LRU String Environment) × String := do
  let cache <- (match cache? with
    | some c => return c
    | none => return <-LRU.mkEmpty 5)
  IO.println s!"Here now: {<-IO.getTID}"
  let response := <- go cache input timeout
  return (cache, toString $ toJson response)

@[export lean_evaluate_batch]
def evaluate_batch
  (inputs : Array String)
  (cache? : Option (LRU String Environment))
  (timeout: UInt32 := 0)
  : IO $ (LRU String Environment) × String := do
  let cache <- (match cache? with
    | some c => return c
    | none => return <-LRU.mkEmpty 5)
  let taskFns := inputs.map (
    fun input => (fun () => do return (<-go cache input timeout) )
    )
  let tasks <- taskFns.mapM (fun t => IO.asTask (t ()) Task.Priority.max)
  let responses := collate (tasks.map fun t => t.get)
  return (cache, toString $ Json.arr $ responses.map toJson)
where
-- Collate results into a JSON string
  collate (results : Array (Except IO.Error EvalResponse)) : Array EvalResponse :=
    results.map (fun except => match except with
      | .error err => ({ error := Json.str err.toString } : EvalResponse)
      | .ok val => val)
