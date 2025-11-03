import json
import pyle


def test_evaluate():
    input_a = "def x : Nat := 5\n#print x"
    input_b = "def y : Nat := 6\n#print y"
    input_c = "#eval x + y"

    msgs, trees, state, err = pyle.evaluate(input_a, timeout=500)
    assert err is None, f"First invocation went wrong: {err}"
    msgs, trees, state, err = pyle.evaluate(input_b, initial_state=state, timeout=500)
    assert err is None, f"Second invocation went wrong: {err}"
    msgs, trees, state, err = pyle.evaluate(input_c, initial_state=state, timeout=500)
    assert err is None, f"Third invocation went wrong: {err}"

    try:
        m = json.loads(msgs)
        assert m[0]['data'] == "11", f"Error: data didn't return right value: {m}"
    except json.JSONDecodeError as e:
        raise Exception(f"Bad response: {msgs}")
