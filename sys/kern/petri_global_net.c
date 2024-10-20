/*
 ============================================================================
 Name        : PetriGlobalNet.c
 Author      :
 Version     :
 Copyright   : Your copyright notice
 Description : Hello World in C, Ansi-style
 ============================================================================
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/cpuset.h>
#include <sys/smp.h>
#include <sys/time.h>
#include <sys/sched_petri.h>

//#include "sched_petri.h"

#define TDF_BOUND       0x02000000     /* Bound to one CPU. */
#define THREAD_CAN_SCHED(td, cpu)       \
       CPU_ISSET((cpu), &(td)->td_cpuset->cs_mask)

int smp_set = 0;
int print_enabled = 1;
int transitions_to_print = 0;
struct petri_cpu_resource_net resource_net;

const int base_resource_matrix[CPU_BASE_PLACES][CPU_BASE_TRANSITIONS] = {
	/*Base matrix */
	{ 1, 0,-1, 0, 0, 0, 0,-1, 0},
	{ 1,-1, 0, 0, 0, 0, 0,-1,-1},
	{ 0,-1, 0, 0, 1, 1,-1, 0, 0},
	{ 0, 1,-1,-1, 0, 0, 1, 0, 0},
	{ 0, 0, 1, 1,-1,-1, 0, 0, 0}
};

const int base_resource_inhibition_matrix[CPU_BASE_PLACES][CPU_BASE_TRANSITIONS] = {
	/*Base inhibition matrix */
	{ 1, 0, 0, 1, 0, 0, 0, 0, 1},
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0}
};

const char *transitions_names[] = {
	"ADDTOQUEUE_P0", "UNQUEUE_P0", "EXEC_P0", "EXEC_EMPTY_P0", "RETURN_VOL_P0", "RETURN_INVOL_P0", "FROM_GLOBAL_CPU_P0", "REMOVE_QUEUE_P0", "REMOVE_EMPTY_QUEUE_P0",
	"ADDTOQUEUE_P1", "UNQUEUE_P1", "EXEC_P1", "EXEC_EMPTY_P1", "RETURN_VOL_P1", "RETURN_INVOL_P1", "FROM_GLOBAL_CPU_P1", "REMOVE_QUEUE_P1", "REMOVE_EMPTY_QUEUE_P1",
	"ADDTOQUEUE_P2", "UNQUEUE_P2", "EXEC_P2", "EXEC_EMPTY_P2", "RETURN_VOL_P2", "RETURN_INVOL_P2", "FROM_GLOBAL_CPU_P2", "REMOVE_QUEUE_P2", "REMOVE_EMPTY_QUEUE_P2",
	"ADDTOQUEUE_P3", "UNQUEUE_P3", "EXEC_P3", "EXEC_EMPTY_P3", "RETURN_VOL_P3", "RETURN_INVOL_P3", "FROM_GLOBAL_CPU_P3", "REMOVE_QUEUE_P3", "REMOVE_EMPTY_QUEUE_P3",
	"REMOVE_GLOBAL_QUEUE", "START_SMP", "THROW", "QUEUE_GLOBAL"
};

const char *cpu_places_names[] = { "CANTQ", "QUEUE", "CPU", "TOEXEC", "EXECUTING", "SUSPENDED" };

const int hierarchical_transitions[] = { 
	TRAN_ADDTOQUEUE,
	TRAN_EXEC,
	TRAN_EXEC_EMPTY,
	TRAN_RETURN_INVOL,
	TRAN_RETURN_VOL,
	TRAN_REMOVE_QUEUE,
	TRAN_REMOVE_EMPTY_QUEUE,
	TRAN_QUEUE_GLOBAL,
	TRAN_REMOVE_GLOBAL_QUEUE
};

const int hierarchical_corresponse[] = { 
	TRAN_ON_QUEUE,
	TRAN_SET_RUNNING,
	TRAN_SET_RUNNING,
	TRAN_SWITCH_OUT,
	TRAN_TO_WAIT_CHANNEL,
	TRAN_REMOVE,
	TRAN_REMOVE,
	TRAN_ON_QUEUE,
	TRAN_REMOVE
};

/* Extended matrix izq der                GLOBAL TRANSITIONS
	{ 1, 0,-1, 0, 0, 0, 0,-1, 0},					  	       ,{ 0, 0, 0,-1, 0}
	{ 1,-1, 0, 0, 0, 0, 0,-1,-1},					           ,{ 0, 0, 0, 0, 0}
	{ 0,-1, 0, 0, 1, 1,-1, 0, 0},					           ,{ 0, 0, 0, 0, 0}
	{ 0, 1,-1,-1, 0, 0, 1, 0, 0}					           ,{ 0,-1, 0, 0, 0}
	{ 0, 0, 1, 1,-1,-1, 0, 0, 0}					           ,{ 0, 0, 0, 0, 0}
						         { 1, 0,-1, 0, 0, 0, 0,-1, 0}, ,{ 0, 0, 0,-1, 0}
							     { 1,-1, 0, 0, 0, 0, 0,-1,-1}, ,{ 0, 0, 0, 0, 0}
							     { 0,-1, 0, 0, 1, 1,-1, 0, 0}, ,{ 0, 0, 0, 0, 0}
							     { 0, 1,-1,-1, 0, 0, 1, 0, 0}  ,{ 0, 0, 0, 0, 0}
							     { 0, 0, 1, 1,-1,-1, 0, 0, 0}  ,{ 0, 0, 0, 0, 0}
	GLOBAL PLACE
	{ 0, 0, 0, 0, 0, 0,-1, 0, 0} { 0, 0, 0, 0, 0, 0,-1, 0, 0}  ,{-1, 0, 0, 0, 1}
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0} { 0, 0, 0, 0, 0, 0, 0, 0, 0}  ,{ 0, 0,-1, 0, 0}
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0} { 0, 0, 0, 0, 0, 0, 0, 0, 0}  ,{ 0, 0, 1, 0, 0}
*/

static __inline int transition_is_sensitized(int transition_index);
static void resource_fire_single_transition(struct thread *pt, int transition_index);
static int get_automatic_transitions_sensitized(void);

void init_resource_net()
{
	int num_cpu;
	int num_place, num_transition;
	//Initialize incidence matrix of resource net
	for (num_cpu = 0; num_cpu < CPU_NUMBER; num_cpu++) {
		for (num_place = 0; num_place < CPU_BASE_PLACES; num_place++) {
			for (num_transition = 0; num_transition < CPU_BASE_TRANSITIONS; num_transition++) {
				//Copy the base matrix on the parts of the extended matrix where it belongs
 				resource_net.incidence_matrix[num_place+(num_cpu*CPU_BASE_PLACES)][num_transition + (num_cpu*CPU_BASE_TRANSITIONS)] = base_resource_matrix[num_place][num_transition];
				//Do the same for the inhibition matrix
				resource_net.inhibition_matrix[num_place + (num_cpu*CPU_BASE_PLACES)][num_transition + (num_cpu*CPU_BASE_TRANSITIONS)] = base_resource_inhibition_matrix[num_place][num_transition];
			}
		}

		/*for (num_transition = 2; num_transition < CPU_NUMBER_TRANSITION; num_transition += CPU_BASE_TRANSITIONS) {
			if (( (num_transition-2) / CPU_BASE_TRANSITIONS) != num_cpu) {
				//This represents the arc coming from the other CPU executions
				resource_net.incidence_matrix[(num_cpu*CPU_BASE_PLACES)][num_transition] = 1;
			}
		}*/
		//Represents arc going to the throw global transition
		resource_net.incidence_matrix[(num_cpu*CPU_BASE_PLACES)][TRAN_THROW] = -1;

		//Set up initial marking
		if (num_cpu != 0) { //CPU0 starts with no token since it will be the initialization thread who is using it until it returns
			resource_net.mark[PLACE_CPU + (num_cpu*CPU_BASE_PLACES)] = 1;
			resource_net.inhibition_matrix[PLACE_SMP_NOT_READY][(num_cpu*CPU_BASE_TRANSITIONS) + TRAN_FROM_GLOBAL_CPU] = 1;
		}

		//INHIBIT exec if smp not started
		resource_net.inhibition_matrix[PLACE_SMP_NOT_READY][(num_cpu*CPU_BASE_TRANSITIONS) + TRAN_EXEC] = 1;
		resource_net.inhibition_matrix[PLACE_SMP_NOT_READY][(num_cpu*CPU_BASE_TRANSITIONS) + TRAN_ADDTOQUEUE] = 1;
	}

	//Transition to remove from global queue
	resource_net.incidence_matrix[PLACE_GLOBAL_QUEUE][TRAN_REMOVE_GLOBAL_QUEUE] = -1;

	//Transitions to go from smp not ready to ready
	resource_net.incidence_matrix[PLACE_SMP_NOT_READY][TRAN_START_SMP] = -1;
	resource_net.incidence_matrix[PLACE_SMP_READY][TRAN_START_SMP] = 1;

	//We add a token to SMP NOT READY
	resource_net.mark[PLACE_SMP_NOT_READY] = 1;

	//We add a token to PLACE_EXECUTING for CPU 0
	resource_net.mark[PLACE_EXECUTING] = 1;

	for (num_transition = TRAN_FROM_GLOBAL_CPU; num_transition < CPU_NUMBER_TRANSITION; num_transition += CPU_BASE_TRANSITIONS) {
		//Represents arc going from global queue to a specific cpu queue
		resource_net.incidence_matrix[PLACE_GLOBAL_QUEUE][num_transition] = -1;
	}
	//Represents arc to queue on the global queue
	resource_net.incidence_matrix[PLACE_GLOBAL_QUEUE][TRAN_QUEUE_GLOBAL] = 1;

	//Throw transition is automatic
	resource_net.is_automatic_transition[TRAN_THROW] = 1;
	print_detailed_places();
}

static __inline int is_inhibited(int places_index, int transition_index) {
	return ((resource_net.inhibition_matrix[places_index][transition_index] == 1) && (resource_net.mark[places_index] > 0));
}

static __inline int is_hierarchical(int transition) {
	int i;
	for (i = 0; i < (int)(sizeof(hierarchical_transitions) / sizeof(hierarchical_transitions[0])); i++) {
		if (transition == hierarchical_transitions[i])
			return hierarchical_corresponse[i];
		else if((transition < TRAN_REMOVE_GLOBAL_QUEUE) && (transition % CPU_BASE_TRANSITIONS) == hierarchical_transitions[i])
			return hierarchical_corresponse[i];
	}
	return 0;
}

void resource_get_sensitized()
{
	int transition_index;

	for (transition_index = 0; transition_index < CPU_NUMBER_TRANSITION; transition_index++) {
		resource_net.sensitized_buffer[transition_index] = transition_is_sensitized(transition_index);
	}
}


void resource_fire_net(char *trigger, struct thread *pt, int transition_index)
{
	if(pt) {
		int automatic_transition;

		if(!smp_set && smp_started) {
			smp_set = 1;
			resource_fire_single_transition(pt, TRAN_START_SMP);
		}

		if(transition_is_sensitized(transition_index)) {
			resource_fire_single_transition(pt, transition_index);
			automatic_transition = get_automatic_transitions_sensitized();
			while (automatic_transition != -1) {
				resource_fire_single_transition(pt, automatic_transition);
				automatic_transition = get_automatic_transitions_sensitized();
			}
		}
		else {
			if(print_enabled) {
				// TODO: Add a kernel panic exit here. We don't care about post error transitions
				printf("!! %s - Non sensitized transition: %2d - Thread %2d - CPU %2d - FROM %s!!\n", transitions_names[transition_index], transition_index, pt->td_tid, PCPU_GET(cpuid), trigger);
				print_detailed_places();
				transitions_to_print = 0;
			}
		}
	}

	for(int i=0; i<4; i++){
		if(resource_net.mark[PLACE_QUEUE + (i*CPU_BASE_PLACES)] <= 5)
			return;
	}
}


static void resource_fire_single_transition(struct thread *pt, int transition_index) {
	int num_place;
	int local_transition;
	struct timespec ts;

	//Fire cpu net
	for (num_place = 0; num_place< CPU_NUMBER_PLACES; num_place++) {
		resource_net.mark[num_place] = resource_net.mark[num_place] + resource_net.incidence_matrix[num_place][transition_index];
	}
	local_transition = is_hierarchical(transition_index);
	if (local_transition) {
		//If we need to fire a local thread transition we fire it here
		thread_petri_fire(pt, local_transition);
	}

	if (print_enabled && transitions_to_print != 0){
    	nanotime(&ts);
		printf("#& %06ld --- %s Transition OK: %2d - Thread %2d - CPU %2d &#\n", ts.tv_nsec, transitions_names[transition_index], transition_index, pt->td_tid, PCPU_GET(cpuid));
		transitions_to_print--;
	}
}

static int get_automatic_transitions_sensitized()
{
	int num_transition;
	/*FIXME - RIGHT now there is only one automatic transition, if
	this is the case by the end we could hardcode the transition number
	instead of looping the whole array*/
	for (num_transition = 0; num_transition< CPU_NUMBER_TRANSITION; num_transition++) {
		if (resource_net.is_automatic_transition[num_transition] && transition_is_sensitized(num_transition)) {
			return num_transition;
		}
	}

	return -1;
}

static __inline int transition_is_sensitized(int transition_index)
{
	int places_index;

	for (places_index = 0; places_index < CPU_NUMBER_PLACES; places_index++) {

		if (((resource_net.incidence_matrix[places_index][transition_index] < 0) &&
			//If incidence is positive we really dont care if there are tokens or not
			((resource_net.incidence_matrix[places_index][transition_index] + resource_net.mark[places_index]) < 0)) ||
			is_inhibited(places_index, transition_index))
		{
			return 0;
		}
	}

	return 1;
}

int resource_choose_cpu(struct thread* td)
{
	//First we need to know which of the cpu queues is sensitized
	int transition_index;
	int cpu_available = NOCPU;

	if (!smp_started) {
		return TRAN_QUEUE_GLOBAL;
	}

	if (td->td_pinned != 0 || (td->td_flags & TDF_BOUND)) {
		if (transition_is_sensitized(td->td_lastcpu * CPU_BASE_TRANSITIONS)) {
			return 	td->td_lastcpu * CPU_BASE_TRANSITIONS;
		}
		else { //IF no cpu queue available or smp is not ready yet then send to global queue
			return TRAN_QUEUE_GLOBAL;
		}
	}

	//Only check for transitions of addtoqueue
	for (transition_index = TRAN_ADDTOQUEUE; transition_index < CPU_NUMBER_TRANSITION-4; transition_index += CPU_BASE_TRANSITIONS) {
		if (transition_is_sensitized(transition_index)) {
			if (THREAD_CAN_SCHED(td, (transition_index / CPU_BASE_TRANSITIONS)))
				return transition_index;
			else
				cpu_available = transition_index;
		}
	}

	KASSERT(cpu_available == NOCPU, ("no valid CPUs"));
	return cpu_available;
}

void resource_expulse_thread(struct thread *td, int flags) {
	int transition_number;

	if (flags & (SW_VOL)) {
		transition_number = (td->td_lastcpu * CPU_BASE_TRANSITIONS) + TRAN_RETURN_VOL;
		(td)->td_frominh = 1;
	}
	else {
		transition_number = (td->td_lastcpu * CPU_BASE_TRANSITIONS) + TRAN_RETURN_INVOL;
		(td)->td_frominh = 0;
	}
	resource_fire_net("resource_expulse_thread", td, transition_number);
}

void resource_execute_thread(struct thread *newtd, int cpu) {
	int transition_number;

	if(transition_is_sensitized((cpu * CPU_BASE_TRANSITIONS)+ TRAN_EXEC))
		transition_number = (cpu * CPU_BASE_TRANSITIONS) + TRAN_EXEC;
	else
		transition_number = (cpu * CPU_BASE_TRANSITIONS) + TRAN_EXEC_EMPTY;

	resource_fire_net("resource_execute_thread", newtd, transition_number);
}

void resource_remove_thread(struct thread *newtd, int cpu) {
	int transition_number;

	if(transition_is_sensitized((cpu * CPU_BASE_TRANSITIONS)+ TRAN_REMOVE_QUEUE))
		transition_number = (cpu * CPU_BASE_TRANSITIONS) + TRAN_REMOVE_QUEUE;
	else
		transition_number = (cpu * CPU_BASE_TRANSITIONS) + TRAN_REMOVE_EMPTY_QUEUE;

	resource_fire_net("resource_remove_thread", newtd, transition_number);
}

void print_resource_net() {
	int i;
	printf(": ");
	for (i = 0; i < CPU_NUMBER_PLACES; i++) {
		printf("%d ", resource_net.mark[i]);
	}
	printf("\n");
}

void print_uni_label() {
printf("_____    ____ U _____ u  _____  __   __  _   _     \n");
printf(" |\" ___|U /\"___|\\| ___\"|/ |\" ___| \\ \\ / / | \\ |\"|   \n");
printf("U| |_  u\\| | u   |  _|\"  U| |_  u  \\ V / <|  \\| |>  \n");
printf("\\|  _|/  | |/__  | |___  \\|  _|/  U_|\"|_uU| |\\  |u  \n");
printf(" |_|      \\____| |_____|  |_|       |_|   |_| \\_|   \n");
printf(" )(\\\\,-  _// \\\\  <<   >>  )(\\\\,-.-,//|(_  ||   \\\\,-.\n");
printf("(__)(_/ (__)(__)(__) (__)(__)(_/ \\_) (__) (_\")  (_/ \n");
printf("Colas de CPU: CPU_0 %d CPU_1 %d CPU_2 %d CPU_3 %d \n", resource_net.mark[PLACE_QUEUE + (0*CPU_BASE_PLACES)],
	resource_net.mark[PLACE_QUEUE + (1*CPU_BASE_PLACES)], resource_net.mark[PLACE_QUEUE + (2*CPU_BASE_PLACES)],
	resource_net.mark[PLACE_QUEUE + (3*CPU_BASE_PLACES)]);
}

void print_cpu_places() {
	printf("PLACE CPU_0: %d \n", resource_net.mark[PLACE_CPU + (0*CPU_BASE_PLACES)]);
	printf("PLACE CPU_1: %d \n", resource_net.mark[PLACE_CPU + (1*CPU_BASE_PLACES)]);
	printf("PLACE CPU_2: %d \n", resource_net.mark[PLACE_CPU + (2*CPU_BASE_PLACES)]);
	printf("PLACE CPU_3: %d \n", resource_net.mark[PLACE_CPU + (3*CPU_BASE_PLACES)]);
}

void print_detailed_places() {
	for (int i = 0; i < CPU_BASE_PLACES; i++){
		for (int j = 0; j < CPU_NUMBER; j++){
			printf("\n#& %d -> %s_P%d &#", resource_net.mark[i + (j*CPU_BASE_PLACES)], cpu_places_names[i], j);
		}
	}
	printf("\n#& %d -> GLOBAL_QUEUE &#", resource_net.mark[PLACE_GLOBAL_QUEUE]);
	printf("\n#& %d -> SMP_NOT_READY &#", resource_net.mark[PLACE_SMP_NOT_READY]);
	printf("\n#& %d -> SMP_READY &#\n", resource_net.mark[PLACE_SMP_READY]);
}

void set_print_transition(int number_transitions) {
	transitions_to_print = number_transitions;
}
