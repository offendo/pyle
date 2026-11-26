import gc
import json
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

import pyle
import pytest


def checked(response):
    data = json.loads(response)
    assert not data['error'], data
    assert not [m for m in data['messages'] if m['severity'] == 'error'], data
    return data


def test_cache_ownership_and_isolation(capfd):
    _, _, cache = pyle.evaluate('import Lean\n', cache_size=1)
    original = cache
    for i in range(20):
        response, _, cache = pyle.evaluate(
            f'import Lean\ndef privateValue := {i}\n#eval privateValue',
            state_cache=cache, cache_size=1)
        assert checked(response)['messages'][-1]['data'] == str(i)
        gc.collect()
    del cache
    gc.collect()
    response, _, _ = pyle.evaluate('import Lean\n#check Nat', state_cache=original)
    checked(response)
    assert 'PANIC' not in capfd.readouterr().err


def test_parallel_order_and_python_threads():
    _, _, cache = pyle.evaluate('import Lean\n')
    inputs = [f'import Lean\n#eval {i} + 1' for i in range(32)]
    responses, _, other = pyle.evaluate_many(inputs, state_cache=cache, n_workers=4)
    assert [checked(r)['messages'][-1]['data'] for r in responses] == [str(i + 1) for i in range(32)]
    with ThreadPoolExecutor(max_workers=4) as pool:
        results = list(pool.map(lambda code: pyle.evaluate(code, state_cache=other)[0], inputs))
    assert [checked(r)['messages'][-1]['data'] for r in results] == [str(i + 1) for i in range(32)]


def test_cache_eviction_and_flags():
    _, _, first = pyle.evaluate('import Init\n', cache_size=1)
    response, _, second = pyle.evaluate('import Lean\n#check Nat', state_cache=first, cache_size=1,
                                        return_info_tree=True)
    assert checked(response)['trees'] is not None
    assert list(second) == ['import Lean']
    response, _, _ = pyle.evaluate('import Init\n#check Nat', state_cache=first)
    assert checked(response)['trees'] is None
    assert pyle.parse_header_and_body('import Lean') == ('import Lean', '')
    response, _, _ = pyle.evaluate('import Init\n#check Nat', persist=True)
    assert checked(response)['trees'] is None
    with pytest.raises(ValueError):
        pyle.evaluate_many([], n_workers=0)
    with pytest.raises(ValueError):
        pyle.evaluate('import Init\n', cache_size=0)


def test_timeout_stops_elaboration():
    # A subprocess bounds the test if cancellation regresses into a deadlock.
    script = '''
import json, pyle, time
_, _, cache = pyle.evaluate('import Lean\\n')
code = """import Lean
set_option maxHeartbeats 0
run_elab
  while true do
    Lean.Core.checkSystem "timeout-test"
"""
for _ in range(4):
    response, duration, _ = pyle.evaluate(code, state_cache=cache, timeout=50)
    assert 'timeout' in json.loads(response)['error'], response
    assert duration < 2000, duration
cpu = time.process_time()
time.sleep(0.5)
assert time.process_time() - cpu < 0.1, 'Timed-out work is still running'
'''
    result = subprocess.run([sys.executable, '-c', script], capture_output=True, text=True, timeout=15)
    assert result.returncode == 0, result.stdout + result.stderr
    assert 'PANIC' not in result.stderr


def test_cold_batch_and_import_errors(tmp_path):
    codes = [f'import {header}\n#eval {i}' for i, header in enumerate(['Init', 'Lean'] * 4)]
    responses, _, _ = pyle.evaluate_many(codes, n_workers=4, cache_size=1)
    assert [checked(r)['messages'][-1]['data'] for r in responses] == [str(i) for i in range(8)]
    cache = None
    for _ in range(2):
        response, _, cache = pyle.evaluate('import PyleNonexistentModule\n', state_cache=cache)
        assert any(m['severity'] == 'error' for m in json.loads(response)['messages'])
        assert not cache
    # The old substring splitter can include body text containing "import" in
    # its cache key. Warm-up must still execute only the parsed Lean header.
    target = tmp_path / 'counter'
    target.write_text('')
    filename = json.dumps(str(target))
    code = f'''import Lean
#eval do
  let contents ← IO.FS.readFile {filename}
  IO.FS.writeFile {filename} (contents ++ "x")
-- important
'''
    responses, _, _ = pyle.evaluate_many([code], n_workers=2)
    checked(responses[0])
    assert target.read_text() == 'x'
