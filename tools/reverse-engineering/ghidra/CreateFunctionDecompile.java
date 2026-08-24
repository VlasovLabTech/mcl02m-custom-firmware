// Create a temporary function for an undecoded task region and export Ghidra's decompiler output.
//@category MCL02M

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SourceType;

public class CreateFunctionDecompile extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 3) {
            throw new IllegalArgumentException(
                "Usage: CreateFunctionDecompile.java START END OUTPUT_FILE");
        }
        Address start = currentProgram.getAddressFactory().getDefaultAddressSpace()
            .getAddress(args[0]);
        Address end = currentProgram.getAddressFactory().getDefaultAddressSpace()
            .getAddress(args[1]);
        Function function = currentProgram.getFunctionManager().getFunctionAt(start);
        if (function == null) {
            function = currentProgram.getFunctionManager().getFunctionContaining(start);
        }
        if (function == null) {
            // Ghidra's import may leave a LAB_* namespace symbol at the task entry.
            // Remove only that symbol in this temporary analysis project so a
            // synthetic function can be created; the source ELF is untouched.
            for (Symbol symbol : currentProgram.getSymbolTable().getSymbols(start)) {
                if (symbol.getName().startsWith("LAB_") || symbol.getName().startsWith("FUN_")) {
                    symbol.delete();
                }
            }
            function = currentProgram.getFunctionManager().createFunction(
                null, start, new AddressSet(start, end),
                SourceType.USER_DEFINED);
        }
        DecompInterface decompiler = new DecompInterface();
        decompiler.setOptions(new DecompileOptions());
        decompiler.openProgram(currentProgram);
        DecompileResults results = decompiler.decompileFunction(function, 120,
            monitor);
        String output = results.getDecompiledFunction() == null
            ? "/* decompiler failed: " + results.getErrorMessage() + " */\n"
            : results.getDecompiledFunction().getC();
        Path path = Paths.get(args[2]);
        Files.createDirectories(path.toAbsolutePath().getParent());
        Files.write(path, output.getBytes(StandardCharsets.UTF_8));
        println("Exported decompilation to " + path.toAbsolutePath());
        decompiler.dispose();
    }
}
