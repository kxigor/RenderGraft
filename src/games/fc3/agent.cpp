#include <array>
#include <bit>
#include <cstdint>
#include <optional>
#include <rgf/abi/abi.hpp>
#include <rgf/games/fc3/internal/agent.hpp>
#include <rgf/games/fc3/internal/class_registry.hpp>
#include <rgf/games/fc3/internal/components.hpp>
#include <rgf/games/fc3/internal/layout.hpp>
#include <rgf/games/fc3/internal/module.hpp>
#include <rgf/games/fc3/internal/pawn.hpp>
#include <rgf/games/fc3/internal/read.hpp>
#include <rgf/games/fc3/module.hpp>

namespace rgf::games::fc3 {

namespace {

using AiComponentFn = void*(RGF_ENGINE_CALL*)(void* self);

constexpr std::array<AgentKind, kAgentKindCount> kAgentKinds{
    AgentKind::Pawn, AgentKind::Animal, AgentKind::Vehicle,
    AgentKind::SimpleAnimal};

[[nodiscard]] std::optional<std::uintptr_t> AgentFromComponents(
    std::uintptr_t address) noexcept {
  return FirstComponent(address, [](std::uintptr_t part) {
    return Engine{part}.Pointer(kComponentAgent);
  });
}

[[nodiscard]] bool WouldBuildHashTable(std::uintptr_t address) noexcept {
  const Engine kEntity{address};
  const std::optional<std::uint32_t> kFlags{
      kEntity.Field<std::uint32_t>(kEntityFlags)};
  if (!kFlags || (*kFlags & kEntityHashTableFlag) == 0) {
    return false;
  }

  return !kEntity.Pointer(kEntityHashTable);
}

[[nodiscard]] std::optional<std::uintptr_t> FindAgent(
    std::uintptr_t address) noexcept {
  const std::uintptr_t kAccessor{Resolve(kGetAiComponent)};
  if (kAccessor == 0) {
    return AgentFromComponents(address);
  }
  if (!IsEntityClass(address) || WouldBuildHashTable(address)) {
    return std::nullopt;
  }

  const AiComponentFn kGetComponent{std::bit_cast<AiComponentFn>(kAccessor)};
  const void* const kComponent{kGetComponent(std::bit_cast<void*>(address))};
  if (kComponent == nullptr) {
    return std::nullopt;
  }

  return Engine{std::bit_cast<std::uintptr_t>(kComponent)}.Pointer(
      kComponentAgent);
}

[[nodiscard]] std::optional<AgentKind> AgentClassOf(
    std::uintptr_t agent) noexcept {
  for (const AgentKind kKind : kAgentKinds) {
    if (AgentProbe(kKind).Matches(agent)) {
      return kKind;
    }
  }

  return std::nullopt;
}

}  // namespace

std::optional<PawnInfo> ReadPawn(std::uintptr_t address) noexcept {
  const std::optional<std::uintptr_t> kAgent{FindAgent(address)};
  if (!kAgent) {
    return std::nullopt;
  }

  const std::optional<AgentKind> kKind{AgentClassOf(*kAgent)};
  if (!kKind) {
    return std::nullopt;
  }

  return ReadAgent(*kAgent, *kKind);
}

}  // namespace rgf::games::fc3
