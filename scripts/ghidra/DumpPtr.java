// DumpPtr.java — dump raw qwords at addresses, optionally following pointers.
//
//   analyzeHeadless <projdir> <proj> -process client.dll -noanalysis \
//     -scriptPath scripts/ghidra -postScript DumpPtr.java <outPath> <spec> [<spec> ...]
//
// A <spec> is `<addr>[-><depth>][:<len>]`, e.g.
//   0x1821c34f8->1:0x120   // read the qword at the address, then dump 0x120 bytes there
//   0x1809d2dc0:0x80       // dump 0x80 bytes at the address itself
import java.io.FileWriter;
import java.io.PrintWriter;

import ghidra.app.script.GhidraScript;

public class DumpPtr extends GhidraScript {

  @Override
  public void run() throws Exception {
    String[] args = getScriptArgs();
    if (args.length < 2) {
      println("usage: DumpPtr <outPath> <addr[->depth][:len]> ...");
      return;
    }
    String outPath = args[0];
    StringBuilder out = new StringBuilder();
    out.append("program: ").append(currentProgram.getName()).append('\n');

    for (int i = 1; i < args.length; i++) {
      String spec = args[i];
      long addr = 0;
      int depth = 0;
      int len = 0x100;
      String main = spec;
      int arrow = spec.indexOf("->");
      if (arrow >= 0) {
        depth = Integer.parseInt(spec.substring(arrow + 2));
        main = spec.substring(0, arrow);
      }
      int colon = main.indexOf(':');
      if (colon >= 0) {
        len = Long.decode(main.substring(colon + 1)).intValue();
        main = main.substring(0, colon);
      }
      addr = Long.decode(main);

      for (int d = 0; d < depth; d++) {
        long value = readQword(addr);
        out.append("follow 0x").append(Long.toHexString(addr)).append(" -> 0x")
            .append(Long.toHexString(value)).append('\n');
        if (value == 0) {
          break;
        }
        addr = value;
      }

      out.append("== dump @ 0x").append(Long.toHexString(addr)).append(" len 0x")
          .append(Integer.toHexString(len)).append(" ==\n");
      byte[] bytes = new byte[len];
      try {
        currentProgram.getMemory().getBytes(toAddr(addr), bytes);
      } catch (Exception e) {
        out.append("  read failed: ").append(e).append('\n');
        continue;
      }
      for (int k = 0; k + 8 <= bytes.length; k += 8) {
        long value = 0;
        for (int j = 7; j >= 0; j--) {
          value = (value << 8) | (bytes[k + j] & 0xffL);
        }
        out.append("  +0x").append(Integer.toHexString(k)).append(": 0x")
            .append(Long.toHexString(value)).append('\n');
      }
    }

    try (PrintWriter writer = new PrintWriter(new FileWriter(outPath))) {
      writer.print(out.toString());
      writer.flush();
    }
    println("DumpPtr: wrote " + out.length() + " chars to " + outPath);
  }

  private long readQword(long addr) {
    byte[] b = new byte[8];
    try {
      currentProgram.getMemory().getBytes(toAddr(addr), b);
    } catch (Exception e) {
      return -1L;
    }
    long value = 0;
    for (int j = 7; j >= 0; j--) {
      value = (value << 8) | (b[j] & 0xffL);
    }
    return value;
  }
}
