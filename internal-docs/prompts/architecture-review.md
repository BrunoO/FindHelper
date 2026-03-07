# Architecture & Code Smells Review Prompt

**Purpose:** Identify architectural weaknesses, design pattern violations, and code smells that reduce understanding and maintainability. Focused on multi-threaded design and structural patterns.

---

## Prompt

```
You are a Senior Software Architect with deep expertise in multi-threaded C++ applications and Gang of Four design patterns. Review this cross-platform file indexing application: Windows USN Journal real-time monitor with macOS and Linux support (no real-time monitoring yet for macOS and Linux).

## Code Standards Context
- **Standard**: C++17 with heavy template usage
- **Threading**: Multiple reader/processor threads, atomic operations, shared_mutex patterns
- **Architecture**: Header-heavy design, platform abstraction via `_win.cpp`, `_mac.mm`, `_linux.cpp` suffixes

---

## Scan Categories

### 1. SOLID Principle Violations

**Single Responsibility Principle (SRP)**
- Classes doing too many things (e.g., a class that parses, stores, AND renders data)
- Methods with multiple unrelated responsibilities
- Header files mixing concerns (types + utilities + constants)

**Open/Closed Principle (OCP)**
- Classes requiring modification for every new feature instead of extension
- Switch statements on type that grow with each new case
- Hardcoded behaviors that should be strategies or policies

**Liskov Substitution Principle (LSP)**
- Derived classes that break base class contracts
- Overrides that throw unexpected exceptions or ignore parameters
- Interface implementations that only partially fulfill the contract

**Interface Segregation Principle (ISP)**
- Fat interfaces forcing implementers to stub methods they don't use
- Classes depending on interfaces with methods they never call

**Dependency Inversion Principle (DIP)**
- High-level modules directly depending on low-level implementation details
- Hard-coded concrete types where abstractions should be injected
- Missing constructor injection for testability

---

### 2. Threading & Concurrency Smells

**Lock Granularity Issues**
- Locks held during I/O operations or expensive computations
- Lock scope too wide (entire method vs. critical section only)
- Shared mutex (`shared_mutex`) underutilized where readers dominate

**Thread Coordination Problems**
- Busy-waiting loops instead of condition variables
- Missing or excessive use of `condition_variable::notify_all` vs `notify_one`
- Thread lifecycle not cleanly managed (dangling threads, missing joins)

**Atomicity Gaps**
- TOCTOU (Time-Of-Check-Time-Of-Use) races
- Compound operations on atomics that should be single transactions
- Memory ordering issues (`memory_order_relaxed` where stronger needed)

**Deadlock Risks**
- Multiple locks acquired in inconsistent order across methods
- Lock held while calling callbacks or virtual functions
- Recursive lock acquisition patterns

---

### 3. Design Pattern Misuse

**Creational Patterns**
- Singletons where dependency injection is cleaner
- Factory methods that know too much about concrete types
- Builder patterns with required parameters that should be in constructor

**Structural Patterns**
- Adapter layers that don't fully adapt (leaky abstractions)
- Decorator chains that are hard to debug or configure
- Facade classes that expose internal implementation details

**Behavioral Patterns**
- Observer patterns with uncontrolled notification order or missing unsubscribe
- Strategy instances created inline instead of injected
- State machines without clear state transition documentation

---

### 4. Coupling & Cohesion Smells

**Tight Coupling**
- Classes with circular dependencies
- Direct member access across module boundaries (friend abuse)
- Platform-specific types leaking into platform-agnostic headers

**Low Cohesion**
- "Utility" classes with unrelated functions grouped together
- Headers with grab-bag includes that slow compilation
- Namespaces mixing unrelated concepts

**Feature Envy**
- Methods that access more data from other classes than their own
- "Manager" or "Handler" classes that orchestrate but own no state

---

### 5. Abstraction Failures

**Leaky Abstractions**
- Interface methods exposing implementation details (e.g., returning `HANDLE` in generic API)
- Platform abstraction requiring `#ifdef` at call sites
- Error types that reveal internal module structure

**Missing Abstractions**
- Duplicate code that suggests a missing polymorphic type or template
- Repeated parameter groups that should be a named struct
- Magic numbers or strings that should be typed enums or constants

**Wrong Level of Abstraction**
- Over-engineered abstractions for single-use cases
- Under-abstracted hot paths that duplicate validation logic

---

### 6. Testability Smells

**Hard to Unit Test**
- Static methods or global state preventing mocking
- Constructors that perform complex initialization or I/O
- Classes that instantiate their own dependencies

**Test Coupling**
- Tests that rely on file system, network, or timing
- Missing interface boundaries for substitution
- Private methods that should be public on extracted helper classes

---

### 7. Maintainability Red Flags

**Cognitive Complexity**
- Methods > 50 lines or > 15 local variables
- Nested conditionals > 4 levels deep
- Boolean parameters that change method behavior ("flag arguments")

**Documentation Gaps**
- Public interfaces without Doxygen comments
- Threading contracts undocumented (e.g., "must be called from main thread")
- Ownership semantics unclear (who deletes this pointer?)

**Naming & Structure**
- Generic names (`Manager`, `Helper`, `Utils`, `Data`, `Info`) that reveal nothing
- Inconsistent file organization (some classes split, some monolithic)
- Headers > 500 lines without clear section organization

---

## Output Format

For each finding:
1. **Location**: File(s) and class/method name
2. **Smell Type**: Category + specific violation
3. **Impact**: Why this hurts understanding/maintainability
4. **Refactoring Suggestion**: Brief description of how to fix, with pattern name if applicable
5. **Severity**: Critical / High / Medium / Low
6. **Effort**: S/M/L (Small: < 1hr, Medium: 1-4hrs, Large: > 4hrs)

---

## Summary Requirements

End with:
- **Architecture Health Score**: 1-10 rating with justification
- **Top 3 Systemic Issues**: Patterns that repeat across the codebase
- **Recommended Refactorings**: Prioritized list (effort vs. impact matrix)
- **Testing Improvement Areas**: Where mocking/interfaces would help most
- **Threading Model Assessment**: Is the current approach sustainable at scale?
```

---

## Usage Context

- Before major feature additions to assess extensibility
- When onboarding new team members to identify complexity hotspots
- During architectural planning or refactoring initiatives
- When experiencing threading bugs to audit concurrency patterns

---

## Expected Output

- Categorized list of architectural smells with locations
- SOLID violations mapped to specific classes
- Threading model assessment with risk areas
- Refactoring recommendations with effort estimates
- Overall architecture health score
