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
    dataset = load_dataset("Goedel-LM/Lean-workbook-proofs", split="train", num_proc=1).select(range(10))
    dataset = dataset['full_proof']
    # dataset = pd.read_json("benchmark.json")["full_theorem"]

    start = time.time()
    msgs, tree, err, tacs, duration, state_cache = pyle.evaluate("import Mathlib\nimport Aesop\n--set up stuff", timeout=0)
    import_end = time.time()
    print(f"Import took: {import_end - start}s")

    results = []
    problems = list(dataset)
    for problem in tqdm(problems, desc='Verifying'):
        header, body = parse_header(problem)
        msgs, tree, err, tacs, duration, state_cache = pyle.evaluate(problem, state_cache=state_cache, timeout=0)
        results.append((problem, header, body, msgs, tree, err, tacs, duration))

    end = time.time()
    df = pd.DataFrame.from_records(
        results,
        columns=["full_theorem", "header", "body", "messages", "trees", "errors", "tactics", "time"],
    )
    print(df, flush=True)
    errs = df.apply(
        lambda row: len(row.errors) == 0 and all([x["severity"] != "error" for x in json.loads(row.messages or "{}")]), axis=1
    )
    print(errs.value_counts() / len(df), flush=True)

    print(f"Time taken: {end - import_end}s ({import_end - start}s)", flush=True)
    df.to_json("benchmark.json")
