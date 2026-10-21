#include <rgf/config/config.hpp>

namespace rgf::config {

Settings& Current() noexcept {
  static Settings settings{};
  return settings;
}

}  // namespace rgf::config
