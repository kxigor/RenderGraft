#include <gtest/gtest.h>
#include <windows.h>

#include <array>
#include <filesystem>
#include <memory>
#include <rgf/core/cast.hpp>
#include <rgf/win/error.hpp>
#include <rgf/win/module_view.hpp>
#include <system_error>
#include <type_traits>

static_assert(!std::is_default_constructible_v<rgf::win::ModuleView>);
static_assert(std::is_trivially_destructible_v<rgf::win::ModuleView>);

namespace {

using ModuleOwner =
    std::unique_ptr<std::remove_pointer_t<HMODULE>, decltype(&FreeLibrary)>;

[[nodiscard]] ModuleOwner LoadProbe() {
  return ModuleOwner{
      LoadLibraryW(/*lpLibFileName = */ RGF_MODULE_VIEW_PROBE_PATH),
      &FreeLibrary};
}

}  // namespace

TEST(ModuleViewUnit, ExecutableReturnsTheCurrentProcessImage) {
  const auto kModule{rgf::win::ModuleView::Executable()};

  ASSERT_TRUE(kModule.has_value());
  EXPECT_EQ(kModule->Get(), GetModuleHandleW(/*lpModuleName = */ nullptr));
}

TEST(ModuleViewUnit, ExecutablePathMatchesWindowsAndNamesAnExistingFile) {
  const auto kModule{rgf::win::ModuleView::Executable()};
  ASSERT_TRUE(kModule.has_value());
  const auto kPath{kModule->Path()};
  ASSERT_TRUE(kPath.has_value());

  constexpr std::size_t kPathCapacity{32768};
  std::array<WCHAR, kPathCapacity> expected{};
  const DWORD kLength{GetModuleFileNameW(
      /*hModule    = */ nullptr,
      /*lpFilename = */ expected.data(),
      /*nSize      = */ static_cast<DWORD>(expected.size()))};
  ASSERT_GT(kLength, 0);
  ASSERT_LT(kLength, expected.size());
  EXPECT_EQ(*kPath, std::filesystem::path{expected.data()});
  EXPECT_TRUE(kPath->is_absolute());

  std::error_code error{};
  EXPECT_TRUE(std::filesystem::is_regular_file(*kPath, error));
  EXPECT_EQ(error, std::error_code{});
}

TEST(ModuleViewUnit, AddressInTheExecutableResolvesToItsModule) {
  static constexpr int kImageData{1};
  const auto kModule{rgf::win::ModuleView::FromAddress(&kImageData)};

  ASSERT_TRUE(kModule.has_value());
  EXPECT_EQ(kModule->Get(), GetModuleHandleW(/*lpModuleName = */ nullptr));
}

TEST(ModuleViewUnit, NullAddressIsRejectedInsteadOfResolvingTheExecutable) {
  const auto kModule{rgf::win::ModuleView::FromAddress(nullptr)};

  ASSERT_FALSE(kModule.has_value());
  EXPECT_EQ(kModule.error(), std::make_error_code(std::errc::invalid_argument));
}

TEST(ModuleViewUnit, AddressOutsideAnImagePreservesTheWin32Failure) {
  int stackData{};
  SetLastError(/*dwErrCode = */ ERROR_SUCCESS);
  const auto kModule{rgf::win::ModuleView::FromAddress(&stackData)};
  const std::error_code kError{rgf::win::LastError()};

  ASSERT_FALSE(kModule.has_value());
  EXPECT_NE(kError.value(), 0);
  EXPECT_EQ(kModule.error(), kError);
  EXPECT_EQ(&kModule.error().category(), &std::system_category());
}

TEST(ModuleViewUnit,
     BorrowedViewsCanBeCopiedAndDestroyedWithoutUnloadingTheDll) {
  const ModuleOwner kOwner{LoadProbe()};
  ASSERT_NE(kOwner.get(), nullptr);
  FARPROC address{GetProcAddress(
      /*hModule    = */ kOwner.get(),
      /*lpProcName = */ "RgfModuleViewProbe")};
  ASSERT_NE(address, nullptr);

  {
    const auto kModule{rgf::win::ModuleView::FromAddress(
        rgf::core::PointerCast<const void*>(address))};
    ASSERT_TRUE(kModule.has_value());
    const rgf::win::ModuleView kCopy{*kModule};
    EXPECT_EQ(kCopy.Get(), kOwner.get());
    const auto kPath{kCopy.Path()};
    ASSERT_TRUE(kPath.has_value());
    std::error_code error{};
    EXPECT_TRUE(std::filesystem::equivalent(
        *kPath, std::filesystem::path{RGF_MODULE_VIEW_PROBE_PATH}, error));
    EXPECT_EQ(error, std::error_code{});
  }

  EXPECT_EQ(GetModuleHandleW(/*lpModuleName = */ RGF_MODULE_VIEW_PROBE_PATH),
            kOwner.get());
}

TEST(ModuleViewUnit, BorrowingDoesNotKeepTheDllLoadedAfterItsOwnerReleasesIt) {
  ModuleOwner owner{LoadProbe()};
  ASSERT_NE(owner.get(), nullptr);
  FARPROC address{GetProcAddress(
      /*hModule    = */ owner.get(),
      /*lpProcName = */ "RgfModuleViewProbe")};
  ASSERT_NE(address, nullptr);
  const auto kModule{rgf::win::ModuleView::FromAddress(
      rgf::core::PointerCast<const void*>(address))};
  ASSERT_TRUE(kModule.has_value());
  EXPECT_EQ(kModule->Get(), owner.get());

  owner.reset();

  EXPECT_EQ(GetModuleHandleW(/*lpModuleName = */ RGF_MODULE_VIEW_PROBE_PATH),
            nullptr);
}
