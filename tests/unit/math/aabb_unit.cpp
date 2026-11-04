#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <rgf/math/aabb.hpp>

namespace {

constexpr float kMaxSpan{500.0F};

[[nodiscard]] rgf::math::Aabb UnitBox() {
  return rgf::math::Aabb{.min = {.x = -1.0F, .y = -2.0F, .z = 0.0F},
                         .max = {.x = 1.0F, .y = 2.0F, .z = 3.0F}};
}

}  // namespace

TEST(AabbUnit, ReproducesSixConsecutiveFloatsCopiedOverIt) {
  const std::array<float, 6> kSource{-1.0F, -2.0F, 0.0F, 1.0F, 2.0F, 3.0F};

  rgf::math::Aabb value{};
  std::memcpy(static_cast<void*>(&value), kSource.data(), sizeof(value));

  EXPECT_FLOAT_EQ(value.min.x, -1.0F);
  EXPECT_FLOAT_EQ(value.min.z, 0.0F);
  EXPECT_FLOAT_EQ(value.max.x, 1.0F);
  EXPECT_FLOAT_EQ(value.max.z, 3.0F);
}

TEST(AabbUnit, ExtentIsMaxMinusMin) {
  const rgf::math::Vec3 kExtent{UnitBox().Extent()};

  EXPECT_FLOAT_EQ(kExtent.x, 2.0F);
  EXPECT_FLOAT_EQ(kExtent.y, 4.0F);
  EXPECT_FLOAT_EQ(kExtent.z, 3.0F);
}

TEST(AabbUnit, CornerVisitsEveryCombinationOfMinAndMaxExactlyOnce) {
  const rgf::math::Aabb kBox{UnitBox()};

  std::array<rgf::math::Vec3, rgf::math::kAabbCorners> corners{};
  for (std::size_t index{0}; index < rgf::math::kAabbCorners; ++index) {
    corners.at(index) = kBox.Corner(index);

    const rgf::math::Vec3& kCorner{corners.at(index)};
    EXPECT_TRUE(kCorner.x == kBox.min.x || kCorner.x == kBox.max.x);
    EXPECT_TRUE(kCorner.y == kBox.min.y || kCorner.y == kBox.max.y);
    EXPECT_TRUE(kCorner.z == kBox.min.z || kCorner.z == kBox.max.z);
  }

  for (std::size_t left{0}; left < corners.size(); ++left) {
    for (std::size_t right{left + 1}; right < corners.size(); ++right) {
      const bool kSame{corners.at(left).x == corners.at(right).x &&
                       corners.at(left).y == corners.at(right).y &&
                       corners.at(left).z == corners.at(right).z};
      EXPECT_FALSE(kSame);
    }
  }
}

TEST(AabbUnit, CornerZeroIsMinAndTheLastIsMax) {
  const rgf::math::Aabb kBox{UnitBox()};

  EXPECT_FLOAT_EQ(kBox.Corner(0).x, kBox.min.x);
  EXPECT_FLOAT_EQ(kBox.Corner(0).z, kBox.min.z);
  EXPECT_FLOAT_EQ(kBox.Corner(rgf::math::kAabbCorners - 1).x, kBox.max.x);
  EXPECT_FLOAT_EQ(kBox.Corner(rgf::math::kAabbCorners - 1).z, kBox.max.z);
}

TEST(AabbUnit, IsValidAcceptsAnOrdinaryBox) {
  EXPECT_TRUE(UnitBox().IsValid(kMaxSpan));
}

// Swapped corners mean the layout was misread, not that the box is odd.
TEST(AabbUnit, IsValidRejectsASwappedBox) {
  const rgf::math::Aabb kSwapped{.min = {.x = 1.0F, .y = 0.0F, .z = 0.0F},
                                 .max = {.x = -1.0F, .y = 0.0F, .z = 0.0F}};

  EXPECT_FALSE(kSwapped.IsValid(kMaxSpan));
}

TEST(AabbUnit, IsValidRejectsABoxLargerThanTheLimit) {
  const rgf::math::Aabb kHuge{
      .min = {.x = 0.0F, .y = 0.0F, .z = 0.0F},
      .max = {.x = kMaxSpan + 1.0F, .y = 0.0F, .z = 0.0F}};

  EXPECT_FALSE(kHuge.IsValid(kMaxSpan));
}
