// DuplicateProgram.java — copy the current program's project file to a new name.
import ghidra.app.script.GhidraScript;
import ghidra.framework.model.DomainFile;
import ghidra.framework.model.DomainFolder;

public class DuplicateProgram extends GhidraScript {
  @Override
  public void run() throws Exception {
    String[] args = getScriptArgs();
    if (args.length < 1) { println("usage: DuplicateProgram <newName>"); return; }
    DomainFile df = currentProgram.getDomainFile();
    DomainFolder parent = df.getParent();
    println("copyTo parent from " + df.getName());
    DomainFile copy = df.copyTo(parent, monitor);
    println("copy name after copyTo: " + (copy == null ? "FAILED" : copy.getName()));
    if (copy != null && !copy.getName().equals(args[0])) {
      copy.setName(args[0]);
      println("renamed copy to: " + copy.getName());
    }
  }
}
