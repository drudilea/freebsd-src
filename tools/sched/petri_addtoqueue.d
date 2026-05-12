#!/usr/sbin/dtrace -s

/*
 * Summarize Petri scheduler enqueue decisions and fired transitions.
 *
 * Usage:
 *     # kldload dtraceall
 *     # dtrace -l -P petri
 *     # ./tools/sched/petri_addtoqueue.d
 *
 * Run the workload in another terminal and stop this script with Ctrl-C.
 */

#pragma D option quiet
#pragma D option dynvarsize=16m

dtrace:::BEGIN
{
	printf("Tracing Petri addtoqueue probes. Press Ctrl-C to summarize.\n");
}

petri:sched:addtoqueue:decision
{
	@decisions[
	    args[3] == 1 ? "pinned" :
	    args[3] == 2 ? "bound" :
	    args[3] == 3 ? "affinity-fallback" : "policy",
	    args[1],
	    args[2]] = count();
}

petri:sched:addtoqueue:global
{
	@global[args[2] ? "smp-started" : "smp-not-started"] = count();
}

petri:resource:addtoqueue:policy
{
	@fired["policy", args[2]] = count();
	@queue_after["policy", args[2], args[3]] = count();
	@cantq_after["policy", args[2], args[4]] = count();
}

petri:resource:addtoqueue:forced
{
	@fired["forced", args[2]] = count();
	@queue_after["forced", args[2], args[3]] = count();
	@cantq_after["forced", args[2], args[4]] = count();
}

petri:resource:transition:blocked
{
	@blocked[args[2], args[3], args[4]] = count();
}

dtrace:::END
{
	printf("\nPetri sched_add decisions by reason, target CPU and transition:\n");
	printa("%20s cpu=%d transition=%d %@d\n", @decisions);

	printf("\nPetri global queue enqueues:\n");
	printa("%20s %@d\n", @global);

	printf("\nPetri addtoqueue transitions fired by kind and CPU:\n");
	printa("%20s cpu=%d %@d\n", @fired);

	printf("\nPetri queue mark after addtoqueue by kind, CPU and mark:\n");
	printa("%20s cpu=%d queue_mark=%d %@d\n", @queue_after);

	printf("\nPetri CANTQ mark after addtoqueue by kind, CPU and mark:\n");
	printa("%20s cpu=%d cantq_mark=%d %@d\n", @cantq_after);

	printf("\nBlocked Petri transitions by transition, target CPU and running CPU:\n");
	printa("transition=%d target_cpu=%d running_cpu=%d %@d\n", @blocked);
}
