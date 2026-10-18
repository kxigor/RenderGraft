#pragma once

// Where things sit in the game's memory, split by the subject each source file
// works on. Everything here is a fact about the binary; what counts as a
// plausible value read from it lives in limits.hpp, and the byte patterns the
// weapon patches match on live in signatures.hpp.

#include <rgf/games/fc3/internal/layout/agent.hpp>
#include <rgf/games/fc3/internal/layout/bones.hpp>
#include <rgf/games/fc3/internal/layout/camera.hpp>
#include <rgf/games/fc3/internal/layout/collision.hpp>
#include <rgf/games/fc3/internal/layout/entity.hpp>
#include <rgf/games/fc3/internal/layout/module.hpp>
#include <rgf/games/fc3/internal/layout/scheduler.hpp>
