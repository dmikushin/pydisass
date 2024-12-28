#include "disass/disass.h"

#include <cstdio>
#include <iostream>
#include <string>
#include <fstream>

int main(int argc, char* argv[])
{
	if (argc != 3)
	{
		printf("Usage: %s <machine> <filename>\n", argv[0]);
		return 1;
	}

	const char* mcpu = argv[1];
	const char* filename = argv[2];

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
	
	return 0;
}
