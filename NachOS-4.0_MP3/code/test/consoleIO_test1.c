#include "syscall.h"

int
main()
{
	/*int n;
	int test = 0;
	for (n=0;n<100;n++){}
	PrintInt(n);
	for (n=0;n<10000;n++) {
		test++;
	}
    Exit(0);
    */
  	int n;
	
	for (n = 10 ;n >0;n--){ /////   aging
		PrintInt(n) ;
	}
	//PrintInt(5) ;
	for (n = 5000 ;n > 2000 ; n --){}
	/*
	 // RR and Priority+preemptive
	for (n = 0 ;n < 10 ; n++){
		PrintInt(n) ;
	}
	
	*/

	return 0 ;   
}

