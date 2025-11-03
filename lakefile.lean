import Lake
open System Lake DSL

package pyle

@[default_target]
lean_lib Pyle where
  defaultFacets := #[LeanLib.staticExportFacet]
  buildType := Lake.BuildType.release
  moreLeancArgs := #["-O3"]
