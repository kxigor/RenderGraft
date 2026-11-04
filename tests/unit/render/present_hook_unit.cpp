#include <gtest/gtest.h>
#include <windows.h>

#include <array>
#include <bit>
#include <expected>
#include <rgf/hook/vmt_hook.hpp>
#include <rgf/hook/vtable.hpp>
#include <rgf/render/dxgi/present_hook.hpp>
#include <rgf/render/dxgi/probe.hpp>
#include <system_error>

namespace {

constexpr std::size_t kEntries{16};

int detourCalls{0};

HRESULT __stdcall FakeOriginal(IDXGISwapChain* /*swapChain*/,
                               UINT /*syncInterval*/, UINT /*flags*/) {
  return S_OK;
}

HRESULT __stdcall FakeDetour(IDXGISwapChain* swapChain, UINT syncInterval,
                             UINT flags) {
  ++detourCalls;

  rgf::render::dxgi::PresentFn original{rgf::render::dxgi::OriginalPresent()};
  if (original == nullptr) {
    return E_FAIL;
  }

  return original(swapChain, syncInterval, flags);
}

class PresentHookUnit : public ::testing::Test {
 protected:
  void SetUp() override {
    detourCalls = 0;
    entries_.fill(nullptr);
    entries_[rgf::render::dxgi::kPresentSlot] =
        std::bit_cast<void*>(&FakeOriginal);
  }

  [[nodiscard]] rgf::hook::Vtable Table() {
    return rgf::hook::Vtable{entries_.data()};
  }

  [[nodiscard]] HRESULT CallPresentSlot() {
    return std::bit_cast<rgf::render::dxgi::PresentFn>(
        entries_[rgf::render::dxgi::kPresentSlot])(nullptr, 0, 0);
  }

  std::array<void*, kEntries> entries_{};
};

}  // namespace

TEST_F(PresentHookUnit, OriginalPresentIsNullWithoutABinding) {
  EXPECT_EQ(rgf::render::dxgi::OriginalPresent(), nullptr);
}

TEST_F(PresentHookUnit, BindingPublishesTheOriginal) {
  std::expected<rgf::hook::VmtHook, std::error_code> hook{
      rgf::hook::VmtHook::Create(Table(), rgf::render::dxgi::kPresentSlot)};
  ASSERT_TRUE(hook.has_value()) << hook.error().message();

  const rgf::render::dxgi::PresentBinding kBound{*hook};

  EXPECT_EQ(rgf::render::dxgi::OriginalPresent(), &FakeOriginal);
}

TEST_F(PresentHookUnit, DestroyingTheBindingClearsIt) {
  std::expected<rgf::hook::VmtHook, std::error_code> hook{
      rgf::hook::VmtHook::Create(Table(), rgf::render::dxgi::kPresentSlot)};
  ASSERT_TRUE(hook.has_value()) << hook.error().message();

  {
    const rgf::render::dxgi::PresentBinding kBound{*hook};
    ASSERT_EQ(rgf::render::dxgi::OriginalPresent(), &FakeOriginal);
  }

  EXPECT_EQ(rgf::render::dxgi::OriginalPresent(), nullptr);
}

TEST_F(PresentHookUnit, TheDetourReachesTheOriginalThroughTheBinding) {
  std::expected<rgf::hook::VmtHook, std::error_code> hook{
      rgf::hook::VmtHook::Create(Table(), rgf::render::dxgi::kPresentSlot)};
  ASSERT_TRUE(hook.has_value()) << hook.error().message();
  ASSERT_TRUE(hook->SetDetour(&FakeDetour).has_value());

  const rgf::render::dxgi::PresentBinding kBound{*hook};
  ASSERT_TRUE(hook->Enable().has_value());

  EXPECT_EQ(CallPresentSlot(), S_OK);
  EXPECT_EQ(detourCalls, 1);
}

TEST_F(PresentHookUnit, ASecondBindingWorksAfterTheFirstIsGone) {
  for (int round{0}; round < 2; ++round) {
    std::expected<rgf::hook::VmtHook, std::error_code> hook{
        rgf::hook::VmtHook::Create(Table(), rgf::render::dxgi::kPresentSlot)};
    ASSERT_TRUE(hook.has_value()) << hook.error().message();
    ASSERT_TRUE(hook->SetDetour(&FakeDetour).has_value());

    const rgf::render::dxgi::PresentBinding kBound{*hook};
    ASSERT_TRUE(hook->Enable().has_value());

    EXPECT_EQ(CallPresentSlot(), S_OK) << "round " << round;
  }

  EXPECT_EQ(detourCalls, 2);
  EXPECT_EQ(rgf::render::dxgi::OriginalPresent(), nullptr);
  EXPECT_EQ(entries_[rgf::render::dxgi::kPresentSlot],
            std::bit_cast<void*>(&FakeOriginal));
}
