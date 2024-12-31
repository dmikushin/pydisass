from pydisass import Disassembler
import unittest

class TestDisassembler(unittest.TestCase):
    def setUp(self):
        # Initialize the disassembler with ARM settings
        self.disassembler = Disassembler(triple='arm-none-eabi', cpu_model='arm926ej-s')
        self.disassembler.detail = True  # Enable detailed disassembly

    def test_disasm_single_instruction(self):
        # Example binary for a single ARM instruction (e.g., MOV R0, #1)
        binary = b'\x01\x00\xa0\xe3'  # MOV R0, #1 in ARM
        instructions = list(self.disassembler.disasm(binary))

        self.assertEqual(len(instructions), 1)
        instr = instructions[0]
        self.assertEqual(instr.address, 0)
        self.assertEqual(instr.binary, 'e3a00001')
        self.assertEqual(instr.mnemonic, 'mov')
        self.assertEqual(instr.op_str, 'r0, #1')
        self.assertEqual(instr.size, 4)
        self.assertEqual(len(instr.operands), 2)
        self.assertEqual(instr.operands[0].text, 'r0')
        self.assertEqual(instr.operands[1].value.imm, 1)

    def test_disasm_multiple_instructions(self):
        # Example binary for multiple ARM instructions
        binary = b'\x01\x00\xa0\xe3\x02\x10\xa0\xe3'  # MOV R0, #1; MOV R1, #2
        instructions = list(self.disassembler.disasm(binary))

        self.assertEqual(len(instructions), 2)

        # Test first instruction
        instr1 = instructions[0]
        self.assertEqual(instr1.address, 0)
        self.assertEqual(instr1.mnemonic, 'mov')
        self.assertEqual(instr1.op_str, 'r0, #1')

        # Test second instruction
        instr2 = instructions[1]
        self.assertEqual(instr2.address, 4)
        self.assertEqual(instr2.mnemonic, 'mov')
        self.assertEqual(instr2.op_str, 'r1, #2')

    def test_disasm_branch_instruction(self):
        # Example binary for a branch instruction (e.g., bl 0x9f38)
        binary = b"\xcc'\x00\xeb"
        instructions = list(self.disassembler.disasm(binary))

        self.assertEqual(len(instructions), 1)
        instr = instructions[0]
        self.assertEqual(instr.address, 0)
        self.assertEqual(instr.mnemonic, 'bl')
        self.assertEqual(instr.op_str, '0x9f38')
        self.assertEqual(instr.size, 4)

if __name__ == '__main__':
    unittest.main()
