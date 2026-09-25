#include "index/mft/MftExtentProvider.h"

#include "index/mft/MftRunlistDecoder.h"
#include "index/mft/NtfsRecordStructures.h"
#include "index/mft/RawMftRecordParser.h"

namespace mft {

#ifdef _WIN32

MftExtentProvider::MftExtentProvider(HANDLE volume, volume_gateway::VolumeGateway& gateway)
    : volume_(volume), gateway_(&gateway) {}

bool MftExtentProvider::GetVolumeGeometry(mft_seams::VolumeGeometry& out_geometry) {
  if (gateway_ == nullptr || volume_ == INVALID_HANDLE_VALUE) {
    return false;
  }

  NTFS_VOLUME_DATA_BUFFER volume_data{};
  if (!gateway_->GetVolumeData(volume_, volume_data)) {
    return false;
  }

  out_geometry.bytes_per_sector = volume_data.BytesPerSector;
  out_geometry.bytes_per_cluster = volume_data.BytesPerCluster;
  out_geometry.bytes_per_file_record = volume_data.BytesPerFileRecordSegment;
  out_geometry.mft_start_lcn = static_cast<uint64_t>(volume_data.MftStartLcn.QuadPart);
  out_geometry.total_clusters = static_cast<uint64_t>(volume_data.TotalClusters.QuadPart);
  return true;
}

bool MftExtentProvider::GetMftExtents(std::vector<mft_seams::MftDiskExtent>& out_extents) {
  mft_seams::VolumeGeometry geom{};
  if (!GetVolumeGeometry(geom)) {
    return false;
  }

  constexpr size_t kRecordBufferSize = 4096;
  std::vector<char> buffer(sizeof(uint64_t) + sizeof(uint32_t) + kRecordBufferSize, 0);
  DWORD bytes_returned = 0;
  if (!gateway_->GetFileRecord(volume_, 0, buffer.data(), static_cast<DWORD>(buffer.size()), bytes_returned)) {
    return false;
  }

  if (bytes_returned < sizeof(uint64_t) + sizeof(uint32_t)) {
    return false;
  }

  const uint32_t record_length = *reinterpret_cast<const uint32_t*>(buffer.data() + sizeof(uint64_t));
  char* record_data = buffer.data() + sizeof(uint64_t) + sizeof(uint32_t);

  if (record_length < sizeof(NtfsFileRecordHeader) || record_length > kRecordBufferSize) {
    return false;
  }

  if (!RawMftRecordParser::ApplyIdempotentUsaFixup(record_data, record_length, geom.bytes_per_sector)) {
    return false;
  }

  const auto* file_record = reinterpret_cast<const NtfsFileRecordHeader*>(record_data);
  if (!file_record->IsValid()) {
    return false;
  }

  const char* record_end = record_data + record_length;
  const char* attr_ptr = record_data + file_record->first_attribute_offset;

  while (attr_ptr + sizeof(NtfsAttributeRecordHeader) <= record_end) {
    const auto* attr = reinterpret_cast<const NtfsAttributeRecordHeader*>(attr_ptr);
    if (attr->type_code == kAttributeEnd) {
      break;
    }
    if (attr->record_length == 0 || attr_ptr + attr->record_length > record_end) {
      break;
    }

    if (attr->type_code == kAttributeData && attr->name_length == 0 && attr->IsNonResident()) {
      if (const size_t run_offset = attr->form.nonresident.data_run_offset; run_offset < attr->record_length) {
        const auto* runlist_ptr = reinterpret_cast<const uint8_t*>(attr_ptr + run_offset);
        const size_t max_runlist_bytes = attr->record_length - run_offset;
        return MftRunlistDecoder::DecodeRunlist(
            runlist_ptr, max_runlist_bytes, 0, geom.bytes_per_cluster, out_extents);
      }
    }

    attr_ptr += attr->record_length;
  }

  return !out_extents.empty();
}

#else

MftExtentProvider::MftExtentProvider() = default;

bool MftExtentProvider::GetVolumeGeometry(mft_seams::VolumeGeometry& /*out_geometry*/) {
  return false;
}

bool MftExtentProvider::GetMftExtents(std::vector<mft_seams::MftDiskExtent>& /*out_extents*/) {
  return false;
}

#endif  // _WIN32

}  // namespace mft
