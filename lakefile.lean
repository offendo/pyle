import Lake
open System Lake DSL

package pyle

require "mathlib" 
  from git "https://github.com/leanprover-community/mathlib4" @ "v4.18.0"

@[default_target]
lean_lib Pyle where
  defaultFacets := #[LeanLib.staticFacet]
  buildType := Lake.BuildType.release
  -- moreLeancArgs := #["-Wall", "-O3"]
