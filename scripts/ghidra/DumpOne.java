// DumpOne.java — decompile caller-listed addresses and write them to a text file.
//
//   analyzeHeadless <projdir> <proj> -import <binary> -scriptPath scripts/ghidra \
//     -postScript DumpOne.java <outPath> <addr> [<addr> ...]
import java.io.FileWriter;
import java.io.PrintWriter;

import ghidra.app.decompiler.DecompileResults;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

public class DumpOne extends GhidraScript {

  @Override
  public void run() throws Exception {
    String[] args = getScriptArgs();
    if (args.length < 2) {
      println("usage: DumpOne <outPath> <addr> [...]");
      return;
    }
    String outPath = args[0];
    DecompInterface decompiler = new DecompInterface();
    decompiler.toggleCCode(true);
    decompiler.openProgram(currentProgram);

    StringBuilder out = new StringBuilder();
    out.append("program: ").append(currentProgram.getName()).append('\n');
    out.append("imageBase: ").append(currentProgram.getImageBase()).append('\n');
    for (int i = 1; i < args.length; i++) {
      long target = Long.decode(args[i]);
      Address address = toAddr(target);
      Function function = getFunctionContaining(address);
      out.append("\n==== ").append(args[i]).append(" ====\n");
      if (function == null) {
        out.append("no function containing that address\n");
        continue;
      }
      out.append("function ").append(function.getName()).append(" @ ")
          .append(function.getEntryPoint()).append('\n');
      DecompileResults results = decompiler.decompileFunction(function, 120, monitor);
      if (results != null && results.decompileCompleted()
          && results.getDecompiledFunction() != null) {
        out.append(results.getDecompiledFunction().getC());
      } else {
        out.append("decompile failed\n");
      }
      out.append('\n');
    }

    try (PrintWriter writer = new PrintWriter(new FileWriter(outPath))) {
      writer.print(out.toString());
      writer.flush();
    }
    println("DumpOne: wrote " + out.length() + " chars to " + outPath);
  }
}
