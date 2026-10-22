// Readable reconstruction of the inspected client, not a replacement engine implementation.
// Client identity and exact RVAs: client-audit.json. Unresolved helpers remain explicit.
#include <cmath>

float add_landing_penalty(float penalty, float landing_scale, float weapon_land_inaccuracy) {
  // RVA 0x81fc70; virtual getter +0xb58 resolves to the firing-mode VData land field.
  return penalty + landing_scale * weapon_land_inaccuracy;
}

float recover_one_accuracy_update(float penalty, float current_baseline, float recovery_time) {
  // RVA 0x827be0. Baseline is selected from grounded/crouched/air/ladder/reload state.
  // Assembly 0x827de0..0x827e3e: V_logf(10), divide by recovery_time,
  // negate, multiply literal 0.015625, V_expf, interpolate toward baseline.
  // Domain handling for invalid recovery_time is not reconstructed here.
  if (current_baseline > penalty) return current_baseline;
  return current_baseline + (penalty - current_baseline) *
      std::exp(-std::log(10.0f) * (1.0f / 64.0f) / recovery_time);
}

float compose_inaccuracy(float updated_penalty, float movement_component,
                        float airborne_component, float turning_component,
                        float additional_weapon_component) {
  // Normal path of RVA 0x80c1a0, simplified; debug overrides and special weapon
  // branches are omitted. Spread is obtained separately (RVA 0x80d490).
  return std::fmin(1.0f, updated_penalty + movement_component + airborne_component +
                        turning_component + additional_weapon_component);
}
