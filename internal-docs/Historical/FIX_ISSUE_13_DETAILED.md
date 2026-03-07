# Fixing Issue 13: Static Variables in Implementation - Detailed Guide

## Problem Statement

**Current Issue:** The `UsnMonitor` implementation uses static global variables in `UsnMonitor.cpp`:
- `static UsnJournalQueue* g_usnQueue = nullptr;`
- `static std::thread g_readerThread;`
- `static std::thread g_processorThread;`
- `static std::mutex g_monitoringMutex;`
- `static MonitoringConfig g_currentConfig;`

**Problems this causes:**
1. **Testing Difficulty**: Cannot create multiple instances for testing different scenarios
2. **Hidden Dependencies**: Thread functions access global state (`g_fileIndex`, `g_monitoringActive`, etc.) making dependencies unclear
3. **Tight Coupling**: Code is tightly coupled to global state, making it hard to mock or replace components
4. **No Isolation**: All tests share the same global state, causing test interference
5. **Single Instance Limitation**: Cannot monitor multiple volumes simultaneously

## Solution: Class-Based Design with Dependency Injection

### Step 1: Design the Class Interface

Create a `UsnMonitor` class that encapsulates all state and provides a clean interface:

```cpp
// UsnMonitor.h
class UsnMonitor {
public:
  // Constructor - takes dependencies via constructor injection
  explicit UsnMonitor(
    FileIndex& fileIndex,                    // Reference to file index
    const MonitoringConfig& config = MonitoringConfig{}  // Configuration
  );
  
  // Destructor - ensures proper cleanup
  ~UsnMonitor();
  
  // Non-copyable, movable
  UsnMonitor(const UsnMonitor&) = delete;
  UsnMonitor& operator=(const UsnMonitor&) = delete;
  UsnMonitor(UsnMonitor&&) = default;
  UsnMonitor& operator=(UsnMonitor&&) = default;
  
  // Start/Stop monitoring
  void Start();
  void Stop();
  
  // Status queries
  bool IsActive() const { return monitoringActive_; }
  size_t GetQueueSize() const;
  size_t GetDroppedBufferCount() const;
  
  // Configuration
  const MonitoringConfig& GetConfig() const { return config_; }
  void UpdateConfig(const MonitoringConfig& config);

private:
  // Internal thread functions (now member functions)
  void ReaderThread();
  void ProcessorThread();
  
  // Member variables (encapsulated state)
  FileIndex& fileIndex_;              // Reference to file index (dependency)
  MonitoringConfig config_;            // Current configuration
  std::unique_ptr<UsnJournalQueue> queue_;  // Queue (owned by class)
  std::thread readerThread_;           // Reader thread
  std::thread processorThread_;        // Processor thread
  mutable std::mutex mutex_;           // Protects start/stop operations
  std::atomic<bool> monitoringActive_; // Active flag
};
```

### Step 2: Refactor Thread Functions to Member Functions

Convert free functions to private member functions that access class state:

```cpp
// UsnMonitor.cpp
void UsnMonitor::ReaderThread() {
  SetThreadName("USN-Reader");
  
  try {
    LOG_INFO("USN Reader thread started");
    
    // Use member variable instead of global
    VolumeHandle hVol(config_.volumePath.c_str());
    
    if (!hVol.valid()) {
      std::string errorMsg = LOG_ERROR_BUILD_AND_GET(
        "Failed to open volume: " << config_.volumePath 
        << ". Error: " << GetLastError());
      std::cerr << errorMsg << std::endl;
      monitoringActive_ = false;  // Use member variable
      return;
    }
    
    // ... rest of implementation using member variables ...
    
    // Push to queue using member variable
    if (!queue_->Push(std::move(queueBuffer))) {
      // Handle queue full
    }
  } catch (...) {
    monitoringActive_ = false;
  }
}

void UsnMonitor::ProcessorThread() {
  SetThreadName("USN-Processor");
  
  try {
    // ... implementation using fileIndex_ and queue_ member variables ...
    std::vector<char> buffer;
    while (queue_->Pop(buffer)) {
      // Process using fileIndex_ member variable
      // fileIndex_.Insert(...);
    }
  } catch (...) {
    // Error handling
  }
}
```

### Step 3: Implement Start/Stop Methods

```cpp
void UsnMonitor::Start() {
  std::lock_guard<std::mutex> lock(mutex_);
  
  if (monitoringActive_) {
    LOG_WARNING("Monitoring already active, stopping before restart");
    // Release lock to avoid deadlock
    lock.~lock_guard();
    Stop();
    lock = std::lock_guard<std::mutex>(mutex_);
  }
  
  // Clean up old threads if any
  if (readerThread_.joinable()) {
    readerThread_.join();
  }
  if (processorThread_.joinable()) {
    processorThread_.join();
  }
  
  // Create queue with configured size
  queue_ = std::make_unique<UsnJournalQueue>(config_.maxQueueSize);
  
  LOG_INFO_BUILD("Starting USN monitoring on volume: " << config_.volumePath);
  
  monitoringActive_ = true;
  readerThread_ = std::thread(&UsnMonitor::ReaderThread, this);
  processorThread_ = std::thread(&UsnMonitor::ProcessorThread, this);
}

void UsnMonitor::Stop() {
  std::lock_guard<std::mutex> lock(mutex_);
  
  if (!monitoringActive_) {
    return;
  }
  
  LOG_INFO("Stopping USN monitoring");
  monitoringActive_ = false;
  
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  
  if (readerThread_.joinable()) {
    readerThread_.join();
  }
  
  if (queue_) {
    queue_->Stop();
  }
  
  if (processorThread_.joinable()) {
    processorThread_.join();
  }
  
  queue_.reset();
  LOG_INFO("USN monitoring stopped");
}
```

### Step 4: Update Constructor and Destructor

```cpp
UsnMonitor::UsnMonitor(FileIndex& fileIndex, const MonitoringConfig& config)
  : fileIndex_(fileIndex)
  , config_(config)
  , monitoringActive_(false)
{
  // Queue and threads are created in Start()
}

UsnMonitor::~UsnMonitor() {
  Stop();  // Ensure cleanup
}
```

### Step 5: Update Global Interface (Backward Compatibility)

To maintain backward compatibility with existing code, provide wrapper functions:

```cpp
// UsnMonitor.h - Keep for backward compatibility
namespace UsnMonitorLegacy {
  // Global instance (for backward compatibility)
  extern std::unique_ptr<UsnMonitor> g_monitor;
  
  // Legacy free functions that delegate to global instance
  inline void StartMonitoring(const MonitoringConfig& config = MonitoringConfig{}) {
    if (!g_monitor) {
      // Initialize with global file index
      g_monitor = std::make_unique<UsnMonitor>(g_fileIndex, config);
    }
    g_monitor->Start();
  }
  
  inline void StopMonitoring() {
    if (g_monitor) {
      g_monitor->Stop();
    }
  }
}

// For backward compatibility, use the namespace
using UsnMonitorLegacy::StartMonitoring;
using UsnMonitorLegacy::StopMonitoring;
```

### Step 6: Migration Strategy

**Option A: Gradual Migration (Recommended)**
1. Implement the class-based design alongside existing code
2. Keep legacy functions that delegate to a global instance
3. Gradually migrate callers to use the class directly
4. Eventually remove legacy functions

**Option B: Complete Refactor**
1. Implement the class
2. Update all callers immediately
3. Remove static variables

### Step 7: Testing Benefits

With the class-based design, testing becomes much easier:

```cpp
// Test example
TEST(UsnMonitorTest, MultipleInstances) {
  FileIndex index1, index2;
  MonitoringConfig config1, config2;
  config1.volumePath = "\\\\.\\C:";
  config2.volumePath = "\\\\.\\D:";
  
  UsnMonitor monitor1(index1, config1);
  UsnMonitor monitor2(index2, config2);
  
  // Can test multiple instances simultaneously
  monitor1.Start();
  monitor2.Start();
  
  // Test isolation - each has its own state
  EXPECT_TRUE(monitor1.IsActive());
  EXPECT_TRUE(monitor2.IsActive());
  
  monitor1.Stop();
  monitor2.Stop();
}

TEST(UsnMonitorTest, MockFileIndex) {
  MockFileIndex mockIndex;
  UsnMonitor monitor(mockIndex);
  
  monitor.Start();
  
  // Can verify interactions with mock
  EXPECT_CALL(mockIndex, Insert(_, _, _, _)).Times(AtLeast(1));
  
  // ... test behavior ...
  
  monitor.Stop();
}
```

## Detailed Implementation Steps

### Step-by-Step Refactoring Process

1. **Create the class skeleton**
   - Add `UsnMonitor` class declaration to `UsnMonitor.h`
   - Move static variables to member variables
   - Convert free functions to private member functions

2. **Update thread functions**
   - Change `void UsnReaderThread()` to `void UsnMonitor::ReaderThread()`
   - Replace all global variable accesses with member variable accesses
   - Pass `this` when creating threads: `std::thread(&UsnMonitor::ReaderThread, this)`

3. **Handle dependencies**
   - Pass `FileIndex&` via constructor (dependency injection)
   - Store as member reference: `FileIndex& fileIndex_;`
   - Update all `g_fileIndex` references to `fileIndex_`

4. **Update StartMonitoring/StopMonitoring**
   - Convert to `Start()` and `Stop()` member functions
   - Move all logic into class methods
   - Use member variables instead of static variables

5. **Maintain backward compatibility**
   - Create legacy wrapper functions if needed
   - Or update all callers immediately

6. **Update main_gui.cpp**
   ```cpp
   // Old:
   StartMonitoring();
   
   // New (Option 1 - Direct use):
   static UsnMonitor monitor(g_fileIndex);
   monitor.Start();
   
   // New (Option 2 - Legacy wrapper):
   StartMonitoring();  // Still works if you keep wrappers
   ```

## Benefits of This Approach

1. **Testability**
   - Can create multiple instances
   - Can inject mock dependencies
   - Tests don't interfere with each other

2. **Clarity**
   - Dependencies are explicit in constructor
   - No hidden global state
   - Clear ownership of resources

3. **Flexibility**
   - Can monitor multiple volumes
   - Can have different configurations per instance
   - Easier to extend functionality

4. **Maintainability**
   - All related state is together
   - Clear lifetime management
   - Easier to reason about code

## Potential Challenges and Solutions

### Challenge 1: Thread Function Signatures
**Problem**: `std::thread` requires callable that can be invoked, but member functions need `this`.

**Solution**: Use lambda or `std::bind`:
```cpp
readerThread_ = std::thread([this]() { this->ReaderThread(); });
// OR
readerThread_ = std::thread(&UsnMonitor::ReaderThread, this);
```

### Challenge 2: Accessing FileIndex in Threads
**Problem**: Thread functions need access to `fileIndex_` member.

**Solution**: Store as member reference, accessible in all member functions.

### Challenge 3: Backward Compatibility
**Problem**: Existing code calls `StartMonitoring()` and `StopMonitoring()`.

**Solution**: Provide wrapper functions or update all callers.

### Challenge 4: Global State Still Needed
**Problem**: Some globals like `g_fileIndex` are still needed.

**Solution**: 
- Pass via constructor (dependency injection)
- Or keep minimal global state for shared resources
- Document which globals are intentional vs. accidental

## Complete Example: Before and After

### Before (Current Implementation)
```cpp
// UsnMonitor.cpp
static UsnJournalQueue* g_usnQueue = nullptr;
static std::thread g_readerThread;
static std::thread g_processorThread;

void UsnReaderThread() {
  // Accesses g_fileIndex, g_monitoringActive, g_currentConfig globally
  VolumeHandle hVol(g_currentConfig.volumePath.c_str());
  // ...
  g_usnQueue->Push(buffer);
}

void StartMonitoring(const MonitoringConfig& config) {
  g_currentConfig = config;
  g_usnQueue = new UsnJournalQueue(config.maxQueueSize);
  g_readerThread = std::thread(UsnReaderThread);
}
```

### After (Class-Based Design)
```cpp
// UsnMonitor.h
class UsnMonitor {
  FileIndex& fileIndex_;
  MonitoringConfig config_;
  std::unique_ptr<UsnJournalQueue> queue_;
  std::thread readerThread_;
  std::thread processorThread_;
  std::atomic<bool> monitoringActive_;
  
  void ReaderThread();
  void ProcessorThread();
  
public:
  UsnMonitor(FileIndex& fileIndex, const MonitoringConfig& config);
  ~UsnMonitor();
  void Start();
  void Stop();
};

// UsnMonitor.cpp
void UsnMonitor::ReaderThread() {
  VolumeHandle hVol(config_.volumePath.c_str());
  // ...
  queue_->Push(buffer);
}

void UsnMonitor::Start() {
  queue_ = std::make_unique<UsnJournalQueue>(config_.maxQueueSize);
  monitoringActive_ = true;
  readerThread_ = std::thread(&UsnMonitor::ReaderThread, this);
}
```

## Migration Checklist

- [ ] Create `UsnMonitor` class declaration
- [ ] Move static variables to member variables
- [ ] Convert thread functions to member functions
- [ ] Update all global variable accesses to member accesses
- [ ] Implement constructor and destructor
- [ ] Implement `Start()` and `Stop()` methods
- [ ] Update `main_gui.cpp` to use new interface
- [ ] Add backward compatibility wrappers (if needed)
- [ ] Remove static variables
- [ ] Update tests to use new class-based design
- [ ] Verify all functionality still works

## Conclusion

Refactoring to a class-based design eliminates the testing and maintainability issues caused by static variables. The key is to:
1. Encapsulate all state in the class
2. Use dependency injection for external dependencies
3. Convert free functions to member functions
4. Maintain backward compatibility during migration

This refactoring is a significant change but provides substantial benefits for testing, maintainability, and future extensibility.
