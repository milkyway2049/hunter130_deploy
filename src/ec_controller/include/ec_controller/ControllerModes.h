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
    case 5: return "quest_controller";
    case 4: return "mimic_controller";
    default: return "default_controller";
  }
}

inline bool transitionAllowed(std::string_view from, std::string_view to)
{
  if (to == "default_controller") {return from != "default_controller";}
  if (from == "default_controller") {return to == "stand_controller";}
  if (from == "stand_controller") {
    return to == "walk_controller" || to == "mimic_controller" || to == "quest_controller";
  }
  if (from == "walk_controller") {return to == "mimic_controller" || to == "quest_controller";}
  if (from == "mimic_controller" || from == "quest_controller") {
    return to == "stand_controller" || to == "walk_controller";
  }
  return false;
}
}  // namespace ec
#endif
