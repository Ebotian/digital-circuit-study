#include <cstddef>
#include <cstdint>
#include <string>

#include "core/json.hpp"

// libFuzzer for the ordered JSON parser used by the CLI and the Z3 protocol.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  if (size > (1u << 20)) return 0;
  try {
    const dcstudy::Json j =
        dcstudy::Json::parse(std::string(reinterpret_cast<const char*>(data), size));
    (void)j.dump();
  } catch (...) {
  }
  return 0;
}
