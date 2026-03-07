### Feature ideas – prioritized roadmap

#### Top priority

- **Cross-platform support (macOS-first workflow)**  
  - Fully support developing and testing under macOS while targeting Windows.  
  - Test the **index-based file option** end-to-end on macOS.  
  - Review and validate the **branch with preliminary prototyping**, then either merge or retire it.
  - bring macos Application/bootstrap to parity with windows
  - refactor the monolith UIRenderer
  - save the AI description together with a search, make it visible in the UI (perhaps a new window to handle saved searches)
  - improve the UI with a "classic" panel vs "AI" panel
  - make the app linux cross platform
  - BasicFinder for MacOs & users without admin rights
  - add the option to generate a prompt in the clipboard to be pasted into Copilot


#### Medium priority

- **Search strategy robustness and performance**
  - Fix the outstanding issues with the various search strategies.
  - Add or refine tests/benchmarks to validate correctness and performance under realistic workloads.
  - on macos, test the value of adding boost

- **PathPatternMatcher improvements**
  - Continue the new `PathPatternMatcher` implementation.
  - Benchmark against the current implementation and keep the faster/cleaner design.

- **AI-assisted interface experiments**
  - Explore an AI-based assistant interface using local LLMs (e.g., `google/gemma-3-1b-it`, function calling / functionGemma-style integration).
  - explore a copilot plug-in to get results from our app
  - explore calling directly gemini API from the app

#### Low priority

- **Logger streaming interface**
  - Improve the logger to support a modern streaming-style interface (see `https://www.cppstories.com/2021/stream-logger/` for inspiration).
  - remove the dependency on the json library (but now needed to call Gemini AI)

