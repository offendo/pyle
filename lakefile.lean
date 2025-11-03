import Lake
open System Lake DSL

package pyle

@[default_target]
lean_lib Pyle where
  defaultFacets := #[LeanLib.sharedFacet]
  buildType := Lake.BuildType.release
  moreLeancArgs := #["-O3", "-Wl,-export_dynamic"]
