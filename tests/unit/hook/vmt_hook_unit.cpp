#include <gtest/gtest.h>
#include <windows.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <rgf/core/cast.hpp>
#include <rgf/hook/error.hpp>
#include <rgf/hook/vmt_hook.hpp>
#include <rgf/hook/vtable.hpp>
#include <support/win_probe.hpp>
#include <system_error>
#include <type_traits>
#include <utility>

namespace {

constexpr SIZE_T kTableSize{4096};
constexpr std::size_t kSlot{0};
constexpr int kOtherDetourResult{3};

using ProbeFn = int (*)();

int OriginalFn() { return 1; }
int DetourFn() { return 2; }
int OtherDetourFn() { return kOtherDetourResult; }

using VirtualProtectExFn = decltype(&VirtualProtectEx);

static_assert(std::is_nothrow_move_constructible_v<rgf::hook::VmtHook>);
static_assert(!std::is_move_assignable_v<rgf::hook::VmtHook>);
static_assert(!std::is_copy_constructible_v<rgf::hook::VmtHook>);
static_assert(!std::is_copy_assignable_v<rgf::hook::VmtHook>);

struct ProtectionFailure {
  VirtualProtectExFn original{nullptr};
  LPVOID address{nullptr};
  DWORD protection{};
};

[[nodiscard]] ProtectionFailure& Failure() {
  static ProtectionFailure failure{};
  return failure;
}

BOOL WINAPI FailNextProtection(HANDLE process, LPVOID address, SIZE_T size,
                               DWORD protection, PDWORD previous) {
  auto& failure{Failure()};
  if (address == failure.address && protection == failure.protection) {
    failure.protection = 0;
    SetLastError(ERROR_ACCESS_DENIED);
    return FALSE;
  }
  return failure.original(process, address, size, protection, previous);
}

[[nodiscard]] void** FindVirtualProtectExImport() {
  auto* image{rgf::core::PointerCast<BYTE*>(
      GetModuleHandleW(/*lpModuleName = */ nullptr))};
  if (image == nullptr) {
    return nullptr;
  }
  const auto* kDos{rgf::core::PointerCast<const IMAGE_DOS_HEADER*>(image)};
  const auto* kNt{
      rgf::core::PointerCast<const IMAGE_NT_HEADERS*>(image + kDos->e_lfanew)};
  const auto& kDirectory{
      kNt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT]};
  const auto* descriptor{rgf::core::PointerCast<const IMAGE_IMPORT_DESCRIPTOR*>(
      image + kDirectory.VirtualAddress)};
  for (; descriptor->Name != 0; ++descriptor) {
    auto* thunk{rgf::core::PointerCast<std::uintptr_t*>(
        image + descriptor->FirstThunk)};
    for (; *thunk != 0; ++thunk) {
      if (*thunk == std::bit_cast<std::uintptr_t>(&VirtualProtectEx)) {
        return rgf::core::PointerCast<void**>(thunk);
      }
    }
  }
  return nullptr;
}

class VmtHookUnit : public ::testing::Test {
 protected:
  void SetUp() override {
    page = VirtualAlloc(
        /*lpAddress        = */ nullptr,
        /*dwSize           = */ kTableSize,
        /*flAllocationType = */ MEM_COMMIT | MEM_RESERVE,
        /*flProtect        = */ PAGE_READWRITE);
    ASSERT_NE(page, nullptr);

    table = static_cast<void**>(page);
    table[kSlot] = rgf::core::PointerCast<void*>(&OriginalFn);

    DWORD previous{};
    ASSERT_NE(VirtualProtect(
                  /*lpAddress      = */ page,
                  /*dwSize         = */ kTableSize,
                  /*flNewProtect   = */ PAGE_READONLY,
                  /*lpflOldProtect = */ &previous),
              FALSE);
  }

  void TearDown() override {
    if (page != nullptr) {
      EXPECT_NE(VirtualFree(
                    /*lpAddress  = */ page,
                    /*dwSize     = */ 0,
                    /*dwFreeType = */ MEM_RELEASE),
                FALSE);
    }
  }

  [[nodiscard]] int CallSlot() const {
    return rgf::core::PointerCast<ProbeFn>(table[kSlot])();
  }

 public:
  LPVOID page{nullptr};
  void** table{nullptr};
};

class VmtHookProtectionFailureUnit : public VmtHookUnit {
 protected:
  void SetUp() override {
    VmtHookUnit::SetUp();
    if (HasFatalFailure()) {
      return;
    }
    void** const kImport{FindVirtualProtectExImport()};
    ASSERT_NE(kImport, nullptr);

    auto hook{rgf::hook::VmtHook::Create(rgf::hook::Vtable{kImport}, kSlot)};
    ASSERT_TRUE(hook.has_value()) << hook.error().message();
    protectionHook_.emplace(std::move(*hook));
    Failure().original = protectionHook_->Original<VirtualProtectExFn>();
    ASSERT_TRUE(protectionHook_->SetDetour(&FailNextProtection).has_value());
    ASSERT_TRUE(protectionHook_->Enable().has_value());
    Failure().address = page;
  }

  void TearDown() override {
    Failure().address = nullptr;
    Failure().protection = 0;
    protectionHook_.reset();
    Failure().original = nullptr;
    VmtHookUnit::TearDown();
  }

  static void FailNextChange(DWORD protection) {
    Failure().protection = protection;
  }

 private:
  std::optional<rgf::hook::VmtHook> protectionHook_;
};

}  // namespace

TEST_F(VmtHookUnit, CreatePublishesTheOriginalWithoutTouchingTheSlot) {
  const std::expected<rgf::hook::VmtHook, std::error_code> kHook{
      rgf::hook::VmtHook::Create(rgf::hook::Vtable{table}, kSlot)};

  ASSERT_TRUE(kHook.has_value()) << kHook.error().message();
  EXPECT_EQ(kHook->Original<ProbeFn>()(), 1);
  EXPECT_EQ(CallSlot(), 1);
}

TEST_F(VmtHookUnit, CreateRejectsAnUnreadableSlot) {
  void** unmapped{nullptr};
  {
    void* const kReserved{VirtualAlloc(
        /*lpAddress        = */ nullptr,
        /*dwSize           = */ kTableSize,
        /*flAllocationType = */ MEM_RESERVE,
        /*flProtect        = */ PAGE_NOACCESS)};
    ASSERT_NE(kReserved, nullptr);
    ASSERT_NE(VirtualFree(
                  /*lpAddress  = */ kReserved,
                  /*dwSize     = */ 0,
                  /*dwFreeType = */ MEM_RELEASE),
              FALSE);
    unmapped = static_cast<void**>(kReserved);
  }

  const std::expected<rgf::hook::VmtHook, std::error_code> kHook{
      rgf::hook::VmtHook::Create(rgf::hook::Vtable{unmapped}, kSlot)};

  ASSERT_FALSE(kHook.has_value());
  EXPECT_EQ(kHook.error(),
            rgf::hook::make_error_code(rgf::hook::Error::SlotNotReadable));
  EXPECT_EQ(&kHook.error().category(), &rgf::hook::ErrorCategory());
}

TEST(VmtHookEmptyUnit, DefaultConstructedHookHasNoBinding) {
  rgf::hook::VmtHook hook{};

  EXPECT_EQ(hook.Original<ProbeFn>(), nullptr);
  EXPECT_TRUE(hook.Disable().has_value());
  const auto kEnabled{hook.Enable()};
  ASSERT_FALSE(kEnabled.has_value());
  EXPECT_EQ(kEnabled.error(), std::errc::invalid_argument);
  const auto kSet{hook.SetDetour(&DetourFn)};
  ASSERT_FALSE(kSet.has_value());
  EXPECT_EQ(kSet.error(), std::errc::invalid_argument);
}

TEST(VmtHookEmptyUnit, MoveConstructingAnEmptyHookKeepsBothObjectsEmpty) {
  std::optional<rgf::hook::VmtHook> source{std::in_place};
  rgf::hook::VmtHook destination{std::move(*source)};

  EXPECT_EQ(source->Original<ProbeFn>(), nullptr);
  EXPECT_EQ(destination.Original<ProbeFn>(), nullptr);
  EXPECT_TRUE(source->Disable().has_value());
  EXPECT_TRUE(destination.Disable().has_value());
}

TEST(VmtHookEmptyUnit, CreateRejectsANullTableBeforeResolvingTheSlot) {
  const auto kHook{rgf::hook::VmtHook::Create(rgf::hook::Vtable{nullptr}, 1)};

  ASSERT_FALSE(kHook.has_value());
  EXPECT_EQ(kHook.error(), std::errc::invalid_argument);
}

TEST_F(VmtHookUnit, MoveTransfersTheBindingAndConfiguredDetour) {
  auto source{rgf::hook::VmtHook::Create(rgf::hook::Vtable{table}, kSlot)};
  ASSERT_TRUE(source.has_value()) << source.error().message();
  ASSERT_TRUE(source->SetDetour(&DetourFn).has_value());

  rgf::hook::VmtHook destination{std::move(*source)};

  EXPECT_EQ(source->Original<ProbeFn>(), nullptr);
  EXPECT_TRUE(source->Disable().has_value());
  const auto kSourceEnabled{source->Enable()};
  ASSERT_FALSE(kSourceEnabled.has_value());
  EXPECT_EQ(kSourceEnabled.error(), std::errc::invalid_argument);
  const auto kSourceSet{source->SetDetour(&OtherDetourFn)};
  ASSERT_FALSE(kSourceSet.has_value());
  EXPECT_EQ(kSourceSet.error(), std::errc::invalid_argument);
  EXPECT_EQ(destination.Original<ProbeFn>(), &OriginalFn);
  EXPECT_EQ(CallSlot(), 1);
  ASSERT_TRUE(destination.Enable().has_value());
  EXPECT_EQ(CallSlot(), 2);
  ASSERT_TRUE(destination.Disable().has_value());
  EXPECT_EQ(CallSlot(), 1);
}

TEST_F(VmtHookUnit, MoveTransfersAnEnabledHookAndItsCleanupResponsibility) {
  std::optional<rgf::hook::VmtHook> destination{};
  {
    auto source{rgf::hook::VmtHook::Create(rgf::hook::Vtable{table}, kSlot)};
    ASSERT_TRUE(source.has_value()) << source.error().message();
    ASSERT_TRUE(source->SetDetour(&DetourFn).has_value());
    ASSERT_TRUE(source->Enable().has_value());

    destination.emplace(std::move(*source));

    EXPECT_EQ(source->Original<ProbeFn>(), nullptr);
    EXPECT_TRUE(source->Disable().has_value());
    EXPECT_EQ(CallSlot(), 2);
  }

  EXPECT_EQ(CallSlot(), 2);
  EXPECT_EQ(destination->Original<ProbeFn>(), &OriginalFn);
  const auto kEnabled{destination->Enable()};
  ASSERT_FALSE(kEnabled.has_value());
  EXPECT_EQ(kEnabled.error(),
            rgf::hook::make_error_code(rgf::hook::Error::HookAlreadyApplied));
  destination.reset();
  EXPECT_EQ(CallSlot(), 1);
}

TEST_F(VmtHookUnit, SetDetourDoesNotTouchTheSlot) {
  std::expected<rgf::hook::VmtHook, std::error_code> hook{
      rgf::hook::VmtHook::Create(rgf::hook::Vtable{table}, kSlot)};
  ASSERT_TRUE(hook.has_value()) << hook.error().message();

  const std::expected<void, std::error_code> kSet{hook->SetDetour(&DetourFn)};

  ASSERT_TRUE(kSet.has_value()) << kSet.error().message();
  EXPECT_EQ(CallSlot(), 1);
}

TEST_F(VmtHookUnit, EnableRedirectsTheSlotToTheDetour) {
  std::expected<rgf::hook::VmtHook, std::error_code> hook{
      rgf::hook::VmtHook::Create(rgf::hook::Vtable{table}, kSlot)};
  ASSERT_TRUE(hook.has_value()) << hook.error().message();
  ASSERT_TRUE(hook->SetDetour(&DetourFn).has_value());
  ASSERT_EQ(CallSlot(), 1);

  const std::expected<void, std::error_code> kEnabled{hook->Enable()};

  ASSERT_TRUE(kEnabled.has_value()) << kEnabled.error().message();
  EXPECT_EQ(CallSlot(), 2);
  EXPECT_EQ(hook->Original<ProbeFn>()(), 1);
}

TEST_F(VmtHookUnit, EnableLeavesThePageProtectionAsItFoundIt) {
  std::expected<rgf::hook::VmtHook, std::error_code> hook{
      rgf::hook::VmtHook::Create(rgf::hook::Vtable{table}, kSlot)};
  ASSERT_TRUE(hook.has_value()) << hook.error().message();
  ASSERT_TRUE(hook->SetDetour(&DetourFn).has_value());

  ASSERT_TRUE(hook->Enable().has_value());

  EXPECT_TRUE(
      rgf::test::RegionHasProtection(GetCurrentProcess(), page, PAGE_READONLY));
}

TEST_F(VmtHookUnit, DisableRestoresTheOriginal) {
  std::expected<rgf::hook::VmtHook, std::error_code> hook{
      rgf::hook::VmtHook::Create(rgf::hook::Vtable{table}, kSlot)};
  ASSERT_TRUE(hook.has_value()) << hook.error().message();
  ASSERT_TRUE(hook->SetDetour(&DetourFn).has_value());
  ASSERT_TRUE(hook->Enable().has_value());
  ASSERT_EQ(CallSlot(), 2);

  const std::expected<void, std::error_code> kDisabled{hook->Disable()};

  ASSERT_TRUE(kDisabled.has_value()) << kDisabled.error().message();
  EXPECT_EQ(CallSlot(), 1);
}

TEST_F(VmtHookUnit, DestructorRestoresTheOriginal) {
  {
    std::expected<rgf::hook::VmtHook, std::error_code> hook{
        rgf::hook::VmtHook::Create(rgf::hook::Vtable{table}, kSlot)};
    ASSERT_TRUE(hook.has_value()) << hook.error().message();
    ASSERT_TRUE(hook->SetDetour(&DetourFn).has_value());
    ASSERT_TRUE(hook->Enable().has_value());
    ASSERT_EQ(CallSlot(), 2);
  }

  EXPECT_EQ(CallSlot(), 1);
}

TEST_F(VmtHookUnit, EnableWithoutADetourReportsDetourNotSet) {
  std::expected<rgf::hook::VmtHook, std::error_code> hook{
      rgf::hook::VmtHook::Create(rgf::hook::Vtable{table}, kSlot)};
  ASSERT_TRUE(hook.has_value()) << hook.error().message();

  const std::expected<void, std::error_code> kEnabled{hook->Enable()};

  ASSERT_FALSE(kEnabled.has_value());
  EXPECT_EQ(kEnabled.error(),
            rgf::hook::make_error_code(rgf::hook::Error::DetourNotSet));
  EXPECT_EQ(CallSlot(), 1);
}

TEST_F(VmtHookUnit, EnableTwiceReportsHookAlreadyApplied) {
  std::expected<rgf::hook::VmtHook, std::error_code> hook{
      rgf::hook::VmtHook::Create(rgf::hook::Vtable{table}, kSlot)};
  ASSERT_TRUE(hook.has_value()) << hook.error().message();
  ASSERT_TRUE(hook->SetDetour(&DetourFn).has_value());
  ASSERT_TRUE(hook->Enable().has_value());

  const std::expected<void, std::error_code> kSecond{hook->Enable()};

  ASSERT_FALSE(kSecond.has_value());
  EXPECT_EQ(kSecond.error(),
            rgf::hook::make_error_code(rgf::hook::Error::HookAlreadyApplied));
  EXPECT_EQ(CallSlot(), 2);
}

TEST_F(VmtHookUnit, SetDetourWhileEnabledReportsHookAlreadyApplied) {
  std::expected<rgf::hook::VmtHook, std::error_code> hook{
      rgf::hook::VmtHook::Create(rgf::hook::Vtable{table}, kSlot)};
  ASSERT_TRUE(hook.has_value()) << hook.error().message();
  ASSERT_TRUE(hook->SetDetour(&DetourFn).has_value());
  ASSERT_TRUE(hook->Enable().has_value());

  const std::expected<void, std::error_code> kSet{
      hook->SetDetour(&OtherDetourFn)};

  ASSERT_FALSE(kSet.has_value());
  EXPECT_EQ(kSet.error(),
            rgf::hook::make_error_code(rgf::hook::Error::HookAlreadyApplied));
  EXPECT_EQ(CallSlot(), 2);
  EXPECT_EQ(hook->Original<ProbeFn>()(), 1);
}

TEST_F(VmtHookUnit, DisableWithoutEnableReportsHookNotApplied) {
  std::expected<rgf::hook::VmtHook, std::error_code> hook{
      rgf::hook::VmtHook::Create(rgf::hook::Vtable{table}, kSlot)};
  ASSERT_TRUE(hook.has_value()) << hook.error().message();
  ASSERT_TRUE(hook->SetDetour(&DetourFn).has_value());

  const std::expected<void, std::error_code> kDisabled{hook->Disable()};

  ASSERT_FALSE(kDisabled.has_value());
  EXPECT_EQ(kDisabled.error(),
            rgf::hook::make_error_code(rgf::hook::Error::HookNotApplied));
  EXPECT_EQ(CallSlot(), 1);
}

TEST_F(VmtHookUnit, EnableAfterDisableHooksAgain) {
  std::expected<rgf::hook::VmtHook, std::error_code> hook{
      rgf::hook::VmtHook::Create(rgf::hook::Vtable{table}, kSlot)};
  ASSERT_TRUE(hook.has_value()) << hook.error().message();
  ASSERT_TRUE(hook->SetDetour(&DetourFn).has_value());
  ASSERT_TRUE(hook->Enable().has_value());
  ASSERT_TRUE(hook->Disable().has_value());

  ASSERT_TRUE(hook->Enable().has_value());

  EXPECT_EQ(CallSlot(), 2);
}

TEST_F(VmtHookUnit, SetDetourAfterDisableReplacesTheDetour) {
  std::expected<rgf::hook::VmtHook, std::error_code> hook{
      rgf::hook::VmtHook::Create(rgf::hook::Vtable{table}, kSlot)};
  ASSERT_TRUE(hook.has_value()) << hook.error().message();
  ASSERT_TRUE(hook->SetDetour(&DetourFn).has_value());
  ASSERT_TRUE(hook->Enable().has_value());
  ASSERT_TRUE(hook->Disable().has_value());

  ASSERT_TRUE(hook->SetDetour(&OtherDetourFn).has_value());
  ASSERT_TRUE(hook->Enable().has_value());

  EXPECT_EQ(CallSlot(), kOtherDetourResult);
  EXPECT_EQ(hook->Original<ProbeFn>()(), 1);
}

TEST_F(VmtHookProtectionFailureUnit,
       EnableProtectionFailureLeavesTheHookDisabled) {
  auto hook{rgf::hook::VmtHook::Create(rgf::hook::Vtable{table}, kSlot)};
  ASSERT_TRUE(hook.has_value()) << hook.error().message();
  ASSERT_TRUE(hook->SetDetour(&DetourFn).has_value());
  FailNextChange(PAGE_READWRITE);

  const auto kEnabled{hook->Enable()};

  ASSERT_FALSE(kEnabled.has_value());
  const std::error_code kProtectionError{ERROR_ACCESS_DENIED,
                                         std::system_category()};
  EXPECT_EQ(kEnabled.error(), kProtectionError);
  EXPECT_EQ(CallSlot(), 1);
  EXPECT_TRUE(hook->SetDetour(&OtherDetourFn).has_value());
  EXPECT_TRUE(hook->Enable().has_value());
  EXPECT_EQ(CallSlot(), kOtherDetourResult);
}

TEST_F(VmtHookProtectionFailureUnit,
       EnableRestoreFailureStillRestoresTheOriginalOnDestruction) {
  {
    auto hook{rgf::hook::VmtHook::Create(rgf::hook::Vtable{table}, kSlot)};
    ASSERT_TRUE(hook.has_value()) << hook.error().message();
    ASSERT_TRUE(hook->SetDetour(&DetourFn).has_value());
    FailNextChange(PAGE_READONLY);

    const auto kEnabled{hook->Enable()};

    ASSERT_TRUE(kEnabled.has_value()) << kEnabled.error().message();
    EXPECT_EQ(CallSlot(), 2);
    const auto kSecond{hook->Enable()};
    ASSERT_FALSE(kSecond.has_value());
    EXPECT_EQ(kSecond.error(),
              rgf::hook::make_error_code(rgf::hook::Error::HookAlreadyApplied));
  }

  EXPECT_EQ(CallSlot(), 1);
}

TEST_F(VmtHookProtectionFailureUnit,
       DisableRestoreFailureStillLeavesTheHookDisabled) {
  auto hook{rgf::hook::VmtHook::Create(rgf::hook::Vtable{table}, kSlot)};
  ASSERT_TRUE(hook.has_value()) << hook.error().message();
  ASSERT_TRUE(hook->SetDetour(&DetourFn).has_value());
  ASSERT_TRUE(hook->Enable().has_value());
  FailNextChange(PAGE_READONLY);

  const auto kDisabled{hook->Disable()};

  ASSERT_TRUE(kDisabled.has_value()) << kDisabled.error().message();
  EXPECT_EQ(CallSlot(), 1);
  ASSERT_TRUE(hook->SetDetour(&OtherDetourFn).has_value());
  EXPECT_TRUE(hook->Enable().has_value());
  EXPECT_EQ(CallSlot(), kOtherDetourResult);
}
