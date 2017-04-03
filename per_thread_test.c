#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

#include "lrcu.h"

int main(int argc, char *argv[]){
	lrcu_init();
	lrcu_thread_init();


	return 0;
}