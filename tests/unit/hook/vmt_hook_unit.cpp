#include <gtest/gtest.h>
#include <windows.h>

#include <bit>
#include <cstddef>
#include <expected>
#include <memory>
#include <rgf/hook/error.hpp>
#include <rgf/hook/vmt_hook.hpp>
#include <rgf/hook/vtable.hpp>
#include <support/win_probe.hpp>
#include <system_error>
#include <utility>

namespace {

constexpr SIZE_T kTableSize{4096};
constexpr std::size_t kSlot{0};

using ProbeFn = int (*)();

int OriginalFn() { return 1; }
int DetourFn() { return 2; }
int OtherDetourFn() { return 3; }

using VirtualProtectExFn = decltype(&VirtualProtectEx);

VirtualProtectExFn originalVirtualProtectEx{nullptr};
LPVOID failurePage{nullptr};
DWORD failureProtection{};

BOOL WINAPI FailNextProtection(HANDLE process, LPVOID address, SIZE_T size,
                               DWORD protection, PDWORD previous) {
  if (address == failurePage && protection == failureProtection) {
    failureProtection = 0;
    SetLastError(ERROR_ACCESS_DENIED);
    return FALSE;
  }
  return originalVirtualProtectEx(process, address, size, protection, previous);
}

[[nodiscard]] void** FindVirtualProtectExImport() {
  auto* image{static_cast<BYTE*>(
      static_cast<void*>(GetModuleHandleW(/*lpModuleName = */ nullptr)))};
  if (image == nullptr) {
    return nullptr;
  }
  const auto* kDos{
      static_cast<const IMAGE_DOS_HEADER*>(static_cast<const void*>(image))};
  const auto* kNt{static_cast<const IMAGE_NT_HEADERS*>(
      static_cast<const void*>(image + kDos->e_lfanew))};
  const auto& kDirectory{
      kNt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT]};
  const auto* descriptor{static_cast<const IMAGE_IMPORT_DESCRIPTOR*>(
      static_cast<const void*>(image + kDirectory.VirtualAddress))};
  for (; descriptor->Name != 0; ++descriptor) {
    auto* thunk{static_cast<IMAGE_THUNK_DATA*>(
        static_cast<void*>(image + descriptor->FirstThunk))};
    for (; thunk->u1.Function != 0; ++thunk) {
      if (thunk->u1.Function == std::bit_cast<ULONG_PTR>(&VirtualProtectEx)) {
        return static_cast<void**>(static_cast<void*>(&thunk->u1.Function));
      }
    }
  }
  return nullptr;
}

class VmtHookUnit : public ::testing::Test {
 protected:
  void SetUp() override {
    page_ = VirtualAlloc(
        /*lpAddress        = */ nullptr,
        /*dwSize           = */ kTableSize,
        /*flAllocationType = */ MEM_COMMIT | MEM_RESERVE,
        /*flProtect        = */ PAGE_READWRITE);
    ASSERT_NE(page_, nullptr);

    table_ = static_cast<void**>(page_);
    table_[kSlot] = std::bit_cast<void*>(&OriginalFn);

    DWORD previous{};
    ASSERT_NE(VirtualProtect(
                  /*lpAddress      = */ page_,
                  /*dwSize         = */ kTableSize,
                  /*flNewProtect   = */ PAGE_READONLY,
                  /*lpflOldProtect = */ &previous),
              FALSE);
  }

  void TearDown() override {
    if (page_ != nullptr) {
      VirtualFree(
          /*lpAddress  = */ page_,
          /*dwSize     = */ 0,
          /*dwFreeType = */ MEM_RELEASE);
    }
  }

  [[nodiscard]] int CallSlot() const {
    return std::bit_cast<ProbeFn>(table_[kSlot])();
  }

  LPVOID page_{nullptr};
  void** table_{nullptr};
};

class VmtHookProtectionFailureUnit : public VmtHookUnit {
 protected:
  void SetUp() override {
    VmtHookUnit::SetUp();
    ASSERT_NE(page_, nullptr);
    void** const kImport{FindVirtualProtectExImport()};
    ASSERT_NE(kImport, nullptr);

    auto hook{
        rgf::hook::VmtHook::CreateDynamic(rgf::hook::Vtable{kImport}, kSlot)};
    ASSERT_TRUE(hook.has_value()) << hook.error().message();
    protectionHook_ = std::move(*hook);
    originalVirtualProtectEx = protectionHook_->Original<VirtualProtectExFn>();
    ASSERT_TRUE(protectionHook_->SetDetour(&FailNextProtection).has_value());
    ASSERT_TRUE(protectionHook_->Enable().has_value());
    failurePage = page_;
  }

  void TearDown() override {
    failurePage = nullptr;
    failureProtection = 0;
    protectionHook_.reset();
    originalVirtualProtectEx = nullptr;
    VmtHookUnit::TearDown();
  }

  void FailNextChange(DWORD protection) { failureProtection = protection; }

 private:
  std::unique_ptr<rgf::hook::VmtHook> protectionHook_;
};

}  // namespace

TEST_F(VmtHookUnit, CreatePublishesTheOriginalWithoutTouchingTheSlot) {
  const std::expected<rgf::hook::VmtHook, std::error_code> kHook{
      rgf::hook::VmtHook::Create(rgf::hook::Vtable{table_}, kSlot)};

  ASSERT_TRUE(kHook.has_value()) << kHook.error().message();
  EXPECT_EQ(kHook->Original<ProbeFn>()(), 1);
  EXPECT_EQ(CallSlot(), 1);
}

TEST_F(VmtHookUnit, CreateRejectsAnUnreadableSlot) {
  void** unmapped{nullptr};
  {
    const LPVOID kReserved{VirtualAlloc(
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

TEST_F(VmtHookUnit, CreateDynamicPublishesTheSameOriginalOnTheHeap) {
  const std::expected<std::unique_ptr<rgf::hook::VmtHook>, std::error_code>
      kHook{
          rgf::hook::VmtHook::CreateDynamic(rgf::hook::Vtable{table_}, kSlot)};

  ASSERT_TRUE(kHook.has_value()) << kHook.error().message();
  ASSERT_NE(kHook->get(), nullptr);
  EXPECT_EQ((*kHook)->Original<ProbeFn>()(), 1);
  EXPECT_EQ(CallSlot(), 1);
}

TEST_F(VmtHookUnit, CreateDynamicRejectsAnUnreadableSlotTheSameWay) {
  void** unmapped{nullptr};
  {
    const LPVOID kReserved{VirtualAlloc(
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

  const std::expected<std::unique_ptr<rgf::hook::VmtHook>, std::error_code>
      kHook{rgf::hook::VmtHook::CreateDynamic(rgf::hook::Vtable{unmapped},
                                              kSlot)};

  ASSERT_FALSE(kHook.has_value());
  EXPECT_EQ(kHook.error(),
            rgf::hook::make_error_code(rgf::hook::Error::SlotNotReadable));
}

TEST_F(VmtHookUnit, SetDetourDoesNotTouchTheSlot) {
  std::expected<rgf::hook::VmtHook, std::error_code> hook{
      rgf::hook::VmtHook::Create(rgf::hook::Vtable{table_}, kSlot)};
  ASSERT_TRUE(hook.has_value()) << hook.error().message();

  const std::expected<void, std::error_code> kSet{hook->SetDetour(&DetourFn)};

  ASSERT_TRUE(kSet.has_value()) << kSet.error().message();
  EXPECT_EQ(CallSlot(), 1);
}

TEST_F(VmtHookUnit, EnableRedirectsTheSlotToTheDetour) {
  std::expected<rgf::hook::VmtHook, std::error_code> hook{
      rgf::hook::VmtHook::Create(rgf::hook::Vtable{table_}, kSlot)};
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
      rgf::hook::VmtHook::Create(rgf::hook::Vtable{table_}, kSlot)};
  ASSERT_TRUE(hook.has_value()) << hook.error().message();
  ASSERT_TRUE(hook->SetDetour(&DetourFn).has_value());

  ASSERT_TRUE(hook->Enable().has_value());

  EXPECT_TRUE(rgf::test::RegionHasProtection(GetCurrentProcess(), page_,
                                             PAGE_READONLY));
}

TEST_F(VmtHookUnit, DisableRestoresTheOriginal) {
  std::expected<rgf::hook::VmtHook, std::error_code> hook{
      rgf::hook::VmtHook::Create(rgf::hook::Vtable{table_}, kSlot)};
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
        rgf::hook::VmtHook::Create(rgf::hook::Vtable{table_}, kSlot)};
    ASSERT_TRUE(hook.has_value()) << hook.error().message();
    ASSERT_TRUE(hook->SetDetour(&DetourFn).has_value());
    ASSERT_TRUE(hook->Enable().has_value());
    ASSERT_EQ(CallSlot(), 2);
  }

  EXPECT_EQ(CallSlot(), 1);
}

TEST_F(VmtHookUnit, EnableWithoutADetourReportsDetourNotSet) {
  std::expected<rgf::hook::VmtHook, std::error_code> hook{
      rgf::hook::VmtHook::Create(rgf::hook::Vtable{table_}, kSlot)};
  ASSERT_TRUE(hook.has_value()) << hook.error().message();

  const std::expected<void, std::error_code> kEnabled{hook->Enable()};

  ASSERT_FALSE(kEnabled.has_value());
  EXPECT_EQ(kEnabled.error(),
            rgf::hook::make_error_code(rgf::hook::Error::DetourNotSet));
  EXPECT_EQ(CallSlot(), 1);
}

TEST_F(VmtHookUnit, EnableTwiceReportsHookAlreadyApplied) {
  std::expected<rgf::hook::VmtHook, std::error_code> hook{
      rgf::hook::VmtHook::Create(rgf::hook::Vtable{table_}, kSlot)};
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
      rgf::hook::VmtHook::Create(rgf::hook::Vtable{table_}, kSlot)};
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
      rgf::hook::VmtHook::Create(rgf::hook::Vtable{table_}, kSlot)};
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
      rgf::hook::VmtHook::Create(rgf::hook::Vtable{table_}, kSlot)};
  ASSERT_TRUE(hook.has_value()) << hook.error().message();
  ASSERT_TRUE(hook->SetDetour(&DetourFn).has_value());
  ASSERT_TRUE(hook->Enable().has_value());
  ASSERT_TRUE(hook->Disable().has_value());

  ASSERT_TRUE(hook->Enable().has_value());

  EXPECT_EQ(CallSlot(), 2);
}

TEST_F(VmtHookUnit, SetDetourAfterDisableReplacesTheDetour) {
  std::expected<rgf::hook::VmtHook, std::error_code> hook{
      rgf::hook::VmtHook::Create(rgf::hook::Vtable{table_}, kSlot)};
  ASSERT_TRUE(hook.has_value()) << hook.error().message();
  ASSERT_TRUE(hook->SetDetour(&DetourFn).has_value());
  ASSERT_TRUE(hook->Enable().has_value());
  ASSERT_TRUE(hook->Disable().has_value());

  ASSERT_TRUE(hook->SetDetour(&OtherDetourFn).has_value());
  ASSERT_TRUE(hook->Enable().has_value());

  EXPECT_EQ(CallSlot(), 3);
  EXPECT_EQ(hook->Original<ProbeFn>()(), 1);
}

TEST_F(VmtHookProtectionFailureUnit,
       EnableProtectionFailureLeavesTheHookDisabled) {
  auto hook{rgf::hook::VmtHook::Create(rgf::hook::Vtable{table_}, kSlot)};
  ASSERT_TRUE(hook.has_value()) << hook.error().message();
  ASSERT_TRUE(hook->SetDetour(&DetourFn).has_value());
  FailNextChange(PAGE_READWRITE);

  const auto kEnabled{hook->Enable()};

  ASSERT_FALSE(kEnabled.has_value());
  EXPECT_EQ(kEnabled.error(),
            (std::error_code{ERROR_ACCESS_DENIED, std::system_category()}));
  EXPECT_EQ(CallSlot(), 1);
  EXPECT_TRUE(hook->SetDetour(&OtherDetourFn).has_value());
  EXPECT_TRUE(hook->Enable().has_value());
  EXPECT_EQ(CallSlot(), 3);
}

TEST_F(VmtHookProtectionFailureUnit,
       EnableRestoreFailureStillRestoresTheOriginalOnDestruction) {
  {
    auto hook{rgf::hook::VmtHook::Create(rgf::hook::Vtable{table_}, kSlot)};
    ASSERT_TRUE(hook.has_value()) << hook.error().message();
    ASSERT_TRUE(hook->SetDetour(&DetourFn).has_value());
    FailNextChange(PAGE_READONLY);

    const auto kEnabled{hook->Enable()};

    ASSERT_FALSE(kEnabled.has_value());
    EXPECT_EQ(kEnabled.error(),
              (std::error_code{ERROR_ACCESS_DENIED, std::system_category()}));
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
  auto hook{rgf::hook::VmtHook::Create(rgf::hook::Vtable{table_}, kSlot)};
  ASSERT_TRUE(hook.has_value()) << hook.error().message();
  ASSERT_TRUE(hook->SetDetour(&DetourFn).has_value());
  ASSERT_TRUE(hook->Enable().has_value());
  FailNextChange(PAGE_READONLY);

  const auto kDisabled{hook->Disable()};

  ASSERT_FALSE(kDisabled.has_value());
  EXPECT_EQ(kDisabled.error(),
            (std::error_code{ERROR_ACCESS_DENIED, std::system_category()}));
  EXPECT_EQ(CallSlot(), 1);
  ASSERT_TRUE(hook->SetDetour(&OtherDetourFn).has_value());
  EXPECT_TRUE(hook->Enable().has_value());
  EXPECT_EQ(CallSlot(), 3);
}
