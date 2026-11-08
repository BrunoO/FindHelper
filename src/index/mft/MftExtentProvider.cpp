#include "index/mft/MftExtentProvider.h"

#include <cstring>

#include "index/mft/MftRunlistDecoder.h"
#include "index/mft/NtfsRecordStructures.h"
#include "index/mft/RawMftRecordParser.h"
#include "utils/Logger.h"

namespace mft {

#ifdef _WIN32

MftExtentProvider::MftExtentProvider(HANDLE volume, volume_gateway::VolumeGateway& gateway)
    : volume_(volume), gateway_(&gateway) {}

std::optional<VolumeGeometry> MftExtentProvider::GetVolumeGeometry() {
  if (geometry_cached_) {
    return cached_geometry_;
  }
  if (gateway_ == nullptr || volume_ == INVALID_HANDLE_VALUE) {
    return std::nullopt;
  }

  NTFS_VOLUME_DATA_BUFFER volume_data{};
  if (!gateway_->GetVolumeData(volume_, volume_data)) {
    return std::nullopt;
  }

  VolumeGeometry geom{};
  geom.bytes_per_sector = volume_data.BytesPerSector;
  geom.bytes_per_cluster = volume_data.BytesPerCluster;
  geom.bytes_per_file_record = volume_data.BytesPerFileRecordSegment;
  geom.mft_start_lcn = static_cast<uint64_t>(volume_data.MftStartLcn.QuadPart);
  geom.total_clusters = static_cast<uint64_t>(volume_data.TotalClusters.QuadPart);
  cached_geometry_ = geom;
  geometry_cached_ = true;
  return geom;
}

std::optional<VolumeGeometry> MftExtentProvider::EnsureGeometry() {
  return GetVolumeGeometry();
}

bool MftExtentProvider::GetMftExtents(std::vector<MftDiskExtent>& out_extents) {
  // Output is exclusively from this call; stale entries would defeat the
  // emptiness check below and mix with partial decoder pushes on failure.
  out_extents.clear();
  const auto geom_opt = EnsureGeometry();
  if (!geom_opt.has_value()) {
    return false;
  }
  const auto& geom = *geom_opt;

  constexpr size_t kRecordBufferSize = 4096;
  std::vector<char> buffer(sizeof(uint64_t) + sizeof(uint32_t) + kRecordBufferSize, 0);
  DWORD bytes_returned = 0;
  if (!gateway_->GetFileRecord(volume_, 0, buffer.data(), static_cast<DWORD>(buffer.size()), bytes_returned)) {
    LOG_ERROR_BUILD("GetFileRecord(0) failed to read Record 0 ($MFT)");
    return false;
  }

  if (bytes_returned < sizeof(uint64_t) + sizeof(uint32_t)) {
    LOG_ERROR_BUILD("GetFileRecord(0) returned incomplete buffer (" << bytes_returned << " bytes)");
    return false;
  }

  // Unaligned-safe scalar read: buffer.data() + 8 is not guaranteed 4-byte aligned.
  uint32_t record_length = 0;
  std::memcpy(&record_length, buffer.data() + sizeof(uint64_t), sizeof(record_length));
  char* record_data = buffer.data() + sizeof(uint64_t) + sizeof(uint32_t);

  if (record_length < sizeof(NtfsFileRecordHeader) || record_length > kRecordBufferSize) {
    LOG_ERROR_BUILD("Record 0 length invalid: " << record_length);
    return false;
  }

  if (!RawMftRecordParser::ApplyIdempotentUsaFixup(record_data, record_length, geom.bytes_per_sector)) {
    LOG_ERROR_BUILD("Record 0 USA fixup failed (bytes_per_sector=" << geom.bytes_per_sector << ")");
    return false;
  }

  const auto* file_record = reinterpret_cast<const NtfsFileRecordHeader*>(record_data);  // NOSONAR(cpp:S3630) - on-disk NTFS layout; record_length bounds validated above
  if (!file_record->IsValid()) {
    LOG_ERROR_BUILD("Record 0 header is invalid");
    return false;
  }

  const char* record_end = record_data + record_length;
  const char* attr_ptr = record_data + file_record->first_attribute_offset;

  while (attr_ptr + sizeof(NtfsAttributeRecordHeader) <= record_end) {
    const auto* attr = reinterpret_cast<const NtfsAttributeRecordHeader*>(attr_ptr);  // NOSONAR(cpp:S3630) - on-disk NTFS layout; loop bounds validated
    if (attr->type_code == kAttributeEnd || attr->record_length == 0 ||
        attr_ptr + attr->record_length > record_end) {
      break;
    }

    if (attr->type_code == kAttributeAttributeList) {
      LOG_WARNING_BUILD("Record 0 contains $ATTRIBUTE_LIST (heavily fragmented $MFT)");
    }

    if (attr->type_code == kAttributeData && attr->name_length == 0 && attr->IsNonResident()) {
      // Completeness gate: if $DATA spills into extension records (the
      // $ATTRIBUTE_LIST case), this base runlist is partial and streaming it
      // would silently index a truncated $MFT. Fail closed on partial coverage.
      if (!BaseDataRunCoversStream(attr->form.nonresident.lowest_vcn,
                                   attr->form.nonresident.highest_vcn,
                                   attr->form.nonresident.file_size, geom.bytes_per_cluster)) {
        LOG_ERROR_BUILD("Record 0 $DATA runlist is partial; refusing truncated $MFT"
                        " (check $ATTRIBUTE_LIST spillover)");
        return false;
      }
      if (const size_t run_offset = attr->form.nonresident.data_run_offset; run_offset < attr->record_length) {
        const auto* runlist_ptr = reinterpret_cast<const uint8_t*>(attr_ptr + run_offset);  // NOSONAR(cpp:S3630) - byte-level runlist walk; run_offset bounds validated
        const size_t max_runlist_bytes = attr->record_length - run_offset;
        const auto decode_result = DecodeRunlist(
            runlist_ptr, max_runlist_bytes, 0, geom.bytes_per_cluster, out_extents);
        if (!decode_result.ok()) {
          LOG_WARNING_BUILD("Record 0 $DATA runlist decode failed (error="
                            << static_cast<int>(decode_result.error) << ")");
        }
        return decode_result.ok();
      }
    }

    attr_ptr += attr->record_length;
  }

  if (out_extents.empty()) {
    LOG_ERROR_BUILD("Record 0 does not contain valid non-resident $DATA extents");
    return false;
  }
  return true;
}

#else

std::optional<VolumeGeometry> MftExtentProvider::GetVolumeGeometry() {  // NOSONAR(cpp:S5817) - Mutates cached_geometry_ on Windows
  return std::nullopt;
}

bool MftExtentProvider::GetMftExtents(std::vector<MftDiskExtent>& /*out_extents*/) {  // NOSONAR(cpp:S5817) - Calls EnsureGeometry on Windows
  return false;
}

#endif  // _WIN32

}  // namespace mft
