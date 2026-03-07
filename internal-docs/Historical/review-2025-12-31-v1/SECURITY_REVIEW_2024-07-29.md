# Security Review - 2024-07-29

## Executive Summary
- **Health Score**: 5/10
- **Critical Issues**: 0
- **High Issues**: 2
- **Total Findings**: 3
- **Overall Assessment**: The application has a decent security posture due to its intelligent routing of search patterns, which limits the exposure of the powerful but dangerous `std::regex` engine. However, it suffers from two high-risk Denial-of-Service (DoS) vulnerabilities—one related to unbounded cache growth and another to catastrophic backtracking in the glob matcher. These issues should be remediated before any public release.

## Findings

### High
**1. Unbounded Regex Cache Growth Leading to DoS**
- **Vulnerability**: CWE-400: Uncontrolled Resource Consumption ('Denial-of-Service')
- **Location**: `StdRegexUtils.h`, `ThreadLocalRegexCache` class
- **Attack Vector**: An attacker (or a user with many unique, complex regex patterns) can continuously issue searches with the `rs:` prefix. Each unique pattern is compiled and stored in a `thread_local` cache. Because this cache has no size limit, it can grow indefinitely.
- **Impact**: Availability. A thread's cache could consume all available memory for that thread, leading to a crash or severe performance degradation for that part of the application.
- **Proof of Concept**: A script that sends a few hundred thousand unique regex searches (e.g., `rs:a{1}`, `rs:a{2}`, etc.) would cause the memory usage of one of the search threads to grow significantly.
- **Remediation**: Implement a simple Least Recently Used (LRU) eviction policy for the `ThreadLocalRegexCache`. A small cache size (e.g., 64 or 128 entries) would be sufficient to capture the performance benefits for common searches while preventing unbounded growth.
- **Severity**: High

**2. Catastrophic Backtracking in Glob Matcher Leading to ReDoS**
- **Vulnerability**: CWE-1333: Inefficient Regular Expression Complexity
- **Location**: `SimpleRegex.h`, `GlobMatch` and `GlobMatchI` functions
- **Attack Vector**: An attacker can provide a specially crafted glob pattern that causes the recursive `GlobMatch` function to exhibit exponential behavior.
- **Impact**: Availability. A malicious glob pattern can cause the search thread to consume 100% of a CPU core for an extended period (seconds or even minutes), effectively locking up that part of the search functionality and preventing other searches from being processed in a timely manner.
- **Proof of Concept**: Searching for a pattern like `*a*a*a*a*a*b` in a large file index will cause a noticeable spike in CPU usage and a long delay in search results.
- **Remediation**: Replace the recursive glob matcher with an iterative, dynamic programming-based approach. This will reduce the complexity from exponential to polynomial (O(m*n), where m is the pattern length and n is the text length), which is sufficient to mitigate the ReDoS vulnerability.
- **Severity**: High

### Medium
**1. Potential for Quadratic Complexity in Simple Regex Matcher**
- **Vulnerability**: CWE-1333: Inefficient Regular Expression Complexity
- **Location**: `SimpleRegex.h`, `matchstar` and `matchstarI` functions
- **Attack Vector**: Similar to the glob matcher, a crafted pattern can cause quadratic behavior.
- **Impact**: Availability. While not as severe as the exponential complexity of the glob matcher, a malicious pattern could still cause a noticeable performance degradation.
- **Proof of Concept**: Searching for a pattern like `^a*a*a*a*b` against a long string of 'a's will be noticeably slower than a simple substring search.
- **Remediation**: For this simple regex implementation, the risk is relatively low. However, if this becomes a problem, the implementation could be replaced with a more robust one based on finite automata (e.g., a simple NFA simulation). Given the other, more pressing issues, this is a lower priority.
- **Severity**: Medium

## Summary
- **Security Posture Score**: 5/10. The application is not currently secure enough for a public release due to the high-impact DoS vulnerabilities.
- **Critical Vulnerabilities**: None that would lead to code execution or privilege escalation, but the DoS issues are severe.
- **Attack Surface Assessment**: The primary attack surface is the search input field, which accepts various pattern syntaxes. The `rs:` (std::regex) and glob (`*`, `?`) patterns are the most dangerous.
- **Hardening Recommendations**:
  1. Implement an LRU cache for `std::regex` compilation.
  2. Replace the recursive glob matcher with a DP-based implementation.
  3. Consider adding a timeout to all regex and glob matching operations to prevent excessively long computations.
