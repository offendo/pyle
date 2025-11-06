import Lake
open System Lake DSL

package pyle

require "mathlib" 
  from git "https://github.com/leanprover-community/mathlib4" @ "v4.24.0"

@[default_target]
lean_lib Pyle where
  defaultFacets := #[LeanLib.sharedFacet]
  buildType := Lake.BuildType.debug
  moreLeancArgs := #["-g", "-Wl,-export_dynamic"]
