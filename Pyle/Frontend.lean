import Lean.Elab.Frontend
import Pyle.Timeout
import Pyle.Lean.InfoTree
import Pyle.Lean.InfoTree.ToJson
import Pyle.JSON
import Pyle.LruCache

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

def processCommandsWithInfoTrees2 : Frontend.FrontendM (Command.State × List Message × List InfoTree) := do
  let _ <- Frontend.processCommands
  let s <- Frontend.getCommandState
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

@[export run_search_path_init]
def runSearchPathInit : IO Unit := unsafe do
  Lean.initSearchPath (← Lean.findSysroot)
  enableInitializersExecution

@[export lean_evaluate]
def evaluate_one
  (input : String)
  (cache? : Option (LRU String Environment))
  : IO $ (LRU String Environment) × EvalResponse := do
  let fileName   := "<input>"
  let inputCtx   := Parser.mkInputContext input fileName
  let (header, parserState, messages) ← Parser.parseHeader inputCtx
  let opts : Options := {}
  let cache <- (match cache? with
    | some c => return c
    | none => return <-LRU.mkEmpty 5)
  -- Search cache for header
  let env? <- cache.get (toString header)
  -- TODO everything below here should be moved to a function which can be run in a thread
  -- That way, hopefully, the Command.State which is modified in place is
  -- created as a thread-local variable and isn't modified again.
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
  let frontendState := Frontend.State.mk cmdStateBefore parserState parserState.pos Array.empty
  let stateT: StateRefT' IO.RealWorld Frontend.State IO EvalResponse := process inputCtx
  -- TODO maybe return this finalState for iterative computation
  let (response, finalState) <- stateT.run frontendState
  return (cache, response)
where
  process
    (inputCtx : Parser.InputContext)
    : StateRefT Frontend.State IO EvalResponse := do
    let ctx := Frontend.Context.mk inputCtx
    let (cmdStateAfter, messages, trees) ← processCommandsWithInfoTrees2.run ctx

    -- Parse output
    let tree := Json.arr (← trees.toArray.mapM fun t => t.toJson none)
    let msgs := Json.arr (← messages.toArray.mapM fun m => m.toJson)
    let tacs <- tactics trees
    let jsontactics := Json.arr (tacs.toArray.map fun m => toJson m)

    -- return
    return ⟨cmdStateAfter, toString msgs, toString tree, toString jsontactics⟩
