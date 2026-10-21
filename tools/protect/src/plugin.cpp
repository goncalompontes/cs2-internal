// The protection pass: the plugin entry point and the two passes.
//
// The pass runs in two phases, because the transform needs fully optimised
// IR and the selection needs to happen before the optimiser runs:
//
//   * the *selection* phase runs at the pipeline start
//     (registerPipelineStartEPCallback). It selects the functions, marks
//     them `noinline` so that they survive to the late point, and records
//     the selection in the module's metadata. The module flag
//     `cs2.protect.selected` makes a second run a no-op.
//
//   * the *transform* phase runs at the optimizer last
//     (registerOptimizerLastEPCallback). It rewrites the calls of the
//     selected functions into indirect dispatches through an encrypted
//     descriptor. It has to run there: the optimiser's
//     `SimplifyIndirectCalls` would rewrite the encrypted indirect call
//     straight back into a direct one, and the inliner would have eaten the
//     selected function before a late point could rewrite it. The module
//     flag `cs2.protect.transformed` makes a second run a no-op.
//
// The two flags are per phase on purpose: one flag would make the late
// phase bail out, because the early phase would have set it.
//
// A pass that is only registered through registerPipelineParsingCallback
// would never run unless the pipeline was requested by name, and the
// "protected" build would then be byte-identical to the baseline -- the
// easiest way to ship a no-op protection. The parsing callback exists as
// well, under the name `cs2-protect`, so that `opt` can run the pass over a
// fixture (see tools/protect/README.md).

#include <string>
#include <string_view>

#include "llvm/IR/Function.h"
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
#include "transform.hpp"

namespace {

/// The module flag that marks a module the selection phase already ran on.
///
/// The pipeline-start extension point fires in the LTO pre-link phase as
/// well as in the ordinary compile, so a module can reach the pass twice.
/// The flag makes the second run a no-op: without it, the module would be
/// selected twice.
constexpr std::string_view k_selected_flag = "cs2.protect.selected";

/// The module flag that marks a module the transform phase already ran on.
///
/// The flag is per phase, because the selection phase sets its own flag: one
/// flag would make the late phase always bail out, and the build would
/// produce an unprotected payload that *looks* protected.
constexpr std::string_view k_transformed_flag = "cs2.protect.transformed";

/// Reads the configuration, or fails the compile.
///
/// The configuration is read once per phase: it is the environment that the
/// compiler process hands down to the plugin, and it cannot change between
/// the two phases of one compilation.
[[nodiscard]] auto configuration() -> cs2::protect::Config {
  const auto loaded = cs2::protect::load_config();
  if (!loaded) {
    llvm::errs() << "[cs2-protect] " << loaded.error().message << "\n";
    llvm::report_fatal_error("cs2-protect: the configuration is invalid",
                             /*gen_crash_diag=*/false);
  }
  return *loaded;
}

/// The phase that selects the functions and marks them `noinline`.
class SelectPass : public llvm::PassInfoMixin<SelectPass> {
 public:
  /// The pass must run even when the optimisation pipeline would drop it:
  /// a build that "protects" nothing has to say so.
  static bool isRequired() {
    return true;
  }

  llvm::PreservedAnalyses run(llvm::Module& module, llvm::ModuleAnalysisManager&) {
    if (module.getModuleFlag(k_selected_flag) != nullptr) {
      return llvm::PreservedAnalyses::all();
    }
    // The flag is metadata: the backend ignores it, so it does not reach
    // the object file. The "Warning" behaviour is the gentle one: two
    // modules that both carry the flag agree on its value, so merging them
    // (an LTO build) is quiet.
    module.setModuleFlag(llvm::Module::Warning, k_selected_flag, 1);

    const cs2::protect::Config settings = configuration();

    cs2::protect::SelectionResult result = cs2::protect::select_functions(module, settings);

    // A policy entry that is scoped to this module and matched nothing is a
    // typo: the compile must fail instead of protecting nothing. An entry
    // without a scope may well match in another translation unit, so the
    // build as a whole checks those (tools/protect/check_policy.py).
    //
    // An entry that named a function which cannot be protected (an
    // `alwaysinline` function, a declaration, a denied function) is the
    // same failure with a different cause: the entry protects nothing, and
    // the operator has to know why.
    //
    // An entry that asks for a level that is not implemented yet (the
    // `virtualized` level of T20) is the same failure again: this task does
    // not transform the function, so the entry protects nothing, and a build
    // that looks protected and is not is the failure mode this project
    // exists to avoid.
    for (const cs2::protect::PolicyMatch& match : result.policy_matches) {
      if (match.module.empty() || !match.applicable) {
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
      if (match.unimplemented) {
        // The level that was *applied* to the function, not the level of the
        // entry: the level of a function is the strongest level the entries
        // that name it ask for, so an entry that asks for the call level can
        // name a function that was selected at the virtualized level. The
        // message names the entry and the level that was applied, because
        // "the level 'call' is not implemented" would be a lie: the call
        // level is the one this task implements.
        llvm::errs() << "[cs2-protect] " << module.getSourceFileName() << ": the policy entry '"
                     << match.pattern << "' named a function that is selected at the level '"
                     << cs2::protect::to_string(match.applied_level)
                     << "', which is not implemented yet (T20)\n";
        llvm::report_fatal_error(
            "cs2-protect: a policy entry matched a function whose level is not implemented",
            /*gen_crash_diag=*/false);
      }
      if (!match.matched) {
        llvm::errs() << "[cs2-protect] " << module.getSourceFileName() << ": the policy entry '"
                     << match.pattern << "' matched no function in this module\n";
        llvm::report_fatal_error("cs2-protect: a policy entry matched no function",
                                 /*gen_crash_diag=*/false);
      }
    }

    // The selected functions are marked `noinline`: the inliner runs between
    // the two phases, and a selected function that was inlined into its
    // callers would be gone before the transform phase could rewrite it.
    // The marking changes the generated code, so the report names it.
    for (cs2::protect::Selection& selection : result.selected) {
      if (!selection.may_inline) {
        continue;  // the function is `noinline` already
      }
      llvm::Function* function = module.getFunction(selection.symbol);
      if (function == nullptr) {
        continue;
      }
      function->addFnAttr(llvm::Attribute::NoInline);
      selection.marked_noinline = true;
    }

    // The hand-off to the transform phase.
    cs2::protect::record_selection(module, result);

    cs2::protect::write_report(module, result, settings);

    if (const auto error = cs2::protect::write_coverage(settings, module, result)) {
      llvm::errs() << "[cs2-protect] " << *error << "\n";
      llvm::report_fatal_error("cs2-protect: the coverage could not be written",
                               /*gen_crash_diag=*/false);
    }

    // The pass changes the attributes of the selected functions, so the
    // analyses are not preserved. (The module flag is metadata: it does not
    // reach the object file.)
    return llvm::PreservedAnalyses::none();
  }
};

/// The phase that rewrites the calls of the selected functions.
class TransformPass : public llvm::PassInfoMixin<TransformPass> {
 public:
  /// The pass must run even when the optimisation pipeline would drop it:
  /// a build that "protects" nothing has to say so.
  static bool isRequired() {
    return true;
  }

  llvm::PreservedAnalyses run(llvm::Module& module, llvm::ModuleAnalysisManager&) {
    if (module.getModuleFlag(k_transformed_flag) != nullptr) {
      return llvm::PreservedAnalyses::all();
    }
    module.setModuleFlag(llvm::Module::Warning, k_transformed_flag, 1);

    const cs2::protect::Config settings = configuration();

    const cs2::protect::RecordedSelection selection = cs2::protect::read_selection(module);

    // The strings first: they do not need a selected function, and their
    // verdict has to be in the coverage even when the policy selected no
    // function at all.
    const cs2::protect::StringResult strings =
        cs2::protect::transform_strings(module, settings, selection);
    cs2::protect::write_string_report(module, strings, settings);

    cs2::protect::TransformResult result;
    if (!selection.functions.empty()) {
      result = cs2::protect::transform_module(module, settings, selection);
      cs2::protect::write_transform_report(module, result, settings);
    }

    // The coverage: the final state of every policy entry. An entry whose
    // selected function did not survive to this phase is recorded as
    // vanished, and the whole-build check fails for it.
    const auto entries =
        cs2::protect::coverage_entries(selection, module.getSourceFileName(), result.vanished);
    std::vector<cs2::protect::CoverageFunction> functions;
    functions.reserve(result.functions.size());
    for (const cs2::protect::FunctionTransform& transform : result.functions) {
      functions.push_back(cs2::protect::CoverageFunction{transform.symbol, transform.rewritten,
                                                         transform.skipped.size()});
    }
    cs2::protect::CoverageProtect protect;
    protect.table_size = result.table_size;
    protect.descriptor_size = result.descriptor_size;
    protect.mix_multiplier = result.mix_multiplier;
    protect.mix_xor = result.mix_xor;
    protect.key_parts.assign(result.key_parts.begin(), result.key_parts.end());
    const std::vector<cs2::protect::CoverageString> covered =
        cs2::protect::coverage_strings(strings);
    // The salt of the digests is named in the coverage, and the salt itself
    // is not: the coverage cannot be brute forced back into the strings.
    // The proof tells a reader with a salt whether it is the salt of the
    // build.
    std::string salt;
    if (strings.salted) {
      salt = "build-seed sha256=" + cs2::protect::salt_proof(settings);
    }
    if (const auto error = cs2::protect::write_coverage(settings, module, entries, functions, protect,
                                                        covered, salt)) {
      llvm::errs() << "[cs2-protect] " << *error << "\n";
      llvm::report_fatal_error("cs2-protect: the coverage could not be written",
                               /*gen_crash_diag=*/false);
    }

    // The pass changes the code, so the analyses are not preserved.
    return llvm::PreservedAnalyses::none();
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

// The SDK header declares the entry point without `dllexport`, which makes the
// redeclaration warn.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdll-attribute-on-redeclaration"
extern "C" CS2_PROTECT_EXPORT ::llvm::PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "Cs2Protect", LLVM_VERSION_STRING,
          [](llvm::PassBuilder& builder) {
            // The early phase: the selection, at the start of the pipeline.
            builder.registerPipelineStartEPCallback(
                [](llvm::ModulePassManager& manager, llvm::OptimizationLevel) {
                  manager.addPass(SelectPass());
                });
            // The late phase: the rewrite, after the optimiser.
            builder.registerOptimizerLastEPCallback(
                [](llvm::ModulePassManager& manager, llvm::OptimizationLevel,
                   llvm::ThinOrFullLTOPhase) { manager.addPass(TransformPass()); });
            // The named pipeline, so that `opt` can run the pass over a
            // fixture: `opt -load-pass-plugin=Cs2Protect.dll -passes=cs2-protect`.
            builder.registerPipelineParsingCallback(
                [](llvm::StringRef name, llvm::ModulePassManager& manager,
                   llvm::ArrayRef<llvm::PassBuilder::PipelineElement>) {
                  if (name != "cs2-protect") {
                    return false;
                  }
                  manager.addPass(SelectPass());
                  manager.addPass(TransformPass());
                  return true;
                });
          }};
}
#pragma clang diagnostic pop
