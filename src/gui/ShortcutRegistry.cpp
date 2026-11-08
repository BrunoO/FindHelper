/**
 * @file gui/ShortcutRegistry.cpp
 * @brief Non-inline implementations for the shortcut registry helpers.
 */

#include "gui/ShortcutRegistry.h"

#include <cassert>
#include <string>

const ShortcutDef& FindShortcut(ShortcutAction action) noexcept {
  for (const auto& def : kShortcuts) {
    if (def.action == action) {
      return def;
    }
  }
  assert(false && "ShortcutAction not found in kShortcuts — add it to the registry");  // NOLINT(cert-dcl03-c,hicpp-static-assert) - runtime guard for missing registry entry
  return kShortcuts.front();  // Unreachable in correct code; silences compiler warning
}

std::string FormatLabel(const ShortcutDef& def) {
  if (!def.primary_modifier) {
    return std::string(def.key_label);
  }
  std::string label;
  label += GetPrimaryModifierName();
  label += '+';
  label += def.key_label;
  return label;
}
