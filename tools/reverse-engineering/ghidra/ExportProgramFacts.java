// Export Ghidra program facts to deterministic CSV/JSON files.
//@category MCL02M

import java.io.BufferedWriter;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.LinkedHashSet;
import java.util.Set;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;
import ghidra.program.model.symbol.SymbolIterator;

public class ExportProgramFacts extends GhidraScript {

    private static String csv(String value) {
        if (value == null) {
            return "";
        }
        String normalized = value.replace("\r", "\\r").replace("\n", "\\n");
        return "\"" + normalized.replace("\"", "\"\"") + "\"";
    }

    private static String json(String value) {
        if (value == null) {
            return "null";
        }
        return "\"" + value
            .replace("\\", "\\\\")
            .replace("\"", "\\\"")
            .replace("\r", "\\r")
            .replace("\n", "\\n")
            .replace("\t", "\\t") + "\"";
    }

    private static String address(Address value) {
        return value == null ? "" : "0x" + value.toString();
    }

    private static String functionName(Function function) {
        return function == null ? "" : function.getName(true);
    }

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 1) {
            throw new IllegalArgumentException("Usage: ExportProgramFacts.java OUTPUT_DIR");
        }
        Path output = Paths.get(args[0]);
        Files.createDirectories(output);

        Listing listing = currentProgram.getListing();
        FunctionManager functions = currentProgram.getFunctionManager();
        ReferenceManager references = currentProgram.getReferenceManager();

        long functionCount = exportFunctions(output.resolve("functions.csv"), functions);
        long instructionCount = exportCalls(
            output.resolve("calls.csv"), listing, functions
        );
        long stringCount = exportStrings(
            output.resolve("strings.csv"), listing, functions, references
        );
        long symbolCount = countSymbols();
        exportMemory(output.resolve("memory_blocks.csv"));

        try (BufferedWriter writer = Files.newBufferedWriter(
                output.resolve("summary.json"), StandardCharsets.UTF_8)) {
            writer.write("{\n");
            writer.write("  \"program\": " + json(currentProgram.getName()) + ",\n");
            writer.write("  \"language\": " + json(currentProgram.getLanguageID().toString()) + ",\n");
            writer.write("  \"compiler\": " + json(currentProgram.getCompilerSpec().getCompilerSpecID().toString()) + ",\n");
            writer.write("  \"image_base\": " + json(address(currentProgram.getImageBase())) + ",\n");
            writer.write("  \"executable_sha256\": " + json(currentProgram.getExecutableSHA256()) + ",\n");
            writer.write("  \"functions\": " + functionCount + ",\n");
            writer.write("  \"instructions\": " + instructionCount + ",\n");
            writer.write("  \"defined_strings\": " + stringCount + ",\n");
            writer.write("  \"symbols\": " + symbolCount + "\n");
            writer.write("}\n");
        }
        println("Exported facts to " + output.toAbsolutePath());
    }

    private long exportFunctions(Path path, FunctionManager functions) throws IOException {
        long count = 0;
        try (BufferedWriter writer = Files.newBufferedWriter(path, StandardCharsets.UTF_8)) {
            writer.write("entry,name,size,min_address,max_address,thunk,external,noreturn,calling_convention,parameter_count,caller_count,callee_count,signature\n");
            FunctionIterator iterator = functions.getFunctions(true);
            while (iterator.hasNext() && !monitor.isCancelled()) {
                Function function = iterator.next();
                Set<Function> callers = function.getCallingFunctions(monitor);
                Set<Function> callees = function.getCalledFunctions(monitor);
                writer.write(String.join(",",
                    csv(address(function.getEntryPoint())),
                    csv(function.getName(true)),
                    Long.toString(function.getBody().getNumAddresses()),
                    csv(address(function.getBody().getMinAddress())),
                    csv(address(function.getBody().getMaxAddress())),
                    Boolean.toString(function.isThunk()),
                    Boolean.toString(function.isExternal()),
                    Boolean.toString(function.hasNoReturn()),
                    csv(function.getCallingConventionName()),
                    Integer.toString(function.getParameterCount()),
                    Integer.toString(callers.size()),
                    Integer.toString(callees.size()),
                    csv(function.getSignature().getPrototypeString())
                ));
                writer.newLine();
                count++;
            }
        }
        return count;
    }

    private long exportCalls(Path path, Listing listing, FunctionManager functions) throws IOException {
        long instructionCount = 0;
        try (BufferedWriter writer = Files.newBufferedWriter(path, StandardCharsets.UTF_8)) {
            writer.write("caller_entry,caller_name,callsite,target,target_function,reference_type\n");
            InstructionIterator iterator = listing.getInstructions(true);
            while (iterator.hasNext() && !monitor.isCancelled()) {
                Instruction instruction = iterator.next();
                instructionCount++;
                Function caller = functions.getFunctionContaining(instruction.getAddress());
                for (Reference reference : instruction.getReferencesFrom()) {
                    if (!reference.getReferenceType().isCall()) {
                        continue;
                    }
                    Function target = functions.getFunctionAt(reference.getToAddress());
                    writer.write(String.join(",",
                        csv(caller == null ? "" : address(caller.getEntryPoint())),
                        csv(functionName(caller)),
                        csv(address(instruction.getAddress())),
                        csv(address(reference.getToAddress())),
                        csv(functionName(target)),
                        csv(reference.getReferenceType().getName())
                    ));
                    writer.newLine();
                }
            }
        }
        return instructionCount;
    }

    private long exportStrings(
            Path path,
            Listing listing,
            FunctionManager functions,
            ReferenceManager references) throws IOException {
        long count = 0;
        try (BufferedWriter writer = Files.newBufferedWriter(path, StandardCharsets.UTF_8)) {
            writer.write("address,length,value,reference_count,source_functions\n");
            DataIterator iterator = listing.getDefinedData(true);
            while (iterator.hasNext() && !monitor.isCancelled()) {
                Data data = iterator.next();
                if (!data.hasStringValue()) {
                    continue;
                }
                String value = data.getDefaultValueRepresentation();
                Set<String> sources = new LinkedHashSet<>();
                int referenceCount = 0;
                ReferenceIterator refs = references.getReferencesTo(data.getAddress());
                while (refs.hasNext()) {
                    Reference reference = refs.next();
                    referenceCount++;
                    Function function = functions.getFunctionContaining(reference.getFromAddress());
                    if (function != null) {
                        sources.add(address(function.getEntryPoint()) + ":" + function.getName(true));
                    }
                }
                writer.write(String.join(",",
                    csv(address(data.getAddress())),
                    Integer.toString(data.getLength()),
                    csv(value),
                    Integer.toString(referenceCount),
                    csv(String.join(";", sources))
                ));
                writer.newLine();
                count++;
            }
        }
        return count;
    }

    private void exportMemory(Path path) throws IOException {
        try (BufferedWriter writer = Files.newBufferedWriter(path, StandardCharsets.UTF_8)) {
            writer.write("name,start,end,size,read,write,execute,initialized,source\n");
            for (MemoryBlock block : currentProgram.getMemory().getBlocks()) {
                writer.write(String.join(",",
                    csv(block.getName()),
                    csv(address(block.getStart())),
                    csv(address(block.getEnd())),
                    Long.toString(block.getSize()),
                    Boolean.toString(block.isRead()),
                    Boolean.toString(block.isWrite()),
                    Boolean.toString(block.isExecute()),
                    Boolean.toString(block.isInitialized()),
                    csv(block.getSourceName())
                ));
                writer.newLine();
            }
        }
    }

    private long countSymbols() {
        long count = 0;
        SymbolIterator iterator = currentProgram.getSymbolTable().getAllSymbols(true);
        while (iterator.hasNext()) {
            iterator.next();
            count++;
        }
        return count;
    }
}
