/*
 * primes_trivial.c
 *  CQ tester application
 * @author Jiri Kaspar, CVUT FIT
 *
 * verze 0.9.7	11.12.2017
 *
 *
 *
 */

#define VERSION "0.9.7"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

//
#define SHARED volatile
#define ALIGNED __attribute__ ((aligned (64)))

#include "CQ.h"


/**
 */
unsigned int *primes;
unsigned int max_prime;
unsigned int max_prime_sqrt;
volatile unsigned int known_primes = 1;
volatile unsigned int required_primes = 1;
FILE *f;

void worker (CQhandle *input, char *buffer, CQhandle *output) {
  CQ *queue;
  unsigned int p, *pp, required, i;

  required = required_primes;
  pp = (unsigned int *) buffer;
  p = *pp;
  for (i = input->q->stage; i < required; i += num_threads) {
    if (p % primes[i] == 0) break;
  }
  if (i >= required) {
    queue = output->q;
    queue->writeMsg (output, buffer);
  }
}

void reader (CQhandle *input) {
  CQ *queue;
  char *buffer;
  unsigned int *pp;

  queue = input->q;
  if (!queue->isEmpty(input)) {
    buffer = queue->getMsg(input);
//  while (buffer = queue->getMsgNW(input)) {
    if (buffer) {
      pp = (unsigned int *) buffer;
      primes[known_primes++] = *pp;
      fprintf (f,"%d\n", *pp);
      queue->putBuffer(input, buffer);
    }
  }
}

void generator (CQhandle *input, char *buffer, CQhandle *output) {
  unsigned int next=1;
  CQ *queue;
  unsigned int p, *pp, required, local_max, i;

  required = 1;
  p = 3;
  local_max = 4;
  queue = output->q;

  while (p < max_prime) {		// all required work
    while ( (p < local_max) && (p < max_prime) ) {		// all candidates bellow current fence
      for (i = input->q->stage; i < required; i += num_threads) {
        if (p % primes[i] == 0) break;
      }
      if (i >= required) {		// a new prime candidate
	do {
	  buffer = queue->getBufferNW (output);	// try allocate an output buffer
	  if (buffer) {				// success
	    pp = (unsigned int *) buffer;
	    *pp = p;
	    queue->putMsg (output, buffer);	// send a candidate to the next seive
	  } else {
	    reader(input);		// a pipeline is full, try read new primes from the last seive
	  }
        } while (buffer == 0);
      }
      p+=2;		// a next prime candidate
    }
    reader(input);	// read new primes from the last seive
    if (known_primes > required) { 	// at least one new primer was read since last round
      required++; // need to check one more prime - move fence up
      required_primes = required;
      local_max = primes[required-1]*primes[required-1];	// the next local limit to generate
    }
  }
  queue->close(output);
  while (!queue->isEof (input)) reader(input);
  printf("prime: %d\n", primes[known_primes-1]);
  fclose(f);
}

void appl_help () {
  printf("prime MAX \n");
}

void appl_init(int argc, char **argv) {
int a=1;

  if ( (argc > 1) && (sscanf (argv[1], "%u", &max_prime) != 1) ) argc = 1;
  if (argc < 2) {
    appl_help();
    exit(1);
  }
  primes = malloc( max_prime * sizeof(unsigned int) );
  primes[0]=2;
  f = fopen("primes.txt","w");
  fprintf (f,"%d\n", 2);
}


void appl_run (int thread, int stage, int index, CQhandle *input, CQhandle *output) {
  // main application routine called from all threads
  unsigned char *b;
  CQ *queue = NULL;
  unsigned int localsum = 0;

  if (input) queue = input->q;
  if (index == 0) { // finalize initialization
//    cq_queues[num_stages - 2]->nextqueue = 0;
  }
  if (stage == 0) { // first stage = writer
    generator (input, b, output);
    output->q->close(output);
  } else {	    // workers
    while (!queue->isEof (input)) {
      b = queue->getMsg (input);
      if (b) {
	worker (input, b, output);
	queue->putBuffer (input, b);
      }
    }
    queue->close(input);
    output->q->close(output);
  }
}

void appl_stat () {
  // application statistics
}
