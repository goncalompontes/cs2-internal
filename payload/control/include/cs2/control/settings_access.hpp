#pragma once

// The control channel's platform-independent command helpers: the typed access
// to the live feature settings block (the `settings` command), the batch
// runner (the `batch` command) and the log tail (the `log` command).
//
// They live here, away from the Windows-only `Service`, so the host test
// (`control-core`) can cover them without a game: an unknown key, a wrong
// value type, the changed list, the batch's failure isolation and the batch's
// recursion guard.
//
// The settings block itself is **owned elsewhere** (ADR-0021): the in-process
// overlay menu owns it, and in the Vulkan-layer mode the shared segment does
// (the layer's menu writes it, the payload publishes it). The owner registers
// a reader and a writer here, so a control-side change and a menu-side change
// are the same block.

#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "cs2/control/json.hpp"
#include "cs2/overlay/settings_table.hpp"
#include "cs2/overlay/wire.hpp"

namespace cs2::control {

// A command's error: the message the client sees.
struct CommandError {
  std::string message;
};

// --- The settings access ----------------------------------------------------

// The reader copies the live block into `out`; the writer publishes the
// changed **fields** to the owner, which merges them into its own block under
// its lock.
//
// The write is a **field-wise merge**, not a whole-block replacement: the
// owner (the menu) writes the same block from its own thread, so a
// replacement would both race with it and clobber the fields the request did
// not name — a lost update (ADR-0021).
using SettingsReader = void (*)(void* context, cs2::overlay::wire::Settings& out);
using SettingsWriter = void (*)(void* context, const cs2::overlay::wire::SettingsPatch& patch);
// The readiness probe: whether the owner has published the block yet. A
// control-side write must not touch a block the control channel has never
// seen.
using SettingsReady = bool (*)(void* context);

// The registered access to the live settings block. It is empty until the
// owner registers it; the `settings` command then reports that the block is
// not available yet.
class SettingsAccess {
 public:
  void set(SettingsReader reader, SettingsWriter writer, void* context) noexcept {
    set(reader, writer, nullptr, context);
  }

  void set(SettingsReader reader, SettingsWriter writer, SettingsReady ready,
           void* context) noexcept {
    reader_ = reader;
    writer_ = writer;
    ready_ = ready;
    context_ = context;
  }

  [[nodiscard]] bool available() const noexcept {
    return reader_ != nullptr && writer_ != nullptr;
  }

  // Whether the owner has published the block yet (always true without a
  // readiness probe).
  [[nodiscard]] bool ready() const noexcept {
    return ready_ == nullptr || ready_(context_);
  }

  // Copy the live block into `out`. A no-op when no access is registered.
  void read(cs2::overlay::wire::Settings& out) const noexcept {
    if (reader_ != nullptr) {
      reader_(context_, out);
    }
  }

  // Publish `patch` to the live block: the owner merges the changed fields
  // into its own block. A no-op when no access is registered.
  void write(const cs2::overlay::wire::SettingsPatch& patch) const noexcept {
    if (writer_ != nullptr) {
      writer_(context_, patch);
    }
  }

 private:
  SettingsReader reader_ = nullptr;
  SettingsWriter writer_ = nullptr;
  SettingsReady ready_ = nullptr;
  void* context_ = nullptr;
};

// --- The `settings` command -------------------------------------------------

// What a `settings` request produced: the values the client asked for and the
// keys whose value actually changed.
struct SettingsOutcome {
  Json values = Json::object();
  std::vector<std::string> changed;
  // Whether the request carried a `set` (the `changed` list is only reported
  // then).
  bool wrote = false;
};

// Apply a `settings` request to the live block.
//
// The request is `{"get": [<name>…], "set": {<name>: <value>…}}`; both are
// optional and with neither every field is returned. The keys are the **flat
// wire field names** (`esp_box`), not the config file's `<section>.<field>`
// keys.
//
// The whole request is validated before anything is written, so a typo in one
// key leaves the block untouched (a typo must not look like a successful
// write). The values are written through `access`, i.e. through the block's
// owner.
[[nodiscard]] auto run_settings(const Json& args, SettingsAccess& access)
    -> std::expected<SettingsOutcome, CommandError>;

// --- The `batch` command ----------------------------------------------------

// The batch runner: the "run a script" primitive.
//
// The steps run in order, exactly as if they had been sent one at a time: the
// dispatch callback is the service's own dispatcher, so a step behaves exactly
// like a direct request. A failing step is recorded and the batch continues;
// only a malformed batch (not an array, too many steps, a nested batch, a step
// that is not a request) fails the whole command.
class BatchRunner {
 public:
  // The dispatch callback: run one step and fill `response` with the step's
  // response (`{"ok": …, "result"|"error": …}`). It returns false when the
  // channel must close (a `quit` step).
  using Dispatch = bool (*)(void* context, const Json& step, Json& response);

  // The bounds: at most `k_max_steps` steps, and **no nesting at all** — a
  // step may not be a batch. Exceeding either is an error, never a truncation.
  static constexpr std::size_t k_max_steps = 64;

  // Run the steps of a `batch` request. `results` receives one
  // `{"cmd": …, "ok": …, "result"|"error": …}` per step, in order.
  [[nodiscard]] static auto run(const Json& steps, Dispatch dispatch, void* context, Json& results)
      -> std::expected<void, CommandError>;
};

// --- The `log` command ------------------------------------------------------

// The last `count` lines of a log text, oldest first and without their
// terminator. The file's tail is read by the caller; this is the pure part.
[[nodiscard]] auto log_tail(std::string_view text, std::size_t count) -> std::vector<std::string>;

}  // namespace cs2::control
