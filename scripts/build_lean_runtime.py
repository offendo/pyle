"""Build an isolated Lean 4.34 runtime with optional concurrency patches.

Requires matching Lean sources, mimalloc 2.2.7 sources, and the C++ headers
matching Lean's bundled libc++. No downloads or toolchain installation occur.
See MULTITHREADING-PROBLEM.md and OPTIMIZATION.md for build and validation.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import shutil
import subprocess


def run(args, **kwargs):
    subprocess.run([str(a) for a in args], check=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True, help='Lean source checkout root')
    parser.add_argument('--mimalloc-source', type=Path, required=True)
    parser.add_argument('--include', type=Path, action='append', default=[],
                        help='Additional header directory (repeat for libc++, GMP, OpenSSL, libuv)')
    parser.add_argument('--output', type=Path, default=Path('build/lean-runtime'))
    parser.add_argument('--jobs', type=int, default=min(8, os.cpu_count() or 1))
    parser.add_argument('--baseline', action='store_true', help='Build the same sources without the cache patch')
    parser.add_argument('--completed-task-fast-path', action='store_true',
                        help='Also apply the experimental completed-task reclamation patch')
    args = parser.parse_args()
    if os.uname().sysname != 'Linux':
        parser.error('This isolated archive-based build is validated only on Linux')
    version = subprocess.check_output(['lean', '--version'], text=True)
    if 'version 4.34.0' not in version:
        parser.error('This patch and archive-based build target Lean 4.34.0')
    prefix = Path(subprocess.check_output(['lean', '--print-prefix'], text=True).strip())
    src = args.source.resolve() / 'src'
    output = args.output.resolve()
    objects = output / 'objects'
    objects.mkdir(parents=True, exist_ok=True)
    # Generated metadata goes in our output, never into the source checkout.
    githash = subprocess.check_output(['lean', '--githash'], text=True).strip()
    (output / 'githash.h').write_text(f'#define LEAN_GITHASH "{githash}"\n')
    original = src / 'library/ir_interpreter.cpp'
    working = output / 'patch-source/src/library'
    working.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(original, working / original.name)
    if not args.baseline:
        patch = Path(__file__).resolve().parents[1] / 'patches/lean-native-symbol-cache.patch'
        run(['patch', '--batch', '-p1', '-i', patch], cwd=output / 'patch-source')
    runtime_sources = src / 'runtime'
    if args.completed_task_fast_path:
        runtime_sources = output / 'patch-source/src/runtime'
        runtime_sources.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(src / 'runtime/object.cpp', runtime_sources / 'object.cpp')
        patch = Path(__file__).resolve().parents[1] / 'patches/lean-completed-task.patch'
        run(['patch', '--batch', '-p1', '-i', patch], cwd=output / 'patch-source')
    flags = ['leanc', '-x', 'c++', '-std=c++20', '-O3', '-DNDEBUG', '-DLEAN_EXPORTING',
             '-DLEAN_MULTI_THREAD', '-DLEAN_USE_GMP', '-fPIC', '-ftls-model=initial-exec',
             '-I' + str(output), '-I' + str(src), '-I' + str(args.mimalloc_source.resolve() / 'include')]
    flags += ['-I' + str(p.resolve()) for p in args.include]
    # leanc uses a hermetic C sysroot; supply the host development headers.
    flags += ['-isystem', '/usr/include']
    import sysconfig
    multiarch = sysconfig.get_config_var('MULTIARCH')
    if multiarch:
        flags += ['-isystem', '/usr/include/' + multiarch]
    members = set(subprocess.check_output(['ar', 't', prefix / 'lib/lean/libleanrt.a'], text=True).splitlines())
    sources = [p for p in (src / 'runtime').rglob('*.cpp') if p.name + '.o' in members]
    if args.completed_task_fast_path:
        sources = [runtime_sources / p.name if p.name == 'object.cpp' else p for p in sources]
    sources += [args.mimalloc_source.resolve() / 'src/static.c', working / original.name]

    def compile_source(source):
        extra = ['-DMI_SHARED_LIB', '-DMI_SHARED_LIB_EXPORT', '-DMI_WIN_NOREDIRECT', '-DMI_SECURE=0'] if source.suffix == '.c' else []
        run(flags + extra + ['-c', source, '-o', objects / (source.name + '.o')])

    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        list(pool.map(compile_source, sources))
    archive = output / 'libleancpp.a'
    shutil.copyfile(prefix / 'lib/lean/libleancpp.a', archive)
    run(['ar', 'r', archive, objects / 'ir_interpreter.cpp.o'])
    libraries = [prefix / 'lib/lean' / name for name in ('libInit.a', 'libStd.a', 'libLean.a')]
    libraries += [archive] + [objects / (s.name + '.o') for s in sources if s.name != original.name]
    libraries += [prefix / 'lib' / name for name in ('libc++.a', 'libc++abi.a')]
    run(['leanc', '-shared', '-o', output / 'libleanshared.so', '-Wl,--whole-archive', *libraries,
         '-Wl,--no-whole-archive', '-Wl,-soname,libleanshared.so', '-lgmp', '-luv', '-lssl', '-lcrypto', '-ldl', '-lm'])
    print(f'Built {output / "libleanshared.so"}')
    print(f'Configure pyle with -DPYLE_LEAN_RUNTIME_DIR={output}')


if __name__ == '__main__':
    main()
