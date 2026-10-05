# Credits

This document lists third-party sources used in FindHelper, both direct dependencies and design references. All are used in compliance with their respective licenses.

---

## Direct Dependencies (Libraries)

### Dear ImGui

- **Source:** [github.com/ocornut/imgui](https://github.com/ocornut/imgui)
- **License:** MIT
- **Use:** Immediate-mode GUI framework for the application interface

### doctest

- **Source:** [github.com/doctest/doctest](https://github.com/doctest/doctest)
- **License:** MIT
- **Use:** Unit testing framework

### nlohmann/json

- **Source:** [github.com/nlohmann/json](https://github.com/nlohmann/json)
- **License:** MIT
- **Use:** JSON parsing and serialization (settings, saved searches, etc.)

### FreeType

- **Source:** [freetype.org](https://freetype.org/)
- **License:** FreeType License (FTL) or GPLv2 (dual license; we use FTL)
- **Use:** Font rendering via ImGui FreeType backend

### GLFW

- **Source:** [github.com/glfw/glfw](https://github.com/glfw/glfw)
- **License:** zlib/libpng
- **Use:** Window creation and input handling (macOS, Linux; Windows uses pre-built library)

### libcurl (optional)

- **Source:** [curl.se](https://curl.se/) / [github.com/curl/curl](https://github.com/curl/curl)
- **License:** curl license (MIT-style)
- **Use:** Optional on Linux; HTTP client for Gemini API when found at build time (otherwise a stub is used)

### Dear ImGui Test Engine (optional)

- **Source:** [github.com/ocornut/imgui_test_engine](https://github.com/ocornut/imgui_test_engine)
- **License:** Dear ImGui Test Engine License (v1.04); free for individuals, nonprofits, education, open-source derivatives, or entities with turnover &lt;$2M (see [licenses](http://www.dearimgui.com/licenses))
- **Use:** Optional on macOS when `ENABLE_IMGUI_TEST_ENGINE` is ON; in-process UI test harness

### Embedded fonts

The following fonts are embedded in the application binary (compressed TTF) and used for UI text and icons. All are used in compliance with their licenses (embedding and distribution in software are permitted).

#### Font Awesome (icons)

- **Source:** [fontawesome.com](https://fontawesome.com/)
- **License:** Font Awesome Free License (SIL OFL 1.1)
- **Use:** Icon font (Solid icons) embedded for UI elements (buttons, status indicators, etc.)

#### Cousine (UI text)

- **Source:** [Google Fonts – Cousine](https://fonts.google.com/specimen/Cousine) / [github.com/google/fonts](https://github.com/google/fonts) (OFL fonts)
- **License:** SIL Open Font License, Version 1.1 (OFL 1.1)
- **Use:** Embedded optional UI font (Cousine-Regular)

#### Karla (UI text)

- **Source:** [Google Fonts – Karla](https://fonts.google.com/specimen/Karla) / [github.com/googlefonts/karla](https://github.com/googlefonts/karla)
- **License:** SIL Open Font License, Version 1.1 (OFL 1.1)
- **Use:** Embedded optional UI font (Karla-Regular)

#### Roboto (UI text)

- **Source:** [Google Fonts – Roboto](https://fonts.google.com/specimen/Roboto) / [github.com/googlefonts/roboto](https://github.com/googlefonts/roboto)
- **License:** Apache License 2.0
- **Use:** Embedded optional UI font (Roboto-Medium)

### stb_image

- **Source:** [github.com/nothings/stb](https://github.com/nothings/stb) (stb_image.h)
- **License:** Public domain
- **Use:** Image loading (PNG, etc.) for sprite sheets and textures

### Boost (optional)

- **Source:** [boost.org](https://www.boost.org/)
- **License:** Boost Software License (BSL 1.0)
- **Use:** Optional; `unordered_map`, `regex`, `lockfree` when `FAST_LIBS_BOOST=ON`

---

## Theme Color Palettes (Design References)

The application includes UI themes inspired by popular color schemes. Color values are adapted for ImGui and our semantic color slots; we do not ship original theme code.

### Everforest

- **Source:** [github.com/sainnhe/everforest](https://github.com/sainnhe/everforest)
- **License:** MIT
- **Use:** Everforest Dark Hard palette (backgrounds, accents, text colors)

### Dracula

- **Source:** [draculatheme.com](https://draculatheme.com/) / [github.com/dracula/dracula-theme](https://github.com/dracula/dracula-theme)
- **License:** MIT
- **Use:** Dracula color palette (backgrounds, purple accent, pink hover)

### Nord

- **Source:** [nordtheme.com](https://www.nordtheme.com/) / [github.com/nordtheme/nord](https://github.com/nordtheme/nord)
- **License:** MIT
- **Use:** Nord color palette (frost backgrounds, cyan accent)

### Gruvbox

- **Source:** [github.com/morhetz/gruvbox](https://github.com/morhetz/gruvbox)
- **License:** MIT
- **Use:** Gruvbox Dark Hard palette (warm backgrounds, orange accent)

### One Dark

- **Source:** [github.com/atom/one-dark-syntax](https://github.com/atom/one-dark-syntax) (Atom editor)
- **License:** MIT
- **Use:** One Dark color palette (backgrounds, blue accent, warm text dim)

### Catppuccin

- **Source:** [catppuccin.com](https://catppuccin.com/) / [github.com/catppuccin/catppuccin](https://github.com/catppuccin/catppuccin)
- **License:** MIT
- **Use:** Catppuccin Mocha palette (pastel backgrounds, mauve accent, pink hover)

---

## Acknowledgments

Development of this project was assisted by the following AI-powered tools:

- **[Cursor](https://cursor.com/)** — AI-assisted editing and code completion
- **Jules** — AI pair programming
- **Qodo** — AI development assistance

These tools were used during development only; they are not dependencies of the application.

---

## License Summary

| Component           | License        |
|---------------------|----------------|
| Dear ImGui          | MIT            |
| doctest             | MIT            |
| nlohmann/json       | MIT            |
| FreeType            | FTL / GPLv2    |
| GLFW                | zlib           |
| libcurl (optional)  | curl (MIT-style) |
| ImGui Test Engine (optional) | Custom (v1.04) |
| Font Awesome        | SIL OFL 1.1    |
| Cousine (font)      | SIL OFL 1.1    |
| Karla (font)        | SIL OFL 1.1    |
| Roboto (font)       | Apache 2.0     |
| stb_image           | Public domain  |
| Boost (optional)    | BSL 1.0        |
| Everforest    | MIT            |
| Dracula       | MIT            |
| Nord          | MIT            |
| Gruvbox       | MIT            |
| One Dark      | MIT            |
| Catppuccin    | MIT            |

---

*Last updated: 2026-02-20*
