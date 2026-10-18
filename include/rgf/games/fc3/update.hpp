#pragma once

#include <rgf/abi/abi.hpp>
#include <rgf/hook/hook.hpp>

namespace rgf::games::fc3 {

using RunAllGroupFn = void(RGF_ENGINE_CALL*)(void* scheduler);

[[nodiscard]] void** RunAllGroupSlot() noexcept;

class UpdateBinding {
 public:
  explicit UpdateBinding(const hook::Hook& hook) noexcept;

  UpdateBinding(const UpdateBinding&) = delete;
  UpdateBinding& operator=(const UpdateBinding&) = delete;

  UpdateBinding(UpdateBinding&&) = delete;
  UpdateBinding& operator=(UpdateBinding&&) = delete;

  ~UpdateBinding();
};

[[nodiscard]] RunAllGroupFn OriginalRunAllGroup() noexcept;

}  // namespace rgf::games::fc3
