#include <imgui.h>

#include <expected>
#include <optional>
#include <rgf/config/config.hpp>
#include <rgf/esp/esp.hpp>
#include <rgf/esp/internal/draw.hpp>
#include <rgf/esp/internal/panel.hpp>
#include <rgf/esp/internal/projection.hpp>
#include <rgf/integrations/fc3/fc3.hpp>
#include <system_error>

namespace rgf::esp {

namespace {

constexpr float kNearPlane{0.1F};
constexpr float kFarPlane{10000.0F};
constexpr float kAheadDistance{2.0F};
constexpr float kProbeDistance{10.0F};
constexpr float kProbeOffset{2.0F};
constexpr float kPanelWidth{540.0F};
constexpr float kPanelHeight{1180.0F};

constexpr ImU32 kAheadColour{0xFF00FF00};
constexpr ImU32 kRightColour{0xFF0080FF};

struct Markers {
  std::optional<math::ScreenPoint> ahead;
  std::optional<math::ScreenPoint> probe;
};

[[nodiscard]] Markers ProjectMarkers(const math::Matrix4& projection,
                                     const math::Vec3& eye,
                                     const math::Vec3& forward,
                                     const math::Vec3& right,
                                     const ImVec2& display) noexcept {
  return Markers{
      .ahead = math::WorldToScreen(projection, eye + (forward * kAheadDistance),
                                   display.x, display.y),
      .probe = math::WorldToScreen(
          projection, eye + (forward * kProbeDistance) + (right * kProbeOffset),
          display.x, display.y)};
}

void ShowPanelBody(const math::Vec3& eye, const math::Vec3& forward,
                   const ImVec2& display, const Markers& markers) {
  ShowMarkers(display, markers.ahead, markers.probe);
  ShowPools(eye);
  ShowEntities(eye, forward);
  ShowVtables();
}

void ShowPanel(const integrations::fc3::CameraBasis& camera,
               const ImVec2& display, const Markers& markers) {
  const float kAspect{display.x / display.y};

  ImGui::SetNextWindowSize(ImVec2{kPanelWidth, kPanelHeight},
                           ImGuiCond_FirstUseEver);
  ImGui::Begin("ESP");
  ShowChain(integrations::fc3::Diagnostics::Read(), display);
  ImGui::SeparatorText("camera");
  ShowCamera(camera, camera.right, kAspect,
             math::VerticalFov(camera.fovHorizontalRadians, kAspect));
  ShowPanelBody(camera.position, camera.forward, display, markers);
  ImGui::End();
}

void ShowNoCamera(const char* reason) {
  ImGui::Begin("ESP");
  ImGui::TextUnformatted(reason);
  ImGui::End();
}

}  // namespace

void DrawFrame() noexcept {
  if (!config::Current().espEnabled.load()) {
    return;
  }

  const bool kPanel{config::Current().espPanel.load()};
  const ImVec2 kDisplay{ImGui::GetIO().DisplaySize};
  const std::expected<integrations::fc3::CameraBasis, std::error_code> kCamera{
      integrations::fc3::ReadCamera()};
  if (!kCamera) {
    if (kPanel) {
      ShowNoCamera(kCamera.error().message().c_str());
    }
    return;
  }
  if (kDisplay.x <= 0.0F || kDisplay.y <= 0.0F) {
    if (kPanel) {
      ShowNoCamera("the imgui display size is empty");
    }
    return;
  }

  const float kAspect{kDisplay.x / kDisplay.y};
  const float kVerticalFov{
      math::VerticalFov(kCamera->fovHorizontalRadians, kAspect)};
  const math::Matrix4 kViewProjection{Multiply(
      math::BuildView(kCamera->position, kCamera->forward, kCamera->upAxis,
                      kCamera->right),
      math::BuildProjection(kVerticalFov, kAspect, kNearPlane, kFarPlane))};
  const Markers kMarkers{ProjectMarkers(kViewProjection, kCamera->position,
                                        kCamera->forward, kCamera->right,
                                        kDisplay)};

  if (kPanel) {
    ShowPanel(*kCamera, kDisplay, kMarkers);
  }

  DrawEntities(kCamera->position, kViewProjection, kDisplay);
  DrawAimCone(kDisplay, kVerticalFov);
  DrawCrosshair(kMarkers.ahead, kAheadColour);
}

}  // namespace rgf::esp
