#include "original_resources.hpp"

#include "original_resources.generated.hpp"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>

namespace simtower {
OriginalResources OriginalResources::from_current_module() {
  // The self-contained pack replaces the per-resource GlobalAlloc/Lock and
  // unlock/free lifetimes at 1208:0cb5/0d75. LockResource on a PE resource
  // yields immutable module-owned storage whose lifetime is the process, so
  // find() can return stable spans without emulating Win16 lock counts.
  const HMODULE module = GetModuleHandleW(nullptr);
  const HRSRC resource = FindResourceW(
      module,
      MAKEINTRESOURCEW(generated::kResourcePackRCDATAId),
      RT_RCDATA);
  if (!resource) {
    throw std::runtime_error("Embedded original resource pack is missing");
  }
  const HGLOBAL loaded = LoadResource(module, resource);
  const auto* data = static_cast<const std::byte*>(LockResource(loaded));
  const DWORD size = SizeofResource(module, resource);
  if (!data || size == 0) {
    throw std::runtime_error("Embedded original resource pack could not be locked");
  }
  return OriginalResources({data, static_cast<std::size_t>(size)});
}

}  // namespace simtower
