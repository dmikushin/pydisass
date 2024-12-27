#include <cstdio>

void disass(const char* filename, const char* mcpu);

int main(int argc, char* argv[])
{
	if (argc != 3)
	{
		printf("Usage: %s <machine> <filename>\n", argv[0]);
		return 1;
	}

	const char* mcpu = argv[1];
	const char* filename = argv[2];
	disass(filename, mcpu);
	
	return 0;
}
