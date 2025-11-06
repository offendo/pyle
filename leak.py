"""Simple test to demonstrate the memory leak

Loop through, calling `evaluate` on a simple thing. You should see memory
skyrocket by 300mb every iteration if you watch on a monitor.
"""

import pyle

_, _, _, _, initial_state = pyle.evaluate("-- setup\n", initial_state=None, timeout=0)

# Reuse the state
for i in range(100000):
    print(i)
    msgs, trees, err, time, state = pyle.evaluate(
        "def x : Nat := 5\nexample : x == 5 := by trivial\n#print x",
        initial_state=initial_state,
        timeout=1_000,
    )
    print(msgs)
    # Use same initial_state for next iteration
