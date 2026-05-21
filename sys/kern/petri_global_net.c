#include <sys/types.h>
#include <sys/param.h>
#include <sys/cpuset.h>
#include <sys/pcpu.h>
#include <sys/sdt.h>
#include <sys/smp.h>
#include <sys/systm.h>
#include <sys/time.h>
#include <sys/sched_petri.h>

#define	THREAD_CAN_SCHED(td, cpu)	\
	CPU_ISSET((cpu), &(td)->td_cpuset->cs_mask)

int smp_set = 0;
int print_enabled = 1;
int transitions_to_print = 0;
struct petri_cpu_resource_net resource_net;

SDT_PROVIDER_DEFINE(petri);

SDT_PROBE_DEFINE5(petri, resource, transition, fire,
    "struct thread *", "char *", "int", "int", "int");
SDT_PROBE_DEFINE5(petri, resource, transition, blocked,
    "struct thread *", "char *", "int", "int", "int");
SDT_PROBE_DEFINE5(petri, resource, addtoqueue, policy,
    "struct thread *", "char *", "int", "int", "int");
SDT_PROBE_DEFINE5(petri, resource, addtoqueue, forced,
    "struct thread *", "char *", "int", "int", "int");

const int base_resource_matrix[CPU_BASE_PLACES][CPU_BASE_TRANSITIONS] = {
	/* Base matrix. */
	{ 1, 0, -1, 0, 0, 0, 0, -1, 0, 1 },
	{ 1, -1, 0, 0, 0, 0, 0, -1, -1, 1 },
	{ 0, -1, 0, 0, 1, 1, -1, 0, 0, 0 },
	{ 0, 1, -1, -1, 0, 0, 1, 0, 0, 0 },
	{ 0, 0, 1, 1, -1, -1, 0, 0, 0, 0 }
};

const int base_resource_inhibition_matrix[CPU_BASE_PLACES]
    [CPU_BASE_TRANSITIONS] = {
	/* Base inhibition matrix. */
	{ 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }
};

const char *transitions_names[] = {
	"ADDTOQUEUE_P0",
	"UNQUEUE_P0",
	"EXEC_P0",
	"EXEC_EMPTY_P0",
	"RETURN_VOL_P0",
	"RETURN_INVOL_P0",
	"FROM_GLOBAL_CPU_P0",
	"REMOVE_QUEUE_P0",
	"REMOVE_EMPTY_QUEUE_P0",
	"ADDTOQUEUE_FORCED_P0",
	"ADDTOQUEUE_P1",
	"UNQUEUE_P1",
	"EXEC_P1",
	"EXEC_EMPTY_P1",
	"RETURN_VOL_P1",
	"RETURN_INVOL_P1",
	"FROM_GLOBAL_CPU_P1",
	"REMOVE_QUEUE_P1",
	"REMOVE_EMPTY_QUEUE_P1",
	"ADDTOQUEUE_FORCED_P1",
	"ADDTOQUEUE_P2",
	"UNQUEUE_P2",
	"EXEC_P2",
	"EXEC_EMPTY_P2",
	"RETURN_VOL_P2",
	"RETURN_INVOL_P2",
	"FROM_GLOBAL_CPU_P2",
	"REMOVE_QUEUE_P2",
	"REMOVE_EMPTY_QUEUE_P2",
	"ADDTOQUEUE_FORCED_P2",
	"ADDTOQUEUE_P3",
	"UNQUEUE_P3",
	"EXEC_P3",
	"EXEC_EMPTY_P3",
	"RETURN_VOL_P3",
	"RETURN_INVOL_P3",
	"FROM_GLOBAL_CPU_P3",
	"REMOVE_QUEUE_P3",
	"REMOVE_EMPTY_QUEUE_P3",
	"ADDTOQUEUE_FORCED_P3",
	"REMOVE_GLOBAL_QUEUE",
	"START_SMP",
	"THROW",
	"QUEUE_GLOBAL"
};

const char *cpu_places_names[] = {
	"CANTQ",
	"QUEUE",
	"CPU",
	"TOEXEC",
	"EXECUTING",
	"SUSPENDED"
};

const int hierarchical_transitions[] = {
	TRAN_ADDTOQUEUE,
	TRAN_EXEC,
	TRAN_EXEC_EMPTY,
	TRAN_RETURN_INVOL,
	TRAN_RETURN_VOL,
	TRAN_REMOVE_QUEUE,
	TRAN_REMOVE_EMPTY_QUEUE,
	TRAN_ADDTOQUEUE_FORCED,
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
	TRAN_ON_QUEUE,
	TRAN_REMOVE
};

static void resource_fire_single_transition(struct thread *pt,
    const char *trigger,
    int transition_index);
static int get_automatic_transitions_sensitized(void);
static __inline int resource_transition_base(int transition_index);
static __inline int resource_transition_cpu(int transition_index);

static __inline int
resource_transition_base(int transition_index)
{
	if (transition_index >= 0 &&
	    transition_index < (CPU_BASE_TRANSITIONS * CPU_NUMBER))
		return (transition_index % CPU_BASE_TRANSITIONS);
	return (transition_index);
}

static __inline int
resource_transition_cpu(int transition_index)
{
	if (transition_index >= 0 &&
	    transition_index < (CPU_BASE_TRANSITIONS * CPU_NUMBER))
		return (transition_index / CPU_BASE_TRANSITIONS);
	return (NOCPU);
}

int
resource_valid_cpu(int cpu)
{
	return (cpu >= 0 && cpu < CPU_NUMBER);
}

int
resource_valid_transition(int transition_index)
{
	return (transition_index >= 0 &&
	    transition_index < CPU_NUMBER_TRANSITION);
}

void
init_resource_net(void)
{
	int place_index, transition_index;
	int num_cpu;
	int num_place, num_transition;

	for (num_cpu = 0; num_cpu < CPU_NUMBER; num_cpu++) {
		for (num_place = 0; num_place < CPU_BASE_PLACES;
		    num_place++) {
			for (num_transition = 0;
			    num_transition < CPU_BASE_TRANSITIONS;
			    num_transition++) {
				place_index = num_place +
				    (num_cpu * CPU_BASE_PLACES);
				transition_index = num_transition +
				    (num_cpu * CPU_BASE_TRANSITIONS);
				resource_net.incidence_matrix[place_index]
				    [transition_index] =
				    base_resource_matrix[num_place]
				    [num_transition];
				resource_net.inhibition_matrix[place_index]
				    [transition_index] =
				    base_resource_inhibition_matrix[num_place]
				    [num_transition];
			}
		}

		resource_net.incidence_matrix[num_cpu * CPU_BASE_PLACES]
		    [TRAN_THROW] = -1;

		if (num_cpu != 0) {
			resource_net.mark[PLACE_CPU +
			    (num_cpu * CPU_BASE_PLACES)] = 1;
			resource_net.inhibition_matrix[PLACE_SMP_NOT_READY]
			    [(num_cpu * CPU_BASE_TRANSITIONS) +
			    TRAN_FROM_GLOBAL_CPU] = 1;
		}

		resource_net.inhibition_matrix[PLACE_SMP_NOT_READY]
		    [(num_cpu * CPU_BASE_TRANSITIONS) + TRAN_EXEC] = 1;
		resource_net.inhibition_matrix[PLACE_SMP_NOT_READY]
		    [(num_cpu * CPU_BASE_TRANSITIONS) + TRAN_ADDTOQUEUE] = 1;
		resource_net.inhibition_matrix[PLACE_SMP_NOT_READY]
		    [(num_cpu * CPU_BASE_TRANSITIONS) +
		    TRAN_ADDTOQUEUE_FORCED] = 1;
	}

	resource_net.incidence_matrix[PLACE_GLOBAL_QUEUE]
	    [TRAN_REMOVE_GLOBAL_QUEUE] = -1;

	resource_net.incidence_matrix[PLACE_SMP_NOT_READY]
	    [TRAN_START_SMP] = -1;
	resource_net.incidence_matrix[PLACE_SMP_READY][TRAN_START_SMP] = 1;

	resource_net.mark[PLACE_SMP_NOT_READY] = 1;

	resource_net.mark[PLACE_EXECUTING] = 1;

	for (num_transition = TRAN_FROM_GLOBAL_CPU;
	    num_transition < CPU_NUMBER_TRANSITION;
	    num_transition += CPU_BASE_TRANSITIONS) {
		resource_net.incidence_matrix[PLACE_GLOBAL_QUEUE]
		    [num_transition] = -1;
	}
	resource_net.incidence_matrix[PLACE_GLOBAL_QUEUE]
	    [TRAN_QUEUE_GLOBAL] = 1;

	resource_net.is_automatic_transition[TRAN_THROW] = 1;
	print_detailed_places();
}

static __inline int
is_inhibited(int places_index, int transition_index)
{
	return (resource_net.inhibition_matrix[places_index]
	    [transition_index] == 1 &&
	    resource_net.mark[places_index] > 0);
}

static __inline int
is_hierarchical(int transition)
{
	int i;

	for (i = 0; i < (int)(sizeof(hierarchical_transitions) /
	    sizeof(hierarchical_transitions[0])); i++) {
		if (transition == hierarchical_transitions[i])
			return (hierarchical_corresponse[i]);
		else if (transition < TRAN_REMOVE_GLOBAL_QUEUE &&
		    transition % CPU_BASE_TRANSITIONS ==
		    hierarchical_transitions[i])
			return (hierarchical_corresponse[i]);
	}
	return (0);
}

void
resource_get_sensitized(void)
{
	int transition_index;

	for (transition_index = 0; transition_index < CPU_NUMBER_TRANSITION;
	    transition_index++) {
		resource_net.sensitized_buffer[transition_index] =
		    transition_is_sensitized(transition_index);
	}
}


void
resource_fire_net(const char *trigger, struct thread *pt,
    int transition_index)
{
	int automatic_transition;

	if (pt == NULL)
		return;

	if (!resource_valid_transition(transition_index)) {
		panic("petri: invalid resource transition %d from %s, td %p "
		    "tid %d lastcpu %d oncpu %d", transition_index, trigger,
		    pt, pt->td_tid, pt->td_lastcpu, pt->td_oncpu);
	}

	if (!smp_set && smp_started) {
		smp_set = 1;
		resource_fire_single_transition(pt, trigger, TRAN_START_SMP);
	}

	if (transition_is_sensitized(transition_index)) {
		resource_fire_single_transition(pt, trigger, transition_index);
		automatic_transition = get_automatic_transitions_sensitized();
		while (automatic_transition != -1) {
			resource_fire_single_transition(pt, trigger,
			    automatic_transition);
			automatic_transition =
			    get_automatic_transitions_sensitized();
		}
	} else {
		SDT_PROBE5(petri, resource, transition, blocked, pt,
		    trigger, transition_index,
		    resource_transition_cpu(transition_index),
		    PCPU_GET(cpuid));
		print_detailed_places();
		panic("petri: non-sensitized resource transition %s(%d) "
		    "from %s, td %p tid %d cpu %d lastcpu %d",
		    transitions_names[transition_index], transition_index,
		    trigger, pt, pt->td_tid, PCPU_GET(cpuid),
		    pt->td_lastcpu);
	}
}


static void
resource_fire_single_transition(struct thread *pt, const char *trigger,
    int transition_index)
{
	int num_place;
	int cpu;
	int local_transition;

	if (!resource_valid_transition(transition_index)) {
		panic("petri: invalid single resource transition %d, td %p "
		    "tid %d", transition_index, pt, pt != NULL ? pt->td_tid :
		    -1);
	}

	for (num_place = 0; num_place < CPU_NUMBER_PLACES; num_place++) {
		resource_net.mark[num_place] +=
		    resource_net.incidence_matrix[num_place][transition_index];
	}
	local_transition = is_hierarchical(transition_index);
	cpu = resource_transition_cpu(transition_index);
	SDT_PROBE5(petri, resource, transition, fire, pt, trigger,
	    transition_index, cpu, local_transition);
	if (cpu != NOCPU) {
		switch (resource_transition_base(transition_index)) {
		case TRAN_ADDTOQUEUE:
			SDT_PROBE5(petri, resource, addtoqueue, policy, pt,
			    trigger, cpu,
			    resource_net.mark[PLACE_QUEUE +
			    (cpu * CPU_BASE_PLACES)],
			    resource_net.mark[PLACE_CANTQ +
			    (cpu * CPU_BASE_PLACES)]);
			break;
		case TRAN_ADDTOQUEUE_FORCED:
			SDT_PROBE5(petri, resource, addtoqueue, forced, pt,
			    trigger, cpu,
			    resource_net.mark[PLACE_QUEUE +
			    (cpu * CPU_BASE_PLACES)],
			    resource_net.mark[PLACE_CANTQ +
			    (cpu * CPU_BASE_PLACES)]);
			break;
		}
	}
	if (local_transition) {
		thread_petri_fire(pt, local_transition);
	}

	if (print_enabled && transitions_to_print != 0) {
		printf("#& %s Transition OK: %2d - Thread %2d - CPU %2d "
		    "&#\n", transitions_names[transition_index],
		    transition_index, pt->td_tid, PCPU_GET(cpuid));
		transitions_to_print--;
	}
}

static int
get_automatic_transitions_sensitized(void)
{
	int num_transition;

	/*
	 * TRAN_THROW is currently the only automatic transition, but this
	 * loop keeps the transition table extensible.
	 */
	for (num_transition = 0; num_transition < CPU_NUMBER_TRANSITION;
	    num_transition++) {
		if (resource_net.is_automatic_transition[num_transition] &&
		    transition_is_sensitized(num_transition))
			return (num_transition);
	}

	return (-1);
}

int
transition_is_sensitized(int transition_index)
{
	int places_index;

	if (!resource_valid_transition(transition_index))
		panic("petri: invalid transition_is_sensitized index %d",
		    transition_index);

	for (places_index = 0; places_index < CPU_NUMBER_PLACES;
	    places_index++) {
		if ((resource_net.incidence_matrix[places_index]
		    [transition_index] < 0 &&
		    resource_net.incidence_matrix[places_index]
		    [transition_index] + resource_net.mark[places_index] < 0) ||
		    is_inhibited(places_index, transition_index))
			return (0);
	}

	return (1);
}

int
resource_choose_cpu(struct thread *td)
{
	int transition_index;
	int best = NOCPU;

	if (td->td_lastcpu != NOCPU && !resource_valid_cpu(td->td_lastcpu)) {
		panic("petri: invalid lastcpu %d in resource_choose_cpu, td %p "
		    "tid %d", td->td_lastcpu, td, td->td_tid);
	}

	if (td->td_lastcpu != NOCPU &&
	    resource_valid_cpu(td->td_lastcpu) &&
	    THREAD_CAN_SCHED(td, td->td_lastcpu) &&
	    transition_is_sensitized(td->td_lastcpu * CPU_BASE_TRANSITIONS)) {
		best = td->td_lastcpu;
		return (best);
	}

	for (transition_index = TRAN_ADDTOQUEUE;
	    transition_index < CPU_NUMBER_TRANSITION - 4;
	    transition_index += CPU_BASE_TRANSITIONS) {
		if (transition_is_sensitized(transition_index)) {
			if (!THREAD_CAN_SCHED(td, transition_index /
			    CPU_BASE_TRANSITIONS))
				continue;
			else {
				best = transition_index / CPU_BASE_TRANSITIONS;
				break;
			}
		}
	}

	return (best);
}

void
resource_expulse_thread(struct thread *td, int flags)
{
	int transition_number;

	if (!resource_valid_cpu(td->td_lastcpu)) {
		panic("petri: invalid lastcpu %d in resource_expulse_thread, "
		    "td %p tid %d flags %#x", td->td_lastcpu, td, td->td_tid,
		    flags);
	}

	if (flags & (SW_VOL)) {
		transition_number = (td->td_lastcpu * CPU_BASE_TRANSITIONS) +
		    TRAN_RETURN_VOL;
		(td)->td_frominh = 1;
	} else {
		transition_number = (td->td_lastcpu * CPU_BASE_TRANSITIONS) +
		    TRAN_RETURN_INVOL;
		(td)->td_frominh = 0;
	}
	resource_fire_net("resource_expulse_thread", td, transition_number);
}

void
resource_execute_thread(struct thread *newtd, int cpu)
{
	int transition_number;

	if (!resource_valid_cpu(cpu)) {
		panic("petri: invalid cpu %d in resource_execute_thread, "
		    "td %p tid %d", cpu, newtd, newtd != NULL ? newtd->td_tid :
		    -1);
	}

	transition_number = (cpu * CPU_BASE_TRANSITIONS) + TRAN_EXEC;
	if (!transition_is_sensitized(transition_number)) {
		transition_number = (cpu * CPU_BASE_TRANSITIONS) +
		    TRAN_EXEC_EMPTY;
	}

	resource_fire_net("resource_execute_thread", newtd, transition_number);
}

void
resource_remove_thread(struct thread *newtd, int cpu)
{
	int transition_number;

	if (!resource_valid_cpu(cpu)) {
		panic("petri: invalid cpu %d in resource_remove_thread, td %p "
		    "tid %d", cpu, newtd, newtd != NULL ? newtd->td_tid : -1);
	}

	if (transition_is_sensitized((cpu * CPU_BASE_TRANSITIONS) +
	    TRAN_REMOVE_QUEUE))
		transition_number = (cpu * CPU_BASE_TRANSITIONS) +
		    TRAN_REMOVE_QUEUE;
	else
		transition_number = (cpu * CPU_BASE_TRANSITIONS) +
		    TRAN_REMOVE_EMPTY_QUEUE;

	resource_fire_net("resource_remove_thread", newtd, transition_number);
}

void
print_detailed_places(void)
{
	int i, j;

	for (i = 0; i < CPU_BASE_PLACES; i++) {
		for (j = 0; j < CPU_NUMBER; j++) {
			printf("\n#& %d -> %s_P%d &#",
			    resource_net.mark[i + (j * CPU_BASE_PLACES)],
			    cpu_places_names[i], j);
		}
	}
	printf("\n#& %d -> GLOBAL_QUEUE &#",
	    resource_net.mark[PLACE_GLOBAL_QUEUE]);
	printf("\n#& %d -> SMP_NOT_READY &#",
	    resource_net.mark[PLACE_SMP_NOT_READY]);
	printf("\n#& %d -> SMP_READY &#\n", resource_net.mark[PLACE_SMP_READY]);
}

void
set_print_transition(int number_transitions)
{
	transitions_to_print = number_transitions;
}
