// Trace.java — map the physics-query call sites: for a set of target functions,
// list every caller and decompile the callers. Also list functions in a range.
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.*;

import ghidra.app.decompiler.DecompileResults;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceManager;

public class Trace extends GhidraScript {

  // query-method name-string xrefs (inside each function) on client 659940c1
  private static final long[] TARGETS = {
      0x180a1a06eL, // TraceShape name xref
      0x180a1afd6L, // TraceShapeWithSortedResults name xref
      0x180a19ef2L, // TraceLineIntervals name xref
      0x180a1aad8L, // TraceShapeToEntity name xref
  };

  @Override
  public void run() throws Exception {
    DecompInterface decompiler = new DecompInterface();
    decompiler.toggleCCode(true);
    decompiler.toggleSyntaxTree(true);
    decompiler.openProgram(currentProgram);

    StringBuilder out = new StringBuilder();
    out.append("program: ").append(currentProgram.getName()).append('\n');
    out.append("md5: ").append(currentProgram.getExecutableMD5()).append('\n');
    out.append("imageBase: ").append(currentProgram.getImageBase()).append('\n');

    for (long t : TARGETS) {
      Function f = getFunctionContaining(toAddr(t));
      out.append("\n#### target ").append(Long.toHexString(t)).append(" -> ");
      if (f == null) { out.append("(no function)\n"); continue; }
      out.append(f.getName()).append(" @ ").append(f.getEntryPoint()).append('\n');
    }

    // Callers of the query methods (resolve entry from the name xref).
    Set<Long> targetEntries = new LinkedHashSet<>();
    for (long t : TARGETS) {
      Function f = getFunctionContaining(toAddr(t));
      if (f != null) targetEntries.add(f.getEntryPoint().getOffset());
    }

    ReferenceManager rm = currentProgram.getReferenceManager();
    Set<Function> callers = new LinkedHashSet<>();
    for (long entry : targetEntries) {
      out.append("\n==== callers of ").append(Long.toHexString(entry)).append(" ====\n");
      for (Reference r : rm.getReferencesTo(toAddr(entry))) {
        Function cf = getFunctionContaining(r.getFromAddress());
        out.append("  ").append(r.getFromAddress());
        if (cf != null) {
          out.append("  in ").append(cf.getName()).append(" @ ").append(cf.getEntryPoint());
          callers.add(cf);
        }
        out.append('\n');
      }
    }

    for (Function cf : callers) {
      out.append("\n######## ").append(cf.getName()).append(" @ ").append(cf.getEntryPoint()).append(" ########\n");
      DecompileResults results = decompiler.decompileFunction(cf, 180, monitor);
      if (results != null && results.decompileCompleted() && results.getDecompiledFunction() != null) {
        out.append(results.getDecompiledFunction().getC());
      } else {
        out.append("decompile failed\n");
      }
      out.append('\n');
    }

    try (PrintWriter w = new PrintWriter(new FileWriter(
        "C:/Users/gpmpo/cs2-internal/artifacts/trace_callers.txt"))) {
      w.print(out.toString());
      w.flush();
    }
    println("Trace: wrote " + out.length() + " chars");
  }
}
