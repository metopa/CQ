/*
 * cqtest.c
 *  Circular queue tester
 * @author Jiri Kaspar, CVUT FIT
 *
 * verze 0.9.7	9.12.2017
 *
 *
 *
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

//
#define SHARED volatile
#define ALIGNED __attribute__ ((aligned (64)))

//#define CLOCK CLOCK_THREAD_CPUTIME_ID
#define CLOCK CLOCK_REALTIME

#include "CQpipeline.h"

#ifdef DEBUG
#define Dprintf(f, v)                                                                              \
  {                                                                                                \
    if (cq_opt & OPT_DEBUG) printf (f, v);                                                            \
  }
#define Eprintf(f, v, w)                                                                           \
  {                                                                                                \
    if (cq_opt & OPT_DEBUG) printf (f, v, w);                                                         \
  }
#else
#define Dprintf(f, v)                                                                              \
  {}
#define Eprintf(f, v, w)                                                                           \
  {}
#endif

/**
 */
#define handle_error_en(en, msg)                                                                   \
  do {                                                                                             \
    errno = en;                                                                                    \
    perror (msg);                                                                                  \
    exit (EXIT_FAILURE);                                                                           \
  } while (0)

int messages; 	// number of messages to process
volatile unsigned int sumwr = 0;	// write checksum
volatile unsigned int sumrd = 0;	// read checksum

void reader (CQPipelineCtx* ctx, CQhandle *h_in,
			 char *buffer, CQhandle *h_out) {
  int j;
  unsigned char x;
  x = 0;
  for (j = 0; j < ctx->cq_msgsizes[0]; j++)
	  x += buffer[j];
  if (x != 0)
	  h_in->errcnt++;
  if (h_in->id < 0)
    __sync_fetch_and_add (&sumrd, buffer[0]); // global read checksum
}

void forwarder (CQPipelineCtx* ctx, CQhandle *h_in,
				char *buffer, CQhandle *h_out) {
  int i, j;
  unsigned char x;
  CQ *queue;

  queue = h_out->q;
  x = 0;
  for (j = 0; j < ctx->cq_msgsizes[0]; j++)
	  x += buffer[j];
  if (x != 0)
	  h_in->errcnt++;
  queue->writeMsg (h_out, buffer);
}

void writer (CQPipelineCtx* ctx, CQhandle *h) {
  int i, j;
  unsigned char c, x;
  unsigned char *b;
  CQ *queue;
  unsigned int localsum = 0;
  b = malloc (ctx->cq_msgsizes[0]);
  queue = h->q;
  for (i = 0; i < messages; i++) {
    c = (unsigned char) ((i ^ h->id) & 255);
    x = 0;
    for (j = 1; j < ctx->cq_msgsizes[0]; j++) {
      b[j] = c;
      x += c++;
    }
    *b = 256 - x;
    localsum += *b;
    if (queue->writeMsg (h, b) == 0) break;
  }
  __sync_fetch_and_add (&sumwr, localsum); // global write checksum
  free (b);
}

void appl_help (CQPipelineCtx* ctx) {
  // application options and parameters help
  printf ("Circular queue application tester v%s\n\n", VERSION);
  printf ("Usage: CQtest [CQoptions] [--] messages\n");
  CQ_help ();
}

int appl_init (CQPipelineCtx* ctx, int argc, char **argv) {
  int i;
  Dprintf("appl_init %d\n", argc);
  // process application options and parameters  before queues are created
  if (argc < 2) {
    appl_help(ctx);
    return 1;
  }
  if ((sscanf (argv[1], "%u", &messages) == 1)) {
  } else argc = 1;
  // it can change some options if they are incorect
  if (ctx->num_stages < 2)
	  ctx->num_stages = 2;
  Dprintf("%d messages\n", messages);
  // define worker routines
  if (ctx->cq_opt & OPT_WST) {
    for (i = 0; i < ctx->num_stages - 1; i++) ctx->stealer_func[i] = &forwarder;
	  ctx->stealer_func[ctx->num_stages - 2] = &reader;
  }

  return 0;
}

void appl_run (CQPipelineCtx* ctx, int thread, int stage, int index, CQhandle *input, CQhandle *output) {
  // main application routine called from all threads
  unsigned char *b;
  CQ *queue = NULL;
  unsigned int localsum = 0;

  if (input) queue = input->q;
  if (index == 0) { // finalize initialization
//    cq_queues[num_stages - 2]->nextqueue = 0;
  }
  if (stage == 0) { // first stage = writer
    writer (ctx, output);
    output->q->close(output);
  } else if (input->q->optimizations & OPT_NTM) { // use own buffer
    b = malloc (ctx->cq_msgsizes[0]);
    if (stage == ctx->num_stages - 1) { // last stage = reader
      while (!queue->isEof (input)) {
	if (queue->readMsg (input, b)) {
	  reader (ctx, input, b, output);
	  localsum += b[0];
	}
      }
      queue->close(input);
    } else { // otherwise forwarder
      while (!queue->isEof (input)) {
	if (queue->readMsg (input, b)) { forwarder (ctx, input, b, output); }
      }
      queue->close(input);
      output->q->close(output);
    }
    free (b);
  } else {	    // use queue buffer
    if (stage == ctx->num_stages - 1) { // last stage = reader
      while (!queue->isEof (input)) {
	b = queue->getMsg (input);
	if (b) {
	  reader (ctx, input, b, output);
          localsum += b[0];
	  queue->putBuffer (input, b);
	}
      }
      queue->close(input);
    } else { // otherwise forwarder
      while (!queue->isEof (input)) {
	b = queue->getMsg (input);
	if (b) {
	  forwarder (ctx, input, b, output);
	  queue->putBuffer (input, b);
	}
      }
      queue->close(input);
      output->q->close(output);
    }
  }
  __sync_fetch_and_add (&sumrd, localsum); // global read checksum
}

void appl_stat (CQPipelineCtx* ctx) {
  // application statistics
  printf("Write checksum:\t%u\n", sumwr);
  printf("Read checksum:\t%u\n", sumrd);
}

int main(int argc, char** argv) {
  CQPipelineCtx* ctx = CQPipeline_create(argc, argv, appl_run, appl_help,
										 appl_stat, appl_init, NULL);
  if (ctx == NULL)
	  return 0;

  CQPipeline_start(ctx);
  CQPipeline_join(ctx);

  CQPipeline_destroy(ctx);
  return 0;
}
