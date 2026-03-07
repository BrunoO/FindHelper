# Load Balancing Strategy Pattern Implementation

## Overview

This document describes the **Strategy Pattern** implementation for load balancing in parallel search operations. This pattern allows switching between different load balancing strategies at runtime via a settings parameter, enabling easy experimentation.

## Design Pattern: Strategy

The **Strategy Pattern** defines a family of algorithms, encapsulates each one, and makes them interchangeable. It lets the algorithm vary independently from clients that use it.

### Benefits

1. **Easy Experimentation**: Switch strategies via settings without code changes
2. **Extensibility**: Add new strategies without modifying existing code
3. **Testability**: Test each strategy independently
4. **Maintainability**: Each strategy is isolated and easier to understand

## Architecture

```
┌─────────────────────────────────────────┐
│         FileIndex::SearchAsyncWithData   │
│                                         │
│  ┌───────────────────────────────────┐ │
│  │   LoadBalancingStrategy           │ │
│  │   (Interface)                     │ │
│  └───────────────────────────────────┘ │
│              ▲                          │
│              │                          │
│    ┌─────────┼─────────┐               │
│    │         │         │               │
│  Static    Hybrid   Dynamic            │
│  Strategy  Strategy  Strategy          │
└─────────────────────────────────────────┘
```

## Implementation Structure

### 1. Strategy Interface

**File:** `LoadBalancingStrategy.h`

```cpp
class LoadBalancingStrategy {
public:
  virtual ~LoadBalancingStrategy() = default;
  
  virtual std::vector<std::future<...>>
  LaunchSearchTasks(...) const = 0;
  
  virtual std::string GetName() const = 0;
  virtual std::string GetDescription() const = 0;
};
```

### 2. Concrete Strategies

- **StaticChunkingStrategy**: Original static chunking
- **HybridStrategy**: Initial chunks + dynamic chunks (current implementation)
- **DynamicStrategy**: Pure dynamic chunking

### 3. Factory Function

```cpp
std::unique_ptr<LoadBalancingStrategy>
CreateLoadBalancingStrategy(const std::string &strategy_name);
```

### 4. Settings Integration

**File:** `Settings.h`

```cpp
struct AppSettings {
  // ...
  std::string loadBalancingStrategy = "hybrid";
};
```

## Current Implementation Status

### ✅ Completed

1. **Settings Integration**
   - Added `loadBalancingStrategy` to `AppSettings`
   - Settings load/save support
   - Default: "hybrid"

2. **Strategy Interface**
   - Created `LoadBalancingStrategy` base class
   - Factory function for creating strategies
   - Helper functions for available strategies

### 🔄 To Be Implemented

1. **Refactor FileIndex**
   - Extract chunk assignment logic into strategies
   - Modify `SearchAsyncWithData` to use strategy
   - Pass strategy from settings

2. **Implement Concrete Strategies**
   - Move current hybrid implementation to `HybridStrategy`
   - Implement `StaticChunkingStrategy` (original code)
   - Implement `DynamicStrategy` (pure dynamic)

3. **Settings UI** (Optional)
   - Add dropdown in settings window to select strategy
   - Show strategy description

## Usage

### Changing Strategy via Settings File

Edit `settings.json`:

```json
{
  "fontFamily": "Consolas",
  "fontSize": 14.0,
  "loadBalancingStrategy": "hybrid"
}
```

Available options:
- `"static"`: Static chunking (original)
- `"hybrid"`: Hybrid approach (recommended)
- `"dynamic"`: Pure dynamic chunking

### Programmatic Usage

```cpp
// Load settings
AppSettings settings;
LoadSettings(settings);

// Create strategy
auto strategy = CreateLoadBalancingStrategy(settings.loadBalancingStrategy);

// Use in FileIndex
auto futures = strategy->LaunchSearchTasks(...);
```

## Adding a New Strategy

### Step 1: Create Strategy Class

```cpp
class ByteBasedStrategy : public LoadBalancingStrategy {
public:
  std::vector<std::future<...>>
  LaunchSearchTasks(...) const override {
    // Implement byte-based chunking
  }
  
  std::string GetName() const override { return "byte-based"; }
  std::string GetDescription() const override {
    return "Byte-based: Chunks based on byte size";
  }
};
```

### Step 2: Register in Factory

```cpp
std::unique_ptr<LoadBalancingStrategy>
CreateLoadBalancingStrategy(const std::string &strategy_name) {
  // ... existing strategies ...
  else if (strategy_name == "byte-based") {
    return std::make_unique<ByteBasedStrategy>();
  }
  // ...
}
```

### Step 3: Update Settings Validation

```cpp
// In Settings.cpp LoadSettings()
if (strategy == "static" || strategy == "hybrid" || 
    strategy == "dynamic" || strategy == "byte-based") {
  out.loadBalancingStrategy = strategy;
}
```

### Step 4: Update Available Strategies

```cpp
std::vector<std::string> GetAvailableStrategyNames() {
  return {"static", "hybrid", "dynamic", "byte-based"};
}
```

## Refactoring Plan

To fully implement the strategy pattern, we need to:

### Phase 1: Extract Common Code

1. Create helper function for processing a chunk range
2. Extract matcher creation logic
3. Extract timing collection logic

### Phase 2: Implement Strategies

1. **StaticChunkingStrategy**
   - Copy current static chunking logic
   - No dynamic chunks

2. **HybridStrategy** (Current Implementation)
   - Initial chunks + dynamic chunks
   - Move existing code here

3. **DynamicStrategy**
   - Pure dynamic chunking
   - No initial chunks, all work via atomic counter

### Phase 3: Integrate with FileIndex

1. Add strategy parameter to `SearchAsyncWithData`
2. Load strategy from settings
3. Delegate to strategy's `LaunchSearchTasks`

### Phase 4: Testing

1. Test each strategy independently
2. Compare performance metrics
3. Verify logs show correct strategy

## Example: Strategy Selection Flow

```
1. Application starts
   ↓
2. LoadSettings() reads settings.json
   ↓
3. AppSettings.loadBalancingStrategy = "hybrid"
   ↓
4. User performs search
   ↓
5. FileIndex::SearchAsyncWithData() called
   ↓
6. CreateLoadBalancingStrategy("hybrid")
   ↓
7. Returns HybridStrategy instance
   ↓
8. strategy->LaunchSearchTasks(...)
   ↓
9. Hybrid strategy distributes work
   ↓
10. Logs show "Using strategy: hybrid"
```

## Benefits for Experimentation

### Easy A/B Testing

1. **Run with static**: Set `"loadBalancingStrategy": "static"` → test
2. **Run with hybrid**: Set `"loadBalancingStrategy": "hybrid"` → test
3. **Compare results**: Check logs for timing metrics

### Batch Experiments

```bash
# Test all strategies
for strategy in static hybrid dynamic; do
  # Update settings.json
  # Run search
  # Collect metrics
done
```

### Performance Comparison

Each strategy logs:
- Per-thread timing
- Load balance metrics
- Strategy name

Compare across strategies to find optimal approach.

## Future Enhancements

1. **Adaptive Strategy**: Automatically choose strategy based on data characteristics
2. **Strategy Parameters**: Allow tuning (e.g., chunk size) per strategy
3. **Strategy Metrics**: Track which strategy performs best for different scenarios
4. **UI Integration**: Dropdown in settings window to select strategy

## Code Organization

```
LoadBalancingStrategy.h          - Interface and factory
LoadBalancingStrategy.cpp         - Concrete strategies
FileIndex.cpp                     - Uses strategy (after refactoring)
Settings.h/cpp                    - Settings integration
docs/LOAD_BALANCING_STRATEGY_PATTERN.md - This document
```

## Notes

- **Current State**: Interface and settings integration complete
- **Next Step**: Refactor FileIndex to use strategies
- **Complexity**: Medium (requires extracting chunk assignment logic)
- **Risk**: Low (strategies are isolated, easy to test)

## References

- **Design Pattern**: Strategy Pattern (Gang of Four)
- **Related Docs**: 
  - `DYNAMIC_LOAD_BALANCING_OPTIONS.md`
  - `DYNAMIC_CHUNKING_OVERHEAD_ANALYSIS.md`
  - `HOW_TO_VERIFY_DYNAMIC_CHUNKING.md`
