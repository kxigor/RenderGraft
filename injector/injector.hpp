#pragma once

#include <windows.h>

namespace afw::injector {

// TODO: Model Run and Target as one cohesive abstraction, distinguish the target
// process from the injected DLL, and make ownership explicit.
struct Target {
  PCWSTR processImage;
  PCWSTR dllName;
};

int Run(const Target& target);

}  // namespace afw::injector
