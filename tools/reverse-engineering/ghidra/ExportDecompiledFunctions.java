// Decompile every non-external function and export deterministic text/index files.
//@category MCL02M

import java.io.BufferedWriter;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;

public class ExportDecompiledFunctions extends GhidraScript {

    private static String csv(String value) {
        if (value == null) {
            return "";
        }
        return "\"" + value.replace("\r", "\\r").replace("\n", "\\n")
            .replace("\"", "\"\"") + "\"";
    }

    private static String address(Function function) {
        return "0x" + function.getEntryPoint().toString();
    }

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 1) {
            throw new IllegalArgumentException(
                "Usage: ExportDecompiledFunctions.java OUTPUT_DIR"
            );
        }
        Path output = Paths.get(args[0]);
        Files.createDirectories(output);

        DecompInterface decompiler = new DecompInterface();
        DecompileOptions options = new DecompileOptions();
        options.grabFromProgram(currentProgram);
        decompiler.setOptions(options);
        decompiler.setSimplificationStyle("decompile");
        if (!decompiler.openProgram(currentProgram)) {
            throw new IllegalStateException("Cannot open program in decompiler");
        }

        int attempted = 0;
        int succeeded = 0;
        try (BufferedWriter code = Files.newBufferedWriter(
                 output.resolve("all_functions.c"), StandardCharsets.UTF_8);
             BufferedWriter index = Files.newBufferedWriter(
                 output.resolve("decompile_index.csv"), StandardCharsets.UTF_8)) {
            index.write("entry,name,size,status,error");
            index.newLine();

            FunctionIterator iterator = currentProgram.getFunctionManager()
                .getFunctions(true);
            while (iterator.hasNext() && !monitor.isCancelled()) {
                Function function = iterator.next();
                if (function.isExternal() || function.isThunk()) {
                    continue;
                }
                attempted++;
                String status = "failed";
                String error = "";
                try {
                    DecompileResults result = decompiler.decompileFunction(
                        function, 45, monitor
                    );
                    if (result.decompileCompleted() &&
                            result.getDecompiledFunction() != null) {
                        String c = result.getDecompiledFunction().getC();
                        code.write("\n/* ===== " + address(function) + " " +
                            function.getName(true) + " ===== */\n");
                        code.write(c);
                        if (!c.endsWith("\n")) {
                            code.newLine();
                        }
                        status = "ok";
                        succeeded++;
                    }
                    else {
                        error = result.getErrorMessage();
                    }
                }
                catch (Exception exception) {
                    error = exception.getClass().getSimpleName() + ": " +
                        exception.getMessage();
                }
                index.write(String.join(",",
                    csv(address(function)),
                    csv(function.getName(true)),
                    Long.toString(function.getBody().getNumAddresses()),
                    csv(status),
                    csv(error)
                ));
                index.newLine();
                if (attempted % 250 == 0) {
                    println("Decompiler progress: " + attempted +
                        " attempted, " + succeeded + " succeeded");
                }
            }
        }
        decompiler.dispose();
        println("Decompiler export complete: " + attempted +
            " attempted, " + succeeded + " succeeded");
    }
}
