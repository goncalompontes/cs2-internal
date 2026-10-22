#include <cstdio>
#include "cs2/control/aim_options.hpp"
namespace {
int failures = 0;
void expect(bool v, const char* text) {
  if (!v) {
    std::printf("FAIL: %s\n", text);
    ++failures;
  }
}
}  // namespace
auto main() -> int {
  using namespace cs2::control;
  cs2::aim::SolveInput input;
  std::string error;
  auto apply = [&](const char* text) {
    const auto args = Json::parse(text);
    return args && apply_aim_options(*args, input, error);
  };
  expect(apply(R"({"spread":0.02,"inaccuracy":0.01,"min_damage":40,"min_probability":0.8})"),
         "spread profile parses");
  expect(input.kernel.size() == 64 && input.spread_known && input.policy.minimum_damage == 40,
         "profile produces64 weighted nodes");
  expect(!apply(R"({"kernel":[[0,0,-1]]})") && input.kernel.size() == 64,
         "bad kernel does not partially replace profile");
  expect(!apply(R"({"min_probability":2,"min_damage":90})") && input.policy.minimum_damage == 40,
         "bad policy is transactional");
  expect(apply(R"({"kernel":[[0.1,0,3],[0,0,1]],"require_spread":true})"),
         "explicit weighted offsets parse");
  expect(input.kernel.size() == 2 && input.policy.require_known_spread,
         "explicit kernel replaces distribution");
  expect(!apply(R"({"kernel":[[0,0,0]]})"), "zero total weight rejected");
  expect(!apply(R"({"kernel":[]})"), "empty explicit kernel is not verified zero spread");
  expect(apply(R"({"spread":0,"inaccuracy":0})") && input.kernel.size() == 1,
         "explicit zero spread uses one node");
  expect(
      apply(
          R"({"world":[{"mins":[1,-1,-1],"maxs":[2,1,1]}],"world_complete":true,"require_world":true})"),
      "opaque world model parses");
  expect(input.world.size() == 1 && input.world_complete && input.world[0].retained_per_unit == 0,
         "opaque is the default");
  expect(!apply(R"({"world":[{"mins":[2,0,0],"maxs":[1,1,1]}]})") && input.world_complete,
         "bad world does not replace known model");
  expect(apply(R"({"world":[]})") && !input.world_complete && input.world.empty(),
         "new world resets completeness claim");
  expect(!apply(R"({"spread":-1})"), "negative spread rejected");
  expect(!apply("[]"), "object options required");
  std::printf("aim options: %d failures\n", failures);
  return failures == 0 ? 0 : 1;
}
