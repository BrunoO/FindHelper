#pragma once

#include <vector>

#include "index/FileIndex.h"
#include "index/mft/seams/IIngestor.h"

namespace mft_seams {

/**
 * @class FileIndexIngestor
 * @brief Default production implementation of IIngestor committing batches to FileIndex.
 */
class FileIndexIngestor : public IIngestor {
 public:
  explicit FileIndexIngestor(FileIndex& index) : index_(index) {}

  void IngestBatch(const std::vector<FileIndex::PopulationBatchEntry>& entries) override {
    // Bulk staging: defer path indexing until topological finalization
    index_.InsertBatch(entries, /*defer_path_indexing=*/true);
  }

  void ResolveExtensions() override {
    // Extension stitching hook; base records merged before final recompute
  }

  void FinalizeIndex() override {
    // Single-pass topological path recompute + orphan pruning
    index_.RecomputeAllPaths();
  }

 private:
  FileIndex& index_;
};

}  // namespace mft_seams
