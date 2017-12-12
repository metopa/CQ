/*
 * cqpipeline.c
 *  Circular queue pipiline module
 * @author Jiri Kaspar, CVUT FIT
 * @author Viacheslav Kroilov, CVUT FIT
 *
 * verze 0.9.7    9.12.2017
 */

#define VERSION "0.9.7"

// #define _GNU_SOURCE je tu proto, aby fungovaly CPU_SET apod.
#define _GNU_SOURCE

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <zconf.h>

//
#define SHARED volatile
#define ALIGNED __attribute__ ((aligned (64)))

//#define CLOCK CLOCK_THREAD_CPUTIME_ID
#define CLOCK CLOCK_REALTIME

#include "CQpipeline.h"

#ifdef DEBUG
#define Dprintf(f, v)                                                                              \
  {                                                                                                \
	if (cq_opt & OPT_DEBUG) printf (f, v);                                                         \
  }
#define Eprintf(f, v, w)                                                                           \
  {                                                                                                \
	if (cq_opt & OPT_DEBUG) printf (f, v, w);                                                      \
  }
#else
#define Dprintf(f, v)                                                                              \
  {}
#define Eprintf(f, v, w)                                                                           \
  {}
#endif

#define handle_error_en(en, msg)                                                                   \
  do {                                                                                             \
    errno = en;                                                                                    \
    perror (msg);                                                                                  \
    exit (EXIT_FAILURE);                                                                           \
  } while (0)




void cqBindThread(int cpu) {
	cpu_set_t cpuset;
	int status;

	CPU_ZERO (&cpuset);
	CPU_SET (cpu, &cpuset);
	status = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
	if (status != 0)
		handle_error_en (status, "pthread_setaffinity_np");
}

void* CQPipelineThread(void* parg) {
	unsigned int stage, incarnation;
	CQhandle *in_h, *out_h;

	CQPipelineThreadCtx* arg = (CQPipelineThreadCtx*) parg;
	unsigned int id = arg->id;
	CQPipelineCtx* ctx = arg->ctx;

	in_h = ctx->cq_inputs[id];
	out_h = ctx->cq_outputs[id];
	if (in_h)
		in_h->cpu = id;
	if (out_h)
		out_h->cpu = id;
	incarnation = id;
	stage = 0;
	while (incarnation >= ctx->cq_stages[stage]) {
		incarnation -= ctx->cq_stages[stage];
		stage++;
	}

	cqBindThread(ctx->cq_cpus[id]);
	Eprintf ("Thread %d bound to CPU %d\n", id, cq_cpus[id]);

	Eprintf("Thread %d, Stage=%d\n", id, stage);

	__sync_fetch_and_add(&ctx->atomic_barrier, 1); // count running threads
	while (ctx->atomic_barrier != ctx->num_threads) {}; // a barrier wait for all other threads

	ctx->worker_func(ctx, id, stage, incarnation, in_h, out_h);

	__sync_fetch_and_add(&ctx->atomic_barrier, -1); // mark thread done
}


CQPipelineCtx* CQPipelineCtx_create(unsigned int queue_type,
									unsigned int queue_size,
									unsigned int msg_size,
									pipeline_worker_func_t worker_func,
									pipeline_help_func_t help_func,
									pipeline_stat_func_t stat_func,
									pipeline_unknown_cl_opt_handler_t unknown_opt_handler,
									void* user_data) {
	unsigned int i;
	CQPipelineCtx* ctx = (CQPipelineCtx*) malloc(sizeof(CQPipelineCtx));

	memset(ctx, 0, sizeof(ctx));

	for (i = 0; i < MAX_THREADS; i++) { // default setting
		ctx->cq_cpus[i] = i;
		ctx->cq_queuetypes[i] = queue_type;
		ctx->cq_queuesizes[i] = queue_size;
		ctx->cq_msgsizes[i] = msg_size;
		ctx->thread_ctx[i].ctx = ctx;
		ctx->thread_ctx[i].id = i;
	}
	ctx->cq_stages[0] = 1;
	ctx->num_threads = 1;
	ctx->num_stages = 1;

	ctx->worker_func = worker_func;
	ctx->help_func = help_func;
	ctx->stat_func = stat_func;
	ctx->unknown_opt_handler = unknown_opt_handler;
	ctx->cq_user_data = user_data;

	return ctx;
}

int CQPipelineCtx_parseClOpts(CQPipelineCtx* ctx, int argc, char** argv) {
	int a, i, j, appl_argc;
	char* t;
	unsigned int c1, c2;

	for (a = 1, appl_argc = 1; a < argc; a++) { // overwrite defaults by command line values

		if (((strcmp (argv[a], "-c") == 0) || (strcmp (argv[a], "--cpus") == 0)) && (a + 1 < argc)) {
			a++;
			t = strtok(argv[a], ",.");
			j = 0;
			while (t) {
				if (sscanf(t, "%u:%u", &c1, &c2) == 2) {
					for (i = c1; i <= c2; i++, j++) { ctx->cq_cpus[j] = (unsigned int) i; }
				} else if (sscanf(t, "%u", &(ctx->cq_cpus[j])) == 1) {
					j++;
				}
				t = strtok(NULL, ",.");
			}
		} else if (((strcmp (argv[a], "-t") == 0) || (strcmp (argv[a], "--threads") == 0)) &&
				   (a + 1 < argc)) {
			a++;
			if (argv[a] && (sscanf(argv[a], "%u", &ctx->num_threads) == 1)) {}
		} else if (((strcmp (argv[a], "-s") == 0) || (strcmp (argv[a], "--stages") == 0)) &&
				   (a + 1 < argc)) {
			a++;
			t = strtok(argv[a], ",.");
			j = 0;
			ctx->num_stages = 0;
			while (t) {
				if (sscanf(t, "%u", &(ctx->cq_stages[j])) == 1) {
					j++;
					ctx->num_stages++;
				}
				t = strtok(NULL, ",.");
			}
		} else if (((strcmp (argv[a], "-q") == 0) || (strcmp (argv[a], "--queuesizes") == 0)) &&
				   (a + 1 < argc)) {
			a++;
			t = strtok(argv[a], ",.");
			j = 0;
			while (t) {
				if (sscanf(t, "%u", &(ctx->cq_queuesizes[j])) == 1) { j++; }
				t = strtok(NULL, ",.");
			}
		} else if (((strcmp (argv[a], "-m") == 0) || (strcmp (argv[a], "--msgsize") == 0)) &&
				   (a + 1 < argc)) {
			a++;
			if (argv[a] && (sscanf(argv[a], "%u", &(ctx->cq_msgsizes[0])) == 1)) {}
		} else if (((strcmp (argv[a], "-T") == 0) || (strcmp (argv[a], "--types") == 0)) &&
				   (a + 1 < argc)) {
			a++;
			t = strtok(argv[a], ",.");
			j = 0;
			while (t) {
				if (t[0] == 'N') ctx->cq_queuetypes[j] = RDR_N;
				if (t[0] == 'B') ctx->cq_queuetypes[j] = RDR_B;
				j++;
				t = strtok(NULL, ",.");
			}
		} else if (((strcmp (argv[a], "-o") == 0) || (strcmp (argv[a], "--optimizations") == 0)) &&
				   (a + 1 < argc)) {
			a++;
			t = strtok(argv[a], ",");
			while (t) {
				if (strcmp (t, "PRERP") == 0) ctx->cq_opt |= OPT_PRERP;
				if (strcmp (t, "PRERD") == 0) ctx->cq_opt |= OPT_PRERD;
				if (strcmp (t, "PREWP") == 0) ctx->cq_opt |= OPT_PREWP;
				if (strcmp (t, "PREWD") == 0) ctx->cq_opt |= OPT_PREWD;
				if (strcmp (t, "PRE") == 0) ctx->cq_opt |= OPT_PRE;
				if (strcmp (t, "FLURP") == 0) ctx->cq_opt |= OPT_FLURP;
				if (strcmp (t, "FLURD") == 0) ctx->cq_opt |= OPT_FLURD;
				if (strcmp (t, "FLUWP") == 0) ctx->cq_opt |= OPT_FLUWP;
				if (strcmp (t, "FLUWD") == 0) ctx->cq_opt |= OPT_FLUWD;
				if (strcmp (t, "FLU") == 0) ctx->cq_opt |= OPT_FLU;
				if (strcmp (t, "NTMRP") == 0) ctx->cq_opt |= OPT_NTMRP;
				if (strcmp (t, "NTMRD") == 0) ctx->cq_opt |= OPT_NTMRD;
				if (strcmp (t, "NTMWP") == 0) ctx->cq_opt |= OPT_NTMWP;
				if (strcmp (t, "NTMWD") == 0) ctx->cq_opt |= OPT_NTMWD;
				if (strcmp (t, "NTM") == 0) ctx->cq_opt |= OPT_NTM;
				//        if (strcmp(t, "LATER") == 0) cq_opt |= OPT_LATER;
				//        if (strcmp(t, "LATEW") == 0) cq_opt |= OPT_LATEW;
				if (strcmp (t, "LPUR") == 0) ctx->cq_opt |= OPT_LPUR;
				if (strcmp (t, "LPUW") == 0) ctx->cq_opt |= OPT_LPUW;
				if (strcmp (t, "CCA") == 0) ctx->cq_opt |= OPT_CCA;
				if (strcmp (t, "DEBUG") == 0) ctx->cq_opt |= OPT_DEBUG;
				if (strcmp (t, "WSTFW") == 0) ctx->cq_opt |= OPT_WSTFW;
				if (strcmp (t, "WSTBW") == 0) ctx->cq_opt |= OPT_WSTBW;
				if (strcmp (t, "WST") == 0) ctx->cq_opt |= OPT_WST;
				if (strcmp (t, "TRACE") == 0) {
					ctx->cq_opt |= OPT_TRACE;
					//notracing = 0; //TODO Check usage
				}
				if (strcmp (t, "CNT") == 0) ctx->cq_opt |= OPT_CNT;
				//        if (strcmp(t, "BATCH") == 0) cq_opt |= OPT_BATCH;
				t = strtok(NULL, ",.");
			}
			/*  } else if ( ((strcmp(argv[a], "-S") == 0) || (strcmp(argv[a], "--tracestart") == 0)) &&
		   (a+1<arg) ) {
				a++;
				if (argv[a] && ((sscanf(argv[a], "%u", &ctx->cq_tracestart) == 1))) {
				}
			} else if ( ((strcmp(argv[a], "-I") == 0) || (strcmp(argv[a], "--traceint") == 0)) &&
		   (a+1<arg) ) {
				a++;
				if (argv[a] && ((sscanf(argv[a], "%u", &ctx->cq_traceint) == 1))) {
				}
			 */
		} else { // application option/parameter
			argv[appl_argc] = argv[a];
			appl_argc++;
		}
	}
	argv[appl_argc] = 0;
	if (ctx->num_threads > 1) {
		ctx->num_stages = ctx->num_threads;
		for (i = 0; i < ctx->num_stages; i++)
			ctx->cq_stages[i] = 1;
	}
	for (i = 0; i < ctx->num_stages; i++) {
		ctx->cq_msgsizes[i] = ctx->cq_msgsizes[0];
		ctx->cq_optimizations[i] = ctx->cq_opt;
	}

	if (ctx->unknown_opt_handler)
		return ctx->unknown_opt_handler(ctx, appl_argc, argv); // process application command line
	else
		return 0;
}

void CQPipeline_initQueues(CQPipelineCtx* ctx, int verbose_print) {
	unsigned i;
	int wrcorrections[MAX_THREADS];
	for (i = 0; i < ctx->num_stages; i++)
		wrcorrections[i] = 0;
	for (i = 0; i < ctx->num_stages; i++)
		if (ctx->cq_optimizations[i] & OPT_WSTFW)
			wrcorrections[(i + 1) % ctx->num_stages] = 1;
	for (i = 0; i < ctx->num_stages; i++)
		if (ctx->cq_optimizations[i] & OPT_WSTBW)
			wrcorrections[i] = 1;

	for (i = 0; i < ctx->num_stages; i++) { // create queues
		ctx->cq_queues[i] = CQ_init(ctx->cq_queuesizes[i], ctx->cq_msgsizes[i],
									(short) ctx->cq_stages[(i + 1) % ctx->num_stages],
									(short) (ctx->cq_stages[i] + wrcorrections[i]),
									(short) ctx->cq_queuetypes[i],
									ctx->cq_optimizations[i], NULL); //TODO Pass stealer function
		if (verbose_print) {
			printf("Queue %d: %d reader(s), %d writer(s), queuetype=%d, queuesize=%d, msgsize=%d, opt=%lx, work=%c\n",
				   i, ctx->cq_stages[(i + 1) % ctx->num_stages], ctx->cq_stages[i] + wrcorrections[i],
				   ctx->cq_queuetypes[i], ctx->cq_queuesizes[i], ctx->cq_msgsizes[i],
				   ctx->cq_optimizations[i], ctx->stealer_func[i] ? 'Y' : 'N');
		}
		ctx->cq_queues[i]->stage = i;
	}
	ctx->num_threads = 0;
	for (i = 0; i < ctx->num_stages; i++) { // concatenate queues, count threads and open queue handles
		if (i < ctx->num_stages - 1) ctx->cq_queues[i]->nextqueue = ctx->cq_queues[i + 1];
		if (i > 0) ctx->cq_queues[i]->prevqueue = ctx->cq_queues[i - 1];
		for (int k = 0; k < ctx->cq_stages[i]; k++) {
			ctx->cq_outputs[ctx->num_threads] =
					ctx->cq_queues[i]->open(ctx->cq_queues[i], 1);
			if (i > 0) {
				ctx->cq_inputs[ctx->num_threads] =
						ctx->cq_queues[i - 1]->open(ctx->cq_queues[i - 1], 0);
			} else {
				ctx->cq_inputs[ctx->num_threads] =
						ctx->cq_queues[ctx->num_stages - 1]->open(ctx->cq_queues[ctx->num_stages - 1], 0);
			}
			ctx->num_threads++;
		}
	}
}


CQPipelineCtx* CQPipeline_create(int argc, char** argv,
								 pipeline_worker_func_t worker_func,
								 pipeline_help_func_t help_func,
								 pipeline_stat_func_t stat_func,
								 pipeline_unknown_cl_opt_handler_t unknown_opt_handler,
								 void* user_data) {
	int status;
	CQPipelineCtx* ctx = CQPipelineCtx_create(RDR_1, 64, 128, worker_func,
											  help_func, stat_func,
											  unknown_opt_handler, user_data);

	status = CQPipelineCtx_parseClOpts(ctx, argc, argv);
	if (status != 0) {
		CQPipeline_destroy(ctx);
		return NULL;
	}
	CQPipeline_initQueues(ctx, 1);
	Dprintf("Stages = %d\n", ctx->num_stages);

	return ctx;
}

void CQPipeline_start(CQPipelineCtx* ctx) {
	int i, status;

	Dprintf ("Starting %d thread(s)...\n", ctx->num_threads);

	for (i = 0; i < ctx->num_threads; i++) {
		if (pthread_create(&ctx->cq_threads[i], NULL, CQPipelineThread,
						   (void*) &ctx->thread_ctx[i]) != 0) {
			printf("pthread_create() error");
			exit(1);
		}
	}

	// wait for all other threads
	while (ctx->atomic_barrier != 0) {}

	clock_gettime(CLOCK, &ctx->cq_start_time); // warm-up clock_gettime
	status = clock_gettime(CLOCK, &ctx->cq_start_time);
	if (status != 0) {
		handle_error_en (status, "clock_gettime");
	}
}

void CQPipeline_join(CQPipelineCtx* ctx) {
	int i, status;

	Dprintf ("Joining %d thread(s)...\n", num_threads);
	for (i = 0; i < ctx->num_threads; i++)
		pthread_join(ctx->cq_threads[i], NULL);


	status = clock_gettime(CLOCK, &ctx->cq_stop_time);
	if (status != 0) {
		handle_error_en (status, "clock_gettime");
	}
}

void CQPipeline_stats(CQPipelineCtx* ctx) {
	long long int ttime, millisec;
	long long int ii, ie, ir, iw, is;
	CQhandle* h;
	int i;

	ttime = ((long long int) ctx->cq_stop_time.tv_sec -
			 (long long int) ctx->cq_start_time.tv_sec) * 1000000000L +
			(long long int) ctx->cq_stop_time.tv_nsec -
			(long long int) ctx->cq_start_time.tv_nsec;

	millisec = ttime / 1000000;


	if (ctx->cq_opt & OPT_CNT) {
		int num_handles;
		for (i = 0; i < ctx->num_stages; i++) {
			printf("\nQueue\t%d %s\t\tMessages\n", i, ctx->cq_queues[i]->queuetype);
			printf("Thread\tcpu\twritten    read       stolen     errors\n");
			ir = iw = is = ie = 0;
			num_handles = ctx->cq_stages[i] + ctx->cq_stages[(i + 1) % ctx->num_stages];
			for (i = 0; i < num_handles; i++) {
				h = ctx->cq_queues[i]->handles + i;
				if (h) {
					ir += h->rdcnt;
					iw += h->wrcnt;
					is += h->stcnt;
					ie += h->errcnt;
					if (h->rdwr)
						printf("W");
					else
						printf("R");
					printf("%ld\t%ld\t%-11ld%-11ld%-11ld%ld\n", h->id,
						   h->cpu, h->wrcnt, h->rdcnt, h->stcnt, h->errcnt);
				}
			}
			if (ctx->cq_queuetypes[i] == RDR_1)
				ii = iw;
			else
				ii = ctx->cq_stages[i + 1] * iw;
			printf("WST\t-\t%-11ld%-11ld%-11ld%ld\n", ctx->cq_queues[i]->injcnt,
				   ctx->cq_queues[i]->stcnt, 0L, ctx->cq_queues[i]->errcnt);
			printf("-------------------------------------------------------------\n");
			printf("Total:\t\t%-11lld%-11lld%-11lld%lld\n", iw + ctx->cq_queues[i]->injcnt,
				   ir + ctx->cq_queues[i]->stcnt, is, ie);
			printf("Lost:    \t%lld\n", ii - ir - ctx->cq_queues[i]->stcnt + ctx->cq_queues[i]->injcnt);
			if (millisec) {
				printf("Throughput:\t%lld messages/sec\n", ii * 1000 / millisec);
			} else {
				printf("Throughput:\tToo fast to measure\n");
			}
		}
	}
	printf("Time:    \t%llu ms\n", millisec);

	if (ctx->stat_func)
		ctx->stat_func(ctx);
}

void CQPipeline_destroy(CQPipelineCtx* ctx) {
	int i;

	for (i = 0; i < ctx->num_stages - 1; i++) { // delete queues
		if (ctx->cq_queues[i])
			ctx->cq_queues[i]->free(ctx->cq_queues[i]);
	}

	free(ctx);
}

void CQ_help() {
	printf("CQoptions: \n");
	printf("-c --cpus list of cpus\n");
	printf("-t --threads #threads\n");
	printf("-s --stages list of #threads in stages\n");
	printf("-T --types list of queue types \t 1=1 reader, N=broacast to all readers, B=barrier\n");
	printf("-q --queuesizes list of queue sizes\n");
	printf("-m --msgsizes list of message sizes\n");
	//    printf("-M --messages # messages sent from each writer\n");
	printf("-o --optimizations list of optimization options\n");
	printf("\t all, reader pointer, reader data, writer pointer, writer data \n");
	printf("\t PRE, PRERP, PRERD, PREWP, PREWD: prefetch\n");
	printf("\t FLU, FLURP, FLURD, FLUWP, FLUWD: cache line flush\n");
	printf("\t NTM, NTMRP, NTMRD, NTMWP, NTMWD: non temporal move\n");
	printf("\t WST, WSTFW, WSTBW:\t work stealing: both, forward, backward\n");
	//  printf("\t LATER: \t later update reader pointer\n");
	//  printf("\t LATEW: \t later update writer pointer\n");
	printf("\t LPUR: \t lazy pointer update read\n");
	printf("\t LPUW: \t lazy pointer update write\n");
	printf("\t CCA:  \t cache conscitious allocation\n");
	printf("\t CNT:  \t counters and throughput\n");
	//  printf("\t BATCH:\t batch writing and batch reading (W1 or R1)\n");
	printf("\t DEBUG:\t debug output\n");
	//  printf ("\t TRACE:\t thread trace output\n");
	printf("\t NONE: \t no optimizations\n");
	printf("\t ALL:  \t all optimizations\n");
	//  printf("-S --tracestart \t a delay before the first trace output in msec [1000]\n");
	//  printf("-I --traceint \t an interval between two trace outputs in msec [1000]\n");
};
