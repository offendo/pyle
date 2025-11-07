from datasets import load_dataset
import json
import pandas as pd
import time
import pyle
import re
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
    dataset = load_dataset("Goedel-LM/Lean-workbook-proofs", split="train", num_proc=1).select(range(50))
    dataset = dataset['full_proof']
    # dataset = pd.read_json("benchmark.json")["full_theorem"]

    start = time.time()
    output, state_cache = pyle.evaluate_many(["import Mathlib\nimport Aesop\n--set up stuff"])
    import_end = time.time()
    print(f"Import took: {import_end - start}s")

    results = []
    problems = list(dataset)
    output, state_cache = pyle.evaluate_many(list(dataset), state_cache, timeout=20_000)

    # Parse output
    for problem, (msgs, trees, err, tactics, duration) in zip(problems, output):
        header, body = parse_header(problem)
        results.append((problem, header, body, msgs, trees, tactics, err, duration))


    end = time.time()
    df = pd.DataFrame.from_records(
        results,
        columns=["full_theorem", "header", "body", "messages", "trees", "tactics", "errors", "time"],
    )
    print(df, flush=True)
    errs = df.apply(
        lambda row: len(row.errors) == 0 and all([x["severity"] != "error" for x in json.loads(row.messages or "{}")]), axis=1
    )
    print(errs.value_counts() / len(df), flush=True)

    print(f"Time taken: {end - import_end}s ({import_end - start}s)", flush=True)
    df.to_json("benchmark.json")
