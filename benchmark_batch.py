import json
import re
import time
from collections import defaultdict

import pandas as pd
from datasets import load_dataset
from more_itertools import chunked
from tqdm import tqdm

import pyle


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
    dataset = load_dataset( "Goedel-LM/Lean-workbook-proofs", split="train[:25]", num_proc=1)
    dataset = dataset["full_proof"]
    # dataset = pd.read_json("benchmark.json")["full_theorem"]

    start = time.time()
    response, duration, state_cache = pyle.evaluate("import Mathlib\nimport Aesop\n--asdf", state_cache=None, timeout=1)
    import_end = time.time()
    print(
        f"Import took: {import_end - start:0.3f}s, or {duration / 1000:0.3f}s measured by lean"
    )

    results = defaultdict(list)
    problems = list(dataset)
    responses, durations, state_cache = pyle.evaluate_many(
        problems,
        state_cache=state_cache,
        timeout=20_000,
        n_workers=8,
    )
    results = [
        {**json.loads(resp), "duration": duration}
        for resp, duration in zip(responses, durations)
    ]

    end = time.time()
    print(f"Total time: {end - import_end:0.3f}s")
    df = pd.DataFrame(results)
    print(df)
    df.to_json("benchmark.json")
