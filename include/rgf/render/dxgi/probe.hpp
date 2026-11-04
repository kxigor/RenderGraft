#pragma once

#include <cstddef>
#include <expected>
#include <rgf/hook/vtable.hpp>
#include <system_error>

namespace rgf::render::dxgi {

constexpr std::size_t kPresentSlot{8};

[[nodiscard]] std::expected<hook::Vtable, std::error_code>
ProbeSwapChainVtable() noexcept;

}  // namespace rgf::render::dxgi
