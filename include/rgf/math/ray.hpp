#pragma once

#include <rgf/math/vec3.hpp>
#include <type_traits>

namespace rgf::math {

struct Ray {
  Vec3 origin{};
  Vec3 direction{};
};

static_assert(std::is_trivially_copyable_v<Ray>);

}  // namespace rgf::math
