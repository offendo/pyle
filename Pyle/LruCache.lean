import Std

open Std

/--
A mutable Least Recently Used (LRU) cache.
Stores up to `capacity` key–value pairs of type `α → β`.
When inserting beyond capacity, the least recently used entry is evicted.
-/
structure LRUState (α β : Type) [BEq α] [Hashable α] where
  capacity : UInt32
  table    : HashMap α (β × Option α × Option α) := {}
    -- map: key -> (value, prevKey?, nextKey?)
  head     : Option α := none
  tail     : Option α := none
  size     : UInt32 := 0
deriving Inhabited

/-- A mutable LRU handle stored in an IO.Ref. -/
structure LRU (α β : Type) [BEq α] [Hashable α] where
  ref : Std.Mutex (LRUState α β)


/-- Create a new LRU cache with a given capacity (> 0). -/
def LRU.mkEmpty [BEq α] [Hashable α] (capacity : UInt32) : IO (LRU α β) := do
  if capacity == 0 then
    throw (IO.userError "LRU.mkEmpty: capacity must be > 0")
  let st : LRUState α β := { capacity := capacity }
  let r ← Std.Mutex.new st
  return { ref := r }


/-- Internal helper: detach a key from the linked list. -/
private def detach [BEq α] [Hashable α] (st : LRUState α β) (k : α) : LRUState α β :=
  match st.table.get? k with
  | none => st
  | some (v, prev?, next?) => Id.run do
    let mut tbl := st.table
    -- Update prev.next := next
    match prev? with
    | some p =>
      match tbl.get? p with
      | some (pv, pp?, pn?) => tbl := tbl.insert p (pv, pp?, next?)
      | none => ()
    | none => ()
    -- Update next.prev := prev
    match next? with
    | some n =>
      match tbl.get? n with
      | some (nv, np?, nn?) => tbl := tbl.insert n (nv, prev?, nn?)
      | none => ()
    | none => ()
    let newHead := if st.head == some k then next? else st.head
    let newTail := if st.tail == some k then prev? else st.tail
    { st with table := tbl.erase k, head := newHead, tail := newTail, size := st.size - 1 }

/-- Internal helper: attach a key at the head of the linked list. -/
private def attachHead [BEq α] [Hashable α] (st : LRUState α β) (k : α) (v : β) : LRUState α β :=
  match st.head with
  | none =>
    -- empty list
    { st with
      head := some k
      tail := some k
      table := st.table.insert k (v, none, none)
      size := st.size + 1
    }
  | some oldHead =>
    -- prepend
    let tbl₁ := st.table.insert k (v, none, some oldHead)
    match tbl₁.get? oldHead with
    | some (oldVal, oldPrev?, oldNext?) =>
      let tbl₂ := tbl₁.insert oldHead (oldVal, some k, oldNext?)
      { st with head := some k, table := tbl₂, size := st.size + 1 }
    | none =>
      { st with table := tbl₁, head := some k, size := st.size + 1 }

/-- Internal helper: evict the least recently used entry (tail) if over capacity. -/
private def evictIfNeeded [BEq α] [Hashable α]
  (st : LRUState α β) : (LRUState α β × Option (α × β)) :=
  if st.size <= st.capacity then
    (st, none)
  else
    match st.tail with
    | none => (st, none)
    | some t =>
      match st.table.get? t with
      | none => (st, none)
      | some (v, prev?, next?) =>
        let tbl := st.table.erase t
        let newTail := prev?
        let tbl :=
          match prev? with
          | some p =>
            match tbl.get? p with
            | some (pv, pp?, pn?) => tbl.insert p (pv, pp?, none)
            | none => tbl
          | none => tbl
        let st' := { st with table := tbl, tail := newTail, size := st.size - 1 }
        (st', some (t, v))

/-- Get a value by key and mark it as most recently used. -/
def LRU.get [BEq α] [Hashable α] [ToString α] (c : LRU α β) (k : α) : IO (Option β) := do
  c.ref.atomically (fun ref => do
    let st <- ref.get
    let got := st.table.get? k
    match st.table.get? k with
    | none => do
      return none
    | some (v, _, _) =>
      let st1 := detach st k
      let st2 := attachHead st1 k v
      ref.set st2
      return some v)

/-- Check whether the cache contains a key (does not update recency). -/
def LRU.contains [BEq α] [Hashable α] (c : LRU α β) (k : α) : IO Bool := do
  c.ref.atomically (fun ref => do
    let st <- ref.get
    return st.table.contains k)

/-- Insert or update a key–value pair. Evicts least recently used if over capacity. -/
def LRU.put [BEq α] [Hashable α] (c : LRU α β) (k : α) (v : β) : IO Unit := do
  if (<-LRU.contains c k) then
    return
  c.ref.atomically (fun ref => do
    let st ← ref.get
    let st1 := detach st k -- remove if it exists
    let st2 := attachHead st1 k v
    let (st3, _) := evictIfNeeded st2
    ref.set st3
    )


/-- Clear the entire cache. -/
def LRU.clear [BEq α] [Hashable α] (c : LRU α β) : IO Unit := do
  c.ref.atomically (fun ref => do
    let st <- ref.get
    ref.set { capacity := st.capacity })

/-- Return the number of elements currently in the cache. -/
def LRU.size [BEq α] [Hashable α] (c : LRU α β) : IO UInt32 := do
  c.ref.atomically (fun ref => do
    let st <- ref.get
    let s := st.size
    return s
    )

/-- Simple example with string→int cache. -/
def example1 : IO Unit := do
  let cache : LRU String Nat ← LRU.mkEmpty 3
  cache.put "a" 1
  cache.put "b" 2
  cache.put "c" 3
  IO.println s!"size: {(← cache.size)}"
  IO.println s!"get a: {(← cache.get "a")}"
  cache.put "d" 4 -- evict LRU ("b")
  cache.put "e" 5 -- evict LRU ("b")
  IO.println s!"contains c: {(← cache.contains "c")}"
  IO.println s!"contains b: {(← cache.contains "b")}"
  IO.println s!"get c: {(← cache.get "c")}"
  IO.println s!"contains a: {(← cache.contains "a")}"
  IO.println s!"size: {(← cache.size)}"

-- #eval example1
