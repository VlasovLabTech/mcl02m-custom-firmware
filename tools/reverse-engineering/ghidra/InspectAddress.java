//@category MCL02M
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Symbol;

public class InspectAddress extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        Address a = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(args[0]);
        Function at = currentProgram.getFunctionManager().getFunctionAt(a);
        Function containing = currentProgram.getFunctionManager().getFunctionContaining(a);
        println("address=" + a + " functionAt=" + (at == null ? "null" : at.getName())
            + " containing=" + (containing == null ? "null" : containing.getName()));
        for (Symbol s : currentProgram.getSymbolTable().getSymbols(a)) {
            println("symbol name=" + s.getName() + " namespace=" + s.getParentNamespace()
                + " primary=" + s.isPrimary() + " type=" + s.getSymbolType());
        }
    }
}
