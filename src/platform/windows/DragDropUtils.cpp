#include "platform/windows/DragDropUtils.h"

#include <cstddef>
#include <memory>
#include <objidl.h>
#include <shellapi.h>
#include <windows.h>  // NOSONAR(cpp:S3806) - Windows-only include; path case matches filesystem

#include "platform/FileOperations.h"
#include "utils/Logger.h"
#include "utils/StringUtils.h"

namespace drag_drop {

// Helper to duplicate an HGLOBAL so IDataObject can hand ownership to callers.
HGLOBAL DuplicateGlobal(HGLOBAL h_global) {
  if (!h_global) {
    return nullptr;
  }

  SIZE_T size = GlobalSize(h_global);
  if (size == 0) {
    return nullptr;
  }

  HGLOBAL h_copy = GlobalAlloc(GHND, size);
  if (!h_copy) {
    return nullptr;
  }

  const void* src = GlobalLock(h_global);
  void* dst = GlobalLock(h_copy);
  if (src == nullptr || dst == nullptr) {
    if (src != nullptr) {
      GlobalUnlock(h_global);
    }
    if (dst != nullptr) {
      GlobalUnlock(h_copy);
    }
    GlobalFree(h_copy);
    return nullptr;
  }

  memcpy(dst, src, size);

  GlobalUnlock(h_global);
  GlobalUnlock(h_copy);

  return h_copy;
}

// Base class for COM objects implementing IUnknown reference counting
class ComBase : public IUnknown {
 public:
  virtual ~ComBase() = default;

  IFACEMETHODIMP_(ULONG) AddRef() override {  // NOLINT(readability-identifier-naming) - COM macro expands to return type
    return static_cast<ULONG>(InterlockedIncrement(&ref_count_));
  }

  IFACEMETHODIMP_(ULONG) Release() override {  // NOLINT(readability-identifier-naming) - COM macro expands to return type
    LONG count = InterlockedDecrement(&ref_count_);
    if (count == 0) {
      // Use RAII to self-destruct when the reference count reaches zero.
      // This avoids an explicit delete while preserving the COM lifetime pattern.
      std::unique_ptr<ComBase> self(this);
      return 0;
    }
    return static_cast<ULONG>(count);
  }

 private:
  LONG ref_count_{1};
};

// Simple enumerator for a single FORMATETC (used by IDataObject::EnumFormatEtc).
class SingleFormatEtcEnum final : public IEnumFORMATETC, public ComBase {
 public:
  explicit SingleFormatEtcEnum(const FORMATETC& fmt) : format_(fmt) {}  // NOLINT(cppcoreguidelines-pro-type-member-init,hicpp-member-init) - member initialized from ctor

  // IUnknown
  IFACEMETHODIMP QueryInterface(REFIID riid,
                                void** ppv) override {  // NOSONAR(cpp:S5008) - COM API requires void**
    if (ppv == nullptr) {
      return E_POINTER;
    }
    if (riid == IID_IUnknown || riid == IID_IEnumFORMATETC) {
      *ppv = static_cast<IEnumFORMATETC*>(this);
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }

  IFACEMETHODIMP_(ULONG) AddRef() override {  // NOLINT(readability-identifier-naming) - COM macro
    return ComBase::AddRef();
  }
  IFACEMETHODIMP_(ULONG) Release() override {  // NOLINT(readability-identifier-naming) - COM macro
    return ComBase::Release();
  }

  // IEnumFORMATETC
  // NOLINTNEXTLINE(readability-identifier-naming) - COM signature with ULONG params
  IFACEMETHODIMP Next(ULONG celt, FORMATETC* rgelt, ULONG* pceltFetched) override {
    if (rgelt == nullptr) {
      return E_INVALIDARG;
    }
    if (celt == 0) {
      return S_OK;
    }
    if (current_index_ == 0) {
      rgelt[0] = format_;
      current_index_ = 1;
      if (pceltFetched) {
        *pceltFetched = 1;
      }
      return S_OK;
    }
    if (pceltFetched) {
      *pceltFetched = 0;
    }
    return S_FALSE;
  }

  IFACEMETHODIMP Skip(ULONG celt) override {
    if (celt == 0) {
      return S_OK;
    }
    if (current_index_ == 0 && celt > 0) {
      current_index_ = 1;
      return S_OK;
    }
    return S_FALSE;
  }

  IFACEMETHODIMP Reset() override {
    current_index_ = 0;
    return S_OK;
  }

  IFACEMETHODIMP Clone(IEnumFORMATETC** ppenum) override {
    if (ppenum == nullptr) {
      return E_POINTER;
    }
    auto clone = std::make_unique<SingleFormatEtcEnum>(format_);
    clone->current_index_ = current_index_;
    *ppenum = clone.release();
    return S_OK;
  }

 private:
  ULONG current_index_{0};   // NOLINT(readability-identifier-naming) - COM impl; ULONG is Windows type
  FORMATETC format_;         // NOLINT(readability-identifier-naming) - COM impl; init from ctor

  friend class ComBase;
};

// Minimal IDataObject implementation exposing a single CF_HDROP.
class FileDropDataObject final : public IDataObject, public ComBase {
 public:
  explicit FileDropDataObject(HGLOBAL h_drop) : h_drop_(h_drop) {}  // NOLINT(cppcoreguidelines-pro-type-member-init,hicpp-member-init) - h_drop_ initialized

  FileDropDataObject(const FileDropDataObject&) = delete;
  FileDropDataObject& operator=(const FileDropDataObject&) = delete;
  FileDropDataObject(FileDropDataObject&&) = delete;
  FileDropDataObject& operator=(FileDropDataObject&&) = delete;

  ~FileDropDataObject() override {
    if (h_drop_ != nullptr) {
      GlobalFree(h_drop_);
      h_drop_ = nullptr;
    }
  }

  // IUnknown
  IFACEMETHODIMP QueryInterface(REFIID riid,
                                void** ppv) override {  // NOSONAR(cpp:S5008) - COM API requires void**
    if (ppv == nullptr) {
      return E_POINTER;
    }
    if (riid == IID_IUnknown || riid == IID_IDataObject) {
      *ppv = static_cast<IDataObject*>(this);
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }

  IFACEMETHODIMP_(ULONG) AddRef() override {  // NOLINT(readability-identifier-naming) - COM macro
    return ComBase::AddRef();
  }
  IFACEMETHODIMP_(ULONG) Release() override {  // NOLINT(readability-identifier-naming) - COM macro
    return ComBase::Release();
  }

  // IDataObject
  IFACEMETHODIMP GetData(FORMATETC* format_etc, STGMEDIUM* medium) override {
    if (!format_etc || !medium) {
      return E_INVALIDARG;
    }

    if (format_etc->cfFormat != CF_HDROP || !(format_etc->tymed & TYMED_HGLOBAL) ||
        format_etc->dwAspect != DVASPECT_CONTENT) {
      return DV_E_FORMATETC;
    }

    HGLOBAL copy = DuplicateGlobal(h_drop_);
    if (!copy) {
      return E_OUTOFMEMORY;
    }

    medium->tymed = TYMED_HGLOBAL;
    medium->hGlobal = copy;
    medium->pUnkForRelease = nullptr;
    return S_OK;
  }

  IFACEMETHODIMP GetDataHere(FORMATETC*, STGMEDIUM*) override {
    return E_NOTIMPL;
  }

  IFACEMETHODIMP QueryGetData(FORMATETC* format_etc) override {
    if (!format_etc) {
      return E_INVALIDARG;
    }
    if (format_etc->cfFormat == CF_HDROP && (format_etc->tymed & TYMED_HGLOBAL) &&
        format_etc->dwAspect == DVASPECT_CONTENT) {
      return S_OK;
    }
    return DV_E_FORMATETC;
  }

  IFACEMETHODIMP GetCanonicalFormatEtc(FORMATETC*, FORMATETC* format_etc_out) override {
    if (!format_etc_out) {
      return E_INVALIDARG;
    }
    format_etc_out->ptd = nullptr;
    return E_NOTIMPL;
  }

  IFACEMETHODIMP SetData(FORMATETC*, STGMEDIUM*, BOOL) override {
    return E_NOTIMPL;
  }

  IFACEMETHODIMP EnumFormatEtc(DWORD dw_direction, IEnumFORMATETC** ppenum) override {
    if (ppenum == nullptr) {
      return E_POINTER;
    }
    if (dw_direction != DATADIR_GET) {
      return E_NOTIMPL;
    }

    FORMATETC fmt = {};
    fmt.cfFormat = CF_HDROP;
    fmt.dwAspect = DVASPECT_CONTENT;
    fmt.lindex = -1;
    fmt.tymed = TYMED_HGLOBAL;

    auto enumerator = std::make_unique<SingleFormatEtcEnum>(fmt);
    *ppenum = enumerator.release();
    return S_OK;
  }

  IFACEMETHODIMP DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override {
    return OLE_E_ADVISENOTSUPPORTED;
  }

  IFACEMETHODIMP DUnadvise(DWORD) override {
    return OLE_E_ADVISENOTSUPPORTED;
  }

  IFACEMETHODIMP EnumDAdvise(IEnumSTATDATA**) override {
    return OLE_E_ADVISENOTSUPPORTED;
  }

 private:
  HGLOBAL h_drop_;
};

// Minimal IDropSource implementation for basic drag behavior.
class BasicDropSource final : public IDropSource, public ComBase {  // NOLINT(cppcoreguidelines-pro-type-member-init,hicpp-member-init) - COM macro reports false ULONG member
 public:
  BasicDropSource() = default;

  // IUnknown
  IFACEMETHODIMP QueryInterface(REFIID riid,
                                void** ppv) override {  // NOSONAR(cpp:S5008) - COM API requires void**
    if (ppv == nullptr) {
      return E_POINTER;
    }
    if (riid == IID_IUnknown || riid == IID_IDropSource) {
      *ppv = static_cast<IDropSource*>(this);
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }

  IFACEMETHODIMP_(ULONG) AddRef() override {  // NOLINT(readability-identifier-naming) - COM macro
    return ComBase::AddRef();
  }
  IFACEMETHODIMP_(ULONG) Release() override {  // NOLINT(readability-identifier-naming) - COM macro
    return ComBase::Release();
  }

  // IDropSource
  IFACEMETHODIMP QueryContinueDrag(BOOL escape_pressed, DWORD key_state) override {
    // Cancel if escape is pressed.
    if (escape_pressed) {
      return DRAGDROP_S_CANCEL;
    }
    // Cancel if left mouse button released.
    if ((key_state & MK_LBUTTON) == 0) {
      return DRAGDROP_S_DROP;
    }
    return S_OK;
  }

  IFACEMETHODIMP GiveFeedback(DWORD) override {
    // Use default cursor feedback.
    return DRAGDROP_S_USEDEFAULTCURSORS;
  }
};

HGLOBAL CreateHDropForPath(const std::wstring& path) {
  const size_t char_count = path.length() + 1;  // include terminating null
  const SIZE_T bytes_for_path = char_count * sizeof(wchar_t);

  const SIZE_T total_size = sizeof(DROPFILES) + bytes_for_path + sizeof(wchar_t);

  HGLOBAL h_global = GlobalAlloc(GHND, total_size);
  if (!h_global) {
    return nullptr;
  }

  auto* drop_files = static_cast<DROPFILES*>(GlobalLock(h_global));
  if (!drop_files) {
    GlobalFree(h_global);
    return nullptr;
  }

  drop_files->pFiles = sizeof(DROPFILES);
  drop_files->pt.x = 0;
  drop_files->pt.y = 0;
  drop_files->fNC = FALSE;
  drop_files->fWide = TRUE;

  auto* buffer = reinterpret_cast<wchar_t*>(drop_files + 1);  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast) NOSONAR(cpp:S3630) - DROPFILES layout defined by Win32; reinterpret_cast from struct tail to wchar_t buffer follows documented pattern
  memcpy(buffer, path.c_str(), bytes_for_path);

  // Double-null terminate the list.
  buffer[char_count] = L'\0';

  GlobalUnlock(h_global);
  return h_global;
}

bool StartFileDragDrop(std::string_view full_path_utf8) {
  const std::string path(full_path_utf8);
  if (!file_operations::internal::ValidatePath(path, "StartFileDragDrop")) {
    return false;
  }
  const std::wstring wide_path = Utf8ToWide(path);
  if (wide_path.empty() && !path.empty()) {
    LOG_ERROR("StartFileDragDrop: Failed to convert path to wide string: " + path);
    return false;
  }
  DWORD attributes = GetFileAttributesW(wide_path.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES) {
    LOG_WARNING("StartFileDragDrop: Path does not exist: " + path);
    return false;
  }
  if (attributes & FILE_ATTRIBUTE_DIRECTORY) {
    LOG_WARNING("StartFileDragDrop: Directories are not supported for drag-and-drop: " + path);
    return false;
  }
  HGLOBAL h_drop = CreateHDropForPath(wide_path);
  if (!h_drop) {
    LOG_ERROR("StartFileDragDrop: Failed to create HDROP for path: " + path);
    return false;
  }

  auto* data_object = new FileDropDataObject(h_drop);  // NOSONAR(cpp:S5025) NOLINT(cppcoreguidelines-owning-memory) - COM DoDragDrop takes raw pointers, Release() transfers
  auto* const drop_source = new BasicDropSource();  // NOSONAR(cpp:S5025) NOLINT(cppcoreguidelines-owning-memory) - COM; pointee modified by DoDragDrop

  DWORD effect = DROPEFFECT_NONE;
  // Allow the drop target to choose among common effects; most will pick COPY.
  HRESULT hr = DoDragDrop(data_object, drop_source,
                          DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK, &effect);

  data_object->Release();
  drop_source->Release();

  if (FAILED(hr)) {
    LOG_ERROR_BUILD("StartFileDragDrop: DoDragDrop failed with HRESULT="
                    << static_cast<long>(hr) << " for path: " << path);
    return false;
  }

  // Treat both successful drop and user cancel as non-fatal.
  const std::string
    msg =  // NOLINT(bugprone-unused-local-non-trivial-variable) - passed to LOG_INFO macro
    (effect == DROPEFFECT_NONE)
      ? (std::string(
           "StartFileDragDrop: Drag cancelled or no drop target accepted for path: ") +
         path)
      : (std::string("StartFileDragDrop: Drag completed with effect=") +
         std::to_string(static_cast<long>(effect)) + " for path: " + path);
  LOG_INFO(msg);

  return true;
}

}  // namespace drag_drop
