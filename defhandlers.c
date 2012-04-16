void doNothingHandler(void){
	;
}

void sigIntHandler(void){
	//release(&ptable.lock);
	//kill(proc->pid);
	//acquire(&ptable.lock);
	proc->killed = 1;
	//TODO: IF NOT RUNNABLE - wakeup
}

void sigUsr1Handler(void){
	cprintf("SIGUSR1 %d", proc->pid);
}

void sigUsr2Handler(void){
	cprintf("SIGUSR2 %d", proc->pid);
}

void sigChldHandler(void){
	;
}
