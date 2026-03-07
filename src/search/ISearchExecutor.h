#pragma once

// Forward declaration (SearchThreadPool is only used as a reference)
class SearchThreadPool;

/**
 * @file ISearchExecutor.h
 * @brief Interface for search execution operations used by load balancing strategies
 *
 * This interface decouples load balancing strategies from the concrete
 * ParallelSearchEngine implementation, enabling:
 * - Better testability (can mock the interface)
 * - Reduced coupling (strategies don't depend on ParallelSearchEngine)
 * - Flexibility (can swap implementations)
 *
 * DESIGN DECISION:
 * Only GetThreadPool() is virtualized because:
 * - It's the only instance method needed by strategies
 * - Static methods (CreatePatternMatchers, ProcessChunkRange, etc.) remain static
 * - This minimizes virtual call overhead (~1-2 nanoseconds per search)
 *
 * @see ParallelSearchEngine for the concrete implementation
 * @see LoadBalancingStrategy for usage
 */
class ISearchExecutor {
public:
  virtual ~ISearchExecutor() = default;
  
protected:
  // Protected default constructor (interface class)
  ISearchExecutor() = default;
  
public:
  // Non-copyable, non-movable (interface class)
  ISearchExecutor(const ISearchExecutor&) = delete;
  ISearchExecutor& operator=(const ISearchExecutor&) = delete;
  ISearchExecutor(ISearchExecutor&&) = delete;
  ISearchExecutor& operator=(ISearchExecutor&&) = delete;

  /**
   * Get the thread pool used for executing search tasks.
   * 
   * @return Reference to the SearchThreadPool
   * 
   * @note This is the only virtual method in the interface to minimize
   *       overhead. Static methods are accessed directly via ParallelSearchEngine.
   */
  [[nodiscard]] virtual SearchThreadPool& GetThreadPool() const = 0;
};

