#ifndef EC_CONTROLLER_CONTROLLER_MODES_H_
#define EC_CONTROLLER_CONTROLLER_MODES_H_

#include <cstdint>
#include <string_view>

namespace ec
{
inline std::string_view controllerForMode(uint8_t mode)
{
  switch (mode) {
    case 2: return "stand_controller";
    case 3: return "walk_controller";
    case 4: return "mimic_controller";
    default: return "default_controller";
  }
}

inline bool transitionAllowed(std::string_view from, std::string_view to)
{
  return (from == "default_controller" && to == "stand_controller") ||
         (from == "stand_controller" && (to == "walk_controller" ||
           to == "mimic_controller" || to == "default_controller")) ||
         (from == "walk_controller" && (to == "mimic_controller" || to == "default_controller")) ||
         (from == "mimic_controller" && (to == "stand_controller" ||
           to == "walk_controller" || to == "default_controller"));
}
}  // namespace ec
#endif
