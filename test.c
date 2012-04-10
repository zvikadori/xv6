#include "types.h"
#include "stat.h"
#include "user.h"

#ifndef sighandler_t
typedef void (*sighandler_t)(void);
#endif

int main (void){
	//printf("\n%d\n\n\n\n\n", 512);
	sighandler_t firstHandler = (sighandler_t) 5;
	
	signal(1, firstHandler);
	
	exit();
}
