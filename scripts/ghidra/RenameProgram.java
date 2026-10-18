// RenameProgram.java — rename the currently processed program's domain file.
import ghidra.app.script.GhidraScript;
import ghidra.framework.model.DomainFile;

public class RenameProgram extends GhidraScript {
  @Override
  public void run() throws Exception {
    String[] args = getScriptArgs();
    if (args.length < 1) { println("usage: RenameProgram <newName>"); return; }
    DomainFile df = currentProgram.getDomainFile();
    println("old name: " + df.getName());
    df.setName(args[0]);
    println("new name: " + currentProgram.getDomainFile().getName());
  }
}
