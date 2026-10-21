#pragma once

#include <optional>
#include <rgf/integrations/fc3/fc3.hpp>
#include <rgf/math/matrix4.hpp>
#include <rgf/math/projection.hpp>
#include <rgf/math/screen.hpp>
#include <rgf/math/vec3.hpp>

namespace rgf::esp {

constexpr float kDrawDistance{150.0F};

[[nodiscard]] inline std::optional<math::ScreenRect> ProjectBox(
    const integrations::fc3::EntityInfo& entity,
    const math::Matrix4& projection, float width, float height) noexcept {
  return math::ProjectCorners(entity.box, entity.transform, projection, width,
                              height);
}

}  // namespace rgf::esp
