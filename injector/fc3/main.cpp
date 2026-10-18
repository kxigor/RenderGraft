#include "injector.hpp"

int main() {
  return rgf::injector::Run(
      {.processImage = L"farcry3_d3d11.exe", .dllName = L"aimbot_fc3.dll"});
}
