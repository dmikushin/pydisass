# Raw binary ARM disassembler for embedding and Python scripting

This is a standalone raw binary disassembler for ARM targets based on GNU binutils. It is made to be a replacement of Capstone, which is not reliable enough in our scenarios.

It re-uses GNU binutils codebase and fully replicates `objdump -b binary` results. The disassembler is made embeddable using C++ API:

```c++
nlohmann::json disass(const std::string& binary, const std::string& mcpu, uint64_t offset);
```

It receives the assembly binary as a string (no disk file is required) and returns a JSON with the parsed disassembly:

```json
{
    ...
    "4960": {
        "binary": "15940074",
        "constant": " 0x74",
        "mnemonic": "ldrne",
        "op_str": "r0, [r4, #116]",
        "size": 4
    },
    "4964": {
        "binary": "13100004",
        "mnemonic": "tstne",
        "op_str": "r0, #4",
        "size": 4
    },
    ...
}
```

We provide Python bindings, which reproduce Capstone behavior:

```python
import pydisass

md = pydisass.Disassembler()
for instr in md.disasm(binary, 0x0):
    print(instr)
```


## Usage

* CMake:

    1. Clone this repository as a submodule: `git submodule add https://github.com/dmikushin/pydisass.git`
    2. Add submodule to the parent `CMakeLists.txt`: `add_subdirectory(path/to/submodule)`
    3. Link your CMake target with the disass library: `target_link_libraries(your_target diass::disass)`
    4. Include `#include "disass/disass.h"` header in your code and use the `disass()` API:

       ```c++
       std::ifstream file(filename, std::ios::binary);
	   if (!file.is_open())
	   {
	       fprintf(stderr, "Cannot open file %s for reading\n", filename);
    	   exit(EXIT_FAILURE);
	   }

       std::string binary((std::istreambuf_iterator<char>(file)),
         std::istreambuf_iterator<char>());

       auto result = disass(binary, mcpu, 0);
	   std::cout << result.dump(4) << std::endl;
       ```

* Python:

    1. Install this repository as a Python package:

       ```
       python3 -m pip install git+https://github.com/dmikushin/pydisass.git
       ```

    2. Use the disassembler, providing the input binary as an array of bytes:

       ```python
       import pydisass
 
       md = pydisass.Disassembler()
       for instr in md.disasm(binary, 0x0):
           print(instr)
       ```

## Development

A standalone native shared library can be build with CMake:

```
mkdir build
cd build
cmake ..
make -j12
```

Test programs are included:

```
ctest -V
```

## Licensing

This projects inherits GPL license of GNU binutils.

