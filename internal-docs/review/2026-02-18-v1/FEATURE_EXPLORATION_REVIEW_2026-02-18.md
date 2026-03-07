# Feature Exploration & Product Strategy - 2026-02-18

## Executive Summary
- **Core Value**: Ultra-fast file indexing and real-time monitoring on Windows, outperforming OS-native search in speed and responsiveness.
- **Key Differentiator**: Hybrid approach combining high-performance C++ (SIMD, SoA) with AI-assisted natural language search.
- **Top Opportunity**: Enhancing the "AI-to-Query" pipeline to make powerful regex/glob searches accessible to non-technical users.
- **Quick Win**: "Save Search" functionality to reduce repeated input for common developer/admin tasks.

## Current Product & Personas

### Value Proposition
Fast, lightweight, and real-time file searching that respects system resources. It provides a bridge between traditional technical search (regex, glob) and modern intuitive search (AI-assisted).

### Personas
1. **Developer (Power User)**: Needs to find source files, logs, or build artifacts across large projects. Uses regex and complex filters.
2. **System Administrator (Professional)**: Monitors system changes, large file growth, and log rotations. Values real-time updates and bulk actions.
3. **General Power User**: Wants a faster alternative to Windows Explorer for finding documents and media.

## Competitive Positioning

### Competitor Archetypes
1. **Everything (voidtools)**: The gold standard for Windows filename search.
   - *Strengths*: Instant, low memory.
   - *Weaknesses*: Technical UI, lacks advanced content/metadata filtering in base version, no native AI integration.
2. **Windows Search (OS-Native)**:
   - *Strengths*: Integrated, content indexing.
   - *Weaknesses*: Slow, resource-heavy, often yields irrelevant results.
3. **WizTree/WinDirStat**:
   - *Strengths*: Visualizing disk space.
   - *Weaknesses*: Not focused on search workflow, static snapshots (WizTree is fast but still a "scan" tool).

### Positioning Statement
"The ultimate productivity tool for Windows power users that combines the raw speed of USN-journal indexing with the intelligence of AI to find exactly what you need, even when you don't know the exact filename."

## Opportunities & Candidate Features

### Theme: "Workflow Integration"
- **Candidate: "Save & Pin Searches"**
  - *User Story*: "As a Developer, I want to pin my 'last 24h modified .cpp files' search so I can resume work instantly."
  - *Value*: Retention and efficiency.
  - *Complexity*: Low (Settings persistence).

### Theme: "Actionable Results"
- **Candidate: "Custom Context Menu Actions"**
  - *User Story*: "As a SysAdmin, I want to right-click a log file and select 'Open in Notepad++' or 'Tail -f'."
  - *Value*: Utility and stickiness.
  - *Complexity*: Medium (Registry/Config integration).

### Theme: "AI Deepening"
- **Candidate: "Semantic Content Preview"**
  - *User Story*: "As a Power User, I want the AI to summarize the first few lines of a document to confirm it's what I need."
  - *Value*: Major differentiation.
  - *Complexity*: High (Content indexing required).

## Quick Wins (Top 5)
1. **Recent Searches Dropdown**: High clarity, low effort (UI/Settings).
2. **"Open Folder" Button in Toolbar**: High utility for Explorer-heavy workflows.
3. **Results Export to CSV/JSON**: (Already partially done, but can be improved).
4. **Regex Helper UI**: Small interactive tool to build regex patterns visually.
5. **Keyboard Shortcut for "Focus Search"**: (Already exists as Ctrl+F/Cmd+F, ensure it's prominent).

## Roadmap Proposal

### Phase 1 – Quick Wins & On-Ramp
- **Persistence**: Save window position, column widths, and recent searches.
- **UI Polish**: Improve contrast and add shortcut hints.
- **Discovery**: Add a "Getting Started" guide or overlay.

### Phase 2 – Deepening the Core
- **Bulk Operations**: Multi-select results and perform moves/deletes (Partial support exists).
- **Advanced Filtering**: Add "Size Range" and "Date Range" visual sliders.
- **Performance**: Move to RE2 for regex search to eliminate ReDoS.

### Phase 3 – Strategic Bets
- **Remote Search**: Search files on other machines running the same agent.
- **Local AI (LLM)**: Run small models locally for search query generation without needing an API key.

## Risks, Constraints & Assumptions
- **Risk**: Over-complicating the UI for casual users.
- **Constraint**: Must remain fast; any feature that slows down the search loop is a "no-go".
- **Assumption**: Users are willing to provide an API key for AI features (might need local fallback).
