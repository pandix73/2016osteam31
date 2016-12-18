#include "syscall.h"

int
main()
{
	int n;
	int test = 0;
	for (n=0;n<10000;n++) {
		test++;
	}
    Exit(0); 
}

