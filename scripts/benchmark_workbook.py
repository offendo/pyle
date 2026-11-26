"""Measure unchanged Lean Workbook proofs from the original cached dataset.

Run fresh processes under lake env/taskset. Imports are warmed separately;
proof bodies are measured once by default, with the original 20 s deadline.
"""
import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import resource
import time

from datasets import load_dataset
import pyle


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--workers', type=int, required=True)
    parser.add_argument('--count', type=int, default=200)
    parser.add_argument('--timeout', type=int, default=20000)
    parser.add_argument('--repeat', type=int, default=1)
    parser.add_argument('--ids-from', type=Path,
                        help='Restrict to successful IDs from a prior result (explicit subset)')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    dataset = load_dataset('Goedel-LM/Lean-workbook-proofs', split='train', num_proc=1)
    rows = list(dataset.select(range(args.count)))
    if args.ids_from:
        prior = json.loads(args.ids_from.read_text())
        good = {j['problem_id'] for j in prior['rounds'][0]['jobs'] if j['status'] == 'success'}
        rows = [r for r in rows if r['problem_id'] in good]
    codes = [r['full_proof'] for r in rows]
    digest = hashlib.sha256(json.dumps(rows, ensure_ascii=False, sort_keys=True).encode()).hexdigest()
    cache = None
    start = time.perf_counter()
    for header in sorted({pyle.parse_header_and_body(c)[0] for c in codes}):
        response, _, cache = pyle.evaluate(header + '\n', state_cache=cache)
        data = json.loads(response)
        if data.get('error') or any(m['severity'] == 'error' for m in data['messages']):
            raise RuntimeError(response)
    result = dict(dataset='Goedel-LM/Lean-workbook-proofs', fingerprint=dataset._fingerprint,
                  corpus_sha256=digest, original_count=args.count, count=len(rows),
                  subset_from=str(args.ids_from) if args.ids_from else None,
                  workers=args.workers, timeout_ms=args.timeout,
                  import_s=time.perf_counter()-start,
                  affinity=sorted(os.sched_getaffinity(0)),
                  runtime_paths=sorted({line.split()[-1] for line in Path('/proc/self/maps').read_text().splitlines()
                                        if 'libleanshared.so' in line}), rounds=[])
    print(json.dumps({k:v for k,v in result.items() if k != 'rounds'}), flush=True)
    for index in range(args.repeat):
        start, cpu = time.perf_counter(), time.process_time()
        responses, durations, cache = pyle.evaluate_many(codes, state_cache=cache,
                                           n_workers=args.workers, timeout=args.timeout)
        wall, cpu = time.perf_counter()-start, time.process_time()-cpu
        jobs=[]
        for row, response, duration in zip(rows, responses, durations):
            data=json.loads(response)
            errors=[m for m in data.get('messages', []) if m['severity']=='error']
            error=data.get('error', '')
            status='timeout' if 'timeout' in str(error).lower() else 'error' if error or errors else 'success'
            jobs.append(dict(problem_id=row['problem_id'], status=status, duration_ms=duration,
                             error=error, diagnostics=errors))
        counts=dict(Counter(j['status'] for j in jobs))
        result['rounds'].append(dict(wall_s=wall,cpu_s=cpu,cpu_per_wall=cpu/wall,
                           attempts_per_s=len(rows)/wall, successes_per_s=counts.get('success',0)/wall,
                           counts=counts,jobs=jobs))
        result['peak_rss_mib']=resource.getrusage(resource.RUSAGE_SELF).ru_maxrss/1024
        args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(json.dumps(result,indent=2)+'\n')
        print('\nROUND_JSON='+json.dumps(dict(round=index,wall_s=wall,cpu_s=cpu,counts=counts)),flush=True)


if __name__ == '__main__':
    main()
