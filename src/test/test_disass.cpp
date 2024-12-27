#include <cstdio>
#include <iostream>
#include <string>
#include <nlohmann/json.hpp>

nlohmann::json disass(const char* filename, const char* mcpu, int offset);

int main(int argc, char* argv[])
{
	if (argc != 3)
	{
		printf("Usage: %s <machine> <filename>\n", argv[0]);
		return 1;
	}

	const char* mcpu = argv[1];
	const char* filename = argv[2];
	auto result = disass(filename, mcpu, 0);
	std::cout << result.dump(4) << std::endl;
	
	return 0;
}
