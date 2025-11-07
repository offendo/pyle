import pyle
import gc

_, cache = pyle.evaluate_many(
    ["def x : Nat := 5\nexample : x == 5 := by trivial\n#print x"],
    None,
    timeout=1_000,
)

print('Got the cache obj')

thousand, cache = pyle.evaluate_many(
    ["def x : Nat := 5\nexample : x == 5 := by trivial\n#print x"] * 8,
    cache,
    timeout=1_000,
)

print(*thousand[:10],sep='\n=======================================\n')
print('all done')
input()
hundred, _ = pyle.evaluate_many(
    ["def x : Nat := 5\nexample : x == 5 := by trivial\n#print x"]* 100,
    cache,
    timeout=1_000,
)
print(*hundred[:10],sep='\n=======================================\n')
print('all done')
input()


# _, _, _, _, initial_state = pyle.evaluate("-- setup\n", initial_state=None, timeout=0)
# for i, thm in enumerate([""] * 1000):
#     print(i)
#     outputs = pyle.evaluate(
#         thm,
#         initial_state=initial_state,
#         timeout=1_000,
#     )
# 
# input()
# print('hi all done')
