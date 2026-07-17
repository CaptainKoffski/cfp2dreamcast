# Ghidra post-script: disassemble at the Naomi header entrypoint and print
# the first 32 instructions. Sanity-checks that SuperH4:LE:32 decodes the
# boot binary imported at base 0x8c020000.
from ghidra.app.cmd.disassemble import DisassembleCommand

ENTRY = 0x8c04ae2c
addr = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(ENTRY)
DisassembleCommand(addr, None, True).applyTo(currentProgram)
ins = currentProgram.getListing().getInstructionAt(addr)
n = 0
while ins is not None and n < 32:
    println("%s  %s" % (ins.getAddress(), ins))
    ins = ins.getNext()
    n += 1
if n == 0:
    println("FAIL: no instructions decoded at 0x%08x" % ENTRY)
