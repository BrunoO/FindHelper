#pragma once

#include <vector>

#include "index/FileIndex.h"

namespace mft_seams {

/**
 * @class IIngestor
 * @brief Seam 4: Ingestion of parsed records into FileIndex and topological finalization.
 */
class IIngestor {
 public:
  virtual ~IIngestor() = default;

  /**
   * Commits a batch of parsed entries into the index.
   * @param entries Vector of population batch entries.
   */
  virtual void IngestBatch(const std::vector<FileIndex::PopulationBatchEntry>& entries) = 0;

  /**
   * Resolves any stashed extension records onto their base records.
   */
  virtual void ResolveExtensions() = 0;

  /**
   * Finalizes the index: executes topological path recomputation and heals parent chains.
   */
  virtual void FinalizeIndex() = 0;
};

}  // namespace mft_seams
