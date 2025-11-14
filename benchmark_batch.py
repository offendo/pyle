from collections import defaultdict
from datasets import load_dataset
import json
import pandas as pd
import time
import pyle
import re
from more_itertools import chunked
from tqdm import tqdm


def parse_header(theorem: str):
    pattern = r"^import .*$"
    header = []
    rest = []
    for line in (l for l in theorem.splitlines() if len(l.strip())):
        if re.match(pattern, line):
            header.append(line.strip())
        else:
            rest.append(line)
    return header, "\n".join(rest).strip()


if __name__ == "__main__":
    dataset = load_dataset("Goedel-LM/Lean-workbook-proofs", split="train", num_proc=1).select(range(10))
    dataset = dataset['full_proof']
    # dataset = pd.read_json("benchmark.json")["full_theorem"]

    start = time.time()
    responses, durations, state_cache = pyle.evaluate("import Aesop\n--asdf", state_cache=None, timeout=0)
    import_end = time.time()
    # print(f"Import took: {import_end - start:0.3f}s, or {duration / 1000:0.3f}s measured by lean")

    results = defaultdict(list)
    problems = list(dataset)
    responses, durations, state_cache = pyle.evaluate_many([d.replace('import Mathlib', '') for d in list(dataset)], state_cache=state_cache, timeout=0)
    responses = [json.loads(resp) for resp in responses]
    results['duration'] = durations
    for resp in responses:
        msgs, trees, tactics, errs = resp
        results['trees'].append(trees)
        results['messages'].append(msgs)
        results['tactics'].append(tactics)
        results['errors'].append(errs)

    end = time.time()
    df = pd.DataFrame(results)
    df.to_json('benchmark.json')
    print(f"Total time: {end - import_end:0.3f}s")
