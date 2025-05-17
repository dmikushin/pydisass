#!/usr/bin/env python3
from pydisass import Disassembler
import unittest

class TestBranchOffsets(unittest.TestCase):
    """Test for branch target calculation with non-zero offset"""
    
    def test_branch_with_offset(self):
        """Test that branch targets are correctly calculated with non-zero offset"""
        print("Starting branch target test with offset")
        
        # Initialize the disassembler with ARM settings
        disassembler = Disassembler(triple='arm-none-eabi', cpu_model='arm926ej-s')
        disassembler.detail = True
        
        # Example binary for a branch instruction
        binary = b'\x00\x00\x00\xeb'  # This is 'bl' instruction in ARM
        
        # Test with a series of offsets to determine behavior
        offsets = [0, 0x1000, 0x8000]
        
        print("Testing branch target calculation with different offsets:")
        for offset in offsets:
            instructions = list(disassembler.disasm(binary, offset=offset))
            instr = instructions[0]
            addr = instr.address
            target = instr.op_str
            print(f"  Offset: 0x{offset:x}, Address: 0x{addr:x}, Target: {target}")
            
            # Check if the offset affects the branch target
            self.assertEqual(instr.mnemonic, 'bl')
            
            # The key test: branch targets should be adjusted by the offset
            expected_target = f"0x{offset+8:x}"
            self.assertEqual(target, expected_target, 
                            f"Branch target not adjusted correctly for offset 0x{offset:x}")
            
            # The address is correctly set to the base address
            self.assertEqual(addr, offset)
            
        # Test a conditional branch (beq)
        binary_beq = b'\x00\x00\x00\x0a'  # beq 0x8
        
        print("Testing conditional branch target calculation:")
        for offset in offsets:
            instructions = list(disassembler.disasm(binary_beq, offset=offset))
            instr = instructions[0]
            addr = instr.address
            target = instr.op_str
            print(f"  Offset: 0x{offset:x}, Address: 0x{addr:x}, Target: {target}")
            
            # Check for correct target address adjustment
            expected_target = f"0x{offset+8:x}"
            self.assertEqual(target, expected_target, 
                            f"Conditional branch target not adjusted for offset 0x{offset:x}")
        
        print("Branch target test completed successfully!")

if __name__ == '__main__':
    print("Starting test execution")
    unittest.main()

if __name__ == '__main__':
    print("Starting test execution")
    unittest.main()
