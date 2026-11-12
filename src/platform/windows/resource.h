#ifndef RESOURCE_H  // Windows resource files require traditional header guards for .rc file compatibility
#define RESOURCE_H

// Icon resource ID
// Note: Windows uses the icon with the lowest ID value as the application icon
// Using ID 1 ensures it's recognized as the main application icon
// The manifest resource uses a different resource type (RT_MANIFEST), so it doesn't conflict
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage) - Must remain macro for Windows resource compiler (.rc files require macros)
#define IDI_APP_ICON 1  // NOSONAR(cpp:S5028) - Must remain macro for Windows resource compiler (.rc files require macros)

#endif  // RESOURCE_H
