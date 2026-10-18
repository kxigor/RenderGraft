#include <gtest/gtest.h>
#include <windows.h>

#include <rgf/win/scoped_handle.hpp>
#include <string>
#include <support/win_probe.hpp>
#include <utility>

namespace {

[[nodiscard]] HANDLE CreateUnnamedEvent() {
  return CreateEventW(
      /*lpEventAttributes = */ nullptr,
      /*bManualReset      = */ TRUE,
      /*bInitialState     = */ FALSE,
      /*lpName            = */ nullptr);
}

}  // namespace

TEST(ScopedHandleUnit, DefaultConstructedOwnsNothing) {
  const rgf::win::ScopedHandle kGuard{};

  EXPECT_EQ(kGuard.Get(), nullptr);
  EXPECT_FALSE(static_cast<bool>(kGuard));
}

TEST(ScopedHandleUnit, InvalidHandleValueIsTreatedAsEmpty) {
  const rgf::win::ScopedHandle kGuard{INVALID_HANDLE_VALUE};

  EXPECT_FALSE(static_cast<bool>(kGuard));
  EXPECT_EQ(kGuard.Get(), INVALID_HANDLE_VALUE);
}

TEST(ScopedHandleUnit, DestructorClosesTheHandle) {
  const std::wstring kName{rgf::test::ProbeName(L"dtor")};
  const HANDLE kRaw{rgf::test::CreateProbeEvent(kName)};
  ASSERT_NE(kRaw, nullptr);
  ASSERT_TRUE(rgf::test::ProbeObjectExists(kName));

  {
    const rgf::win::ScopedHandle kGuard{kRaw};
    EXPECT_TRUE(static_cast<bool>(kGuard));
    EXPECT_EQ(kGuard.Get(), kRaw);
  }

  EXPECT_FALSE(rgf::test::ProbeObjectExists(kName));
}

TEST(ScopedHandleUnit, DestructorClosesTheHandleValue) {
  const HANDLE kRaw{CreateUnnamedEvent()};
  ASSERT_NE(kRaw, nullptr);

  {
    const rgf::win::ScopedHandle kGuard{kRaw};
    EXPECT_TRUE(static_cast<bool>(kGuard));
  }

  EXPECT_TRUE(rgf::test::HandleIsClosed(kRaw));
}

TEST(ScopedHandleUnit, ReleaseTransfersOwnershipAwayFromTheGuard) {
  const HANDLE kRaw{CreateUnnamedEvent()};
  ASSERT_NE(kRaw, nullptr);

  {
    rgf::win::ScopedHandle guard{kRaw};

    EXPECT_EQ(guard.Release(), kRaw);
    EXPECT_FALSE(static_cast<bool>(guard));
    EXPECT_EQ(guard.Get(), nullptr);
  }

  EXPECT_FALSE(rgf::test::HandleIsClosed(kRaw));
  EXPECT_NE(CloseHandle(
                /*hObject = */ kRaw),
            FALSE);
}

TEST(ScopedHandleUnit, ResetWithTheSameHandleIsANoOp) {
  const HANDLE kRaw{CreateUnnamedEvent()};
  ASSERT_NE(kRaw, nullptr);

  {
    rgf::win::ScopedHandle guard{kRaw};
    guard.Reset(kRaw);

    EXPECT_FALSE(rgf::test::HandleIsClosed(kRaw));
    EXPECT_EQ(guard.Get(), kRaw);
  }

  EXPECT_TRUE(rgf::test::HandleIsClosed(kRaw));
}

TEST(ScopedHandleUnit, ResetClosesTheOldHandleAndAdoptsTheNew) {
  const HANDLE kFirst{CreateUnnamedEvent()};
  ASSERT_NE(kFirst, nullptr);
  const HANDLE kSecond{CreateUnnamedEvent()};
  ASSERT_NE(kSecond, nullptr);
  ASSERT_NE(kFirst, kSecond);

  {
    rgf::win::ScopedHandle guard{kFirst};
    guard.Reset(kSecond);

    EXPECT_TRUE(rgf::test::HandleIsClosed(kFirst));
    EXPECT_EQ(guard.Get(), kSecond);
  }

  EXPECT_TRUE(rgf::test::HandleIsClosed(kSecond));
}

TEST(ScopedHandleUnit, MoveConstructionTransfersOwnership) {
  const HANDLE kRaw{CreateUnnamedEvent()};
  ASSERT_NE(kRaw, nullptr);

  {
    rgf::win::ScopedHandle src{kRaw};
    const rgf::win::ScopedHandle kDst{std::move(src)};

    EXPECT_EQ(kDst.Get(), kRaw);
    EXPECT_FALSE(static_cast<bool>(src));
    EXPECT_EQ(src.Get(), nullptr);
    EXPECT_FALSE(rgf::test::HandleIsClosed(kRaw));
  }

  EXPECT_TRUE(rgf::test::HandleIsClosed(kRaw));
}

TEST(ScopedHandleUnit, MoveAssignmentClosesTheHandleItReplaces) {
  const std::wstring kVictimName{rgf::test::ProbeName(L"moveassign")};
  const HANDLE kVictim{rgf::test::CreateProbeEvent(kVictimName)};
  ASSERT_NE(kVictim, nullptr);
  const HANDLE kSurvivor{CreateUnnamedEvent()};
  ASSERT_NE(kSurvivor, nullptr);

  {
    rgf::win::ScopedHandle dst{kVictim};
    rgf::win::ScopedHandle src{kSurvivor};
    dst = std::move(src);

    EXPECT_FALSE(rgf::test::ProbeObjectExists(kVictimName));
    EXPECT_EQ(dst.Get(), kSurvivor);
    EXPECT_FALSE(static_cast<bool>(src));
    EXPECT_EQ(src.Get(), nullptr);
  }

  EXPECT_TRUE(rgf::test::HandleIsClosed(kSurvivor));
}

TEST(ScopedHandleUnit, SelfMoveAssignmentKeepsTheHandle) {
  const HANDLE kRaw{CreateUnnamedEvent()};
  ASSERT_NE(kRaw, nullptr);

  {
    rgf::win::ScopedHandle guard{kRaw};

    guard = std::move(guard);

    EXPECT_TRUE(static_cast<bool>(guard));
    EXPECT_EQ(guard.Get(), kRaw);
    EXPECT_FALSE(rgf::test::HandleIsClosed(kRaw));
  }

  EXPECT_TRUE(rgf::test::HandleIsClosed(kRaw));
}
