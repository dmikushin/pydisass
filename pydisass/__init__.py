import os
import sys

# Get the directory of the current file
current_dir = os.path.dirname(os.path.abspath(__file__))

# Add the current directory to sys.path
if current_dir not in sys.path:
    sys.path.insert(0, current_dir)

from .disass import (Instruction, Disassembler)
