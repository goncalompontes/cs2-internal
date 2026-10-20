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

class ProtectPass : public llvm::PassInfoMixin<ProtectPass> {
 public:
  llvm::PreservedAnalyses run(llvm::Module& module, llvm::ModuleAnalysisManager&) {
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
    for (const cs2::protect::PolicyMatch& match : result.policy_matches) {
      if (match.module.empty() || !match.applicable || match.matched) {
        continue;
      }
      llvm::errs() << "[cs2-protect] " << module.getSourceFileName() << ": the policy entry '"
                   << match.pattern << "' matched no function in this module\n";
      llvm::report_fatal_error("cs2-protect: a policy entry matched no function",
                               /*gen_crash_diag=*/false);
    }

    cs2::protect::write_report(module, result, settings);

    if (const auto error =
            cs2::protect::write_coverage(settings, module.getSourceFileName(), result)) {
      llvm::errs() << "[cs2-protect] " << *error << "\n";
      llvm::report_fatal_error("cs2-protect: the coverage could not be written",
                               /*gen_crash_diag=*/false);
    }

    // The pass only reports; it changes nothing, so everything is preserved.
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
