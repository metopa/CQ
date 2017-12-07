/*
 * cqmain.c
 *  Circular queue tester&main loop
 * @author Jiri Kaspar, CVUT FIT
 *
 * verze 0.9.6    7.12.2017
 *
 *
 *
 */

#define VERSION "0.9.6"

// #define _GNU_SOURCE je tu proto, aby fungovaly CPU_SET apod.
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

//
#define SHARED volatile
#define ALIGNED __attribute__ ((aligned (64)))

//#define CLOCK CLOCK_THREAD_CPUTIME_ID
#define CLOCK CLOCK_REALTIME

#include "CQ.h"

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

unsigned int num_threads;			 // pocet obsluznych vlaken
unsigned int cq_cpus[MAX_THREADS];		 // cisla pouzitych cpu
unsigned int cq_stages[MAX_THREADS];		 // pocty vlaken pro jednotlive faze pipeline
unsigned int num_stages;			 // pocet fazi v pipeline
unsigned int cq_queuetypes[MAX_THREADS];	 // typy front pro jednotlive faze pipeline
unsigned int cq_queuesizes[MAX_THREADS];	 // delky front
unsigned long int cq_optimizations[MAX_THREADS]; // volby front
unsigned int cq_msgsizes[MAX_THREADS];		 // delky zprav
unsigned long int cq_opt = OPT_NONE;		 // zvolene optimalizace
unsigned int cq_tracestart = 1000;		 // cas pocatku detailnich vypisu
unsigned int cq_traceint = 1000;		 // interval mezi vypisy

pthread_t cq_threads[MAX_THREADS]; // vlakna
CQ *cq_queues[MAX_THREADS];	// fronty
CQhandle *cq_inputs[MAX_THREADS];  // vstupy
CQhandle *cq_outputs[MAX_THREADS]; // vystupy

volatile unsigned sync1 = 0;

void *cq_thread (void *arg) {
  int i, j, status;
  cpu_set_t cpuset;
  long long int thrd;
  unsigned int thread, stage, incarnation;
  CQhandle *in_h, *out_h;
  unsigned char x;


  thrd = (long long int)arg;
  thread = thrd;
  in_h = cq_inputs[thread];
  out_h = cq_outputs[thread];
  if (in_h) in_h->cpu = thread;
  if (out_h) out_h->cpu = thread;
  incarnation = thread;
  stage = 0;
  while (incarnation >= cq_stages[stage]) {
    incarnation -= cq_stages[stage];
    stage++;
  }
  Eprintf("Thread %d, Stage=%d\n", thread, stage);

  CPU_ZERO (&cpuset);
  CPU_SET (cq_cpus[thread], &cpuset);
  status = pthread_setaffinity_np (pthread_self (), sizeof (cpu_set_t), &cpuset);
  if (status != 0) handle_error_en (status, "pthread_setaffinity_np");
  Eprintf ("Thread %d bound to CPU %d\n", thread, cq_cpus[thread]);

  __sync_fetch_and_add (&sync1, 1); // count running threads
  do
    ;
  while (sync1 != num_threads); // a barier wait for all other threads

  appl_run (thread, stage, incarnation, in_h, out_h); // run an application thread

  __sync_fetch_and_add (&sync1, -1); // mark thread done
}

void CQ_help () {
  printf ("CQoptions: \n");
  printf ("-c --cpus list of cpus\n");
  printf ("-t --threads #threads\n");
  printf ("-s --stages list of #threads in stages\n");
  printf ("-T --types list of queue types \t 1=1 reader, N=broacast to all readers, B=barrier\n");
  printf ("-q --queuesizes list of queue sizes\n");
  printf ("-m --msgsizes list of message sizes\n");
  //    printf("-M --messages # messages sent from each writer\n");
  printf ("-o --optimizations list of optimization options\n");
  printf ("\t all, reader pointer, reader data, writer pointer, writer data \n");
  printf ("\t PRE, PRERP, PRERD, PREWP, PREWD: prefetch\n");
  printf ("\t FLU, FLURP, FLURD, FLUWP, FLUWD: cache line flush\n");
  printf ("\t NTM, NTMRP, NTMRD, NTMWP, NTMWD: non temporal move\n");
  printf ("\t WST, WSTFW, WSTBW:\t work stealing: both, forward, backward\n");
  //  printf("\t LATER: \t later update reader pointer\n");
  //  printf("\t LATEW: \t later update writer pointer\n");
  printf ("\t LPUR: \t lazy pointer update read\n");
  printf ("\t LPUW: \t lazy pointer update write\n");
  printf ("\t CCA:  \t cache conscitious allocation\n");
  printf ("\t CNT:  \t counters and throughput\n");
   //  printf("\t BATCH:\t batch writing and batch reading (W1 or R1)\n");
  printf ("\t DEBUG:\t debug output\n");
  //  printf ("\t TRACE:\t thread trace output\n");
  printf ("\t NONE: \t no optimizations\n");
  printf ("\t ALL:  \t all optimizations\n");
  //  printf("-S --tracestart \t a delay before the first trace output in msec [1000]\n");
  //  printf("-I --traceint \t an interval between two trace outputs in msec [1000]\n");
};

int main (int argc, char **argv) {
  int appl_argc;
  int status;
  int a, i, j, c1, c2;
  int notracing = 1;
  long long int ii, ie, ir, iw, is;
  char *t;
  struct timespec stop, start, sleep;
  long long int ttime;
  unsigned int milisec;
  //  pthread_attr_t attr;
  //  size_t stacksize;
  cpu_set_t cpuset;
  CQhandle *h;

  for (i = 0; i < MAX_THREADS; i++) { // default setting
    cq_cpus[i] = i;
    cq_stages[i] = 0;
    cq_queuetypes[i] = RDR_1;
    cq_queuesizes[i] = 64;
    cq_msgsizes[i] = 128;
    cq_queues[i] = 0;
    cq_inputs[i] = 0;
    cq_outputs[i] = 0;
  }
  cq_stages[0] = 1;
  num_threads = 1;
  num_stages = 1;

  for (a = 1, appl_argc = 1; a < argc; a++) { // overwite defaults by command line values

    if (((strcmp (argv[a], "-c") == 0) || (strcmp (argv[a], "--cpus") == 0)) && (a + 1 < argc)) {
      a++;
      t = strtok (argv[a], ",.");
      j = 0;
      while (t) {
	if (sscanf (t, "%u:%u", &c1, &c2) == 2) {
	  for (i = c1; i <= c2; i++, j++) { cq_cpus[j] = i; }
	} else if (sscanf (t, "%u", &cq_cpus[j]) == 1) {
	  j++;
	}
	t = strtok (NULL, ",.");
      }
    } else if (((strcmp (argv[a], "-t") == 0) || (strcmp (argv[a], "--threads") == 0)) &&
               (a + 1 < argc)) {
      a++;
      if (argv[a] && ((sscanf (argv[a], "%u", &num_threads) == 1))) {}
     } else if (((strcmp (argv[a], "-s") == 0) || (strcmp (argv[a], "--stages") == 0)) &&
	       (a + 1 < argc)) {
      a++;
      t = strtok (argv[a], ",.");
      j = 0;
      num_stages = 0;
      while (t) {
	if (sscanf (t, "%u", &cq_stages[j]) == 1) { j++; num_stages++; }
	t = strtok (NULL, ",.");
      }
    } else if (((strcmp (argv[a], "-q") == 0) || (strcmp (argv[a], "--queuesizes") == 0)) &&
	       (a + 1 < argc)) {
      a++;
      t = strtok (argv[a], ",.");
      j = 0;
      while (t) {
	if (sscanf (t, "%u", &cq_queuesizes[j]) == 1) { j++; }
	t = strtok (NULL, ",.");
      }
    } else if (((strcmp (argv[a], "-m") == 0) || (strcmp (argv[a], "--msgsize") == 0)) &&
	       (a + 1 < argc)) {
      a++;
      if (argv[a] && ((sscanf (argv[a], "%u", &cq_msgsizes[0]) == 1))) {}
    } else if (((strcmp (argv[a], "-T") == 0) || (strcmp (argv[a], "--types") == 0)) &&
	       (a + 1 < argc)) {
      a++;
      t = strtok (argv[a], ",.");
      j = 0;
      while (t) {
	if (t[0] == 'N') cq_queuetypes[j] = RDR_N;
	if (t[0] == 'B') cq_queuetypes[j] = RDR_B;
	j++;
	t = strtok (NULL, ",.");
      }
    } else if (((strcmp (argv[a], "-o") == 0) || (strcmp (argv[a], "--optimizations") == 0)) &&
	       (a + 1 < argc)) {
      a++;
      t = strtok (argv[a], ",");
      while (t) {
	if (strcmp (t, "PRERP") == 0) cq_opt |= OPT_PRERP;
	if (strcmp (t, "PRERD") == 0) cq_opt |= OPT_PRERD;
	if (strcmp (t, "PREWP") == 0) cq_opt |= OPT_PREWP;
	if (strcmp (t, "PREWD") == 0) cq_opt |= OPT_PREWD;
	if (strcmp (t, "PRE") == 0) cq_opt |= OPT_PRE;
	if (strcmp (t, "FLURP") == 0) cq_opt |= OPT_FLURP;
	if (strcmp (t, "FLURD") == 0) cq_opt |= OPT_FLURD;
	if (strcmp (t, "FLUWP") == 0) cq_opt |= OPT_FLUWP;
	if (strcmp (t, "FLUWD") == 0) cq_opt |= OPT_FLUWD;
	if (strcmp (t, "FLU") == 0) cq_opt |= OPT_FLU;
	if (strcmp (t, "NTMRP") == 0) cq_opt |= OPT_NTMRP;
	if (strcmp (t, "NTMRD") == 0) cq_opt |= OPT_NTMRD;
	if (strcmp (t, "NTMWP") == 0) cq_opt |= OPT_NTMWP;
	if (strcmp (t, "NTMWD") == 0) cq_opt |= OPT_NTMWD;
	if (strcmp (t, "NTM") == 0) cq_opt |= OPT_NTM;
	//        if (strcmp(t, "LATER") == 0) cq_opt |= OPT_LATER;
	//        if (strcmp(t, "LATEW") == 0) cq_opt |= OPT_LATEW;
	if (strcmp (t, "LPUR") == 0) cq_opt |= OPT_LPUR;
	if (strcmp (t, "LPUW") == 0) cq_opt |= OPT_LPUW;
	if (strcmp (t, "CCA") == 0) cq_opt |= OPT_CCA;
	if (strcmp (t, "DEBUG") == 0) cq_opt |= OPT_DEBUG;
	if (strcmp (t, "WSTFW") == 0) cq_opt |= OPT_WSTFW;
	if (strcmp (t, "WSTBW") == 0) cq_opt |= OPT_WSTBW;
	if (strcmp (t, "WST") == 0) cq_opt |= OPT_WST;
	if (strcmp (t, "TRACE") == 0) {
	  cq_opt |= OPT_TRACE;
	  notracing = 0;
	}
	if (strcmp (t, "CNT") == 0) cq_opt |= OPT_CNT;
	//        if (strcmp(t, "BATCH") == 0) cq_opt |= OPT_BATCH;
	t = strtok (NULL, ",.");
      }
      /*  } else if ( ((strcmp(argv[a], "-S") == 0) || (strcmp(argv[a], "--tracestart") == 0)) &&
	 (a+1<arg) ) {
	      a++;
	      if (argv[a] && ((sscanf(argv[a], "%u", &cq_tracestart) == 1))) {
	      }
	  } else if ( ((strcmp(argv[a], "-I") == 0) || (strcmp(argv[a], "--traceint") == 0)) &&
	 (a+1<arg) ) {
	      a++;
	      if (argv[a] && ((sscanf(argv[a], "%u", &cq_traceint) == 1))) {
	      }
       */
    } else { // application option/parameter
      argv[appl_argc] = argv[a];
      appl_argc++;
    }
  }
  argv[appl_argc] = 0;
  if (num_threads > 1) {
    num_stages = num_threads;
    for (i = 0; i < num_stages; i++) cq_stages[i] = 1;
  }
  for (i = 0; i < num_stages; i++) {
    cq_msgsizes[i] = cq_msgsizes[0];
    cq_optimizations[i] = cq_opt;
  }

  appl_init (appl_argc, argv); // process application command line

  printf("Stages=%d\n", num_stages);
  //  pthread_attr_init(&attr);
  //  pthread_attr_getstacksize(&attr, &stacksize);
  //  Dprintf("stacksize=%zd\n", stacksize);
  //  stacksize *= 8;
  //  pthread_attr_setstacksize(&attr, stacksize);
  //  pthread_attr_getstacksize(&attr, &stacksize);
  //  printf("%zd\n", stacksize);
  //  pthread_attr_destroy(&attr);

  for (i = 0; i < num_stages; i++) { // create queues
    cq_queues[i] = CQ_init (cq_queuesizes[i], cq_msgsizes[i],
			    cq_stages[(i + 1) % num_stages], cq_stages[i],
			    cq_queuetypes[i], cq_optimizations[i]);
    printf ("Queue %d: %d reader(s), %d writer(s), queuetype=%d, queuesize=%d, msgsize=%d, opt=%lx.\n",
	    i, cq_stages[(i + 1) % num_stages], cq_stages[i],
	    cq_queuetypes[i], cq_queuesizes[i], cq_msgsizes[i], cq_optimizations[i]);
  }
  num_threads = 0;
  for (i = 0; i < num_stages; i++) { // concatenate queues, count threads and open queue handles
    if (i < num_stages - 1) cq_queues[i]->nextqueue = cq_queues[i+1];
    if (i > 0) cq_queues[i]->prevqueue = cq_queues[i-1];
    for (int k = 0; k < cq_stages[i]; k++) {
      cq_outputs[num_threads] = cq_queues[i]->open (cq_queues[i], 1);
      if (i > 0) cq_inputs[num_threads] = cq_queues[i - 1]->open (cq_queues[i - 1], 0);
      else cq_inputs[num_threads] = cq_queues[num_stages - 1]->open (cq_queues[num_stages - 1], 0);
      num_threads++;
    }
  }

  Dprintf ("Starting %d thread(s)...\n", num_threads);

  for (j = 0; j < num_threads - 1; j++) {
    ii = j;
    if (pthread_create (&cq_threads[j], NULL, cq_thread, (void *)ii) != 0) {
      printf ("pthread_create() error");
      exit (1);
    }
  }

  status = clock_gettime (CLOCK, &start); // warm-up clock_gettime

  status = clock_gettime (CLOCK, &start);
  if (status != 0) { handle_error_en (status, "clock_gettime"); }
  ii = num_threads - 1;
  cq_thread ((void *)ii);

  do
    ;
  while (sync1 != 0); // wait for all other threads

  status = clock_gettime (CLOCK, &stop);
  if (status != 0) { handle_error_en (status, "clock_gettime"); }

  ttime = ((long long int)stop.tv_sec - (long long int)start.tv_sec) * 1000000000L +
	  (long long int)stop.tv_nsec - (long long int)start.tv_nsec;

  milisec = ttime / 1000000;

  Dprintf ("Joining %d thread(s)...\n", num_threads);
  for (i = 0; i < num_threads - 1; i++) pthread_join (cq_threads[i], NULL);

  if (cq_opt & OPT_CNT) {
    int num_handles;
    for (j = 0; j < num_stages; j++) {
      printf ("\nQueue\t%d\t\tMessages\n", j);
      printf ("Thread\tcpu\twritten    read       stolen     errors\n");
      ir = iw = is = ie = 0;
      num_handles = cq_stages[j] + cq_stages[(j+1) % num_stages];
      for (i = 0; i < num_handles; i++) {
	h = cq_queues[j]->handles + i;
	if (h) {
	  ir += h->rdcnt;
	  iw += h->wrcnt;
	  is += h->stcnt;
	  ie += h->errcnt;
	  if (h->rdwr)
	    printf ("W");
	  else
	    printf ("R");
	  printf ("%ld\t%ld\t%-11ld%-11ld%-11ld%ld\n", h->id, h->cpu, h->wrcnt, h->rdcnt, h->stcnt,
		  h->errcnt);
	}
      }
      if (cq_queuetypes[j] == RDR_1)
	ii = iw;
      else
	ii = cq_stages[j + 1] * iw;
      printf ("-------------------------------------------------------------\n");
      printf ("Total:   \t%-11lld%-11lld%-11lld%lld\n", iw, ir, is, ie);
      printf ("Lost:    \t%lld\n", ii - ir - is);
      if (milisec) {
	printf ("Throughput:\t%lld messages/sec\n", ii * 1000 / milisec);
      } else {
	printf ("Throughput:\tToo fast to measure\n");
      }
    }
  }
  printf ("Time:    \t%u ms\n", milisec);

  appl_stat ();

  for (i = 0; i < num_stages - 1; i++) { // delete queues
    cq_queues[i]->free (cq_queues[i]);
  }
}
