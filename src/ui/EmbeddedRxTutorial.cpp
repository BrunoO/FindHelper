/**
 * @file ui/EmbeddedRxTutorial.cpp
 * @brief Embedded rx: tutorial HTML documentation
 */

#include "ui/EmbeddedRxTutorial.h"

#include <filesystem>
#include <fstream>
#include <string>

#include "platform/FileOperations.h"
#include "utils/Logger.h"

namespace ui {

namespace {

// MSVC limit (C2026): A single string literal token cannot exceed 16,380 characters
// before concatenation. We split the HTML payload into adjacent raw string literals
// (each < 6 KB), which C++ concatenates into a single contiguous string_view at compile time.
constexpr std::string_view kRxTutorialHtml =
    R"rxtut(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Tutorial: Structured Regex (rx:) in FindHelper</title>
  <style>
    :root {
      --bg: #0d1117;
      --card-bg: #161b22;
      --border: #30363d;
      --text: #c9d1d9;
      --heading: #f0f6fc;
      --accent: #58a6ff;
      --cyan: #4ec9b0;
      --amber: #ce9178;
      --red: #f44747;
      --purple: #bc89bd;
      --code-bg: #090d13;
      --code-text: #e6edf3;
      --tag-bg: #1f242c;
    }

    * {
      box-sizing: border-box;
      margin: 0;
      padding: 0;
    }

    body {
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
      background-color: var(--bg);
      color: var(--text);
      line-height: 1.6;
      padding: 2.5rem 1rem;
    }

    .container {
      max-width: 920px;
      margin: 0 auto;
    }

    header {
      margin-bottom: 2.5rem;
      border-bottom: 1px solid var(--border);
      padding-bottom: 1.5rem;
    }

    h1 {
      color: var(--heading);
      font-size: 2.4rem;
      font-weight: 700;
      margin-bottom: 0.5rem;
      display: flex;
      align-items: center;
      gap: 0.75rem;
    }

    .badge {
      display: inline-block;
      font-size: 0.85rem;
      font-weight: 600;
      padding: 0.2rem 0.6rem;
      border-radius: 9999px;
      background: rgba(78, 201, 176, 0.15);
      color: var(--cyan);
      border: 1px solid rgba(78, 201, 176, 0.35);
      vertical-align: middle;
    }

    .subtitle {
      font-size: 1.15rem;
      color: #8b949e;
    }

    section {
      background: var(--card-bg);
      border: 1px solid var(--border);
      border-radius: 8px;
      padding: 1.75rem;
      margin-bottom: 2rem;
    }

    h2 {
      color: var(--heading);
      font-size: 1.45rem;
      margin-bottom: 1rem;
      display: flex;
      align-items: center;
      gap: 0.5rem;
      border-bottom: 1px solid var(--border);
      padding-bottom: 0.5rem;
    }

    h3 {
      color: var(--cyan);
      font-size: 1.15rem;
      margin: 1.25rem 0 0.5rem;
    }

    p {
      margin-bottom: 0.85rem;
    }

    ul, ol {
      margin-left: 1.5rem;
      margin-bottom: 1rem;
    }

    li {
      margin-bottom: 0.4rem;
    }

    code {
      font-family: ui-monospace, SFMono-Regular, "SF Mono", Menlo, Consolas, "Liberation Mono", monospace;
      background: var(--code-bg);
      color: var(--code-text);
      padding: 0.2rem 0.45rem;
      border-radius: 4px;
      font-size: 0.9em;
      border: 1px solid rgba(255, 255, 255, 0.08);
    }

    pre {
      background: var(--code-bg);
      color: var(--code-text);
      padding: 1rem 1.25rem;
      border-radius: 6px;
      border: 1px solid var(--border);
      overflow-x: auto;
      font-family: ui-monospace, SFMono-Regular, "SF Mono", Menlo, Consolas, "Liberation Mono", monospace;
      font-size: 0.92rem;
      margin: 0.85rem 0 1.2rem;
      line-height: 1.5;
    }

    .comment { color: #8b949e; font-style: italic; }
    .keyword { color: var(--purple); font-weight: bold; }
    .operator { color: var(--accent); }
    .string { color: var(--amber); }

    table {
      width: 100%;
      border-collapse: collapse;
      margin: 1rem 0;
      font-size: 0.95rem;
    }

    th, td {
      border: 1px solid var(--border);
      padding: 0.75rem 1rem;
      text-align: left;
    }

    th {
      background: rgba(255, 255, 255, 0.03);
      color: var(--heading);
      font-weight: 600;
    }

    .ui-indicator {
      display: inline-flex;
      align-items: center;
      gap: 0.5rem;
      font-weight: 600;
    }

    .dot {
      width: 12px;
      height: 12px;
      border-radius: 50%;
      display: inline-block;
    }
    .dot-cyan { background-color: var(--cyan); box-shadow: 0 0 8px rgba(78, 201, 176, 0.6); }
    .dot-amber { background-color: var(--amber); box-shadow: 0 0 8px rgba(206, 145, 120, 0.6); }
    .dot-red { background-color: var(--red); box-shadow: 0 0 8px rgba(244, 71, 71, 0.6); }

    .callout {
      border-left: 4px solid var(--accent);
      background: rgba(88, 166, 255, 0.08);
      padding: 1rem 1.25rem;
      border-radius: 0 6px 6px 0;
      margin: 1.25rem 0;
    }

    .callout-title {
      font-weight: 600;
      color: var(--accent);
      margin-bottom: 0.35rem;
    }

    .example-grid {
      display: grid;
      grid-template-columns: 1fr;
      gap: 1.25rem;
      margin-top: 1rem;
    }

    @media (min-width: 768px) {
      .example-grid {
        grid-template-columns: 1fr 1fr;
      }
    }

    .example-card {
      background: var(--code-bg);
      border: 1px solid var(--border);
      border-radius: 6px;
      padding: 1rem;
    }

    .example-card h4 {
      color: var(--heading);
      font-size: 1rem;
      margin-bottom: 0.5rem;
    }

    .syntax-tree {
      width: 100%;
      height: auto;
      background: var(--code-bg);
      border: 1px solid var(--border);
      border-radius: 6px;
      margin: 1rem 0;
      display: block;
    }

    .mermaid {
      background: var(--code-bg);
      border: 1px solid var(--border);
      border-radius: 6px;
      padding: 1.25rem;
      text-align: center;
      margin: 1rem 0;
      overflow-x: auto;
    }

    .rule-card {
      background: var(--code-bg);
      border: 1px solid var(--border);
      border-left: 4px solid var(--cyan);
      border-radius: 0 6px 6px 0;
      padding: 1rem 1.25rem;
      margin: 1.2rem 0;
    }

    .rule-card.warning {
      border-left-color: var(--amber);
    }

    .rule-card.error {
      border-left-color: var(--red);
    }

    .rule-card.purple {
      border-left-color: var(--purple);
    }

    .rule-card h4 {
      color: var(--heading);
      font-size: 1.05rem;
      margin-bottom: 0.45rem;
    }

    footer {
      text-align: center;
      margin-top: 3rem;
      font-size: 0.85rem;
      color: #8b949e;
      border-top: 1px solid var(--border);
      padding-top: 1.5rem;
    }
  </style>
  <script type="module">
    import mermaid from 'https://cdn.jsdelivr.net/npm/mermaid@10/dist/mermaid.esm.min.mjs';
    mermaid.initialize({ startOnLoad: true, theme: 'dark', securityLevel: 'loose' });
  </script>
</head>
<body>
  <div class="container">
    <header>
      <h1>Tutorial: Structured Regex (rx:) <span class="badge">FindHelper</span></h1>
      <p class="subtitle">A human-readable, S-expression based regular expression alternative inspired by Emacs Lisp rx.</p>
    </header>

)rxtut"
    R"rxtut(    <section id="introduction">
      <h2>1. Why rx: ?</h2>
      <p>Traditional regular expressions in search boxes suffer from dense punctuation, escaping hassles, and lookahead fragility:</p>
      <pre><code><span class="comment"># Raw ECMAScript Regex: cryptic and escaping-heavy</span>
re:^(?!.*[\\/](?:build|node_modules)[\\/]).*[\\/]src[\\/].*test_[a-zA-Z0-9]+\.(?:cpp|h)$</code></pre>
      <p>With <code>rx:</code>, the same logic is written using self-documenting S-expressions where bare words require no quotes and directory slashes work seamlessly across Windows and POSIX:</p>
      <pre><code><span class="comment"># Structured rx: clean, readable, self-documenting</span>
rx:(path
     (exclude-dir build node_modules)
     (dir src)
     (name (seq test_ (+ alnum)))
     (ext cpp h))</code></pre>

      <div class="callout">
        <div class="callout-title">Core Principles of rx:</div>
        <ul>
          <li><strong>No mandatory quotes</strong>: Words like <code>cpp</code>, <code>src</code>, <code>test_</code> are written plainly. Use double quotes <code>"..."</code> only when tokens contain spaces (e.g., <code>(dir "Program Files")</code>).</li>
          <li><strong>Auto-escaping</strong>: Dots, brackets, pluses, and stars are treated literally in names. <code>(seq CMakeLists.txt)</code> matches literal dots.</li>
          <li><strong>Unanchored by default</strong>: Matches anywhere in target strings, unless anchored with <code>bos</code> (beginning of string) or <code>eos</code> (end of string).</li>
          <li><strong>Zero hot-path overhead</strong>: Lowered to optimized ECMAScript regex with SIMD Boyer-Moore literal prefilters before parallel search workers start.</li>
        </ul>
      </div>
    </section>

    <section id="ui-feedback">
      <h2>2. Interactive Immediate-Mode UI &amp; Editor</h2>
      <p>As you type into FindHelper's <strong>Path</strong> or <strong>Name</strong> input fields, real-time styling guides your syntax:</p>
      <table>
        <thead>
          <tr>
            <th>Border Indicator</th>
            <th>State</th>
            <th>Behavior & Diagnostics</th>
          </tr>
        </thead>
        <tbody>
          <tr>
            <td><span class="ui-indicator"><span class="dot dot-cyan"></span> Cyan Border</span></td>
            <td><strong>Valid <code>rx:</code></strong></td>
            <td>Query compiles cleanly. Hovering displays the emitted regex and harvested SIMD literal prefilter.</td>
          </tr>
          <tr>
            <td><span class="ui-indicator"><span class="dot dot-amber"></span> Amber Border</span></td>
            <td><strong>Incomplete</strong></td>
            <td>Unclosed parentheses while actively typing: <code>[rx: incomplete] missing ')': unclosed '('</code>.</td>
          </tr>
          <tr>
            <td><span class="ui-indicator"><span class="dot dot-red"></span> Red Border</span></td>
            <td><strong>Syntax Error</strong></td>
            <td>Detailed error message with "did you mean" suggestion (e.g. <code>unknown operator 'dire' (did you mean 'dir'?)</code>).</td>
          </tr>
        </tbody>
      </table>

      <div class="callout">
        <div class="callout-title">In-Place Multi-Line Structured Editor</div>
        <p>For longer expressions, click the <strong>Expand</strong> icon (<code>⤢</code>) next to the Name or Path field:</p>
        <ul>
          <li><strong>In-place expansion</strong>: Opens a multi-line editing canvas with tab indentation support.</li>
          <li><strong>Format Button</strong>: Click <code>[Format]</code> to automatically indent nested S-expressions with clean 2-space nesting.</li>
          <li><strong>Live Drawer Strip</strong>: Shows live compiled regex and diagnostic feedback directly below the edit box.</li>
          <li><strong>Keyboard Shortcuts</strong>: Press <code>Ctrl+Enter</code> (or <code>Cmd+Enter</code> on macOS) to submit search immediately; press <code>Esc</code> or click <code>[Collapse]</code> to return to single-line mode.</li>
        </ul>
      </div>
    </section>

)rxtut"
    R"rxtut(    <section id="structural-anatomy">
      <h2>3. Structural Anatomy &amp; Grammar</h2>
      <p>Every <code>rx:</code> query compiles a human-readable S-expression AST into an ECMAScript regex with SIMD literal prefilters. The structure follows a strict hierarchy of clause heads, combinators, and argument leaves:</p>

      <svg class="syntax-tree" viewBox="0 0 860 375" xmlns="http://www.w3.org/2000/svg">
        <defs>
          <marker id="arrow" viewBox="0 0 10 10" refX="6" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse">
            <path d="M 0 1 L 10 5 L 0 9 z" fill="#8b949e"/>
          </marker>
        </defs>
        <!-- Root Node -->
        <rect x="330" y="16" width="200" height="38" rx="8" fill="#1f2d3d" stroke="#4ec9b0" stroke-width="2"/>
        <text x="430" y="41" fill="#4ec9b0" font-family="-apple-system, BlinkMacSystemFont, Segoe UI, sans-serif" font-weight="bold" font-size="14" text-anchor="middle">rx: Query Root</text>
        <line x1="430" y1="54" x2="430" y2="78" stroke="#8b949e" stroke-width="2" marker-end="url(#arrow)"/>

        <!-- S-Expression Node -->
        <rect x="290" y="80" width="280" height="38" rx="8" fill="#231e33" stroke="#bc89bd" stroke-width="2"/>
        <text x="430" y="104" fill="#f0f6fc" font-family="-apple-system, BlinkMacSystemFont, Segoe UI, sans-serif" font-weight="600" font-size="13" text-anchor="middle">S-Expression: ( &lt;head&gt; &lt;arg&gt;* )</text>
        <line x1="360" y1="118" x2="225" y2="148" stroke="#8b949e" stroke-width="1.8" marker-end="url(#arrow)"/>
        <line x1="500" y1="118" x2="635" y2="148" stroke="#8b949e" stroke-width="1.8" marker-end="url(#arrow)"/>

        <!-- Path Scope Head -->
        <rect x="50" y="150" width="350" height="78" rx="8" fill="#142538" stroke="#58a6ff" stroke-width="1.8"/>
        <text x="225" y="174" fill="#58a6ff" font-family="-apple-system, BlinkMacSystemFont, Segoe UI, sans-serif" font-weight="bold" font-size="13" text-anchor="middle">Path Clause Head</text>
        <text x="225" y="195" fill="#c9d1d9" font-family="-apple-system, BlinkMacSystemFont, Segoe UI, sans-serif" font-size="11.5" text-anchor="middle">Composite &amp; File System Scope Operators</text>
        <text x="225" y="216" fill="#79c0ff" font-family="ui-monospace, monospace" font-size="12" text-anchor="middle">path &bull; dir &bull; name &bull; ext &bull; exclude-dir</text>

        <!-- Pattern Operator Head -->
        <rect x="460" y="150" width="350" height="78" rx="8" fill="#231e33" stroke="#bc89bd" stroke-width="1.8"/>
        <text x="635" y="174" fill="#bc89bd" font-family="-apple-system, BlinkMacSystemFont, Segoe UI, sans-serif" font-weight="bold" font-size="13" text-anchor="middle">Pattern Operator Head</text>
        <text x="635" y="195" fill="#c9d1d9" font-family="-apple-system, BlinkMacSystemFont, Segoe UI, sans-serif" font-size="11.5" text-anchor="middle">Combinators, Quantifiers &amp; Character Sets</text>
        <text x="635" y="216" fill="#d2a8ff" font-family="ui-monospace, monospace" font-size="12" text-anchor="middle">seq &bull; or &bull; * &bull; + &bull; ? &bull; repeat &bull; any &bull; not</text>

        <!-- Arguments Flow -->
        <line x1="225" y1="228" x2="330" y2="258" stroke="#8b949e" stroke-width="1.8" marker-end="url(#arrow)"/>
        <line x1="635" y1="228" x2="530" y2="258" stroke="#8b949e" stroke-width="1.8" marker-end="url(#arrow)"/>

        <!-- Arguments Node -->
        <rect x="50" y="260" width="760" height="95" rx="8" fill="#161b22" stroke="#30363d" stroke-width="1.8"/>
        <text x="430" y="284" fill="#f0f6fc" font-family="-apple-system, BlinkMacSystemFont, Segoe UI, sans-serif" font-weight="bold" font-size="13" text-anchor="middle">Arguments &amp; Leaf Types (Evaluated in Argument Position)</text>
        <text x="430" y="308" fill="#ce9178" font-family="ui-monospace, monospace" font-size="12" text-anchor="middle">Nested S-Expressions (op ...) &nbsp;&bull;&nbsp; Bare Literals (cpp, src) &nbsp;&bull;&nbsp; Quoted Strings ("...")</text>
        <text x="430" y="330" fill="#4ec9b0" font-family="ui-monospace, monospace" font-size="12" text-anchor="middle">Named Classes: digit, alpha, alnum, word &nbsp;&bull;&nbsp; Anchors: bos, eos, bow, eow, word-boundary</text>
      </svg>

      <pre class="mermaid">
flowchart TD
    RX["rx: Query Root"] --> TOP["S-Expression: (head arg...)"]
    TOP --> SCOPE{"Clause Head Type"}
    SCOPE -->|"Path Scope"| PATH_CLAUSE["Path Clause Head"]
    SCOPE -->|"Pattern Matcher"| CORE_CLAUSE["Pattern Operator Head"]

    subgraph PATH_OPS["Path Operators (Clause Head)"]
        PATH_CLAUSE --> P_PATH["path (composite)"]
        PATH_CLAUSE --> P_DIR["dir (directory)"]
        PATH_CLAUSE --> P_NAME["name (filename)"]
        PATH_CLAUSE --> P_EXT["ext (extension)"]
        PATH_CLAUSE --> P_EXCL["exclude-dir / not-dir"]
    end

    subgraph CORE_OPS["Core Pattern Operators (Clause Head)"]
        CORE_CLAUSE --> C_SEQ["seq / : / and"]
        CORE_CLAUSE --> C_OR["or / |"]
        CORE_CLAUSE --> C_REP["* / + / ? / repeat"]
        CORE_CLAUSE --> C_CLASS["any / not"]
    end

    PATH_OPS --> ARGS["Arguments / Children"]
    CORE_OPS --> ARGS

    subgraph ARG_TYPES["Argument &amp; Leaf Types"]
        ARGS --> A_SUB["Nested S-Expression: (op ...)"]
        ARGS --> A_STR["Quoted String: 'file name'"]
        ARGS --> A_BARE["Bare Literal: txt, v1, log"]
        ARGS --> A_NAMED["Named Class: digit, alpha, alnum"]
        ARGS --> A_ANCHOR["Boundary: bos, eos, word-boundary"]
    end

    classDef root fill:#1f2d3d,stroke:#4ec9b0,stroke-width:2px,color:#4ec9b0;
    classDef head fill:#231e33,stroke:#bc89bd,stroke-width:1.5px,color:#bc89bd;
    classDef path fill:#142538,stroke:#58a6ff,stroke-width:1.5px,color:#58a6ff;
    classDef leaf fill:#1f242c,stroke:#ce9178,stroke-width:1px,color:#ce9178;

    class RX,TOP root;
    class C_SEQ,C_OR,C_REP,C_CLASS head;
    class P_PATH,P_DIR,P_NAME,P_EXT,P_EXCL path;
    class A_SUB,A_STR,A_BARE,A_NAMED,A_ANCHOR leaf;
      </pre>

      <h3>Concrete Example Decomposition</h3>
      <p>Consider how this structured query decomposes into an AST:</p>
      <pre><code>rx:(path
     (dir "build")
     (name (seq "release-" (+ digit)))
     (ext "tar.gz" "zip"))</code></pre>

      <pre class="mermaid">
flowchart TD
    ROOT["rx:"] --> PATH["(path ...)"]
    PATH --> DIR["(dir ...)"]
    DIR --> D_VAL["'build'"]
    PATH --> NAME["(name ...)"]
    NAME --> SEQ["(seq ...)"]
    SEQ --> PREFIX["'release-'"]
    SEQ --> PLUS["(+ ...)"]
    PLUS --> DIGIT["digit"]
    PATH --> EXT["(ext ...)"]
    EXT --> EXT1["'tar.gz'"]
    EXT --> EXT2["'zip'"]

    classDef op fill:#231e33,stroke:#bc89bd,stroke-width:1.5px,color:#bc89bd;
    classDef path fill:#142538,stroke:#58a6ff,stroke-width:1.5px,color:#58a6ff;
    classDef lit fill:#1f242c,stroke:#ce9178,stroke-width:1px,color:#ce9178;
    classDef cls fill:#1f2d3d,stroke:#4ec9b0,stroke-width:1px,color:#4ec9b0;

    class PATH,DIR,NAME,EXT path;
    class SEQ,PLUS op;
    class D_VAL,PREFIX,EXT1,EXT2 lit;
    class DIGIT cls;
      </pre>
)rxtut"
    R"rxtut(      <h3>Formal Grammar (EBNF)</h3>
      <p>The dialect syntax is defined by this formal grammar:</p>
      <pre><code>Query        ::= 'rx:' SExpr?
SExpr        ::= Atom | List
List         ::= '(' Operator Argument* ')'
Argument     ::= SExpr

Operator     ::= CoreOp | PathOp
CoreOp       ::= 'seq' | ':' | 'and' | 'or' | '|'
               | '*' | 'zero-or-more' | '+' | 'one-or-more' | '?' | 'opt' | 'repeat'
               | 'any' | 'not' | 'bos' | 'eos' | 'word-boundary' | 'bow' | 'eow'
PathOp       ::= 'path' | 'dir' | 'exclude-dir' | 'not-dir' | 'name' | 'ext'

Atom         ::= QuotedString | BareWord | NamedClass
QuotedString ::= '"' [^"]* '"'
BareWord     ::= [a-zA-Z0-9_\-\.\*\?\+\\\/]+
NamedClass   ::= 'digit' | 'alpha' | 'alnum' | 'space' | 'blank' | 'word' | 'xdigit' | 'any-char'</code></pre>

      <h3>Key Structural Rules</h3>
      <p>Five structural invariants govern how FindHelper parses, validates, and optimizes <code>rx:</code> expressions:</p>

      <div class="rule-card purple">
        <h4>1. Clause Head Disambiguation</h4>
        <p>A keyword acts as an <strong>operator</strong> if and only if it appears as the first element immediately following an opening parenthesis <code>(</code>.</p>
        <p>In all argument positions, keywords behave as bare literal strings or named classes:</p>
        <pre><code><span class="comment"># 'seq' is clause head (operator); 'dir' and 'digit' are arguments (literal strings)</span>
(seq dir digit)  <span class="comment">&rarr; matches "dirdigit" verbatim</span>

<span class="comment"># To use 'dir' as a folder constraint, it must be the clause head:</span>
(path (dir "src") (name "main.cpp"))</code></pre>
      </div>

      <div class="rule-card error">
        <h4>2. Nesting &amp; Quantifier Safety (ReDoS Prevention)</h4>
        <p>Quantifiers (<code>*</code>, <code>+</code>, <code>?</code>, <code>repeat</code>) accept sub-expressions, but <strong>nested quantifiers are strictly prohibited</strong>.</p>
        <p>Expressions such as <code>(+ (* digit))</code> or <code>(* (+ alnum))</code> are rejected at compile time with <code>RxError::NestedQuantifier</code>. This guarantees linear-time matching without catastrophic backtracking.</p>
      </div>

      <div class="rule-card warning">
        <h4>3. Restricted Scope of Negation (<code>not</code>) in V1</h4>
        <p>The <code>not</code> operator is strictly restricted to <strong>character classes and character sets</strong>:</p>
        <ul>
          <li><strong>Valid</strong>: <code>(not digit)</code>, <code>(not (any "aeiou"))</code>, <code>(not (any "/" "\\"))</code> &mdash; lowers to <code>[^0-9]</code>, <code>[^aeiou]</code>, <code>[^/\\]</code>.</li>
          <li><strong>Rejected</strong>: <code>(not (seq "foo"))</code> or arbitrary sub-expression negation is rejected with <code>RxError::GeneralNegationNotSupported</code>.</li>
        </ul>
        <p>To exclude entire folder hierarchies, use the dedicated first-class <code>(exclude-dir ...)</code> path operator instead.</p>
      </div>

      <div class="rule-card">
        <h4>4. Verbatim Quoted Strings</h4>
        <p>Double-quoted strings <code>"..."</code> preserve all characters literally. Backslashes are treated as literal characters, not escape prefixes (<code>"C:\Users\"</code> matches literal backslashes without escape gymnastics).</p>
        <p>Double quotes are only necessary when strings contain spaces, parentheses, or delimiters (e.g. <code>(dir "Program Files (x86)")</code>).</p>
      </div>

      <div class="rule-card">
        <h4>5. Cross-Platform Directory Separator Agnosticism</h4>
        <p>Path clauses (<code>dir</code>, <code>exclude-dir</code>, <code>path</code>) automatically treat forward slashes <code>/</code> and backslashes <code>\</code> identically (lowering to <code>[/\\]</code>). A query constructed on macOS or Linux works identically on Windows NTFS paths without alteration.</p>
      </div>
    </section>

)rxtut"
    R"rxtut(    <section id="grammar-operators">
      <h2>4. Language Reference</h2>

      <h3>A. Sequences & Alternatives</h3>
      <table>
        <thead>
          <tr>
            <th>Operator</th>
            <th>Example</th>
            <th>Description</th>
          </tr>
        </thead>
        <tbody>
          <tr>
            <td><code>seq</code>, <code>:</code>, <code>and</code></td>
            <td><code>(seq hello world)</code></td>
            <td>Concatenates expressions in order (emits <code>helloworld</code>).</td>
          </tr>
          <tr>
            <td><code>or</code>, <code>|</code></td>
            <td><code>(or cpp h hpp)</code></td>
            <td>Matches any one alternative (emits <code>(?:cpp|h|hpp)</code>).</td>
          </tr>
        </tbody>
      </table>

      <h3>B. Quantifiers & Repetitions</h3>
      <table>
        <thead>
          <tr>
            <th>Operator</th>
            <th>Example</th>
            <th>Description</th>
          </tr>
        </thead>
        <tbody>
          <tr>
            <td><code>*</code>, <code>zero-or-more</code></td>
            <td><code>(* digit)</code></td>
            <td>Zero or more repetitions (greedy).</td>
          </tr>
          <tr>
            <td><code>+</code>, <code>one-or-more</code></td>
            <td><code>(+ alpha)</code></td>
            <td>One or more repetitions (greedy).</td>
          </tr>
          <tr>
            <td><code>?</code>, <code>opt</code></td>
            <td><code>(? "_backup")</code></td>
            <td>Optional: zero or one occurrence.</td>
          </tr>
          <tr>
            <td><code>repeat</code></td>
            <td><code>(repeat 4 digit)</code><br><code>(repeat 2 4 xdigit)</code></td>
            <td>Exact count <code>{N}</code> or bounded range <code>{MIN,MAX}</code>.</td>
          </tr>
        </tbody>
      </table>

      <h3>C. Character Classes & Sets</h3>
      <table>
        <thead>
          <tr>
            <th>Construct</th>
            <th>Example</th>
            <th>Description</th>
          </tr>
        </thead>
        <tbody>
          <tr>
            <td><code>any</code></td>
            <td><code>(any "a-z" "0-9" "_")</code></td>
            <td>Custom character class (emits <code>[a-z0-9_]</code>).</td>
          </tr>
          <tr>
            <td><code>not</code></td>
            <td><code>(not digit)</code><br><code>(not (any "aeiou"))</code></td>
            <td>Character negation (emits <code>[^0-9]</code>).</td>
          </tr>
          <tr>
            <td>Named Classes &amp; Wildcards</td>
            <td><code>digit</code>, <code>alpha</code>, <code>alnum</code>, <code>space</code>, <code>blank</code>, <code>word</code>, <code>xdigit</code>, <code>any-char</code> (or <code>any</code>, <code>anything</code>)</td>
            <td>Standard ASCII character classes and wildcards.</td>
          </tr>
          <tr>
            <td>Anchors</td>
            <td><code>bos</code>, <code>eos</code>, <code>bow</code>, <code>eow</code>, <code>word-boundary</code></td>
            <td>Start/end of string (<code>^</code>, <code>$</code>) and word boundaries (<code>\b</code>).</td>
          </tr>
        </tbody>
      </table>

      <h3>D. Path Extensions (V1)</h3>
      <table>
        <thead>
          <tr>
            <th>Operator</th>
            <th>Example</th>
            <th>Description</th>
          </tr>
        </thead>
        <tbody>
          <tr>
            <td><code>path</code></td>
            <td><code>(path ...)</code></td>
            <td>Root wrapper assembling path-level constraints.</td>
          </tr>
          <tr>
            <td><code>dir</code></td>
            <td><code>(dir src)</code><br><code>(dir src include)</code></td>
            <td>Matches a folder segment anywhere in path ancestors. Multiple args act as alternatives.</td>
          </tr>
          <tr>
            <td><code>exclude-dir</code>, <code>not-dir</code></td>
            <td><code>(exclude-dir build .git)</code></td>
            <td>Anchored negative lookahead preventing results from specified folder names.</td>
          </tr>
          <tr>
            <td><code>name</code></td>
            <td><code>(name (seq test_ (* alnum)))</code></td>
            <td>Constrains the filename stem.</td>
          </tr>
          <tr>
            <td><code>ext</code></td>
            <td><code>(ext cpp h)</code></td>
            <td>Matches file extensions at end of path (leading dots automatically stripped).</td>
          </tr>
        </tbody>
      </table>
    </section>

)rxtut"
    R"rxtut(    <section id="examples">
      <h2>5. Hands-on Practice Recipes</h2>
      <div class="example-grid">
        <div class="example-card">
          <h4>Recipe 1: C++ Headers in Include</h4>
          <p class="comment">Find all .h/.hpp files located under an include directory:</p>
          <pre><code>rx:(path (dir include) (ext h hpp))</code></pre>
        </div>

        <div class="example-card">
          <h4>Recipe 2: Ignore Build &amp; Dependency Folders</h4>
          <p class="comment">Search config files while pruning build noise across tools:</p>
          <pre><code>rx:(path
     (exclude-dir build target node_modules .git)
     (name (seq config (* any))))</code></pre>
        </div>

        <div class="example-card">
          <h4>Recipe 3: Numbered Server Logs</h4>
          <p class="comment">Find server logs ending with a 4-digit year:</p>
          <pre><code>rx:(seq "server_" (repeat 4 digit) ".log")</code></pre>
        </div>

        <div class="example-card">
          <h4>Recipe 4: Environment &amp; Secret Files</h4>
          <p class="comment">Locate all .env variants while ignoring node_modules and vendor:</p>
          <pre><code>rx:(path
     (exclude-dir node_modules vendor .terraform)
     (name (or ".env"
               (seq ".env." (or "local" "development" "staging" "prod")))))</code></pre>
        </div>

        <div class="example-card">
          <h4>Recipe 5: Git Conflicts &amp; Merge Leftovers</h4>
          <p class="comment">Clean up merge conflicts, backup copies, and reject files:</p>
          <pre><code>rx:(path
     (exclude-dir .git)
     (name (or (seq (+ any) ".orig")
               (seq (+ any) ".rej")
               (seq (+ any) (or "_BACKUP_" "_BASE_") (+ digit)))))</code></pre>
        </div>

        <div class="example-card">
          <h4>Recipe 6: ISO-Dated Camera &amp; Phone Media</h4>
          <p class="comment">Match photos and videos named with YYYY-MM-DD or YYYYMMDD dates:</p>
          <pre><code>rx:(name (seq (or "IMG_" "PHOTO_" "VID_")
               (repeat 4 digit) (? "-")
               (repeat 2 digit) (? "-")
               (repeat 2 digit)
               (+ any)))</code></pre>
        </div>

        <div class="example-card">
          <h4>Recipe 7: Semantic Versioned Packages</h4>
          <p class="comment">Find release archives like v1.2.3.tar.gz or v1.0.0-rc1.zip:</p>
          <pre><code>rx:(name (seq (? "v")
               (repeat 1 3 digit) "."
               (repeat 1 3 digit) "."
               (repeat 1 3 digit)
               (or ".tar.gz" ".zip" ".tgz"
                   (seq "-" alnum (* alnum) (or ".tar.gz" ".zip" ".tgz")))))</code></pre>
        </div>

        <div class="example-card">
          <h4>Recipe 8: Word Boundaries &amp; Exact Tokens</h4>
          <p class="comment">Match exact identifier "Engine" without false matching "Engineer":</p>
          <pre><code>rx:(name (seq word-boundary "Engine" word-boundary))</code></pre>
        </div>

        <div class="example-card">
          <h4>Recipe 9: Typo-Tolerance in Action</h4>
          <p class="comment">Type an unknown operator to verify immediate-mode suggestion:</p>
          <pre><code>rx:(dire src)
<span class="comment"># Output: unknown operator 'dire' (did you mean 'dir'?)</span></code></pre>
        </div>
      </div>
    </section>

    <footer>
      <p>FindHelper &mdash; Cross-Platform High-Performance File Search &bull; C++17 &amp; Dear ImGui</p>
    </footer>
  </div>
</body>
</html>
)rxtut";

}  // namespace

std::string_view GetEmbeddedRxTutorialHtml() {
  return kRxTutorialHtml;
}

std::string WriteEmbeddedRxTutorialToTempFile() {
  try {
    const std::filesystem::path temp_dir = std::filesystem::temp_directory_path() / "FindHelper";  // NOSONAR(cpp:S5443) - safe temporary directory for opening HTML help guide
    std::error_code ec;
    std::filesystem::create_directories(temp_dir, ec);
    const std::filesystem::path temp_file = temp_dir / "rx_pattern_tutorial.html";

    std::ofstream out(temp_file, std::ios::out | std::ios::trunc | std::ios::binary);
    if (!out) {
      LOG_ERROR_BUILD("Failed to open temporary file for writing rx tutorial: " << temp_file.string());
      return {};
    }

    out.write(kRxTutorialHtml.data(), static_cast<std::streamsize>(kRxTutorialHtml.size()));
    out.close();
    return temp_file.string();
  } catch (const std::filesystem::filesystem_error& e) {
    LOG_ERROR_BUILD("Filesystem error writing embedded rx tutorial to temp file: " << e.what());
    return {};
  } catch (const std::ios_base::failure& e) {
    LOG_ERROR_BUILD("IO error writing embedded rx tutorial to temp file: " << e.what());
    return {};
  }
}

void OpenEmbeddedRxTutorialInBrowser(const OpenFileCallback& opener) {
  const std::string path = WriteEmbeddedRxTutorialToTempFile();
  if (path.empty()) {
    return;
  }
  if (opener) {
    opener(path);
  } else {
    file_operations::OpenFileDefault(path);
  }
}

}  // namespace ui
