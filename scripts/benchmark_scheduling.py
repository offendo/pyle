"""Isolate nested elaboration scheduling; run with lake env and taskset.

Set LEAN_NUM_THREADS before starting Python to vary the internal runtime pool.
Each process imports once, then runs a warm-up and measured native batches.
"""
import argparse
import json
import os
from pathlib import Path
import time

import pyle


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--workers', type=int, default=32)
    parser.add_argument('--count', type=int, default=64)
    parser.add_argument('--repeat', type=int, default=2)
    parser.add_argument('--sync', action='store_true')
    args = parser.parse_args()
    code = 'import Mathlib\nimport Aesop\n'
    if args.sync:
        code += 'set_option Elab.async false\n'
    code += '\n'.join('example (a b c : ℝ) : (a + b) * c = a*c + b*c := by ring'
                      for _ in range(20))
    _, _, cache = pyle.evaluate('import Mathlib\nimport Aesop\n')
    rounds = []
    for i in range(args.repeat + 1):
        start, cpu = time.perf_counter(), time.process_time()
        responses, _, _ = pyle.evaluate_many([code] * args.count,
                                             state_cache=cache, n_workers=args.workers)
        wall, cpu = time.perf_counter() - start, time.process_time() - cpu
        parsed = [json.loads(r) for r in responses]
        errors = sum(bool(r.get('error')) or any(m['severity'] == 'error'
                     for m in r['messages']) for r in parsed)
        if errors:
            raise RuntimeError(f'{errors} jobs failed in round {i}')
        rounds.append(dict(wall_s=wall, cpu_s=cpu, errors=errors))
    result = dict(workers=args.workers, count=args.count, sync=args.sync,
                  lean_num_threads=os.environ.get('LEAN_NUM_THREADS'),
                  affinity=sorted(os.sched_getaffinity(0)), warmup=rounds[0],
                  rounds=rounds[1:], runtime_paths=sorted({line.split()[-1]
                      for line in Path('/proc/self/maps').read_text().splitlines()
                      if 'libleanshared.so' in line}))
    # The native API writes progress to stdout; use a machine-readable marker.
    print('\nRESULT_JSON=' + json.dumps(result), flush=True)


if __name__ == '__main__':
    main()
