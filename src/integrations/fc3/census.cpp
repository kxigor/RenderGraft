#include <bit>
#include <cstdint>
#include <expected>
#include <optional>
#include <rgf/integrations/fc3/census.hpp>
#include <rgf/integrations/fc3/internal/agent.hpp>
#include <rgf/integrations/fc3/internal/bones.hpp>
#include <rgf/integrations/fc3/internal/class_registry.hpp>
#include <rgf/integrations/fc3/internal/components.hpp>
#include <rgf/integrations/fc3/internal/entity.hpp>
#include <rgf/integrations/fc3/internal/layout.hpp>
#include <rgf/integrations/fc3/internal/limits.hpp>
#include <rgf/integrations/fc3/internal/module.hpp>
#include <rgf/integrations/fc3/internal/read.hpp>
#include <rgf/integrations/fc3/internal/snapshot.hpp>
#include <rgf/integrations/fc3/module.hpp>
#include <rgf/integrations/fc3/snapshot.hpp>
#include <rgf/process/memory.hpp>

namespace rgf::integrations::fc3 {

namespace {

void AcceptEntity(std::uintptr_t address, EntitySnapshot& snapshot,
                  std::uint32_t typeBit) noexcept {
  const std::optional<RawEntity> kRaw{ReadEntity(address)};
  if (!kRaw) {
    ++snapshot.unreadable;
    snapshot.CountByType(typeBit, false);
    return;
  }

  std::expected<EntityInfo, Rejection> classified{Classify(*kRaw)};
  if (!classified) {
    ++snapshot.rejected.at(static_cast<std::size_t>(classified.error()));
    snapshot.CountByType(typeBit, false);
    if (classified.error() == Rejection::NotRigid) {
      snapshot.SampleNotRigid(address);
    }
    return;
  }

  snapshot.CountByType(typeBit, true);

  classified->pawn = ReadPawn(address);
  if (classified->pawn) {
    const BonePoints kBones{ReadBonePoints(address)};
    classified->body = kBones.body;
    if (classified->pawn->kind == AgentKind::Pawn) {
      classified->head = kBones.head;
    }
  }

  snapshot.items.at(snapshot.count) = *classified;
  ++snapshot.count;
  if (classified->pawn) {
    ++snapshot.pawns;
  }
  if (classified->humanoid) {
    ++snapshot.humanoids;
  }
}

void StoreEntity(std::uintptr_t address, EntitySnapshot& snapshot,
                 std::uint32_t typeBit) noexcept {
  if (IsEntityClass(address)) {
    AcceptEntity(address, snapshot, typeBit);
    return;
  }

  const std::optional<std::uintptr_t> kOwner{OwnerOfComponent(address)};
  if (!kOwner) {
    ++snapshot.foreignClass;
    snapshot.CountByType(typeBit, false);
    return;
  }
  if (AlreadySeen(*kOwner)) {
    return;
  }

  ++snapshot.viaComponent;
  AcceptEntity(*kOwner, snapshot, typeBit);
}

[[nodiscard]] std::uint32_t CollectGroup(std::uintptr_t group) noexcept {
  const std::optional<std::uintptr_t> kEntityGroup{
      Engine{group}.Pointer(kGroupEntityGroup)};
  if (!kEntityGroup) {
    return 0;
  }

  const std::optional<EngineArray> kEntities{EngineArray::At(
      *kEntityGroup, kEntityGroupArray, kEntityGroupCount, kMaxGroupEntities)};
  if (!kEntities) {
    return 0;
  }

  const std::uint32_t kTypeBit{
      Engine{*kEntityGroup}.FieldOr<std::uint32_t>(
          kEntityGroupTypeBit, static_cast<std::uint32_t>(kGroupTypes)) &
      static_cast<std::uint32_t>(kGroupTypes - 1)};

  EntitySnapshot& snapshot{WritingSnapshot()};

  for (std::uint32_t index{0}; index < kEntities->Size(); ++index) {
    if (snapshot.count >= kMaxEntities) {
      snapshot.dropped += kEntities->Size() - index;
      break;
    }

    const std::optional<std::uintptr_t> kEntity{kEntities->Slot(index)};
    if (!kEntity || AlreadySeen(*kEntity)) {
      continue;
    }

    StoreEntity(*kEntity, snapshot, kTypeBit);
  }

  return kEntities->Size();
}

}  // namespace

void TakeCensus(std::uintptr_t scheduler) noexcept {
  OpenGenerationIfWanted();

  EntityCensus& census{WritingCensus()};
  ++census.calls;

  const Engine kScheduler{scheduler};
  const std::optional<std::uintptr_t> kGroups{
      kScheduler.Pointer(kSchedulerGroups)};
  const std::optional<std::uint32_t> kWanted{
      kScheduler.Field<std::uint32_t>(kSchedulerGroupCount)};
  if (!kGroups || !kWanted) {
    return;
  }

  const std::uint32_t kVisited{*kWanted < kMaxGroups ? *kWanted : kMaxGroups};
  census.skipped += *kWanted - kVisited;

  for (std::uint32_t index{0}; index < kVisited; ++index) {
    const std::optional<std::uintptr_t> kGroup{
        Engine{*kGroups}.Pointer(index * sizeof(EnginePtr))};
    if (!kGroup) {
      continue;
    }

    const std::uint32_t kEntities{CollectGroup(*kGroup)};
    if (kEntities == 0) {
      continue;
    }

    ++census.groups;
    census.entities += kEntities;
  }

  const process::RegionStats kStats{process::ReadableRegionStats()};
  EntitySnapshot& snapshot{WritingSnapshot()};
  snapshot.regionQueries = kStats.queries;
  snapshot.regionHits = kStats.hits;
}

}  // namespace rgf::integrations::fc3
