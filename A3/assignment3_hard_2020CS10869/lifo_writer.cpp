#include <iostream>
#include <cstdlib>

int main(int argc, char** argv) {

	FILE *lifo_writer = fopen("/dev/lifo_writer", "wb");

	int n_bytes;
	while (1) {
		std::cout << "> ";
		std::cin >> n_bytes;
		if (n_bytes == 0) break;

		char *buf = (char*)malloc(n_bytes);
		for (int i=0; i<n_bytes; i++) buf[i] = (char)i;
		
		int n_bytes_written = fwrite(buf, 1, n_bytes, lifo_writer);
		std::cout << "wrote " << n_bytes_written << " bytes to file\n";
		fflush(lifo_writer);
	}

	fclose(lifo_writer);
	return 0;
}
		
