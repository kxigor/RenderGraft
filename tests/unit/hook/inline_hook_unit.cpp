#include <gtest/gtest.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <rgf/core/cast.hpp>
#include <rgf/hook/inline_hook.hpp>
#include <rgf/win/remote_memory.hpp>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>

namespace {

using rgf::hook::InlineHook;
using ProbeFn = int (*)();

constexpr SIZE_T kPageSize{4096};
constexpr std::size_t kCodeSize{32};
constexpr std::size_t kOtherOffset{64};
constexpr std::size_t kReturnOffset{21};
constexpr std::uint8_t kNop{0x90};
constexpr std::uint8_t kMoveEax{0xb8};
constexpr std::uint8_t kReturn{0xc3};
constexpr int kOriginalResult{17};
constexpr int kOtherResult{23};
constexpr int kDetourResult{42};
constexpr int kForwardAddend{100};

static_assert(std::is_nothrow_move_constructible_v<InlineHook>);
static_assert(!std::is_move_assignable_v<InlineHook>);
static_assert(!std::is_copy_constructible_v<InlineHook>);
static_assert(!std::is_copy_assignable_v<InlineHook>);

[[nodiscard]] ProbeFn& Trampoline() noexcept {
  static ProbeFn original{nullptr};
  return original;
}

int Detour() noexcept { return kDetourResult; }

int ForwardDetour() noexcept { return Trampoline()() + kForwardAddend; }

[[nodiscard]] std::array<std::uint8_t, kCodeSize> Code(int result) {
  // Both x86 and x64 return an int through EAX. The NOPs make the entry long
  // enough to exercise a real instruction trampoline in either architecture.
  std::array<std::uint8_t, kCodeSize> code{};
  code.fill(kNop);
  code[0] = kMoveEax;
  std::memcpy(code.data() + 1, &result, sizeof(result));
  code[kReturnOffset] = kReturn;
  return code;
}

class InlineHookUnit : public ::testing::Test {
 protected:
  void SetUp() override {
    void* page{VirtualAlloc(nullptr, kPageSize, MEM_COMMIT | MEM_RESERVE,
                            PAGE_READWRITE)};
    ASSERT_NE(page, nullptr);
    memory_.Reset(GetCurrentProcess(), page);
    const auto kCode{Code(kOriginalResult)};
    const auto kOther{Code(kOtherResult)};
    std::memcpy(page, kCode.data(), kCode.size());
    std::memcpy(static_cast<std::uint8_t*>(page) + kOtherOffset, kOther.data(),
                kOther.size());
    DWORD previous{};
    ASSERT_NE(VirtualProtect(page, kPageSize, PAGE_EXECUTE_READ, &previous),
              FALSE);
    ASSERT_NE(FlushInstructionCache(GetCurrentProcess(), page, kPageSize),
              FALSE);
  }

  void TearDown() override { Trampoline() = nullptr; }

  [[nodiscard]] ProbeFn Target() const noexcept {
    return rgf::core::PointerCast<ProbeFn>(memory_.Get());
  }

  [[nodiscard]] ProbeFn OtherTarget() const noexcept {
    return rgf::core::PointerCast<ProbeFn>(
        static_cast<std::uint8_t*>(memory_.Get()) + kOtherOffset);
  }

  [[nodiscard]] std::array<std::uint8_t, kCodeSize> EntryBytes() const {
    std::array<std::uint8_t, kCodeSize> bytes{};
    std::memcpy(bytes.data(), memory_.Get(), bytes.size());
    return bytes;
  }

 private:
  rgf::win::RemoteMemory memory_;
};

}  // namespace

TEST(InlineHook, EmptyOwnerIsSafeAndHasNoOriginal) {
  InlineHook hook;

  EXPECT_EQ(hook.Original<ProbeFn>(), nullptr);
  EXPECT_TRUE(hook.Disable().has_value());
  const auto kEnabled{hook.Enable()};
  ASSERT_FALSE(kEnabled.has_value());
  EXPECT_EQ(kEnabled.error(),
            std::make_error_code(std::errc::invalid_argument));
}

TEST(InlineHook, RejectsMissingOrIdenticalFunctions) {
  const ProbeFn kMissing{nullptr};

  EXPECT_FALSE(InlineHook::Create(kMissing, &Detour).has_value());
  EXPECT_FALSE(InlineHook::Create<ProbeFn>(&Detour, kMissing).has_value());
  EXPECT_FALSE(InlineHook::Create(&Detour, &Detour).has_value());
}

TEST(InlineHook, PreservesTheBackendErrorForNonExecutableMemory) {
  const ProbeFn kInvalid{std::bit_cast<ProbeFn>(std::uintptr_t{1})};

  const auto kHook{InlineHook::Create(kInvalid, &Detour)};

  ASSERT_FALSE(kHook.has_value());
  EXPECT_EQ(std::string_view{kHook.error().category().name()},
            "rgf::hook::minhook");
  EXPECT_EQ(kHook.error().message(), "MH_ERROR_NOT_EXECUTABLE");
}

TEST_F(InlineHookUnit, CreationBuildsTheTrampolineWithoutPatchingTheEntry) {
  const auto kBefore{EntryBytes()};

  const auto kHook{InlineHook::Create(Target(), &Detour)};

  ASSERT_TRUE(kHook.has_value()) << kHook.error().message();
  EXPECT_EQ(Target()(), kOriginalResult);
  EXPECT_EQ(kHook->Original<ProbeFn>()(), kOriginalResult);
  EXPECT_EQ(EntryBytes(), kBefore);
}

TEST_F(InlineHookUnit, EnabledDetourCanCallTheOriginalTrampoline) {
  auto hook{InlineHook::Create(Target(), &ForwardDetour)};
  ASSERT_TRUE(hook.has_value()) << hook.error().message();
  Trampoline() = hook->Original<ProbeFn>();

  ASSERT_TRUE(hook->Enable().has_value());

  EXPECT_EQ(Target()(), kOriginalResult + kForwardAddend);
  EXPECT_EQ(Trampoline()(), kOriginalResult);
  ASSERT_TRUE(hook->Disable().has_value());
  EXPECT_EQ(Target()(), kOriginalResult);
}

TEST_F(InlineHookUnit, DestructionRestoresEveryPatchedByte) {
  const auto kBefore{EntryBytes()};
  {
    auto hook{InlineHook::Create(Target(), &Detour)};
    ASSERT_TRUE(hook.has_value()) << hook.error().message();
    ASSERT_TRUE(hook->Enable().has_value());
    EXPECT_EQ(Target()(), kDetourResult);
    EXPECT_NE(EntryBytes(), kBefore);
  }

  EXPECT_EQ(EntryBytes(), kBefore);
  EXPECT_EQ(Target()(), kOriginalResult);
}

TEST_F(InlineHookUnit, MovingAnEnabledOwnerTransfersItsBinding) {
  auto created{InlineHook::Create(Target(), &Detour)};
  ASSERT_TRUE(created.has_value()) << created.error().message();
  ASSERT_TRUE(created->Enable().has_value());
  const auto kOriginal{created->Original<ProbeFn>()};

  InlineHook moved{std::move(*created)};

  EXPECT_EQ(created->Original<ProbeFn>(), nullptr);
  EXPECT_TRUE(created->Disable().has_value());
  EXPECT_EQ(moved.Original<ProbeFn>(), kOriginal);
  EXPECT_EQ(Target()(), kDetourResult);
  ASSERT_TRUE(moved.Disable().has_value());
  EXPECT_EQ(Target()(), kOriginalResult);
}

TEST_F(InlineHookUnit, RemovingOneOwnerPreservesTheOtherHookAndRuntime) {
  auto created{InlineHook::Create(Target(), &Detour)};
  ASSERT_TRUE(created.has_value()) << created.error().message();
  std::optional<InlineHook> first{std::move(*created)};
  auto second{InlineHook::Create(OtherTarget(), &Detour)};
  ASSERT_TRUE(second.has_value()) << second.error().message();
  ASSERT_TRUE(first->Enable().has_value());
  ASSERT_TRUE(second->Enable().has_value());

  first.reset();

  EXPECT_EQ(Target()(), kOriginalResult);
  EXPECT_EQ(OtherTarget()(), kDetourResult);
  EXPECT_EQ(second->Original<ProbeFn>()(), kOtherResult);
  ASSERT_TRUE(second->Disable().has_value());
  EXPECT_EQ(OtherTarget()(), kOtherResult);
}

TEST_F(InlineHookUnit, FailedDuplicateCreationLeavesTheExistingOwnerUsable) {
  auto first{InlineHook::Create(Target(), &Detour)};
  ASSERT_TRUE(first.has_value()) << first.error().message();

  const auto kDuplicate{InlineHook::Create(Target(), &ForwardDetour)};

  ASSERT_FALSE(kDuplicate.has_value());
  EXPECT_EQ(kDuplicate.error().message(), "MH_ERROR_ALREADY_CREATED");
  ASSERT_TRUE(first->Enable().has_value());
  EXPECT_EQ(Target()(), kDetourResult);
  EXPECT_EQ(first->Original<ProbeFn>()(), kOriginalResult);
}

TEST_F(InlineHookUnit, RepeatedEnablePreservesTheEnabledStateAndBackendError) {
  auto hook{InlineHook::Create(Target(), &Detour)};
  ASSERT_TRUE(hook.has_value()) << hook.error().message();
  ASSERT_TRUE(hook->Enable().has_value());

  const auto kEnabledAgain{hook->Enable()};

  ASSERT_FALSE(kEnabledAgain.has_value());
  EXPECT_EQ(kEnabledAgain.error().message(), "MH_ERROR_ENABLED");
  EXPECT_EQ(Target()(), kDetourResult);
  ASSERT_TRUE(hook->Disable().has_value());
  EXPECT_TRUE(hook->Disable().has_value());
  EXPECT_EQ(Target()(), kOriginalResult);
}
