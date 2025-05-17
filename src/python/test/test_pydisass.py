from pydisass import Disassembler
import unittest

class TestDisassembler(unittest.TestCase):
    def setUp(self):
        # Initialize the disassembler with ARM settings
        self.disassembler = Disassembler(triple='arm-none-eabi', cpu_model='arm926ej-s')
        self.disassembler.detail = True  # Enable detailed disassembly for all tests

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

    def test_disasm_arm_push_bl_pop_sequence(self):
        # Test the specific sequence:
        # 0: e52de004 push {lr} ; (str lr, [sp, #-4]!)
        # 4: eb000000 bl 0xc
        # 8: e49df004 pop {pc} ; (ldr pc, [sp], #4)
        
        # Binary representation of the instructions (little-endian format)
        binary = b'\x04\xe0\x2d\xe5\x00\x00\x00\xeb\x04\xf0\x9d\xe4'
        
        # Disassemble the binary code starting at offset 0
        instructions = list(self.disassembler.disasm(binary, offset=0))
        
        self.assertEqual(len(instructions), 3)
        
        # Check first instruction: push {lr}
        instr1 = instructions[0]
        self.assertEqual(instr1.address, 0)
        self.assertEqual(instr1.binary, 'e52de004')
        self.assertEqual(instr1.mnemonic, 'push')
        self.assertEqual(instr1.op_str, '{lr}')
        
        # When detail is enabled, check if we can get alternative representations
        if hasattr(instr1, 'constant') and instr1.constant:
            # This might contain the comment: str lr, [sp, #-4]!
            self.assertIn('str lr, [sp, #-4]!', instr1.constant, "Alternative representation missing for push instruction")
        
        # Check second instruction: bl 0xc
        instr2 = instructions[1]
        self.assertEqual(instr2.address, 4)
        self.assertEqual(instr2.binary, 'eb000000')
        self.assertEqual(instr2.mnemonic, 'bl')
        self.assertEqual(instr2.op_str, '0xc')
        
        # Check third instruction: pop {pc}
        instr3 = instructions[2]
        self.assertEqual(instr3.address, 8)
        self.assertEqual(instr3.binary, 'e49df004')
        self.assertEqual(instr3.mnemonic, 'pop')
        self.assertEqual(instr3.op_str, '{pc}')
        
        # When detail is enabled, check if we can get alternative representations
        if hasattr(instr3, 'constant') and instr3.constant:
            # This might contain the comment: ldr pc, [sp], #4
            self.assertIn('ldr pc, [sp], #4', instr3.constant, "Alternative representation missing for pop instruction")

if __name__ == '__main__':
    unittest.main()
