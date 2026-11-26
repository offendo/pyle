# pyle: Python Lean Evaluator

Native Python bindings for Lean code evaluation.

## Install from GitHub

Install the toolchain specified in [`lean-toolchain`](lean-toolchain) first:

```sh
elan toolchain install leanprover/lean4:v4.34.0
uv add git+https://github.com/offendo/pyle
```

Source installs require Linux or macOS, `lean` and `lake` on PATH (normally
provided by elan), Git, and a C/C++ compiler with C++17 support. CMake and the
Python build dependencies are installed by the build backend as needed.
Lake downloads the pinned dependencies, so the first build requires network
access. Windows source installs are not currently supported.

The build checks the Lean version, runs `lake build`, and compiles the Python
extension. The standalone testing executable and its jsoncpp dependency are
excluded by default. To build that executable manually, configure CMake with
`-DPYLE_BUILD_CLI=ON`.

```python
import json
import pyle

response, duration_ms, cache = pyle.evaluate("import Lean\n#eval 2 + 2")
print(json.loads(response))
```

You can run Python from any directory, without `lake env`. Pyle uses the Lean
installation selected at build time for both shared libraries and standard
module lookup; keep that toolchain installed at the same location. Wheels built
this way are local artifacts, not portable wheels for PyPI. Rebuild Pyle if you
move or replace the toolchain. Changing the active elan default after installation
does not change which toolchain Pyle uses.

## Other Lean projects and Mathlib

Pyle respects `LEAN_PATH`. To evaluate code importing Mathlib or modules from
another project, use a project with the **same Lean version**, build its modules
(or fetch its Mathlib cache), then run Python through that project's Lake environment:

```sh
lake env python your_script.py
```

Installing Pyle does not bundle Mathlib's compiled modules. Its Lake dependency
checkout is a build artifact, not a persistent runtime search path.

## Build locally

```sh
uv build
```

This builds a source distribution and a wheel from that distribution. For an
optional compatible patched runtime, pass
`-Ccmake.define.PYLE_LEAN_RUNTIME_DIR=/absolute/path/to/runtime` to `uv build`.
That runtime directory must also remain available after installation.

## Patch Lean separately (Linux)

`patch-lean.sh` builds a private `libleanshared.so` before the Python install.
It reuses the installed Lean archives and leaves the elan toolchain untouched.
Provide matching Lean 4.34.0 sources, mimalloc 2.2.7 sources, matching libc++
headers (including generated configuration headers), and the GMP, libuv, and
OpenSSL development libraries. It does not download these inputs.

```bash
export LEAN_VERSION=4.34.0
export LEAN_SOURCE=/path/to/lean4
export MIMALLOC_SOURCE=/path/to/mimalloc
export LEAN_INCLUDE_DIRS=/path/to/libcxx/include:/path/to/other/headers
export PYLE_LEAN_RUNTIME_DIR="$PWD/build/lean-runtime-4.34.0"
./patch-lean.sh
source "$PYLE_LEAN_RUNTIME_DIR/pyle-runtime.env"

uv add --no-cache git+https://github.com/offendo/pyle
# Or, for a virtual environment:
# uv pip install --no-cache git+https://github.com/offendo/pyle
```

The symbol-cache patch is enabled by default. Set `PYLE_PATCH_TASKS=1` to
also enable the experimental completed-task patch; it has shown a serial
performance regression in benchmarks. `PYLE_BUILD_JOBS` controls parallelism.
Run `./patch-lean.sh --help` for all environment variables. Other Lean versions
are rejected until their patches and build compatibility have been validated.

Pyle's CMake build reads `PYLE_LEAN_RUNTIME_DIR` from the environment; an explicit
`-Ccmake.define.PYLE_LEAN_RUNTIME_DIR=...` overrides it. Keep the runtime output
directory in place after installation. Use `--no-cache` for the initial install
or when selecting a different runtime, so uv builds a wheel with that selection.
If Pyle is already installed, also use `--reinstall-package pyle`. Subsequent
Python runs do not need the environment variable because the library path is
recorded in the extension. Changing the variable alone does not change an
already-installed extension.
