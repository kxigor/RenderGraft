#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <rgf/esp/internal/panel.hpp>
#include <rgf/esp/internal/projection.hpp>
#include <rgf/integrations/fc3/fc3.hpp>

namespace rgf::esp {

namespace {

constexpr std::size_t kVtableSlots{12};

struct VtableTally {
  std::uintptr_t vtable{};
  std::uint32_t count{};
};

struct VtableReport {
  std::array<VtableTally, kVtableSlots> rows{};
  std::uint32_t overflow{};
};

[[nodiscard]] VtableReport CollectVtables() noexcept {
  const integrations::fc3::EntitySnapshot& kSnapshot{
      integrations::fc3::LastSnapshot()};

  VtableReport report{};
  for (const integrations::fc3::EntityInfo& kEntity :
       integrations::fc3::Entities(kSnapshot)) {
    const std::uintptr_t kVtable{kEntity.vtable};

    bool placed{false};
    for (VtableTally& row : report.rows) {
      if (row.count == 0) {
        row = VtableTally{.vtable = kVtable, .count = 1};
        placed = true;
        break;
      }
      if (row.vtable == kVtable) {
        ++row.count;
        placed = true;
        break;
      }
    }

    if (!placed) {
      ++report.overflow;
    }
  }

  std::ranges::sort(report.rows, std::ranges::greater{}, &VtableTally::count);

  return report;
}

}  // namespace

void ShowVtables() {
  const VtableReport kReport{CollectVtables()};

  ImGui::SeparatorText("vtables");
  for (const VtableTally& kRow : kReport.rows) {
    if (kRow.count == 0) {
      break;
    }
    ImGui::Text("%-18llX %5u", static_cast<std::uint64_t>(kRow.vtable),
                kRow.count);
  }

  if (kReport.overflow != 0) {
    ImGui::Text("%-18s %u", "beyond the table", kReport.overflow);
  }
}

void ShowPools(const math::Vec3& eye) {
  const integrations::fc3::EntitySnapshot& kSnapshot{
      integrations::fc3::LastSnapshot()};

  std::uintptr_t low{0};
  std::uintptr_t high{0};
  std::uint32_t nearby{0};

  for (const integrations::fc3::EntityInfo& kEntity :
       integrations::fc3::Entities(kSnapshot)) {
    if (low == 0 || kEntity.address < low) {
      low = kEntity.address;
    }
    high = std::max(kEntity.address, high);
    if ((kEntity.transform.Translation() - eye).Length() <= kDrawDistance) {
      ++nearby;
    }
  }

  ImGui::SeparatorText("pools");
  ImGui::Text("%-18s %llX .. %llX", "entity addresses",
              static_cast<std::uint64_t>(low),
              static_cast<std::uint64_t>(high));
  ImGui::Text("%-18s %u  of %u", "within range", nearby, kSnapshot.count);
}

}  // namespace rgf::esp
