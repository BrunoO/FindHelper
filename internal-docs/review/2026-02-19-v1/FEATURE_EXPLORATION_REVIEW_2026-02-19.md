# Feature Exploration Review - 2026-02-19

## Executive Summary
- **Primary Value**: Instant search across millions of files with zero indexing lag on Windows.
- **Key Differentiator**: High-performance C++/SoA architecture combined with natural language AI search.
- **Top Opportunity**: Enhancing the "Dired-style" power-user workflow to rival mature tools like Everything or fzf.
- **Roadmap Focus**: Transitioning from a "fast searcher" to a "comprehensive file management companion."

## Current Product & Personas
- **Value Proposition**: "Find anything, instantly, naturally." A performance-first tool that abstracts the complexity of the Windows USN Journal while providing an AI-powered on-ramp for casual users.
- **Personas**:
  1. **The Developer (Power User)**: Needs regex, path filtering, and rapid keyboard-driven actions (copy, delete, move).
  2. **The System Admin**: Needs to find large/old files across volumes quickly to manage disk space.
  3. **The Casual User**: Uses natural language ("Find my tax documents from 2023") to avoid learning search syntax.

## Competitive Positioning
- **Archetype 1: OS Native Search (Windows Search)**:
  - *Strengths*: Integrated, content indexing.
  - *Weaknesses*: Slow, unpredictable, high resource usage.
  - *Our Position*: Win on speed and UI responsiveness.
- **Archetype 2: Fast Indexers (Everything by voidtools)**:
  - *Strengths*: Proven, ultra-lightweight, mature feature set.
  - *Weaknesses*: Windows-only, dated UI, steep learning curve for advanced queries.
  - *Our Position*: Win on Cross-platform support and AI-assisted search.
- **Archetype 3: CLI Tools (fzf, fd)**:
  - *Strengths*: Dev-friendly, scriptable.
  - *Weaknesses*: No GUI, requires terminal proficiency.
  - *Our Position*: Provide CLI speed with GUI discoverability.

## Opportunities & Candidate Features
### Theme 1: Advanced Power-User Workflows
- **Feature: Custom Action Scripts**:
  - *User story*: "As a Developer, I want to run a custom batch script on marked files so that I can automate my specific workflow."
  - *Value*: Unlocks unlimited extensibility for power users.
- **Feature: Interactive File Preview**:
  - *User story*: "As a user, I want to see a preview of the file content so I can verify it's the right one without opening it."
  - *Value*: Reduces task time significantly.

### Theme 2: AI-Driven Insights
- **Feature: Semantic Grouping**:
  - *User story*: "As a Casual User, I want search results grouped by project or topic so I can see related files together."
  - *Value*: Leverages LLM capabilities for better organization.

## Quick Wins
1. **Shortcut Legend in UI**: Increases discoverability of 'm', 'u', 't' keys. (Complexity: Quick)
2. **"Copy as CSV" for Results**: Simple export for data analysis. (Complexity: Quick)
3. **Recent Searches History**: Dropdown for the last 10 queries. (Complexity: Quick)
4. **Enhanced Tooltips for Truncated Paths**: Show full path immediately on hover. (Complexity: Quick)

## Roadmap Proposal
### Phase 1 – Quick Wins & On-Ramp
- Shortcut Legend (Power User) - High Impact/Quick
- Recent Searches History (All) - Medium Impact/Quick
- Export to CSV (Admin) - Medium Impact/Quick

### Phase 2 – Deepening the Core
- File Preview Panel (All) - High Impact/Medium
- Recursive Folder Size Calculation (Admin) - High Impact/Medium
- Saved Search "Smart Folders" (All) - Medium Impact/Medium

### Phase 3 – Strategic Bets
- Custom Action Engine (Power User) - High Impact/Big
- AI-Powered File Tagging/Clustering (All) - Medium Impact/Big
- Native macOS/Linux Monitoring (All) - High Impact/Big (Architecture-defining)

## Risks, Constraints & Assumptions
- **Technical Risk**: Implementing file previews without crashing on large/corrupt files.
- **Constraint**: Must maintain < 100ms UI latency even during background AI processing.
- **Assumption**: Users value "Natural Language" search enough to configure/use an API key.
- **Validation**: Track usage frequency of AI Search vs. Manual Search (if telemetry is added).
