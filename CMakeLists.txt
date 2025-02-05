cmake_minimum_required(VERSION 3.10)

project(disass LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(ZLIB REQUIRED)
find_package(ZSTD REQUIRED)

add_subdirectory(ThirdParty/json EXCLUDE_FROM_ALL)
add_subdirectory(ThirdParty/pybind11 EXCLUDE_FROM_ALL)

if (UNIX)
# Use $ORIGIN or @loader_path, which allow to pick up
# dependencies relative to the executable folder
if (APPLE)
set(CMAKE_INSTALL_RPATH "@loader_path")
else()
set(CMAKE_INSTALL_RPATH "$ORIGIN")
endif()
endif()

add_library(opcodes STATIC
	opcodes/dis-buf.c
	opcodes/disassemble.c
	opcodes/dis-init.c
	opcodes/arm-dis.c)
set_property(TARGET opcodes PROPERTY POSITION_INDEPENDENT_CODE ON)
target_include_directories(opcodes PRIVATE
	include
	opcodes
	bfd)
target_compile_definitions(opcodes PRIVATE
	HAVE_CONFIG_H
	ARCH_arm)
target_compile_options(opcodes PRIVATE
    -fvisibility=hidden
	-W
	-Wall
	-Wstrict-prototypes
	-Wmissing-prototypes
	-Wshadow
	-Wstack-usage=262144
	-Werror)

add_library(bfd STATIC
	bfd/archive.c
	bfd/archures.c
	bfd/bfd.c
	bfd/bfdio.c
	bfd/cache.c
	bfd/coff-bfd.c
	bfd/compress.c
	bfd/corefile.c
	bfd/elf-properties.c
	bfd/format.c
	bfd/hash.c
	bfd/libbfd.c
	bfd/linker.c
	bfd/merge.c
	bfd/opncls.c
	bfd/reloc.c
	bfd/section.c
	bfd/simple.c
	bfd/stab-syms.c
	bfd/stabs.c
	bfd/syms.c
	bfd/targets.c
	bfd/binary.c
	bfd/ihex.c
	bfd/srec.c
	bfd/tekhex.c
	bfd/verilog.c
	bfd/elf32-arm.c
	bfd/elf32.c
	bfd/elf-nacl.c
	bfd/elf-vxworks.c
	bfd/elf.c
	bfd/elflink.c
	bfd/elf-attrs.c
	bfd/elf-strtab.c
	bfd/elf-eh-frame.c
	bfd/dwarf1.c
	bfd/dwarf2.c
	bfd/elf32-gen.c
	bfd/plugin.c
	bfd/cpu-arm.c
	bfd/archive64.c)
set_property(TARGET bfd PROPERTY POSITION_INDEPENDENT_CODE ON)
target_include_directories(bfd PRIVATE
	include
	bfd)
target_compile_definitions(bfd PRIVATE
	HAVE_CONFIG_H
	BINDIR="/usr/local/bin"
	LIBDIR="/usr/local/lib"
	DEBUGDIR="/usr/local/lib/debug"
	SELECT_VECS=&arm_elf32_le_vec,&arm_elf32_fdpic_le_vec,&arm_elf32_be_vec,&arm_elf32_fdpic_be_vec,&elf32_le_vec,&elf32_be_vec
	SELECT_ARCHITECTURES=&bfd_arm_arch
	HAVE_arm_elf32_le_vec
	HAVE_arm_elf32_fdpic_le_vec
	HAVE_arm_elf32_be_vec
	HAVE_arm_elf32_fdpic_be_vec
	HAVE_elf32_le_vec
	HAVE_elf32_be_vec)
target_compile_options(bfd PRIVATE
    -fvisibility=hidden
	-W
	-Wall
	-Wstrict-prototypes
	-Wmissing-prototypes
	-Wshadow
	-Wstack-usage=262144
	-Werror)
target_link_libraries(bfd ZLIB::ZLIB ZSTD::ZSTD)

add_library(liberty STATIC
	liberty/regex.c
	liberty/cplus-dem.c
	liberty/cp-demangle.c
	liberty/md5.c
	liberty/sha1.c
	liberty/alloca.c
	liberty/argv.c
	liberty/bsearch_r.c
	liberty/choose-temp.c
	liberty/concat.c
	liberty/cp-demint.c
	liberty/crc32.c
	liberty/d-demangle.c
	liberty/dwarfnames.c
	liberty/dyn-string.c
	liberty/fdmatch.c
	liberty/fibheap.c
	liberty/filedescriptor.c
	liberty/filename_cmp.c
	liberty/floatformat.c
	liberty/fnmatch.c
	liberty/fopen_unlocked.c
	liberty/getopt.c
	liberty/getopt1.c
	liberty/getpwd.c
	liberty/getruntime.c
	liberty/hashtab.c
	liberty/hex.c
	liberty/lbasename.c
	liberty/lrealpath.c
	liberty/make-relative-prefix.c
	liberty/make-temp-file.c
	liberty/objalloc.c
	liberty/obstack.c
	liberty/partition.c
	liberty/pexecute.c
	liberty/physmem.c
	liberty/pex-common.c
	liberty/pex-one.c
	liberty/pex-unix.c
	liberty/vprintf-support.c
	liberty/rust-demangle.c
	liberty/safe-ctype.c
	liberty/simple-object.c
	liberty/simple-object-coff.c
	liberty/simple-object-elf.c
	liberty/simple-object-mach-o.c
	liberty/simple-object-xcoff.c
	liberty/sort.c
	liberty/spaces.c
	liberty/splay-tree.c
	liberty/stack-limit.c
	liberty/strerror.c
	liberty/strsignal.c
	liberty/timeval-utils.c
	liberty/unlink-if-ordinary.c
	liberty/xasprintf.c
	liberty/xatexit.c
	liberty/xexit.c
	liberty/xmalloc.c
	liberty/xmemdup.c
	liberty/xstrdup.c
	liberty/xstrerror.c
	liberty/xstrndup.c
	liberty/xvasprintf.c
	liberty/setproctitle.c)
set_property(TARGET liberty PROPERTY POSITION_INDEPENDENT_CODE ON)
target_include_directories(liberty PRIVATE
	include
	liberty)
target_compile_definitions(liberty PRIVATE
	HAVE_CONFIG_H
	_GNU_SOURCE)
target_compile_options(liberty PRIVATE
    -fvisibility=hidden
	-W
	-Wall
	-Wwrite-strings
	-Wc++-compat
	-Wstrict-prototypes
	-Wshadow=local
	-pedantic
	-fcf-protection)

add_library(${PROJECT_NAME} SHARED
	src/${PROJECT_NAME}.cpp
	src/prdbg.c
	src/demanguse.c
	src/rddbg.c
	src/debug.c
	src/stabs.c
	src/rdcoff.c
	src/bucomm.c
	src/version.c
	src/filemode.c
	src/elfcomm.c)
add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(${PROJECT_NAME} PRIVATE bfd)
target_compile_definitions(${PROJECT_NAME} PRIVATE
	HAVE_CONFIG_H
	LOCALEDIR="/usr/local/share/locale"
	bin_dummy_emulation=bin_vanilla_emulation
	OBJDUMP_PRIVATE_VECTORS=)
target_compile_options(${PROJECT_NAME} PRIVATE
    -fvisibility=hidden
	-W
	-Wall
	-Wstrict-prototypes
	-Wmissing-prototypes
	-Wshadow
	-Wstack-usage=262144
	-Werror)
target_link_libraries(${PROJECT_NAME} PRIVATE
	opcodes
	bfd
	liberty)
target_link_libraries(${PROJECT_NAME} PUBLIC
    nlohmann_json::nlohmann_json)

pybind11_add_module(py${PROJECT_NAME}_native src/python/disass.cpp)
target_link_libraries(py${PROJECT_NAME}_native PRIVATE ${PROJECT_NAME})

add_executable(test_${PROJECT_NAME} src/test/test_${PROJECT_NAME}.cpp)
target_link_libraries(test_${PROJECT_NAME} ${PROJECT_NAME})

enable_testing()

add_test(NAME test_${PROJECT_NAME}
	COMMAND test_${PROJECT_NAME} arm926ej-s ${CMAKE_CURRENT_SOURCE_DIR}/examples/CyU3PDmaThreadEntry.bin
	WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

add_test(NAME example_py${PROJECT_NAME}
	COMMAND ${CMAKE_COMMAND} -E env PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}:${CMAKE_CURRENT_BINARY_DIR} python3 ${CMAKE_CURRENT_SOURCE_DIR}/examples/example.py
	WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/examples)

add_test(NAME test_py${PROJECT_NAME}
	COMMAND ${CMAKE_COMMAND} -E env PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}:${CMAKE_CURRENT_BINARY_DIR} python3 ${CMAKE_CURRENT_SOURCE_DIR}/src/python/test/test_py${PROJECT_NAME}.py)


install(TARGETS ${PROJECT_NAME} py${PROJECT_NAME}_native DESTINATION .)
install(FILES include/disass/disass.h DESTINATION include/disass)
