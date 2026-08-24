// Export raw Ghidra disassembly for a bounded address range.
//@category MCL02M

import java.io.BufferedWriter;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;

public class ExportDisassemblyRange extends GhidraScript {
    private static String address(Address value) {
        return value == null ? "" : "0x" + value.toString();
    }

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 3) {
            throw new IllegalArgumentException(
                "Usage: ExportDisassemblyRange.java START END OUTPUT_FILE");
        }
        Address start = currentProgram.getAddressFactory().getDefaultAddressSpace()
            .getAddress(args[0]);
        Address end = currentProgram.getAddressFactory().getDefaultAddressSpace()
            .getAddress(args[1]);
        Path output = Paths.get(args[2]);
        Files.createDirectories(output.toAbsolutePath().getParent());
        try (BufferedWriter writer = Files.newBufferedWriter(output, StandardCharsets.UTF_8)) {
            writer.write("address\tfunction\tmnemonic\toperands\tbytes\n");
            Instruction instruction = currentProgram.getListing().getInstructionAt(start);
            while (instruction != null && instruction.getAddress().compareTo(end) <= 0
                    && !monitor.isCancelled()) {
                Function function = currentProgram.getFunctionManager()
                    .getFunctionContaining(instruction.getAddress());
                byte[] bytes = instruction.getBytes();
                StringBuilder hex = new StringBuilder();
                for (byte value : bytes) {
                    if (hex.length() != 0) hex.append(' ');
                    hex.append(String.format("%02x", value & 0xff));
                }
                writer.write(address(instruction.getAddress()));
                writer.write("\t");
                writer.write(function == null ? "" : function.getName(true));
                writer.write("\t");
                writer.write(instruction.getMnemonicString().replace("\\t", " "));
                writer.write("\t");
                for (int operandIndex = 0; operandIndex < instruction.getNumOperands(); operandIndex++) {
                    if (operandIndex != 0) writer.write(", ");
                    writer.write(instruction.getDefaultOperandRepresentation(operandIndex));
                }
                writer.write("\t");
                writer.write(hex.toString());
                writer.write("\n");
                instruction = instruction.getNext();
            }
        }
        println("Exported disassembly to " + output.toAbsolutePath());
    }
}
