#pragma once

#include <windows.h>

#include <rgf/hook/hook.hpp>

struct IDXGISwapChain;

namespace rgf::render::dxgi {

using PresentFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);

class PresentBinding {
 public:
  explicit PresentBinding(const hook::Hook& hook) noexcept;

  PresentBinding(const PresentBinding&) = delete;
  PresentBinding& operator=(const PresentBinding&) = delete;

  PresentBinding(PresentBinding&&) = delete;
  PresentBinding& operator=(PresentBinding&&) = delete;

  ~PresentBinding();
};

[[nodiscard]] PresentFn OriginalPresent() noexcept;

}  // namespace rgf::render::dxgi
