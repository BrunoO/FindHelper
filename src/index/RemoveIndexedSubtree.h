#pragma once

#include <cstdint>

class FileIndex;

/**
 * @brief Remove root_id and, when it is a directory, every indexed descendant.
 *
 * Used for Recycle Bin soft-deletes (folder rename to $R… does not emit per-child
 * deletes). Descendants are discovered by resolved parent-id membership: a child
 * is removed when its parentID (or the id from ResolveEntryReference(parentID))
 * is already in the remove set. Matching by bare MFT record number is intentionally
 * avoided so a synthetic id N cannot evacuate children of a real USN folder whose
 * FRN shares record number N.
 *
 * Volume-root children (parent MFT record 5, e.g. C:\file.txt) are never removed
 * by cascading through the implicit volume root — only when root_id is that file
 * itself. Entries whose stored path is still a drive-root child
 * (IsVolumeRootChildPath) are also skipped, so a corrupted parentID cannot
 * evacuate C:\* files during bulk Recycle Bin soft-deletes. Mirrors
 * PruneOrphanSubtrees' IsRootDirectoryRecord guard.
 *
 * @param file_index Index to mutate
 * @param root_id File or directory id (typically a USN FRN)
 */
void RemoveIndexedSubtree(FileIndex& file_index, uint64_t root_id);
