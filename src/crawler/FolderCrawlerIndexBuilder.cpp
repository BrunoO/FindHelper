#include "core/IndexBuilderBase.h"

#include "crawler/FolderCrawler.h"
#include "index/FileIndex.h"
#include "utils/Logger.h"

namespace {

bool HasCrawlFolder(const IndexBuilderConfig& config) {
  return !config.crawl_folder.empty();
}

void MarkFailedWithMessage(IndexBuildState* state, const char* message) {
  state->MarkFailed();
  state->SetLastErrorMessage(message);
}

class FolderCrawlerIndexBuilder final : public IndexBuilderBase {
public:
  FolderCrawlerIndexBuilder(FileIndex& file_index, IndexBuilderConfig config)
      : IndexBuilderBase(file_index, std::move(config)) {}

  ~FolderCrawlerIndexBuilder() override = default;

  void Start(IndexBuildState& state) override {  // NOLINT(readability-function-cognitive-complexity) - Lambda body contributes complexity; splitting would require complex state sharing (see NOSONAR cpp:S1188)
    // If no crawl folder is configured, there is nothing to do.
    if (!HasCrawlFolder(GetConfig())) {
      LOG_INFO_BUILD("FolderCrawlerIndexBuilder: no crawl folder specified, skipping background build");
      return;
    }

    if (!BeginStart(state)) {
      LOG_WARNING("FolderCrawlerIndexBuilder::Start called while already running");
      return;
    }

    // Launch background thread to perform the crawl.
    GetWorkerThread() = std::thread([this]() {  // NOLINT(readability-function-cognitive-complexity) NOSONAR(cpp:S1188) - Lambda encapsulates crawl + finalize; splitting would complicate state sharing
      if (IndexBuildState* state = GetSharedState(); state != nullptr) {
        try {
          LOG_INFO_BUILD("FolderCrawlerIndexBuilder: starting crawl for folder: " << GetConfig().crawl_folder);

          const FolderCrawlerConfig crawler_config{};
          // Use default thread count (0 = auto) and default batch/progress settings.
          FolderCrawler crawler(GetFileIndex(), crawler_config);

          // Use the shared entries_processed as the progress counter for total entries.
          std::atomic<size_t>* progress_counter = &state->entries_processed;
          const std::atomic<bool>* cancel_flag = &state->cancel_requested;

          const bool success = crawler.Crawl(GetConfig().crawl_folder, progress_counter, cancel_flag);

          // After crawl completes (or fails), finalize index if successful.
          if (success) {
            // FolderCrawler inserts full paths in tree order (see ProcessEntries before
            // PushSubdirs). FinalizeFolderCrawlIndexing releases the name arena without
            // clearing PathStorage; on Windows it also applies OneDrive sentinel resets from
            // paths already in storage (see FileIndex::FinalizeFolderCrawlIndexing).
            // finalizing_population blocks concurrent searches during the locked finalize.
            state->finalizing_population.store(true);
            GetFileIndex().FinalizeFolderCrawlIndexing();
            state->finalizing_population.store(false);

            // Update final metrics
            state->files_processed.store(crawler.GetFilesProcessed());
            state->dirs_processed.store(crawler.GetDirectoriesProcessed());
            state->errors.store(crawler.GetErrorCount());
            state->MarkCompleted();
            LOG_INFO_BUILD("FolderCrawlerIndexBuilder: crawl completed successfully");
          } else {
            state->MarkFailed();
            if (!state->cancel_requested.load()) {
              state->SetLastErrorMessage("Folder crawl failed or was incomplete");
              LOG_ERROR_BUILD("FolderCrawlerIndexBuilder: crawl failed or was cancelled");
            } else {
              LOG_INFO_BUILD("FolderCrawlerIndexBuilder: crawl cancelled by user");
            }
          }
        } catch (const std::exception& e) {  // NOSONAR(cpp:S1181) - Catch-all needed: FolderCrawler and FileIndex operations can throw various exception types (runtime_error, bad_alloc, system_error, etc.)
          MarkFailedWithMessage(state, e.what());
          LOG_ERROR_BUILD("FolderCrawlerIndexBuilder: exception during crawl: " << e.what());
        } catch (...) {  // NOSONAR(cpp:S2738) - Catch-all needed for non-standard exceptions from filesystem operations
          MarkFailedWithMessage(state, "Unknown error during folder crawl");
          LOG_ERROR("FolderCrawlerIndexBuilder: unknown exception during crawl");
        }
        state->MarkInactive();
      }
      GetRunning().store(false);
    });
  }

};

} // namespace

std::unique_ptr<IIndexBuilder> CreateFolderCrawlerIndexBuilder(
    FileIndex& file_index, const IndexBuilderConfig& config) {
  // Only create a builder if we actually have a folder to crawl.
  if (!HasCrawlFolder(config)) {
    return nullptr;
  }
  return std::make_unique<FolderCrawlerIndexBuilder>(file_index, config);
}


