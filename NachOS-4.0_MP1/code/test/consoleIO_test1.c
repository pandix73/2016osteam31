#include "syscall.h"

int
main()
{
	int n;
	for (n=19;n>5;n--) {
		PrintInt(n);
	}
        Halt();
}

