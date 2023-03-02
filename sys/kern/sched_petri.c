#include <sys/sched_petri.h>

/*
GLOBAL VARIABLES
*/
const int matrix_Incidence[PLACES_SIZE][TRANSITIONS_SIZE] = {
	{-1,  0,  0,  0,  0,  0 ,  0},
	{ 1, -1,  0,  1,  0,  1 ,  1},
	{ 0,  1, -1,  0,  0,  0 , -1},
	{ 0,  0,  1, -1, -1,  0 ,  0},
	{ 0,  0,  0,  0,  1, -1 ,  0}
};

const int initial_mark[PLACES_SIZE] = { 0, 1, 0, 0, 0 };

const char *thread_transitions_names[] = {
	"TRAN_INIT", "TRAN_ON_QUEUE", "TRAN_SET_RUNNING", "TRAN_SWITCH_OUT", "TRAN_TO_WAIT_CHANNEL", "TRAN_WAKEUP", "TRAN_REMOVE"
};

const char *thread_places[] = {
	"INACTIVE", "CAN_RUN", "RUNQ", "RUNNING", "INHIBITED",
};

const char *thread_state_to_string[] = {
	"INACTIVE", "INHIBITED", "CAN_RUN", "RUNQ", "RUNNING",
};

__inline int
thread_transition_is_sensitized(struct thread *pt, int transition_index);


void
init_petri_thread(struct thread *pt_thread){
	// Create a new petr_thread structure
	int i;
	for (i = 0; i < PLACES_SIZE; i++) {
		pt_thread->mark[i] = initial_mark[i];
	}
	pt_thread->td_frominh = 0;
}

void
thread_get_sensitized(struct thread *pt)
{
	int k;
	for(k=0; k< TRANSITIONS_SIZE; k++){
		if(thread_transition_is_sensitized(pt, k))
			pt->sensitized_buffer[k] = 1;
		else
			pt->sensitized_buffer[k] = 0;
	}
};

__inline int
thread_transition_is_sensitized(struct thread *pt, int transition_index)
{
	int places_index;

	for (places_index = 0; places_index < PLACES_SIZE; places_index++) {

		if (((matrix_Incidence[places_index][transition_index] < 0) && 
			//If incidence is positive we really dont care if there are tokens or not
			((matrix_Incidence[places_index][transition_index] + pt->mark[places_index]) < 0)))
		{
			return 0;
		}
	}

	return 1;

}

void thread_print_detailed_places(struct thread *pt) {
	for (int i = 0; i < PLACES_SIZE; i++){
		printf("#& %d -> %s &#\n", pt->mark[i], thread_places[i]);
	}
}

void
thread_petri_fire(struct thread *pt, int transition)
{
	int i;
	if(thread_transition_is_sensitized(pt, transition)){
		for(i=0; i< PLACES_SIZE; i++)
			pt->mark[i] += matrix_Incidence[i][transition];
	}
	else {
		printf("!! %s - NON SENSITIZED THREAD transition: %2d - Thread %2d -> State %d !!\n", thread_transitions_names[transition], transition, pt->td_tid, pt->td_state);
		thread_print_detailed_places(pt);
	}
}


/*This method is not used yet */
static void
thread_search_and_fire(struct thread *pt){
	int i;
	thread_get_sensitized(pt);
	i=0;
	while((pt->sensitized_buffer[i] != 1) && (i < TRANSITIONS_SIZE)){
		i++;
	}
	if(i < TRANSITIONS_SIZE){
		thread_petri_fire(pt, i);
	}
}
