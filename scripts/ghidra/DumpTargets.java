// DumpTargets.java — decompile a fixed list of addresses to a text file.
//
// Run through analyzeHeadless against the persistent project that mirrors the
// REA Ghidra cache (REA's own temp-project reopen is broken on this host):
//   analyzeHeadless <projdir> rea-project -process target-<sha>.dll -noanalysis \
//     -scriptPath scripts/ghidra -postScript DumpTargets.java
import java.io.FileWriter;
import java.io.PrintWriter;

import ghidra.app.decompiler.DecompileResults;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

public class DumpTargets extends GhidraScript {

  // Addresses inside the functions of interest (the name-string xrefs recovered
  // from the build). `getFunctionContaining` resolves the entry.
  private static final long[] TARGETS = {
      0x1809dba13L, // CGamePhysicsQueryInterface::EntitiesAlongLinearSweep
      0x1809dc147L, // CGamePhysicsQueryInterface::EntitiesInShape
      0x1809df508L, // CGamePhysicsQueryInterface::FilterOverlapResults
      0x1809d2dc0L, // sweep tail converter (overlap records)
      0x1809d2c40L, // sweep tail converter (hitbox overlap records)
      // callers of the sweep / EntitiesInShape (to find the singleton global load)
      0x1809c6891L,
      0x1809c6cb0L,
      0x1809c0fafL,
      0x180cee3f9L,
      0x1807a80a7L,
  };

  @Override
  public void run() throws Exception {
    DecompInterface decompiler = new DecompInterface();
    decompiler.toggleCCode(true);
    decompiler.toggleSyntaxTree(true);
    decompiler.openProgram(currentProgram);

    StringBuilder out = new StringBuilder();
    for (long target : TARGETS) {
      Address address = toAddr(target);
      Function function = getFunctionContaining(address);
      if (function == null) {
        out.append("== NO FUNCTION containing ").append(Long.toHexString(target)).append(" ==\n\n");
        continue;
      }
      out.append("== ").append(function.getName()).append(" @ ")
          .append(function.getEntryPoint()).append(" ==\n");
      out.append("CALLERS:");
      ghidra.program.model.symbol.Reference[] refs =
          getReferencesTo(function.getEntryPoint());
      if (refs == null || refs.length == 0) {
        out.append(" (none)");
      } else {
        for (ghidra.program.model.symbol.Reference ref : refs) {
          out.append(" ").append(ref.getFromAddress());
        }
      }
      out.append("\n");
      DecompileResults results = decompiler.decompileFunction(function, 120, monitor);
      if (results != null && results.decompileCompleted()
          && results.getDecompiledFunction() != null) {
        out.append(results.getDecompiledFunction().getC());
      } else {
        out.append("decompile failed\n");
      }
      out.append("\n\n");
    }

    for (long address : DATA) {
      out.append(dumpData(address)).append('\n');
    }

    try (PrintWriter writer =
             new PrintWriter(new FileWriter("C:/Users/gpmpo/cs2-internal/artifacts/ghidra-occlusion.txt"))) {
      writer.print(out.toString());
      writer.flush();
    }
    println("DumpTargets: wrote " + out.length() + " chars");
  }

  // Dump the data around the physics-query global so the singleton and the static
  // filter object can be identified from their pointer values.
  private static final long[] DATA = {0x18227058L};

  private String dumpData(long address) {
    StringBuilder out = new StringBuilder();
    out.append("== DATA @ ").append(Long.toHexString(address)).append(" ==\n");
    byte[] bytes = new byte[0x80];
    try {
      currentProgram.getMemory().getBytes(toAddr(address), bytes);
    } catch (Exception e) {
      out.append("read failed: ").append(e).append('\n');
      return out.toString();
    }
    for (int i = 0; i + 8 <= bytes.length; i += 8) {
      long value = 0;
      for (int j = 7; j >= 0; j--) {
        value = (value << 8) | (bytes[i + j] & 0xffL);
      }
      out.append("  +0x").append(Integer.toHexString(i)).append(": 0x")
          .append(Long.toHexString(value)).append('\n');
    }
    return out.toString();
  }
}
