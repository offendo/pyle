"""Run with the installed package, from outside the source checkout."""
import json
import pyle

for module in ("Init", "Lean", "Std"):
    response, _, _ = pyle.evaluate(f"import {module}\n#eval 2 + 2")
    data = json.loads(response)
    assert not data["error"], data
    assert not [m for m in data["messages"] if m["severity"] == "error"], data
    assert data["messages"][-1]["data"] == "4", data
print("Installed Pyle evaluates Init, Lean, and Std imports successfully")
