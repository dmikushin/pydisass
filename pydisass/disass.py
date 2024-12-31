from pydisass_native import disass
from dataclasses import dataclass, field
from typing import List, Dict, Optional

@dataclass
class Value:
    imm: int  # 32-bit integer

@dataclass
class Operand:
    text: str
    value: Optional[Value] = None

@dataclass
class Instruction:
    address: int
    binary: str
    mnemonic: str
    op_str: str
    size: int
    operands: Optional[List[Operand]] = field(default_factory=list)
    constant: Optional[str] = None

    def __str__(self):
        return f"{self.address:08x}:\t{self.mnemonic}\t{self.op_str}"

class Disassembler:
    def __init__(self, triple='arm-none-eabi', cpu_model='arm926ej-s'):
        self.triple = triple
        self.cpu_model = cpu_model
        self.detail = False

    def disasm(self, binary, offset=0):
        result = disass(binary, self.cpu_model, offset, self.detail)

        if self.detail:
            for address in sorted(result, key=lambda x: int(x)):
                instr = result[address]
                operands = [
                    Operand(op.get('text', ''), Value(op['value']['imm']) if 'value' in op and 'imm' in op['value'] else None)
                    for op in instr.get('operands', [])
                ]
                yield Instruction(
                    address=int(address),
                    binary=instr['binary'],
                    size=instr['size'],
                    mnemonic=instr['mnemonic'],
                    op_str=instr['op_str'],
                    operands=operands
                )
        else:
            for address in sorted(result, key=lambda x: int(x)):
                instr = result[address]
                yield Instruction(
                    address=int(address),
                    binary=instr['binary'],
                    size=instr['size'],
                    mnemonic=instr['mnemonic'],
                    op_str=instr['op_str']
                )
