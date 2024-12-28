from pydisass_native import disass

class Instruction:
    def __init__(self, address, size, mnemonic, op_str):
        self.address = address
        self.size = size
        self.mnemonic = mnemonic
        self.op_str = op_str

    def __str__(self):
        return f"{self.address:08x}:\t{self.mnemonic}\t{self.op_str}"

class Disassembler:
    def __init__(self, triple='arm-none-eabi', cpu_model='arm926ej-s'):
        self.triple = triple
        self.cpu_model = cpu_model

    def disasm(self, binary, offset=0):
        result = disass(binary, self.cpu_model, offset)

        for address in sorted(result, key=lambda x: int(x)):
            instr = result[address]
            yield Instruction(int(address), instr['size'], instr['mnemonic'], instr['op_str'])

