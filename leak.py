"""Simple test to demonstrate the memory leak

Loop through, calling `evaluate` on a simple thing. You should see memory
skyrocket by 300mb every iteration if you watch on a monitor.
"""

import pyle
import time
import gc

# start = time.time()
# _, _, _, _, initial_state = pyle.evaluate("-- setup\n", initial_state=None, timeout=0)

# # Reuse the state
# for i in range(1000):
#     msgs, trees, err, duration, state = pyle.evaluate(
#         "def x : Nat := 5\nexample : x == 5 := by trivial\n#print x",
#         initial_state=initial_state,
#         timeout=1_000,
#     )
# end = time.time()
# print(f'single: {end - start}')


start = time.time()
_, state_cache = pyle.evaluate_many(["import Mathlib\nimport Aesop\n-- setup"], None, timeout=0)

# Reuse the state
outputs, state_cache = pyle.evaluate_many(
    ["import Mathlib\nimport Aesop\ndef x : Real := 5\ndef y : Real := 6\nexample : x == 5 := by trivial\n#eval (x + y)"] * 1000,
    state_cache=state_cache,
    timeout=1_000,
)
end = time.time()
print(f'batched: {end - start}')
