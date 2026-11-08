#include "search/rx/RxCompiler.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rx {

namespace {

// ============================================================================
// Vocabulary
// ============================================================================

// Separator class emitted for every '/' or '\' in a literal: paths are matched
// identically on Windows and POSIX (spec §5).
constexpr std::string_view kSepClass = R"([/\\])";
// Separator as a member inside a [...] class.
constexpr std::string_view kSepClassMembers = R"(/\\)";
// "Start of a path segment": the start of the text or right after a separator.
constexpr std::string_view kSegmentStart = R"((?:^|[/\\]))";
// "End of a path segment": a separator or the end of the text.
constexpr std::string_view kSegmentEnd = R"((?:[/\\]|$))";

// Minimum length of a harvested literal worth a substring pre-filter (mirrors
// the `re:` pre-filter threshold in CreateMatcherImplStdRegex).
constexpr std::size_t kMinRequiredLiteralLength = 3;

// Largest edit distance for a "did you mean" operator suggestion.
constexpr std::size_t kMaxSuggestionDistance = 2;

enum class Op : std::uint8_t {  // NOLINT(performance-enum-size)
  kSeq,          // NOLINT(readability-identifier-naming)
  kOr,           // NOLINT(readability-identifier-naming)
  kStar,         // NOLINT(readability-identifier-naming)
  kPlus,         // NOLINT(readability-identifier-naming)
  kOpt,          // NOLINT(readability-identifier-naming)
  kRepeat,       // NOLINT(readability-identifier-naming)
  kAny,          // NOLINT(readability-identifier-naming)
  kNot,          // NOLINT(readability-identifier-naming)
  kPath,         // NOLINT(readability-identifier-naming)
  kDir,          // NOLINT(readability-identifier-naming)
  kExcludeDir,   // NOLINT(readability-identifier-naming)
  kName,         // NOLINT(readability-identifier-naming)
  kExt,          // NOLINT(readability-identifier-naming)
};

struct OpName {
  std::string_view name;
  Op op;
};

constexpr std::array<OpName, 20> kOperators{{
    {"seq", Op::kSeq},
    {":", Op::kSeq},
    {"and", Op::kSeq},
    {"or", Op::kOr},
    {"|", Op::kOr},
    {"*", Op::kStar},
    {"zero-or-more", Op::kStar},
    {"+", Op::kPlus},
    {"one-or-more", Op::kPlus},
    {"?", Op::kOpt},
    {"opt", Op::kOpt},
    {"repeat", Op::kRepeat},
    {"any", Op::kAny},
    {"not", Op::kNot},
    {"path", Op::kPath},
    {"dir", Op::kDir},
    {"exclude-dir", Op::kExcludeDir},
    {"not-dir", Op::kExcludeDir},
    {"name", Op::kName},
    {"ext", Op::kExt},
},};

// Named character classes (ASCII). `members` is the content placed inside
// [...]; any-char has no bracket form and is handled separately.
struct NamedClass {
  std::string_view name;
  std::string_view members;
};

[[nodiscard]] constexpr bool IsAnyCharAtom(std::string_view name) noexcept {
  return name == "any-char" || name == "any" || name == "anything";
}

constexpr std::array<NamedClass, 7> kNamedClasses{{
    {"digit", "0-9"},
    {"alpha", "a-zA-Z"},
    {"alnum", "a-zA-Z0-9"},
    {"space", R"( \t\r\n\v\f)"},
    {"blank", R"( \t)"},
    {"word", "a-zA-Z0-9_"},
    {"xdigit", "0-9a-fA-F"},
},};

struct Anchor {
  std::string_view name;
  std::string_view regex;
};

constexpr std::array<Anchor, 5> kAnchors{{
    {"bos", "^"},
    {"eos", "$"},
    {"bow", "\\b"},
    {"eow", "\\b"},
    {"word-boundary", "\\b"},
},};

[[nodiscard]] std::optional<Op> LookupOp(std::string_view name) {
  for (const OpName& entry : kOperators) {
    if (entry.name == name) {
      return entry.op;
    }
  }
  return std::nullopt;
}

[[nodiscard]] const NamedClass* LookupNamedClass(std::string_view name) {
  for (const NamedClass& entry : kNamedClasses) {
    if (entry.name == name) {
      return &entry;
    }
  }
  return nullptr;
}

[[nodiscard]] const Anchor* LookupAnchor(std::string_view name) {
  for (const Anchor& entry : kAnchors) {
    if (entry.name == name) {
      return &entry;
    }
  }
  return nullptr;
}

[[nodiscard]] bool IsAtomKeyword(std::string_view name) {
  return IsAnyCharAtom(name) || LookupNamedClass(name) != nullptr || LookupAnchor(name) != nullptr;
}

[[nodiscard]] bool IsPathClauseOp(Op op) {
  return op == Op::kDir || op == Op::kExcludeDir || op == Op::kName || op == Op::kExt;
}

[[nodiscard]] bool IsSeparator(char c) {
  return c == '/' || c == '\\';
}

[[nodiscard]] bool IsRegexMetachar(char c) {
  constexpr std::string_view kMeta = "\\^$.|?*+()[]{}";
  return kMeta.find(c) != std::string_view::npos;
}

[[nodiscard]] bool IsSpace(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

// Levenshtein distance, used only for short operator names (bounded inputs).
[[nodiscard]] std::size_t EditDistance(std::string_view a, std::string_view b) {
  std::vector<std::size_t> prev(b.size() + 1);
  std::vector<std::size_t> cur(b.size() + 1);
  for (std::size_t j = 0; j <= b.size(); ++j) {
    prev.at(j) = j;
  }
  for (std::size_t i = 1; i <= a.size(); ++i) {
    cur.at(0) = i;
    for (std::size_t j = 1; j <= b.size(); ++j) {
      const std::size_t substitution = prev.at(j - 1) + (a.at(i - 1) == b.at(j - 1) ? 0U : 1U);
      cur.at(j) = (std::min)({prev.at(j) + 1, cur.at(j - 1) + 1, substitution});
    }
    std::swap(prev, cur);
  }
  return prev.at(b.size());
}

// Closest operator name within kMaxSuggestionDistance, or empty.
[[nodiscard]] std::string_view SuggestOperator(std::string_view name) {
  std::string_view best;
  std::size_t best_distance = kMaxSuggestionDistance + 1;
  for (const OpName& entry : kOperators) {
    if (entry.name.size() < 2) {
      continue;  // never suggest single-character aliases
    }
    if (const std::size_t distance = EditDistance(name, entry.name);
        distance < best_distance && distance < name.size()) {
      best = entry.name;
      best_distance = distance;
    }
  }
  return best;
}

// ============================================================================
// AST
// ============================================================================

enum class NodeKind : std::uint8_t {  // NOLINT(performance-enum-size)
  kAtom,  // NOLINT(readability-identifier-naming)
  kList,  // NOLINT(readability-identifier-naming)
};

struct Node {
  NodeKind kind = NodeKind::kAtom;
  std::string text;             // atom text, quotes stripped
  bool quoted = false;          // atom came from "...": always a literal
  std::size_t offset = 0;       // byte offset in the rx source
  std::vector<Node> children;   // list elements; children[0] is the operator
};

struct CompileError {
  RxError error = RxError::kNone;
  std::string message;
  std::size_t offset = 0;
};

// Records the first failure; later failures are ignored so the reported error
// is the one closest to the start of the input.
class ErrorSink {
public:
  bool Fail(RxError error, std::size_t offset, std::string message) {
    if (error_.error == RxError::kNone) {
      error_.error = error;
      error_.offset = offset;
      error_.message = std::move(message);
    }
    return false;
  }

  [[nodiscard]] const CompileError& Error() const noexcept { return error_; }

private:
  CompileError error_;
};

// ============================================================================
// Parser: S-expression source -> list of top-level Nodes
// ============================================================================

class Parser {
public:
  Parser(std::string_view source, ErrorSink& sink) : source_(source), sink_(sink) {}

  bool ParseAll(std::vector<Node>& forms) {
    SkipSpace();
    while (pos_ < source_.size()) {
      Node node;
      if (!ParseForm(node, 0)) {
        return false;
      }
      forms.push_back(std::move(node));
      SkipSpace();
    }
    return true;
  }

  [[nodiscard]] std::size_t OpenParenCount() const noexcept { return open_paren_count_; }

private:
  void SkipSpace() {
    while (pos_ < source_.size() && IsSpace(source_[pos_])) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - pos_ < size checked
      ++pos_;
    }
  }

  bool ParseForm(Node& out, std::size_t depth) {
    const char c = source_[pos_];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - callers guarantee pos_ < size
    if (c == '(') {
      return ParseList(out, depth + 1);
    }
    if (c == ')') {
      return sink_.Fail(RxError::kUnexpectedCloseParen, pos_, "unexpected ')' with no matching '('");
    }
    if (c == '"') {
      return ParseQuoted(out);
    }
    ParseBare(out);
    return true;
  }

  // `depth` counts the '(' open once this list's own '(' is consumed.
  bool ParseList(Node& out, std::size_t depth) {
    if (depth > kMaxRxNestingDepth) {
      return sink_.Fail(RxError::kNestingTooDeep, pos_,
                        "nesting deeper than " + std::to_string(kMaxRxNestingDepth) + " levels");
    }
    out.kind = NodeKind::kList;
    out.offset = pos_;
    ++pos_;  // '('
    SkipSpace();
    while (pos_ < source_.size() && source_[pos_] != ')') {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - pos_ < size checked
      Node child;
      if (!ParseForm(child, depth)) {
        return false;
      }
      out.children.push_back(std::move(child));
      SkipSpace();
    }
    if (pos_ >= source_.size()) {
      open_paren_count_ = depth;
      const std::string noun = depth == 1 ? "'('" : "'(' (" + std::to_string(depth) + " levels)";
      return sink_.Fail(RxError::kUnclosedParen, out.offset, "missing ')': unclosed " + noun);
    }
    ++pos_;  // ')'
    if (out.children.empty()) {
      return sink_.Fail(RxError::kEmptyList, out.offset, "empty list '()'");
    }
    return true;
  }

  bool ParseQuoted(Node& out) {
    out.kind = NodeKind::kAtom;
    out.quoted = true;
    out.offset = pos_;
    const std::size_t close = source_.find('"', pos_ + 1);
    if (close == std::string_view::npos) {
      return sink_.Fail(RxError::kUnterminatedString, pos_, "unterminated string: missing closing '\"'");
    }
    out.text.assign(source_.substr(pos_ + 1, close - pos_ - 1));
    pos_ = close + 1;
    return true;
  }

  void ParseBare(Node& out) {
    out.kind = NodeKind::kAtom;
    out.offset = pos_;
    const std::size_t start = pos_;
    while (pos_ < source_.size()) {
      if (const char c = source_[pos_];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - pos_ < size checked
          IsSpace(c) || c == '(' || c == ')' || c == '"') {
        break;
      }
      ++pos_;
    }
    out.text.assign(source_.substr(start, pos_ - start));
  }

  std::string_view source_;
  ErrorSink& sink_;
  std::size_t pos_ = 0;
  std::size_t open_paren_count_ = 0;
};

// ============================================================================
// Shared helpers for lowering and harvesting
// ============================================================================

// Operator of a list node, or nullopt (atom, quoted/list head, unknown name).
[[nodiscard]] std::optional<Op> PeekOp(const Node& node) {
  if (node.kind != NodeKind::kList || node.children.empty()) {
    return std::nullopt;
  }
  const Node& head = node.children.front();
  if (head.kind != NodeKind::kAtom || head.quoted) {
    return std::nullopt;
  }
  return LookupOp(head.text);
}

[[nodiscard]] std::string_view OpNameOf(const Node& list) {
  return list.children.front().text;
}

// Parses a non-negative decimal count from a bare atom.
[[nodiscard]] std::optional<std::size_t> ParseCount(const Node& node) {
  if (constexpr std::size_t kMaxDigits = 6;
      node.kind != NodeKind::kAtom || node.quoted || node.text.empty() || node.text.size() > kMaxDigits) {
    return std::nullopt;
  }
  std::size_t value = 0;
  for (const char c : node.text) {
    if (c < '0' || c > '9') {
      return std::nullopt;
    }
    constexpr std::size_t kBase10 = 10U;
    value = (value * kBase10) + static_cast<std::size_t>(c - '0');
  }
  return value;
}

// Bounds of a (repeat ...) form. body_index is the first body child.
struct RepeatSpec {
  std::size_t min = 0;
  std::optional<std::size_t> max;  // nullopt = exact count (min)
  std::size_t body_index = 0;
};

// Shape only (no range validation): (repeat N body...) or (repeat MIN MAX body...).
[[nodiscard]] std::optional<RepeatSpec> ParseRepeatShape(const Node& list) {
  const std::vector<Node>& args = list.children;
  if (args.size() < 3) {
    return std::nullopt;
  }
  const std::optional<std::size_t> first = ParseCount(args.at(1));
  if (!first.has_value()) {
    return std::nullopt;
  }
  if (args.size() >= 4) {
    if (const std::optional<std::size_t> second = ParseCount(args.at(2)); second.has_value()) {
      return RepeatSpec{*first, second, 3};
    }
  }
  return RepeatSpec{*first, std::nullopt, 2};
}

// Literal text -> regex, with '/' and '\' widened to the separator class.
void AppendEscapedLiteral(std::string_view text, std::string& out) {
  for (const char c : text) {
    if (IsSeparator(c)) {
      out += kSepClass;
    } else if (IsRegexMetachar(c)) {
      out += '\\';
      out += c;
    } else {
      out += c;
    }
  }
}

// One character inside a [...] class.
void AppendClassChar(char c, std::string& out) {
  if (c == '\\' || c == ']' || c == '[' || c == '^' || c == '-') {
    out += '\\';
  }
  out += c;
}

// "a|b|c" -> "(?:a|b|c)"; a single alternative is returned unwrapped (it is
// always placed in a concatenation, where a sequence needs no group).
[[nodiscard]] std::string GroupAlternatives(const std::vector<std::string>& alternatives) {
  if (alternatives.size() == 1) {
    return alternatives.front();
  }
  std::string out = "(?:";
  for (std::size_t i = 0; i < alternatives.size(); ++i) {
    if (i > 0) {
      out += '|';
    }
    out += alternatives.at(i);
  }
  out += ')';
  return out;
}

// ============================================================================
// Lowerer: AST -> ECMAScript regex
// ============================================================================

struct Lowered {
  std::string text;
  // True when `text` is a single regex unit that a quantifier can follow
  // directly (one char, a class, a group).
  bool atomic = false;
};

// Collected (path ...) clauses, already lowered.
struct PathParts {
  std::vector<std::string> exclusions;   // alternatives across all exclude-dir clauses
  std::vector<std::string> dirs;         // one grouped alternative set per dir clause, in order
  std::optional<std::string> name;       // lowered name expression
  std::optional<std::string> ext;        // grouped extension alternatives (no leading dot)
};

class Lowerer {
public:
  explicit Lowerer(ErrorSink& sink) : sink_(sink) {}

  bool LowerTop(const std::vector<Node>& forms, std::string& out) {
    if (forms.empty()) {
      return true;
    }
    // (path ...) must be the whole expression.
    for (const Node& form : forms) {
      if (PeekOp(form) == Op::kPath) {
        if (forms.size() != 1) {
          return sink_.Fail(RxError::kPathOperatorMisplaced, form.offset,
                            "'path' must be the whole rx expression");
        }
        return LowerPath(form.children, 1, out);
      }
    }
    // Top-level path clauses alone form an implicit (path ...).
    if (const bool all_clauses = std::all_of(forms.begin(), forms.end(), [](const Node& form) {
          const std::optional<Op> op = PeekOp(form);
          return op.has_value() && IsPathClauseOp(*op);
        });
        all_clauses) {
      return LowerPath(forms, 0, out);
    }
    // Otherwise several forms are an implicit (seq ...).
    for (const Node& form : forms) {
      Lowered lowered;
      if (!Lower(form, lowered)) {
        return false;
      }
      out += lowered.text;
    }
    return true;
  }

private:
  bool Lower(const Node& node, Lowered& out) {
    return node.kind == NodeKind::kAtom ? LowerAtom(node, out) : LowerList(node, out);
  }

  bool LowerAtom(const Node& node, Lowered& out) {
    if (!node.quoted) {
      if (const Anchor* anchor = LookupAnchor(node.text); anchor != nullptr) {
        out.text = anchor->regex;
        out.atomic = false;
        return true;
      }
      if (IsAnyCharAtom(node.text)) {
        // Inside (name ...) a wildcard must not cross into a directory.
        out.text = in_name_ ? std::string("[^") + std::string(kSepClassMembers) + "]" : std::string(".");
        out.atomic = true;
        return true;
      }
      if (const NamedClass* named = LookupNamedClass(node.text); named != nullptr) {
        out.text = "[" + std::string(named->members) + "]";
        out.atomic = true;
        return true;
      }
    }
    if (node.text.empty()) {
      return sink_.Fail(RxError::kInvalidArgument, node.offset, "empty string \"\" matches nothing useful");
    }
    out.text.clear();
    AppendEscapedLiteral(node.text, out.text);
    out.atomic = node.text.size() == 1;
    return true;
  }

  bool ResolveOp(const Node& list, Op& op) {
    const Node& head = list.children.front();
    if (head.kind != NodeKind::kAtom || head.quoted) {
      return sink_.Fail(RxError::kMissingOperator, head.offset,
                        "expected an operator name after '(' (e.g. seq, or, dir, ext)");
    }
    if (const std::optional<Op> found = LookupOp(head.text); found.has_value()) {
      op = *found;
      return true;
    }
    if (IsAtomKeyword(head.text)) {
      return sink_.Fail(RxError::kUnknownOperator, head.offset,
                        "'" + head.text + "' is not an operator; write it without parentheses");
    }
    std::string message = "unknown operator '" + head.text + "'";
    if (const std::string_view suggestion = SuggestOperator(head.text); !suggestion.empty()) {
      message += " (did you mean '";
      message += suggestion;
      message += "'?)";
    }
    return sink_.Fail(RxError::kUnknownOperator, head.offset, std::move(message));
  }

  bool LowerList(const Node& list, Lowered& out) {
    Op op = Op::kSeq;
    if (!ResolveOp(list, op)) {
      return false;
    }
    switch (op) {
      case Op::kSeq:
        return RequireArgs(list, 1) && LowerSequence(list, 1, out);
      case Op::kOr:
        return LowerOr(list, out);
      case Op::kStar:
        return LowerQuantifier(list, "*", out);
      case Op::kPlus:
        return LowerQuantifier(list, "+", out);
      case Op::kOpt:
        return LowerQuantifier(list, "?", out);
      case Op::kRepeat:
        return LowerRepeat(list, out);
      case Op::kAny:
        return LowerAny(list, out);
      case Op::kNot:
        return LowerNot(list, out);
      case Op::kPath:
      case Op::kDir:
      case Op::kExcludeDir:
      case Op::kName:
      case Op::kExt:
      default:
        return sink_.Fail(RxError::kPathOperatorMisplaced, list.offset,
                          "'" + std::string(OpNameOf(list)) +
                              "' is only allowed at the top level (alone or with other path clauses) "
                              "or directly inside (path ...)");
    }
  }

  bool RequireArgs(const Node& list, std::size_t min_args) {
    if (list.children.size() - 1 >= min_args) {
      return true;
    }
    return sink_.Fail(RxError::kWrongArity, list.offset,
                      "'" + std::string(OpNameOf(list)) + "' needs at least " + std::to_string(min_args) +
                          (min_args == 1 ? " argument" : " arguments"));
  }

  // Concatenation of list.children[first..].
  bool LowerSequence(const Node& list, std::size_t first, Lowered& out) {
    out.text.clear();
    std::size_t count = 0;
    bool single_atomic = false;
    for (std::size_t i = first; i < list.children.size(); ++i) {
      Lowered child;
      if (!Lower(list.children.at(i), child)) {
        return false;
      }
      out.text += child.text;
      single_atomic = child.atomic;
      ++count;
    }
    out.atomic = count == 1 && single_atomic;
    return true;
  }

  bool LowerOr(const Node& list, Lowered& out) {
    if (!RequireArgs(list, 1)) {
      return false;
    }
    if (list.children.size() == 2) {
      return Lower(list.children.at(1), out);
    }
    std::vector<std::string> alternatives;
    alternatives.reserve(list.children.size() - 1);
    for (std::size_t i = 1; i < list.children.size(); ++i) {
      Lowered child;
      if (!Lower(list.children.at(i), child)) {
        return false;
      }
      alternatives.push_back(std::move(child.text));
    }
    out.text = GroupAlternatives(alternatives);
    out.atomic = true;
    return true;
  }

  // Lowers children[body_index..] as the body of a quantifier with `suffix`.
  bool LowerQuantifiedBody(const Node& list, std::size_t body_index, std::string_view suffix, Lowered& out) {
    if (in_quantifier_) {
      return sink_.Fail(RxError::kNestedQuantifier, list.offset,
                        "quantifier '" + std::string(OpNameOf(list)) +
                            "' inside another quantifier is not allowed (catastrophic backtracking guard)");
    }
    in_quantifier_ = true;
    Lowered body;
    const bool ok = LowerSequence(list, body_index, body);
    in_quantifier_ = false;
    if (!ok) {
      return false;
    }
    out.text = body.atomic ? body.text : "(?:" + body.text + ")";
    out.text += suffix;
    out.atomic = false;
    return true;
  }

  bool LowerQuantifier(const Node& list, std::string_view suffix, Lowered& out) {
    return RequireArgs(list, 1) && LowerQuantifiedBody(list, 1, suffix, out);
  }

  bool LowerRepeat(const Node& list, Lowered& out) {
    const std::optional<RepeatSpec> spec = ParseRepeatShape(list);
    if (!spec.has_value()) {
      return sink_.Fail(RxError::kWrongArity, list.offset,
                        "'repeat' expects (repeat N expr...) or (repeat MIN MAX expr...)");
    }
    const std::size_t upper = spec->max.value_or(spec->min);
    if (upper > kMaxRxRepeatCount) {
      return sink_.Fail(RxError::kInvalidRange, list.offset,
                        "repeat count above " + std::to_string(kMaxRxRepeatCount));
    }
    if (spec->min > upper) {
      return sink_.Fail(RxError::kInvalidRange, list.offset, "repeat MIN is greater than MAX");
    }
    std::string suffix = "{" + std::to_string(spec->min);
    if (spec->max.has_value()) {
      suffix += "," + std::to_string(*spec->max);
    }
    suffix += '}';
    return LowerQuantifiedBody(list, spec->body_index, suffix, out);
  }

  // Members of a (any ...) class, without the surrounding brackets.
  bool LowerClassMembers(const Node& list, std::string& members) {
    if (!RequireArgs(list, 1)) {
      return false;
    }
    for (std::size_t i = 1; i < list.children.size(); ++i) {
      const Node& arg = list.children.at(i);
      if (arg.kind != NodeKind::kAtom) {
        return sink_.Fail(RxError::kInvalidArgument, arg.offset,
                          "(any ...) takes characters, ranges like a-z, or class names like digit");
      }
      if (!arg.quoted) {
        if (IsAnyCharAtom(arg.text)) {
          return sink_.Fail(RxError::kInvalidArgument, arg.offset, "'any-char' cannot be used inside (any ...)");
        }
        if (const NamedClass* named = LookupNamedClass(arg.text); named != nullptr) {
          members += named->members;
          continue;
        }
      }
      if (!AppendCharSet(arg, members)) {
        return false;
      }
    }
    return true;
  }

  // Characters and a-z ranges of one (any ...) argument.
  bool AppendCharSet(const Node& arg, std::string& members) {
    const std::string& s = arg.text;
    if (s.empty()) {
      return sink_.Fail(RxError::kInvalidArgument, arg.offset, "empty string in (any ...)");
    }
    bool has_separator = false;
    std::size_t i = 0;
    while (i < s.size()) {
      if (i + 2 < s.size() && s.at(i + 1) == '-') {
        if (const auto low = static_cast<unsigned char>(s.at(i)),
                       high = static_cast<unsigned char>(s.at(i + 2));
            low > high) {
          return sink_.Fail(RxError::kInvalidRange, arg.offset,
                            "invalid range '" + s.substr(i, 3) + "': start is after end");
        }
        AppendClassChar(s.at(i), members);
        members += '-';
        AppendClassChar(s.at(i + 2), members);
        i += 3;
        continue;
      }
      if (IsSeparator(s.at(i))) {
        has_separator = true;
      } else {
        AppendClassChar(s.at(i), members);
      }
      ++i;
    }
    if (has_separator) {
      members += kSepClassMembers;  // either separator stands for both
    }
    return true;
  }

  bool LowerAny(const Node& list, Lowered& out) {
    std::string members;
    if (!LowerClassMembers(list, members)) {
      return false;
    }
    out.text = "[" + members + "]";
    out.atomic = true;
    return true;
  }

  bool LowerNot(const Node& list, Lowered& out) {
    if (list.children.size() != 2) {
      return sink_.Fail(RxError::kWrongArity, list.offset, "'not' takes exactly one argument");
    }
    const Node& arg = list.children.at(1);
    std::string members;
    if (arg.kind == NodeKind::kAtom && !arg.quoted && LookupNamedClass(arg.text) != nullptr) {
      members = LookupNamedClass(arg.text)->members;
    } else if (PeekOp(arg) == Op::kAny) {
      if (!LowerClassMembers(arg, members)) {
        return false;
      }
    } else {
      return sink_.Fail(RxError::kGeneralNegationNotSupported, arg.offset,
                        "'not' only negates a character class: (not digit) or (not (any ...))");
    }
    if (in_name_) {
      members += kSepClassMembers;  // a negated class must not match a separator inside a name
    }
    out.text = "[^" + members + "]";
    out.atomic = true;
    return true;
  }

  // ---------------------------------------------------------------- path ----

  // Literal arguments of dir / exclude-dir / ext (bare words are always
  // literal here: (dir word) is the folder "word", not the word class).
  bool CollectLiteralArgs(const Node& clause, std::vector<std::string>& texts) {
    if (!RequireArgs(clause, 1)) {
      return false;
    }
    for (std::size_t i = 1; i < clause.children.size(); ++i) {
      const Node& arg = clause.children.at(i);
      if (arg.kind != NodeKind::kAtom) {
        return sink_.Fail(RxError::kInvalidArgument, arg.offset,
                          "'" + std::string(OpNameOf(clause)) + "' takes names, not expressions");
      }
      if (arg.text.empty()) {
        return sink_.Fail(RxError::kInvalidArgument, arg.offset, "empty name in '" +
                                                                     std::string(OpNameOf(clause)) + "'");
      }
      texts.push_back(arg.text);
    }
    return true;
  }

  bool CollectDirAlternatives(const Node& clause, std::vector<std::string>& alternatives) {
    std::vector<std::string> texts;
    if (!CollectLiteralArgs(clause, texts)) {
      return false;
    }
    for (const std::string& text : texts) {
      std::string escaped;
      AppendEscapedLiteral(text, escaped);
      alternatives.push_back(std::move(escaped));
    }
    return true;
  }

  bool LowerExtClause(const Node& clause, std::string& group) {
    std::vector<std::string> texts;
    if (!CollectLiteralArgs(clause, texts)) {
      return false;
    }
    std::vector<std::string> alternatives;
    alternatives.reserve(texts.size());
    for (std::size_t i = 0; i < texts.size(); ++i) {
      std::string_view ext = texts.at(i);
      while (!ext.empty() && ext.front() == '.') {
        ext.remove_prefix(1);
      }
      const Node& arg = clause.children.at(i + 1);
      if (ext.empty()) {
        return sink_.Fail(RxError::kInvalidArgument, arg.offset, "empty extension");
      }
      if (std::any_of(ext.begin(), ext.end(), IsSeparator)) {
        return sink_.Fail(RxError::kInvalidArgument, arg.offset, "an extension cannot contain '/' or '\\'");
      }
      std::string escaped;
      AppendEscapedLiteral(ext, escaped);
      alternatives.push_back(std::move(escaped));
    }
    // A capturing group on purpose: `\.(cpp|h)$` is the exact shape
    // RecognizeExactExtensionPattern turns into the O(len) ExtensionSetMatcher.
    if (alternatives.size() == 1) {
      group = alternatives.front();
      return true;
    }
    group = "(";
    for (std::size_t i = 0; i < alternatives.size(); ++i) {
      if (i > 0) {
        group += '|';
      }
      group += alternatives.at(i);
    }
    group += ')';
    return true;
  }

  bool LowerNameClause(const Node& clause, std::string& name) {
    if (clause.children.size() != 2) {
      return sink_.Fail(RxError::kWrongArity, clause.offset,
                        "'name' takes exactly one expression; wrap several in (seq ...)");
    }
    in_name_ = true;
    Lowered lowered;
    const bool ok = Lower(clause.children.at(1), lowered);
    in_name_ = false;
    if (!ok) {
      return false;
    }
    name = std::move(lowered.text);
    return true;
  }

  bool CollectPathClause(const Node& clause, PathParts& parts) {
    const std::optional<Op> op = PeekOp(clause);
    if (op == Op::kPath) {
      return sink_.Fail(RxError::kPathOperatorMisplaced, clause.offset, "'path' cannot be nested");
    }
    if (!op.has_value() || !IsPathClauseOp(*op)) {
      if (clause.kind == NodeKind::kList && !op.has_value()) {
        if (Op ignored = Op::kSeq; !ResolveOp(clause, ignored)) {
          return false;  // unknown operator: report it with its suggestion
        }
      }
      if (clause.kind == NodeKind::kAtom) {
        return sink_.Fail(RxError::kInvalidArgument, clause.offset,
                          "expected a clause operator (dir, exclude-dir, name, ext) inside (path ...), got '" +
                              clause.text + "'");
      }
      return sink_.Fail(RxError::kInvalidArgument, clause.offset,
                        "only dir, exclude-dir, name and ext clauses are allowed inside (path ...)");
    }
    switch (*op) {
      case Op::kDir: {
        std::vector<std::string> alternatives;
        if (!CollectDirAlternatives(clause, alternatives)) {
          return false;
        }
        parts.dirs.push_back(GroupAlternatives(alternatives));
        return true;
      }
      case Op::kExcludeDir:
        return CollectDirAlternatives(clause, parts.exclusions);
      case Op::kName: {
        if (parts.name.has_value()) {
          return sink_.Fail(RxError::kDuplicatePathClause, clause.offset, "only one (name ...) per path");
        }
        std::string name;
        if (!LowerNameClause(clause, name)) {
          return false;
        }
        parts.name = std::move(name);
        return true;
      }
      case Op::kExt:
      default: {
        if (parts.ext.has_value()) {
          return sink_.Fail(RxError::kDuplicatePathClause, clause.offset,
                            "only one (ext ...) per path; list several extensions in it");
        }
        std::string group;
        if (!LowerExtClause(clause, group)) {
          return false;
        }
        parts.ext = std::move(group);
        return true;
      }
    }
  }

  // Deterministic 4-stage assembly (spec §5.2):
  //   exclusions -> directory chain -> filename stem -> extension.
  bool LowerPath(const std::vector<Node>& clauses, std::size_t first, std::string& out) {
    if (clauses.size() <= first) {
      return sink_.Fail(RxError::kWrongArity, first > 0 ? clauses.front().offset : 0,
                        "'path' needs at least one dir, exclude-dir, name or ext clause");
    }
    PathParts parts;
    for (std::size_t i = first; i < clauses.size(); ++i) {
      if (!CollectPathClause(clauses.at(i), parts)) {
        return false;
      }
    }

    std::string body;
    const bool has_leaf = parts.name.has_value() || parts.ext.has_value();
    for (std::size_t i = 0; i < parts.dirs.size(); ++i) {
      // The first directory starts a segment; later ones may sit any number of
      // levels below the previous one.
      body += i == 0 ? std::string(kSegmentStart) : "(?:.*" + std::string(kSepClass) + ")?";
      body += parts.dirs.at(i);
      const bool last = i + 1 == parts.dirs.size();
      // A directory followed by a name/ext (or another directory) must be an
      // ancestor; the last one alone may also be the matched item itself.
      body += (last && !has_leaf) ? std::string(kSegmentEnd) : std::string(kSepClass);
    }
    if (parts.name.has_value()) {
      body += parts.dirs.empty() ? std::string(kSegmentStart) : "(?:.*" + std::string(kSepClass) + ")?";
      body += *parts.name;
    } else if (parts.ext.has_value() && !parts.dirs.empty()) {
      body += ".*";  // any filename below the directory chain
    }
    if (parts.ext.has_value()) {
      body += "\\.";
      body += *parts.ext;
      body += '$';
    } else if (parts.name.has_value()) {
      body += '$';
    }

    if (!parts.exclusions.empty()) {
      out += "^(?!.*";
      out += kSegmentStart;
      out += GroupAlternatives(parts.exclusions);
      out += kSegmentEnd;
      out += ')';
      if (!body.empty()) {
        out += ".*";  // the lookahead anchors at the start; let the rest float again
      }
    }
    out += body;
    return true;
  }

  ErrorSink& sink_;
  bool in_quantifier_ = false;
  bool in_name_ = false;
};

// ============================================================================
// Harvester: longest literal every match must contain (spec §6)
// ============================================================================

class Harvester {
public:
  void HarvestTop(const std::vector<Node>& forms) {
    for (const Node& form : forms) {
      HarvestNode(form);
    }
  }

  [[nodiscard]] std::string Take() {
    if (best_.size() < kMinRequiredLiteralLength) {
      best_.clear();
    }
    return std::move(best_);
  }

private:
  // A literal is matched with separators widened to [/\\], so only the
  // separator-free pieces are guaranteed verbatim in the text.
  void Consider(std::string_view text) {
    std::size_t start = 0;
    while (start <= text.size()) {
      std::size_t end = start;
      while (end < text.size() && !IsSeparator(text.at(end))) {
        ++end;
      }
      if (end - start > best_.size()) {
        best_.assign(text.substr(start, end - start));
      }
      start = end + 1;
    }
  }

  void HarvestChildren(const Node& list, std::size_t first) {
    for (std::size_t i = first; i < list.children.size(); ++i) {
      HarvestNode(list.children.at(i));
    }
  }

  void HarvestNode(const Node& node) {
    if (node.kind == NodeKind::kAtom) {
      if (node.quoted || !IsAtomKeyword(node.text)) {
        Consider(node.text);
      }
      return;
    }
    const std::optional<Op> op = PeekOp(node);
    if (!op.has_value()) {
      return;
    }
    switch (*op) {
      case Op::kSeq:
      case Op::kPlus:  // at least one occurrence of the body
      case Op::kPath:
      case Op::kName:
        HarvestChildren(node, 1);
        return;
      case Op::kOr:  // an alternative is required only when it is the only one
        if (node.children.size() == 2) {
          HarvestNode(node.children.at(1));
        }
        return;
      case Op::kRepeat:
        if (const std::optional<RepeatSpec> spec = ParseRepeatShape(node); spec.has_value() && spec->min >= 1) {
          HarvestChildren(node, spec->body_index);
        }
        return;
      case Op::kDir:  // a single folder name is required; alternatives are not
        if (node.children.size() == 2) {
          Consider(node.children.at(1).text);
        }
        return;
      case Op::kExt:
        if (node.children.size() == 2) {
          std::string_view ext = node.children.at(1).text;
          while (!ext.empty() && ext.front() == '.') {
            ext.remove_prefix(1);
          }
          Consider("." + std::string(ext));
        }
        return;
      case Op::kStar:        // zero occurrences allowed
      case Op::kOpt:         // zero occurrences allowed
      case Op::kAny:         // a class, not a literal
      case Op::kNot:         // a class, not a literal
      case Op::kExcludeDir:  // must be absent
      default:
        return;
    }
  }

  std::string best_;
};

}  // namespace

RxCompileResult CompileRx(std::string_view source) {
  RxCompileResult result;
  if (source.size() > kMaxRxSourceLength) {
    result.error = RxError::kPatternTooLong;
    result.message = "pattern longer than " + std::to_string(kMaxRxSourceLength) + " characters";
    return result;
  }

  ErrorSink sink;
  std::vector<Node> forms;
  Parser parser(source, sink);
  std::string regex;
  if (parser.ParseAll(forms)) {
    Lowerer lowerer(sink);
    (void)lowerer.LowerTop(forms, regex);  // failure is recorded in `sink`
  }

  if (const CompileError& error = sink.Error(); error.error != RxError::kNone) {
    result.error = error.error;
    result.message = error.message;
    result.error_offset = error.offset;
    result.open_paren_count = parser.OpenParenCount();
    return result;
  }
  if (regex.size() > kMaxRxEmittedLength) {
    result.error = RxError::kEmittedRegexTooLong;
    result.message = "generated regex longer than " + std::to_string(kMaxRxEmittedLength) + " characters";
    return result;
  }

  Harvester harvester;
  harvester.HarvestTop(forms);
  result.regex = std::move(regex);
  result.required_literal = harvester.Take();
  return result;
}

RxTokenCategory ClassifyRxAtom(std::string_view atom, bool is_clause_head) noexcept {
  if (atom.empty()) {
    return RxTokenCategory::kBareLiteral;
  }

  if (is_clause_head) {
    if (const auto op_opt = LookupOp(atom); op_opt.has_value()) {
      const Op op = *op_opt;
      return (op == Op::kPath || IsPathClauseOp(op)) ? RxTokenCategory::kPathClause
                                                     : RxTokenCategory::kOperator;
    }
    return RxTokenCategory::kBareLiteral;
  }

  if (IsAtomKeyword(atom)) {
    return RxTokenCategory::kNamedClass;
  }

  return RxTokenCategory::kBareLiteral;
}

const char* DescribeRxError(RxError error) noexcept {
  switch (error) {
    case RxError::kNone:
      return "ok";
    case RxError::kPatternTooLong:
      return "pattern too long";
    case RxError::kEmittedRegexTooLong:
      return "generated regex too long";
    case RxError::kNestingTooDeep:
      return "nesting too deep";
    case RxError::kUnclosedParen:
      return "missing ')'";
    case RxError::kUnexpectedCloseParen:
      return "unexpected ')'";
    case RxError::kUnterminatedString:
      return "unterminated string";
    case RxError::kEmptyList:
      return "empty list";
    case RxError::kMissingOperator:
      return "missing operator";
    case RxError::kUnknownOperator:
      return "unknown operator";
    case RxError::kWrongArity:
      return "wrong number of arguments";
    case RxError::kInvalidArgument:
      return "invalid argument";
    case RxError::kInvalidRange:
      return "invalid range";
    case RxError::kNestedQuantifier:
      return "nested quantifier";
    case RxError::kGeneralNegationNotSupported:
      return "general negation not supported";
    case RxError::kPathOperatorMisplaced:
      return "path operator misplaced";
    case RxError::kDuplicatePathClause:
      return "duplicate path clause";
    default:
      return "unknown error";
  }
}

}  // namespace rx
