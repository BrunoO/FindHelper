/**
 * @file search/rx/RxFormatter.cpp
 * @brief Implementation of S-expression pretty-printer for `rx:` expressions.
 */

#include "search/rx/RxFormatter.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace rx {

namespace {

struct Token {
  std::string_view text;
  bool is_lparen = false;
  bool is_rparen = false;
};

// Consumes a quoted string starting at `start` (which must point at '"').
// Returns the index just past the closing quote (or source.size() if unterminated).
std::size_t ConsumeQuotedString(std::string_view source, std::size_t start) {
  std::size_t i = start + 1;
  const std::size_t n = source.size();
  while (i < n && source.at(i) != '"') {
    if (source.at(i) == '\\' && (i + 1 < n)) {
      i += 2;
    } else {
      ++i;
    }
  }
  if (i < n) {
    ++i;  // consume closing quote
  }
  return i;
}

std::vector<Token> Tokenize(std::string_view source) {  // NOSONAR(cpp:S3776) - tokenizer has inherent character-class branching
  std::vector<Token> tokens;
  tokens.reserve((source.size() / 3) + 4);

  std::size_t i = 0;
  const std::size_t n = source.size();

  while (i < n) {
    const auto c = static_cast<unsigned char>(source.at(i));
    if (std::isspace(c) != 0) {
      ++i;
      continue;
    }
    if (c == '(') {
      tokens.push_back({source.substr(i, 1), true, false});
      ++i;
      continue;
    }
    if (c == ')') {
      tokens.push_back({source.substr(i, 1), false, true});
      ++i;
      continue;
    }
    if (c == '"') {
      const std::size_t start = i;
      i = ConsumeQuotedString(source, i);
      tokens.push_back({source.substr(start, i - start), false, false});
      continue;
    }

    const std::size_t start = i;
    while (i < n) {
      if (const auto ch = static_cast<unsigned char>(source.at(i));
          std::isspace(ch) != 0 || ch == '(' || ch == ')' || ch == '"') {
        break;
      }
      ++i;
    }
    tokens.push_back({source.substr(start, i - start), false, false});
  }

  return tokens;
}

bool IsContainerOperator(std::string_view op) noexcept {
  return op == "path" || op == "seq" || op == "or" || op == "and";
}

[[nodiscard]] std::vector<std::size_t> ComputeSubexpressionLengths(const std::vector<Token>& tokens) {
  std::vector<std::size_t> expr_lengths(tokens.size(), 0);
  std::vector<std::size_t> lparen_indices;
  lparen_indices.reserve(tokens.size() / 2);

  for (std::size_t i = 0; i < tokens.size(); ++i) {
    if (tokens.at(i).is_lparen) {
      lparen_indices.push_back(i);
    } else if (tokens.at(i).is_rparen && !lparen_indices.empty()) {
      const std::size_t start_idx = lparen_indices.back();
      lparen_indices.pop_back();

      std::size_t total_len = 0;
      for (std::size_t j = start_idx; j <= i; ++j) {
        total_len += tokens.at(j).text.size() + 1;
      }
      expr_lengths.at(start_idx) = total_len;
    }
  }

  return expr_lengths;
}

[[nodiscard]] bool ShouldWrapSubexpression(std::string_view parent_op, std::string_view op_name,
                                          std::size_t current_len) noexcept {
  constexpr std::size_t kSubtreeWrapThreshold = 28;
  if (!IsContainerOperator(parent_op)) {
    return false;
  }
  return parent_op == "path" || current_len > kSubtreeWrapThreshold || op_name == "name";
}

void AppendLeftParen(std::string& result, std::vector<std::string_view>& op_stack,
                     const std::vector<Token>& tokens, const std::vector<std::size_t>& expr_lengths,
                     std::size_t tok_idx) {
  std::string_view op_name;
  if (tok_idx + 1 < tokens.size() && !tokens.at(tok_idx + 1).is_lparen && !tokens.at(tok_idx + 1).is_rparen) {
    op_name = tokens.at(tok_idx + 1).text;
  }
  op_stack.push_back(op_name);

  if (tok_idx > 0) {
    if (const std::string_view parent_op = (op_stack.size() >= 2) ? op_stack.at(op_stack.size() - 2) : std::string_view{};
        ShouldWrapSubexpression(parent_op, op_name, expr_lengths.at(tok_idx))) {
      result.push_back('\n');
      const std::size_t indent = (op_stack.size() > 1) ? ((op_stack.size() - 1) * 2) : 0;
      result.append(indent, ' ');
    } else if (!tokens.at(tok_idx - 1).is_lparen) {
      result.push_back(' ');
    }
  }

  result.push_back('(');
}

void AppendRightParen(std::string& result, std::vector<std::string_view>& op_stack) {
  if (!op_stack.empty()) {
    op_stack.pop_back();
  }
  result.push_back(')');
}

void AppendAtomToken(std::string& result, const std::vector<Token>& tokens, std::size_t tok_idx) {
  if (tok_idx > 0 && !tokens.at(tok_idx - 1).is_lparen) {
    result.push_back(' ');
  }
  result.append(tokens.at(tok_idx).text);
}

}  // namespace

std::string FormatRx(std::string_view source) {
  std::string_view prefix;
  std::string_view content = source;

  if (content.rfind("rx:", 0) == 0) {
    prefix = "rx:";
    content.remove_prefix(3);
  }

  const std::vector<Token> tokens = Tokenize(content);
  if (tokens.empty()) {
    return std::string(source);
  }

  const std::vector<std::size_t> expr_lengths = ComputeSubexpressionLengths(tokens);

  std::string result;
  result.reserve(source.size() + 32);

  if (!prefix.empty()) {
    result.append(prefix);
  }

  std::vector<std::string_view> op_stack;
  op_stack.reserve(16);

  for (std::size_t tok_idx = 0; tok_idx < tokens.size(); ++tok_idx) {
    if (const Token& tok = tokens.at(tok_idx); tok.is_lparen) {
      AppendLeftParen(result, op_stack, tokens, expr_lengths, tok_idx);
    } else if (tok.is_rparen) {
      AppendRightParen(result, op_stack);
    } else {
      AppendAtomToken(result, tokens, tok_idx);
    }
  }

  return result;
}

}  // namespace rx
