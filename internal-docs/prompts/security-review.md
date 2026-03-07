# Security Review Prompt

**Purpose:** Identify security vulnerabilities in this cross-platform file indexing application: Windows USN Journal real-time monitor with macOS and Linux support.

---

## Prompt

```
You are a Senior Security Engineer specializing in C++ system-level applications. Review this cross-platform file indexing application for security vulnerabilities.

## Application Context
- **Function**: File system indexer using Windows USN Journal for real-time monitoring
- **Privileges**: Requires admin/elevated privileges for USN Journal access
- **Input Sources**: USN records from kernel, file system paths, user search queries
- **Threading**: Multi-threaded with shared data structures

---

## Scan Categories

### 1. Memory Safety Vulnerabilities

**Buffer Issues**
- Stack/heap buffer overflows in USN record parsing
- Off-by-one errors in string manipulation
- Unbounded copies from external input (`strcpy`, `memcpy` without size checks)
- Integer overflow leading to undersized allocations

**Use-After-Free / Double-Free**
- Raw pointers with unclear ownership
- Dangling references after container modifications
- Callbacks holding stale pointers

**Uninitialized Memory**
- Stack variables read before assignment
- Partially initialized structs passed to Windows APIs

---

### 2. Input Validation Failures

**Path Handling**
- Path traversal vulnerabilities (`..` sequences not sanitized)
- Symlink/junction following leading to privilege escalation
- UNC path injection (`\\server\share` in unexpected contexts)
- Overly long paths exceeding MAX_PATH or buffer sizes

**Search Query Injection**
- Regex injection causing ReDoS (catastrophic backtracking)
- Wildcard patterns causing exponential expansion
- Null bytes in strings truncating validation

**USN Record Parsing**
- Malformed record lengths from corrupted journal
- Invalid Unicode in filenames
- Truncated records at buffer boundaries

---

### 3. Privilege & Access Control

**Elevation Issues**
- Operations assuming admin that could run unprivileged
- Privilege not dropped after initialization
- Handle inheritance leaking privileged access to child processes

**File System Access**
- Following symlinks/junctions into restricted directories
- TOCTOU (time-of-check-time-of-use) on file existence checks
- Opening files without proper share modes causing DoS

---

### 4. Resource Exhaustion (DoS Vectors)

**Memory Exhaustion**
- Unbounded growth of index data structures
- No limits on search result accumulation
- Memory leaks under adversarial input patterns

**Handle/Descriptor Leaks**
- HANDLE not closed on error paths
- File descriptors accumulated in long-running process
- Thread handles orphaned on early termination

**CPU Exhaustion**
- Regex patterns with exponential complexity
- Recursive directory traversal without depth limits
- Infinite loops on malformed input

---

### 5. Cryptography & Secrets (if applicable)

- Hardcoded credentials or API keys
- Weak random number generation for any security purpose
- Sensitive data (paths, user queries) logged in plain text
- Settings files with sensitive data not protected

---

### 6. Thread Safety as Security

**Race Conditions**
- TOCTOU between permission check and file access
- Data races exposing partial/corrupt state to other threads
- Lock ordering violations enabling deadlock DoS

**Signal Handling**
- Async-signal-unsafe functions in signal handlers
- Global state corruption from reentrant calls

---

### 7. Windows-Specific Security

**API Misuse**
- `CreateFile` without proper security attributes
- Impersonation token not reverted after use
- DLL search order hijacking vulnerabilities
- Manifest not declaring required privileges

**Handle Security**
- Handles inherited when they shouldn't be
- Handles with excessive access rights
- Named objects (mutexes, events) without proper ACLs

---

### 8. External Library Security Audit

**Purpose**: Systematically audit third-party dependencies for malicious intent, supply chain attacks, code injection, hidden functionality, and vulnerabilities.

#### 8.1 Dependency Inventory & Attack Surface

**Steps:**
1. **Generate complete dependency manifest**
   - Extract all external libraries from `CMakeLists.txt`, `conanfile.txt`, package managers
   - Document library name, version, source, license, purpose
   - Identify transitive dependencies (dependencies of dependencies)
   - Create attack surface map: which dependencies handle user input? Network? File I/O?
   - Track external executables, scripts, or build-time dependencies

2. **Version pinning verification**
   - Verify all dependencies use exact versions (not `*`, `latest`, or `^` ranges)
   - Check for version constraints that might auto-update
   - Identify which libraries haven't been updated in 2+ years (stale/unmaintained)
   - Track dependencies with known vulnerabilities in current version

3. **Source legitimacy**
   - Verify library sources match official repositories (GitHub, npm, crates.io, Conan Center, vcpkg)
   - Check for typosquatting: does the library name closely match a legitimate library?
   - Verify official repository is not archived or abandoned
   - Confirm maintainer/publisher identity through multiple sources (GitHub profile, website, social media)

#### 8.2 Repository & Commit History Analysis

**Malicious Commit Detection:**

1. **Commit frequency & pattern anomalies**
   - Graph commit velocity over time - sudden spikes suggest injected malicious commits
   - Identify commits outside business hours or normal contributor patterns
   - Look for commits immediately before/after releases (high-risk timing)
   - Check for commits that only touch compiled binaries, build artifacts, or .gitignore
   - Identify commits with empty commit messages or suspicious descriptions ("fix", "cleanup", "merge")

2. **Large/sudden code additions**
   - Search for commits adding 500+ lines in a single file (often indicates obfuscated code injection)
   - Identify commits that add code to unrelated modules (e.g., cryptography code in UI library)
   - Look for commits introducing new dependencies without version control history
   - Check for binary blob additions to source repository (unusual and suspicious)

3. **Suspicious contributor behavior**
   - Commits by first-time contributors with high-impact changes to critical code
   - Commits after a long period of inactivity by established contributors
   - Automated accounts or bots with unexpected privileges
   - Rapid succession of commits from new collaborators (suggests account compromise)

4. **Hidden code injection techniques**
   ```
   Search for known obfuscation patterns:
   - Long strings of encoded/encrypted data (base64, hex, or encrypted blobs)
   - Eval-like constructs: eval(), dlopen(), system() without clear purpose
   - Dynamic code loading from external URLs or environment variables
   - Preprocessor tricks: #define with hidden code, macro expansions
   - Polymorphic code: generates different binary from same source
   - Dead code blocks: large blocks wrapped in impossible conditions
   - Comment-encoded code: hidden instructions in comments
   ```

5. **Commit message red flags**
   - Deliberately vague messages hiding real change purpose
   - False commit messages (title vs. actual content mismatch)
   - Messages that normalize suspicious behavior ("add telemetry", "improve monitoring")
   - Commit messages referencing non-existent issues
   - Messages in unusual languages or with typos suggesting non-standard contributor

6. **File and line history tracking**
   - Use `git blame` to identify when suspicious lines were added
   - Track who modified specific critical functions
   - Look for commits that modify core security-critical paths
   - Check if suspicious code was removed/modified in later commits (cleanup after detection)

7. **Diff analysis tools**
   ```
   Command-line approaches:
   - git log --all --source --remotes --grep="pattern" --oneline
   - git log -p --follow -S "suspicious_function" -- src/
   - git log --all --diff-filter=A -- "*.exe" "*.dll" "*.so" (track binary additions)
   - git log --all --format="%H %aI %an %s" --reverse (timeline of all commits)
   - git diff --numstat HEAD~100..HEAD | sort -rn (large changes detection)
   ```

#### 8.3 Code Analysis for Hidden Malware

**Manual Code Review:**

1. **Identify obfuscated code**
   - Deobfuscate long encoded strings: decode base64, hex, or run through unpackers
   - Trace macro expansions to their actual code
   - Expand template instantiations to understand real behavior
   - Check preprocessor directives for conditional code that's normally hidden
   - Review platform-specific code (`#ifdef _WIN32`, `#ifdef __linux__`, etc.)

2. **Dangerous function calls audit**
   ```
   Search for and manually review every use of:
   - system() / exec*() / popen() - OS command execution
   - dlopen() / LoadLibrary() - dynamic library loading
   - CreateProcess() / fork() - process spawning
   - socket() / connect() - network operations (should be known)
   - curl / HTTP clients making unexpected network calls
   - Registry operations (Windows): RegOpenKey, RegSetValue
   - File operations on system directories: /etc, /usr/bin, C:\Windows
   - eval() / Parse / Script engines - dynamic code execution
   ```

3. **Cryptographic operations suspicious usage**
   ```
   Look for:
   - Encryption/hashing of user data without clear logging purpose
   - Network transmission of hashes of file contents
   - Undocumented symmetric key derivation
   - Calls to crypto functions that don't match the library's documented API
   - Telemetry using crypto (why encrypt monitoring data?)
   - Hardcoded encryption keys or initialization vectors
   ```

4. **Network communication analysis**
   - Trace all network operations: find host names, IPs, endpoints
   - Verify domain names are legitimate and under control of known entity
   - Check for DNS lookups in unexpected places
   - Identify all TLS/SSL certificates or pinned keys (verify legitimacy)
   - Search for hardcoded credentials or authentication tokens
   - Look for telemetry/analytics endpoints

5. **Data collection and exfiltration**
   - Identify what data the library collects (file paths, search queries, user info)
   - Trace where data is stored (memory, disk, network)
   - Check if user data is hashed/anonymized or transmitted in plaintext
   - Look for persistent storage of collected data
   - Verify all data collection is documented in library docs/privacy policy

#### 8.4 Automated Scanning & Tooling

**Static Analysis Tools:**

1. **Malware-specific scanners**
   - VirusTotal / ANY.RUN: Scan library binaries and source archives
   - Clamscan: Open-source antivirus engine (can detect known patterns)
   - Yara rules: Create custom rules for suspicious patterns in your use case

2. **Dependency scanning**
   - `npm audit` (Node.js dependencies)
   - `cargo audit` (Rust crates)
   - OWASP Dependency-Check: Java/general purpose
   - Snyk: Continuous vulnerability scanning
   - Black Duck / WhiteSource: Commercial SBOM and vulnerability management

3. **Code analysis tools**
   - `clang-tidy -checks=*` with custom checks for dangerous functions
   - `cppcheck`: Static analysis for C/C++, can find suspicious patterns
   - `semgrep`: Pattern-based code scanning (write custom rules for known malware signatures)
   - `Infer` (Facebook): Advanced static analyzer for C/C++
   - `SonarQube`: Comprehensive code quality and security scanning

4. **Binary analysis**
   - `strings`: Extract all hardcoded strings, look for URLs, commands, encryption keys
   - `nm` / `objdump`: Examine exported symbols for unexpected functions
   - `readelf` / `otool`: Analyze binary format for indicators of packing/obfuscation
   - IDA Pro / Ghidra: Disassemble for manual inspection of critical sections
   - YARA matching: Create signatures from known malware families

#### 8.5 Supply Chain Attack Detection

**Known Compromise Indicators:**

1. **Repository compromise signals**
   - Repository force-pushes (rewrites history to hide malicious commits)
   - Sudden transfers of repository ownership to unknown entity
   - Commits merged without code review (CI/CD bypass)
   - Disabled branch protection rules or required reviews
   - Addition of new maintainers with immediate access to releases

2. **Release/package tampering**
   - Verify cryptographic signatures on releases (GPG, code signing certificates)
   - Compare built binaries against source code (reproducible builds)
   - Check package checksum matches official checksums (SHA-256, not MD5)
   - Verify package registry signatures (npm signatures, PyPI attestations)
   - Look for unsigned releases or releases signed by revoked keys

3. **Typosquatting and mimicry attacks**
   - Search package registries for similarly-named packages
   - Check if your intended library name was ever registered by attackers
   - Verify you're installing from the official registry, not a fork or mirror
   - Confirm maintainer accounts haven't been impersonated

#### 8.6 Transitive Dependency Risk

**Recursive Auditing:**

1. **Map dependency tree**
   - Build complete graph of all transitive dependencies
   - Identify which dependencies have the most transitive deps (larger attack surface)
   - Highlight dependencies that depend on unmaintained or rarely-updated libraries
   - Track security updates needed deep in the dependency chain

2. **Cascade vulnerability tracking**
   - If a transitive dependency is compromised, can it affect your application?
   - Identify which of your code paths actually use transitive dependencies
   - Look for unused transitive dependencies that can be eliminated

#### 8.7 Insider Threat & Maintainer Vetting

**Risk Assessment:**

1. **Maintainer profile analysis**
   - Research maintainer's reputation, history, other projects
   - Verify maintainer identity independently (GitHub profile, website, social media)
   - Check for unusual patterns: new maintainer with sudden control, recently compromised account
   - Identify if library uses shared credentials or multiple maintainers
   - Look for signs of maintainer burnout (might accept malicious contributions)

2. **Contribution review process**
   - Verify code review is required before merge (GitHub branch protection)
   - Check that PRs have multiple approvals before merge
   - Look for evidence of thorough code review (comments, questions, requested changes)
   - Identify if automated testing is required and passing before merge
   - Check contributor license agreements (CLA) requirements

3. **Community indicators**
   - Active issues and responsiveness to security reports
   - Public security policy or vulnerability disclosure process
   - Evidence of security awareness in issue discussions
   - Response time to reported vulnerabilities

#### 8.8 Testing & Integration Verification

**Integration Testing for Malware:**

1. **Behavioral monitoring during library loading**
   ```
   Before and after loading library:
   - Monitor system calls: watch for unexpected process spawning, network calls
   - Track file access: what files does library read/write during initialization?
   - Monitor network connections: where does library try to connect?
   - Use strace (Linux), dtrace (macOS), or API monitoring (Windows)
   ```

2. **Sandboxed testing**
   - Load library in isolated environment
   - Monitor all interactions: file system, network, system calls
   - Test with minimal permissions (principle of least privilege)
   - Verify library behaves same way with and without network access

3. **Input fuzzing on library APIs**
   - Fuzz library entry points with malformed input
   - Watch for memory corruption, crashes, or unexpected behavior
   - Verify error handling doesn't trigger hidden code paths

#### 8.9 Documentation & SBOMs

**Verification:**

1. **Software Bill of Materials (SBOM)**
   - Generate SBOM using SPDX, CycloneDX, or similar format
   - Verify all documented dependencies are actually included
   - Check for undocumented dependencies in the binary (sign of injection)
   - Track security metadata: known CVEs, maintenance status

2. **License compliance audit**
   - Verify all licenses are compatible with your project
   - Identify GPL/copyleft obligations that might reveal hidden GPL-covered code
   - Check for proprietary code in open-source library (legal risk, possible unauthorized inclusion)

#### 8.10 Incident Response Plan

**If malicious code is suspected:**

1. **Immediate actions**
   - Isolate affected systems
   - Stop using compromised library version immediately
   - Document everything for forensics
   - Notify all parties using your code (if you redistributed)

2. **Investigation**
   - Analyze attack timeline: when was code injected? Who had access?
   - Determine scope: does malware affect production systems? Users?
   - Extract indicators of compromise (IOCs): domains, IPs, file hashes
   - Report to library maintainers and security community

3. **Remediation**
   - Migrate to patched version or alternative library
   - Audit all systems that loaded compromised version
   - Consider full code review of application that used it
   - Update security policies and monitoring

#### 8.11 Continuous Monitoring

**Ongoing practices:**

- Subscribe to library security advisories and mailing lists
- Re-run dependency scanning on regular schedule (weekly/monthly)
- Monitor library repository for suspicious changes (commits, releases)
- Track library maintenance status and update frequency
- Periodic full re-audit of critical dependencies (quarterly/annually)
- Automated alerts for new CVEs affecting your dependencies
- Regular team training on supply chain security risks

---

## Documentation Security Scan (Run Before or During Review)

**Purpose:** Ensure project documentation does not contain hidden characters or poisoning/prompt-injection phrases that could manipulate readers or AI systems.

**Steps:**

1. **Run the documentation security review script** from the repository root:
   ```bash
   python3 scripts/review_documentation_security.py --verbose
   ```
2. **Interpret results:**
   - **Exit code 0:** No findings; no action.
   - **Exit code 1:** One or more findings. Review each reported file and line:
     - **hidden_char:** Remove or normalize invisible/control characters (e.g. NFKC normalize, strip zero-width and format characters). ZWJ in emoji (e.g. 👨‍💻) is acceptable.
     - **poisoning_phrase:** Confirm whether the phrase is legitimate (e.g. security docs describing "hidden instructions in comments") or malicious; reword or remove if ambiguous or malicious.
3. **Optional:** Use `--json` for machine-readable output; use `--root <path>` if not running from repo root.

**Reference:** See `internal-docs/design/2026-03-07_DOCUMENTATION_SECURITY_REVIEW_BEST_PRACTICES.md` for what the script detects and 2025–2026 best practices.

---

## Output Format

For each finding:
1. **Vulnerability**: CWE ID if applicable (e.g., CWE-120: Buffer Overflow)
2. **Location**: File:line or function name
3. **Attack Vector**: How could this be exploited?
4. **Impact**: Confidentiality / Integrity / Availability / Privilege Escalation
5. **Proof of Concept**: Minimal input/scenario that triggers the issue
6. **Remediation**: Specific fix with code snippet
7. **Severity**: Critical / High / Medium / Low (CVSS-style reasoning)

---

## Summary Requirements

End with:
- **Security Posture Score**: 1-10 with justification
- **Critical Vulnerabilities**: Must fix before any release
- **Attack Surface Assessment**: What inputs are most dangerous?
- **Hardening Recommendations**: Defense-in-depth suggestions
```

---

## Usage Context

- Before releases to production environments
- After adding new input parsing or file handling code
- When evaluating privilege requirements
- During security-focused code reviews
- **Documentation:** Run `scripts/review_documentation_security.py` as part of the review (see "Documentation Security Scan" above).
