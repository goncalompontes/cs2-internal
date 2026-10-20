// The protection pass: the plugin entry point and the pass itself.
//
// The pass runs at the *start* of the optimisation pipeline of every
// translation unit (registerPipelineStartEPCallback), so it is part of a
// normal -O2 compile. A pass that is only registered through
// registerPipelineParsingCallback would never run unless the pipeline was
// requested by name, and the "protected" build would then be byte-identical
// to the baseline -- the easiest way to ship a no-op protection.
//
// This task only *selects* and *reports*; the transforms (call encryption,
// virtualization) are T19 and T20.
//
// The *rewriting* pass of T19 cannot run here: it needs
// registerOptimizerLastEPCallback, because the optimiser would undo an
// encrypted indirect call (SimplifyIndirectCalls) and would inline a
// selected function away before a late point could rewrite it. See the
// "For the next task" section of tools/protect/README.md.

#include <string>

#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Plugins/PassPlugin.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

#include "config.hpp"
#include "report.hpp"
#include "selection.hpp"

namespace {

/// The module flag that marks a module the pass already ran on.
///
/// The pipeline-start extension point fires in the LTO pre-link phase as
/// well as in the ordinary compile, so a module can reach the pass twice.
/// The flag makes the second run a no-op: without it, T19 would rewrite the
/// module twice.
constexpr std::string_view k_done_flag = "cs2.protect.done";

class ProtectPass : public llvm::PassInfoMixin<ProtectPass> {
 public:
  /// The pass must run even when the optimisation pipeline would drop it:
  /// a build that "protects" nothing has to say so.
  static bool isRequired() {
    return true;
  }

  llvm::PreservedAnalyses run(llvm::Module& module, llvm::ModuleAnalysisManager&) {
    if (module.getModuleFlag(k_done_flag) != nullptr) {
      return llvm::PreservedAnalyses::all();
    }
    // The flag is metadata: the backend ignores it, so it does not reach
    // the object file. The "Warning" behaviour is the gentle one: two
    // modules that both carry the flag agree on its value, so merging them
    // (an LTO build) is quiet.
    module.setModuleFlag(llvm::Module::Warning, k_done_flag, 1);

    const auto config = cs2::protect::load_config();
    if (!config) {
      llvm::errs() << "[cs2-protect] " << config.error().message << "\n";
      llvm::report_fatal_error("cs2-protect: the configuration is invalid",
                               /*gen_crash_diag=*/false);
    }
    const cs2::protect::Config& settings = *config;

    const auto result = cs2::protect::select_functions(module, settings);

    // A policy entry that is scoped to this module and matched nothing is a
    // typo: the compile must fail instead of protecting nothing. An entry
    // without a scope may well match in another translation unit, so the
    // build as a whole checks those (tools/protect/check_policy.py).
    //
    // An entry that named a function which cannot be protected (an
    // `alwaysinline` function, a declaration, a denied function) is the
    // same failure with a different cause: the entry protects nothing, and
    // the operator has to know why.
    for (const cs2::protect::PolicyMatch& match : result.policy_matches) {
      if (match.module.empty() || !match.applicable || match.matched) {
        continue;
      }
      if (match.blocked) {
        llvm::errs() << "[cs2-protect] " << module.getSourceFileName() << ": the policy entry '"
                     << match.pattern << "' matched a function that cannot be protected ("
                     << cs2::protect::to_string(*match.blocked_reason) << ")\n";
        llvm::report_fatal_error(
            "cs2-protect: a policy entry matched a function that cannot be protected",
            /*gen_crash_diag=*/false);
      }
      llvm::errs() << "[cs2-protect] " << module.getSourceFileName() << ": the policy entry '"
                   << match.pattern << "' matched no function in this module\n";
      llvm::report_fatal_error("cs2-protect: a policy entry matched no function",
                               /*gen_crash_diag=*/false);
    }

    cs2::protect::write_report(module, result, settings);

    if (const auto error = cs2::protect::write_coverage(settings, module, result)) {
      llvm::errs() << "[cs2-protect] " << *error << "\n";
      llvm::report_fatal_error("cs2-protect: the coverage could not be written",
                               /*gen_crash_diag=*/false);
    }

    // The pass only reports; it changes no code, so everything is preserved.
    // (The module flag is metadata: it does not reach the object file.)
    return llvm::PreservedAnalyses::all();
  }
};

}  // namespace

// The plugin loader resolves `llvmGetPassPluginInfo` by name with
// GetProcAddress on Windows, so the entry point has to be exported explicitly.
#if defined(_WIN32)
#define CS2_PROTECT_EXPORT __declspec(dllexport)
#else
#define CS2_PROTECT_EXPORT __attribute__((visibility("default")))
#endif

// The SDK header declares the entry point without `dllexport`, which makes
// the redeclaration warn.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdll-attribute-on-redeclaration"
extern "C" CS2_PROTECT_EXPORT ::llvm::PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "Cs2Protect", LLVM_VERSION_STRING,
          [](llvm::PassBuilder& builder) {
            builder.registerPipelineStartEPCallback(
                [](llvm::ModulePassManager& manager, llvm::OptimizationLevel) {
                  manager.addPass(ProtectPass());
                });
          }};
}
#pragma clang diagnostic pop
