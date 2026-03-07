High-impact performance questions
1. "What operations are O(n) or worse that could be O(1) or O(log n)?"
Why it's powerful:
Reveals algorithmic inefficiencies
Finds linear searches that could be hash lookups
Identifies repeated work that could be cached
Example findings:
Extension matching: Are you doing O(n) searches when you could use a hash set?
Pattern detection: Are you scanning strings when you could use a prefix tree?
2. "What data structures are accessed in patterns that don't match their design?"
Why it's powerful:
Reveals cache-unfriendly access patterns
Finds structures optimized for the wrong use case
Identifies opportunities for data layout changes
Example findings:
Are you iterating over a hash map when you need a vector?
Are you doing random access on a linked list?
Are hot and cold data mixed together?
3. "What work is done synchronously that could be deferred, batched, or parallelized?"
Why it's powerful:
Finds blocking operations
Identifies opportunities for async/parallel work
Reveals unnecessary sequential dependencies
Example findings:
File I/O done synchronously in hot paths
Operations that could be batched
Dependencies that aren't real dependencies
4. "What memory allocations happen in hot loops that could be hoisted or eliminated?"
Why it's powerful:
Allocations are expensive (even with SSO)
Reveals opportunities for object pooling
Finds unnecessary temporary objects
Example findings:
String allocations in loops (we found this!)
Temporary objects created repeatedly
Memory that could be reused
5. "What branch predictions are likely to fail, and can we restructure to help the CPU?"
Why it's powerful:
Branch mispredictions cost 10-20 cycles
Reveals opportunities for branchless code
Identifies hot paths that need optimization
Example findings:
50/50 branches in hot loops
Nested conditionals that could be flattened
Switch statements that could be jump tables
6. "What invariants are checked repeatedly that could be checked once and cached?"
Why it's powerful:
Finds repeated validation/checks
Identifies opportunities for pre-computation
Reveals defensive checks in hot paths
Example findings:
Pattern validation on every match (we fixed this!)
Type checks in loops
Bounds checks that could be hoisted
7. "What operations are 'fast enough' individually but accumulate to be slow when called millions of times?"
Why it's powerful:
Reveals death by a thousand cuts
Finds micro-optimizations that matter at scale
Identifies operations that seem cheap but aren't
Example findings:
Function call overhead (virtual calls, std::function)
Small allocations that add up
Repeated string operations
8. "What data is computed on-demand that could be pre-computed or cached with acceptable memory cost?"
Why it's powerful:
Finds opportunities for space-time tradeoffs
Identifies expensive computations done repeatedly
Reveals caching opportunities
Example findings:
Derived data computed every time
Expensive transformations that could be cached
Computations that could be done at index time
9. "What operations have hidden costs (exception handling, virtual calls, dynamic dispatch) that aren't obvious?"
Why it's powerful:
Reveals invisible performance costs
Finds abstractions with overhead
Identifies "zero-cost" abstractions that aren't
Example findings:
Exception handling in hot paths
Virtual function calls
std::function overhead vs function pointers
10. "What would the performance profile look like if we inverted the optimization priorities (memory over speed, or vice versa)?"
Why it's powerful:
Challenges assumptions
Reveals alternative optimization strategies
Identifies when current priorities are wrong
Example findings:
Memory pressure causing cache misses
Over-optimization in wrong areas
Tradeoffs that aren't being considered
The most powerful question for your codebase
Given what we've found, I'd recommend:
"What analysis, validation, or computation happens in matcher lambdas that could be done once when creating the matcher?"
This is what led us to find the pattern analysis issue. It's powerful because:
Matchers are created once but called thousands of times
Any work in the lambda is multiplied by match count
It forces you to think about the lifecycle of operations
Questions specific to your codebase
"Are there any hash map lookups in hot loops that could be replaced with direct array access?"
"What string operations happen per-file that could be batched or eliminated?"
"Are there any virtual function calls or std::function calls in hot paths that could be direct calls?"
"What data is copied when it could be referenced (string_view, const references)?"
"Are there any locks acquired in hot loops that could be eliminated or moved outside?"
These questions target the specific patterns in your codebase and can reveal significant optimizations.