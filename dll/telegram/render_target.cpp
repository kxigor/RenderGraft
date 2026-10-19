#include "render_target.hpp"

#include <windows.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstring>
#include <rgf/win/error.hpp>
#include <rgf/win/module_view.hpp>
#include <span>

#include "internal/widget.hpp"

namespace rgf::telegram {

namespace {

constexpr std::uintptr_t kMessageTableRva{0x8579200};
constexpr std::uintptr_t kMessageDrawRva{0x1d31980};
constexpr std::size_t kMessageDrawSlot{10};

[[nodiscard]] std::error_code UnsupportedBuild() noexcept {
  return std::make_error_code(std::errc::not_supported);
}

[[nodiscard]] std::expected<void, std::error_code> ReadBytes(
    std::uintptr_t address, std::span<std::byte> destination) noexcept {
  SIZE_T read{};
  if (ReadProcessMemory(
          /*hProcess            = */ GetCurrentProcess(),
          /*lpBaseAddress       = */ std::bit_cast<LPCVOID>(address),
          /*lpBuffer            = */ destination.data(),
          /*nSize               = */ destination.size(),
          /*lpNumberOfBytesRead = */ &read) == FALSE) {
    return std::unexpected{win::LastError()};
  }
  if (read != destination.size()) {
    return std::unexpected{
        std::error_code{ERROR_PARTIAL_COPY, std::system_category()}};
  }
  return {};
}

[[nodiscard]] std::expected<void, std::error_code> CheckHeaders(
    std::uintptr_t base) noexcept {
  IMAGE_DOS_HEADER dos{};
  auto read{ReadBytes(base, std::as_writable_bytes(std::span{&dos, 1}))};
  if (!read) {
    return read;
  }
  constexpr LONG kMaxHeaderOffset{4096};
  if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0 ||
      dos.e_lfanew > kMaxHeaderOffset) {
    return std::unexpected{UnsupportedBuild()};
  }
  IMAGE_NT_HEADERS64 headers{};
  read = ReadBytes(base + static_cast<std::uintptr_t>(dos.e_lfanew),
                   std::as_writable_bytes(std::span{&headers, 1}));
  if (!read) {
    return read;
  }
  constexpr DWORD kTimestamp{0x6aca0c36};
  constexpr DWORD kImageSize{0xe673000};
  constexpr DWORD kEntryRva{0x63d29cc};
  constexpr DWORD kChecksum{0xe13ebf8};
  if (headers.Signature != IMAGE_NT_SIGNATURE ||
      headers.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
      headers.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
      headers.FileHeader.TimeDateStamp != kTimestamp ||
      headers.OptionalHeader.SizeOfImage != kImageSize ||
      headers.OptionalHeader.AddressOfEntryPoint != kEntryRva ||
      headers.OptionalHeader.CheckSum != kChecksum) {
    return std::unexpected{UnsupportedBuild()};
  }
  return {};
}

[[nodiscard]] std::expected<void, std::error_code> CheckSlot(
    std::uintptr_t base, std::uintptr_t tableRva, std::size_t slot,
    std::uintptr_t functionRva) noexcept {
  std::uintptr_t function{};
  const auto kRead{ReadBytes(base + tableRva + (slot * sizeof(void*)),
                             std::as_writable_bytes(std::span{&function, 1}))};
  if (!kRead) {
    return kRead;
  }
  if (function != base + functionRva) {
    return std::unexpected{UnsupportedBuild()};
  }
  return {};
}

[[nodiscard]] std::expected<void, std::error_code> CheckFunction(
    std::uintptr_t address, std::span<const std::uint8_t> expected) noexcept {
  constexpr std::size_t kMaxCodeBytes{64};
  std::array<std::byte, kMaxCodeBytes> code{};
  const auto kRead{ReadBytes(address, std::span{code}.first(expected.size()))};
  if (!kRead) {
    return kRead;
  }
  if (std::memcmp(code.data(), expected.data(), expected.size()) != 0) {
    return std::unexpected{UnsupportedBuild()};
  }
  return {};
}

[[nodiscard]] std::expected<void, std::error_code> CheckCode(
    std::uintptr_t base) noexcept {
  const auto kGetter{CheckFunction(base + internal::kGetPaintEngineRva,
                                   internal::kGetPaintEngineCode)};
  if (!kGetter) {
    return kGetter;
  }
  const auto kFind{CheckFunction(base + internal::kFindWidgetNodeRva,
                                 internal::kFindWidgetNodeCode)};
  if (!kFind) {
    return kFind;
  }
  return CheckFunction(base + internal::kUpdateWidgetRva,
                       internal::kUpdateWidgetCode);
}

[[nodiscard]] std::expected<void, std::error_code> CheckTables(
    std::uintptr_t base) noexcept {
  const auto kMessage{
      CheckSlot(base, kMessageTableRva, kMessageDrawSlot, kMessageDrawRva)};
  if (!kMessage) {
    return kMessage;
  }
  const auto kText{CheckSlot(base, internal::kRasterVtableRva,
                             internal::kDrawTextItemSlot,
                             internal::kDrawTextItemRva)};
  if (!kText) {
    return kText;
  }
  return CheckSlot(base, internal::kRasterVtableRva, internal::kFillColorSlot,
                   internal::kFillColorRva);
}

}  // namespace

std::expected<RenderTarget, std::error_code> RenderTarget::Locate() {
  if constexpr (sizeof(void*) != sizeof(std::uint64_t)) {
    return std::unexpected{UnsupportedBuild()};
  }
  const auto kModule{win::ModuleView::Executable()};
  if (!kModule) {
    return std::unexpected{kModule.error()};
  }
  const auto kBase{std::bit_cast<std::uintptr_t>(kModule->Get())};
  const auto kHeaders{CheckHeaders(kBase)};
  if (!kHeaders) {
    return std::unexpected{kHeaders.error()};
  }
  const auto kTables{CheckTables(kBase)};
  if (!kTables) {
    return std::unexpected{kTables.error()};
  }
  const auto kCode{CheckCode(kBase)};
  if (!kCode) {
    return std::unexpected{kCode.error()};
  }
  return RenderTarget{kBase};
}

RenderTarget::RenderTarget(std::uintptr_t base) noexcept : kBase_{base} {}

hook::Vtable RenderTarget::MessageTable() const noexcept {
  return hook::Vtable{std::bit_cast<void**>(kBase_ + kMessageTableRva)};
}

void** RenderTarget::RasterTable() const noexcept {
  return std::bit_cast<void**>(kBase_ + internal::kRasterVtableRva);
}

internal::GetPaintEngine RenderTarget::PaintEngine() const noexcept {
  return std::bit_cast<internal::GetPaintEngine>(kBase_ +
                                                 internal::kGetPaintEngineRva);
}

}  // namespace rgf::telegram
