#include "injector.hpp"

int main() {
  return rgf::injector::Run(
      {.processImage = L"Telegram.exe", .dllName = L"rgf_telegram.dll"});
}
