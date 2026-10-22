#pragma once
#include <string>
#include "cs2/aim/solver.hpp"
#include "cs2/control/json.hpp"
namespace cs2::control {
// Transactional parsing of field model/acceptance controls shared by aim/rage.
[[nodiscard]] auto apply_aim_options(const Json& args, aim::SolveInput& input, std::string& error)
    -> bool;
}  // namespace cs2::control
