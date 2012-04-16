#include "types.h"
#include "stat.h"
#include "user.h"
#include "signal.h"

void finish(void)
{
	signal(3,finish);
	printf(2,"ssss %d\n",getpid());
	kill(getpid());

}


void ouch(void )
{
			signal(4,ouch);
			printf(2,"ouch %d\n",getpid());
			
}


void childRunning()
{
	signal(4,ouch);
	while(1)
	{
		sleep(500);
	}

}


int main ()
{
	int enteredChildId;
	int enteredSignal;
	int pid1, pid2, pid3;
	char input[100];
	//int counter = 3;
	signal(SIGCHLD,finish);
	
	if( (pid1=fork()) ==0)
	{
		childRunning();//first child running
	}
	else
	{
		if( (pid2=fork()) == 0)
		{
			childRunning();  //second child running
		}
		else
		{
			if((pid3=fork()) == 0)
			{
				childRunning();  //third child running
			}
			else
			{
			
				while(1)
				{
					//sleep(250);
					printf(1,"Enter a child id (0 - 2):\n");
					enteredChildId=atoi(gets(input, 10));
					printf(1,"Which signal to send:\n");
					enteredSignal=atoi(gets(input, 10));
					if(enteredChildId==0)
					{
						sigsend(pid1,enteredSignal);
					}
				    if(enteredChildId==1)
					{
						sigsend(pid2,enteredSignal);
					}
					if(enteredChildId==2)
					{
						sigsend(pid3,enteredSignal);
					}
				}
				
			}
			
		}
		
	
	}

	return 0;
}




