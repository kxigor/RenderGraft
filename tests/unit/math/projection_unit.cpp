#include <gtest/gtest.h>

#include <numbers>
#include <optional>
#include <rgf/math/matrix4.hpp>
#include <rgf/math/projection.hpp>
#include <rgf/math/screen.hpp>

namespace {

constexpr float kWidth{1920.0F};
constexpr float kHeight{1080.0F};
constexpr float kAspect{kWidth / kHeight};
constexpr float kFov{std::numbers::pi_v<float> / 2.0F};
constexpr float kNear{0.1F};
constexpr float kFar{10000.0F};
constexpr float kCentreX{kWidth / 2.0F};
constexpr float kCentreY{kHeight / 2.0F};
constexpr float kTolerance{0.01F};

// Right-handed naming, left-handed maths: +Z ahead, +X right, +Y up, which is
// what a Direct3D title hands us.
constexpr rgf::math::Vec3 kOrigin{.x = 0.0F, .y = 0.0F, .z = 0.0F};
constexpr rgf::math::Vec3 kForward{.x = 0.0F, .y = 0.0F, .z = 1.0F};
constexpr rgf::math::Vec3 kUp{.x = 0.0F, .y = 1.0F, .z = 0.0F};
constexpr rgf::math::Vec3 kRight{.x = 1.0F, .y = 0.0F, .z = 0.0F};

[[nodiscard]] rgf::math::Matrix4 DefaultViewProjection(
    const rgf::math::Vec3& eye = kOrigin) {
  return rgf::math::Multiply(
      rgf::math::BuildView(eye, kForward, kUp, kRight),
      rgf::math::BuildProjection(kFov, kAspect, kNear, kFar));
}

[[nodiscard]] std::optional<rgf::math::ScreenPoint> Project(
    const rgf::math::Vec3& world, const rgf::math::Vec3& eye = kOrigin) {
  return rgf::math::WorldToScreen(DefaultViewProjection(eye), world, kWidth,
                                  kHeight);
}

}  // namespace

TEST(MathProjectionUnit, APointStraightAheadLandsAtTheCentre) {
  const std::optional<rgf::math::ScreenPoint> kPoint{
      Project({.x = 0.0F, .y = 0.0F, .z = 10.0F})};

  ASSERT_TRUE(kPoint.has_value());
  EXPECT_NEAR(kPoint->x, kCentreX, kTolerance);
  EXPECT_NEAR(kPoint->y, kCentreY, kTolerance);
}

TEST(MathProjectionUnit, APointBehindTheCameraIsRejected) {
  EXPECT_FALSE(Project({.x = 0.0F, .y = 0.0F, .z = -10.0F}).has_value());
}

TEST(MathProjectionUnit, APointInThePlaneOfTheEyeIsRejected) {
  EXPECT_FALSE(Project({.x = 5.0F, .y = 5.0F, .z = 0.0F}).has_value());
}

TEST(MathProjectionUnit, MovingRightMovesTheScreenPointRight) {
  const std::optional<rgf::math::ScreenPoint> kPoint{
      Project({.x = 1.0F, .y = 0.0F, .z = 10.0F})};

  ASSERT_TRUE(kPoint.has_value());
  EXPECT_GT(kPoint->x, kCentreX);
  EXPECT_NEAR(kPoint->y, kCentreY, kTolerance);
}

TEST(MathProjectionUnit, MovingUpMovesTheScreenPointUp) {
  const std::optional<rgf::math::ScreenPoint> kPoint{
      Project({.x = 0.0F, .y = 1.0F, .z = 10.0F})};

  ASSERT_TRUE(kPoint.has_value());
  EXPECT_LT(kPoint->y, kCentreY);
  EXPECT_NEAR(kPoint->x, kCentreX, kTolerance);
}

TEST(MathProjectionUnit, TheVerticalFieldOfViewEdgeLandsOnTheScreenEdge) {
  const std::optional<rgf::math::ScreenPoint> kTop{
      Project({.x = 0.0F, .y = 10.0F, .z = 10.0F})};

  ASSERT_TRUE(kTop.has_value());
  EXPECT_NEAR(kTop->y, 0.0F, kTolerance);
}

TEST(MathProjectionUnit, TheHorizontalEdgeFollowsTheAspectRatio) {
  const std::optional<rgf::math::ScreenPoint> kEdge{
      Project({.x = 10.0F * kAspect, .y = 0.0F, .z = 10.0F})};

  ASSERT_TRUE(kEdge.has_value());
  EXPECT_NEAR(kEdge->x, kWidth, kTolerance);
}

TEST(MathProjectionUnit, NearAndFarDoNotMoveTheScreenPoint) {
  const rgf::math::Vec3 kWorld{.x = 3.0F, .y = -2.0F, .z = 20.0F};
  const rgf::math::Matrix4 kView{
      rgf::math::BuildView(kOrigin, kForward, kUp, kRight)};

  const std::optional<rgf::math::ScreenPoint> kTight{rgf::math::WorldToScreen(
      rgf::math::Multiply(
          kView, rgf::math::BuildProjection(kFov, kAspect, 0.01F, 100.0F)),
      kWorld, kWidth, kHeight)};
  const std::optional<rgf::math::ScreenPoint> kWide{rgf::math::WorldToScreen(
      rgf::math::Multiply(
          kView, rgf::math::BuildProjection(kFov, kAspect, 5.0F, 90000.0F)),
      kWorld, kWidth, kHeight)};

  ASSERT_TRUE(kTight.has_value());
  ASSERT_TRUE(kWide.has_value());
  EXPECT_NEAR(kTight->x, kWide->x, kTolerance);
  EXPECT_NEAR(kTight->y, kWide->y, kTolerance);
}

TEST(MathProjectionUnit, TheEyePositionShiftsTheWorld) {
  const rgf::math::Vec3 kEye{.x = 0.0F, .y = 0.0F, .z = 5.0F};

  const std::optional<rgf::math::ScreenPoint> kPoint{
      Project({.x = 0.0F, .y = 0.0F, .z = 15.0F}, kEye)};

  ASSERT_TRUE(kPoint.has_value());
  EXPECT_NEAR(kPoint->x, kCentreX, kTolerance);
  EXPECT_NEAR(kPoint->y, kCentreY, kTolerance);
}

TEST(MathProjectionUnit, TheBasisIsUsedRatherThanTheWorldAxes) {
  const rgf::math::Vec3 kLookRight{.x = 1.0F, .y = 0.0F, .z = 0.0F};
  const rgf::math::Vec3 kRightOfThat{.x = 0.0F, .y = 0.0F, .z = -1.0F};

  const std::optional<rgf::math::ScreenPoint> kPoint{rgf::math::WorldToScreen(
      rgf::math::Multiply(
          rgf::math::BuildView(kOrigin, kLookRight, kUp, kRightOfThat),
          rgf::math::BuildProjection(kFov, kAspect, kNear, kFar)),
      {.x = 10.0F, .y = 0.0F, .z = 0.0F}, kWidth, kHeight)};

  ASSERT_TRUE(kPoint.has_value());
  EXPECT_NEAR(kPoint->x, kCentreX, kTolerance);
  EXPECT_NEAR(kPoint->y, kCentreY, kTolerance);
}

TEST(MathProjectionUnit, MultiplyMatchesAHandComputedProduct) {
  const rgf::math::Matrix4 kLhs{.cells = {1.0F, 2.0F, 0.0F, 0.0F,  //
                                          0.0F, 1.0F, 0.0F, 0.0F,  //
                                          0.0F, 0.0F, 1.0F, 0.0F,  //
                                          3.0F, 0.0F, 0.0F, 1.0F}};
  const rgf::math::Matrix4 kRhs{.cells = {2.0F, 0.0F, 0.0F, 0.0F,  //
                                          0.0F, 2.0F, 0.0F, 0.0F,  //
                                          0.0F, 0.0F, 2.0F, 0.0F,  //
                                          0.0F, 5.0F, 0.0F, 1.0F}};

  const rgf::math::Matrix4 kProduct{rgf::math::Multiply(kLhs, kRhs)};

  EXPECT_NEAR(kProduct.cells.at(0), 2.0F, kTolerance);
  EXPECT_NEAR(kProduct.cells.at(1), 4.0F, kTolerance);
  EXPECT_NEAR(kProduct.cells.at(5), 2.0F, kTolerance);
  EXPECT_NEAR(kProduct.cells.at(12), 6.0F, kTolerance);
  EXPECT_NEAR(kProduct.cells.at(13), 5.0F, kTolerance);
  EXPECT_NEAR(kProduct.cells.at(15), 1.0F, kTolerance);
}

TEST(MathProjectionUnit, TheOrderOfMultiplicationMatters) {
  const rgf::math::Matrix4 kView{rgf::math::BuildView(
      {.x = 1.0F, .y = 2.0F, .z = 3.0F}, kForward, kUp, kRight)};
  const rgf::math::Matrix4 kProjection{
      rgf::math::BuildProjection(kFov, kAspect, kNear, kFar)};

  const rgf::math::Matrix4 kCorrect{rgf::math::Multiply(kView, kProjection)};
  const rgf::math::Matrix4 kSwapped{rgf::math::Multiply(kProjection, kView)};

  EXPECT_NE(kCorrect.cells, kSwapped.cells);
}
