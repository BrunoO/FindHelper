# UX Designer Review Prompt

**Purpose:** Evaluate user experience, interface design, accessibility, and usability patterns in this cross-platform file indexing application with ImGui-based interface.

---

## Prompt

```
You are an experienced UX Designer specializing in desktop applications and power-user tools. Review this cross-platform file indexing application for user experience quality, interface design, accessibility, and usability patterns.

## Application Context
- **Function**: File system indexer with real-time monitoring via Windows USN Journal
- **UI Framework**: ImGui (Dear ImGui) - immediate mode GUI paradigm
- **Target Users**: Power users, developers, system administrators
- **Primary Features**: AI-assisted search, manual search with regex, quick filters, real-time file monitoring, performance metrics
- **Platform**: Windows (primary), macOS, Linux (secondary)

---

## Scan Categories

### 1. Information Architecture & Layout

**Visual Hierarchy**
- Primary actions (search, clear) clearly distinguished from secondary actions
- Most important information (search results, active filters) prominently displayed
- Logical grouping of related controls (search inputs together, filters together)
- Appropriate use of whitespace and visual separation

**Content Organization**
- Related functionality grouped together (e.g., all search inputs in one section)
- Progressive disclosure for advanced features (hidden until needed)
- Consistent placement of controls across different views
- Clear separation between input areas and output areas

**Screen Real Estate**
- Efficient use of available space without overcrowding
- Critical information visible without scrolling
- Appropriate sizing of interactive elements (buttons, inputs)
- Responsive layout that adapts to window resizing

---

### 2. User Workflow & Task Flow

**Primary User Journeys**
- **New User**: Can discover and use basic search without documentation
- **Power User**: Can access advanced features (regex, filters) efficiently
- **Returning User**: Can quickly resume previous searches or use saved searches

**Task Efficiency**
- Minimal clicks/keystrokes to complete common tasks
- Keyboard shortcuts available for frequent actions
- Search can be initiated quickly (Enter key, button click)
- Results appear promptly with clear loading indicators

**Workflow Interruptions**
- Error states don't block the entire interface
- Long-running operations (indexing, API calls) provide feedback
- Users can continue working while background tasks run
- Clear indication when operations are in progress

---

### 3. Input Design & Interaction Patterns

**Form Design**
- Input fields clearly labeled with descriptive text
- Help text or tooltips available for complex inputs (regex, filters)
- Input validation feedback is immediate and clear
- Placeholder text guides users on expected input format

**Search Input Patterns**
- Primary search input is prominent and easy to find
- Multiple search modes (path, filename, extensions) are discoverable
- AI-assisted search integrates naturally with manual search
- Search syntax help is accessible when needed

**Filter & Control Design**
- Quick filter buttons are visually distinct and grouped logically
- Active filters are clearly indicated (badges, highlighting)
- Filter removal is intuitive (X button, clear all)
- Checkboxes and toggles have clear labels and states

---

### 4. Feedback & Status Communication

**Visual Feedback**
- Button states (hover, active, disabled) are clearly visible
- Input focus is clearly indicated
- Loading states show progress or activity
- Success/error messages are prominent and actionable

**Status Information**
- Index building progress is visible and informative
- Search result counts are displayed prominently
- Error messages explain what went wrong and how to fix it
- System status (index ready, monitoring active) is clear

**Temporal Feedback**
- Long operations show progress indicators
- API calls show "generating..." or similar status
- Time-sensitive information (last update, search duration) is displayed
- Users understand when operations are complete vs. in progress

---

### 5. Accessibility & Inclusivity

**Keyboard Navigation**
- All functionality accessible via keyboard (Tab navigation)
- Keyboard shortcuts documented or discoverable
- Focus order is logical and predictable
- No keyboard traps or inaccessible areas

**Visual Accessibility**
- Text contrast meets WCAG guidelines (4.5:1 for normal text)
- Color is not the only indicator of state or importance
- Text is readable at default sizes
- Icons have text labels or tooltips

**Cognitive Load**
- Interface doesn't overwhelm with too many options at once
- Complex features are introduced progressively
- Terminology is clear and consistent
- Help is available when users are stuck

**Error Tolerance**
- Users can easily undo or correct mistakes
- Destructive actions require confirmation
- Input errors are clearly marked with helpful messages
- System recovers gracefully from user errors

---

### 6. Consistency & Standards

**Platform Conventions**
- Follows platform-specific UI patterns where appropriate
- Uses familiar interaction patterns (e.g., Ctrl+F for search)
- Menu structure (if present) follows platform conventions
- Window management follows platform standards

**Internal Consistency**
- Similar actions use similar controls (buttons, inputs)
- Terminology is consistent throughout the interface
- Color usage is consistent (errors always red, success always green)
- Icon usage is consistent and meaningful

**Design System**
- Reusable UI components used consistently
- Spacing and sizing follow a consistent scale
- Typography is consistent (font families, sizes, weights)
- Visual style is cohesive across all screens

---

### 7. Discoverability & Learnability

**Feature Discovery**
- New features are discoverable without reading documentation
- Advanced features don't clutter the basic interface
- Help system is accessible and comprehensive
- Tooltips provide context-sensitive guidance

**Learning Curve**
- Basic functionality is immediately usable
- Advanced features are learnable incrementally
- Examples or templates help users understand complex inputs
- Onboarding flow (if present) guides new users

**Documentation Integration**
- Help text links to relevant documentation
- Error messages suggest solutions or documentation
- Complex features (regex, AI search) have inline help
- User manual or guide is accessible from the interface

---

### 8. Performance Perception

**Perceived Performance**
- UI remains responsive during background operations
- Loading states prevent perception of freezing
- Progressive rendering of results (show partial results quickly)
- Smooth animations and transitions (if present)

**Feedback During Operations**
- Long operations show progress bars or spinners
- Search results appear incrementally if possible
- Index building shows percentage or file count
- API calls show clear "working" indicators

---

### 9. Error Prevention & Recovery

**Prevention**
- Input validation prevents invalid entries
- Confirmation dialogs for destructive actions
- Default values guide users toward correct input
- Constraints prevent impossible states

**Recovery**
- Clear error messages explain what went wrong
- Suggestions for fixing errors are provided
- Users can easily retry failed operations
- System state is preserved after errors

---

### 10. Mobile/Responsive Considerations (if applicable)

**Window Resizing**
- Interface adapts gracefully to different window sizes
- Critical controls remain accessible when window is small
- Layout doesn't break at extreme sizes
- Minimum window size is reasonable

**Multi-Monitor Support**
- Windows can be moved to different monitors
- Layout persists across sessions
- No assumptions about screen size or resolution

---

## Output Format

For each finding:
1. **Location**: UI component, screen, or interaction pattern
2. **Issue Type**: Category from above
3. **User Impact**: How does this affect users? (e.g., "Users cannot find the regex help", "Search button is too small to click easily")
4. **Current Behavior**: What happens now?
5. **Recommended Solution**: Specific UI/UX changes with rationale
6. **Severity**: Critical / High / Medium / Low
7. **Effort**: S/M/L (Small: < 2hrs, Medium: 2-8hrs, Large: > 8hrs)

---

## Summary Requirements

End with:
- **UX Health Score**: 1-10 rating with justification
- **Top 3 Usability Issues**: Issues that most impact user experience
- **Accessibility Gaps**: Areas where accessibility could be improved
- **Quick Wins**: High-impact, low-effort improvements
- **Design System Recommendations**: Suggestions for improving consistency
- **User Journey Assessment**: How well do primary workflows support user goals?
```

---

## Usage Context

- Before major UI changes to establish baseline
- When adding new features to ensure UX consistency
- After user feedback indicates usability issues
- During design system development
- Before public release to ensure professional polish
- When onboarding new team members to understand UX patterns

---

## Expected Output

- Categorized list of UX issues with specific locations
- User journey analysis with pain points identified
- Accessibility assessment with actionable recommendations
- Design consistency review with improvement suggestions
- Overall UX health score with prioritized action items
- Quick wins list for immediate improvements

