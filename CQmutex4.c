/*
 * CQmutex4.c
 * verze 0.9.3	5.12.2015
 *
 * Circular queue routines protected by four mutexes
 * @author Jiri Kaspar
 */

#define SHARED volatile
#define ALIGNED __attribute__ ((aligned (64)))

#include "CQ.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef DEBUG
#define Dprintf(f, v)                                                                              \
  {                                                                                                \
    if (q->optimizations & OPT_DEBUG) printf (f, h->id, v);                                        \
  }
#define Eprintf(f, v, w)                                                                           \
  {                                                                                                \
    if (q->optimizations & OPT_DEBUG) printf (f, h->id, v, w);                                     \
  }
#define Fprintf(f, v, w, x)                                                                        \
  {                                                                                                \
    if (q->optimizations & OPT_DEBUG) printf (f, h->id, v, w, x);                                  \
  }
#define Tprintf(f, v)                                                                              \
  {                                                                                                \
    if (q->optimizations & OPT_TRACE) sprintf (h->trace, f, v);                                    \
  }
#else
#define Dprintf(f, v)                                                                              \
  {}
#define Eprintf(f, v, w)                                                                           \
  {}
#define Fprintf(f, v, w, x)                                                                        \
  {}
#define Tprintf(f, v)                                                                              \
  {}
#endif

CQhandle *CQ_open (CQ *q, long rdwr);	/* otevre frontu pro cteni ci zapis */
void CQ_close (CQhandle *h);		     /* uzavre handle na frontu */
void CQ_free (CQ *q);			     /* zrusi frontu */
int CQ_readMsg (CQhandle *h, char *buffer);  /* precte zpravu do lokalniho bufferu */
int CQ_writeMsg (CQhandle *h, char *buffer); /* zapise zpravu z lokalniho bufferu */
int CQ_isEof (CQhandle *h);		     /* testuje konec fronty */
int CQ_isEmpty (CQhandle *h);		     /* testuje prazdnost fronty */
int CQ_isFull (CQhandle *h);		     /* testuje plnost fronty */

/* Wn = n pisaru */
char *Wn_getBuffer (CQhandle *h);   /* doda volny buffer pro zapis zpravy */
char *Wn_getBufferNW (CQhandle *h); /* doda volny buffer pro zapis zpravy, neni-li, neceka */
void Wn_putMsg (CQhandle *h, char *buffer); /* zaradi buffer se zpravou do fronty */

/* Rn1 = n ctenaru, zpravu cte jen jeden z nich */
char *Rn1_getMsg (CQhandle *h);			/* doda buffer se zpravou */
char *Rn1_getMsgNW (CQhandle *h);		/* doda buffer se zpravou, neni-li, neceka */
void Rn1_putBuffer (CQhandle *h, char *buffer); /* vrati buffer s prectenou zpravou */

/* Rnn = n ctenaru, zpravu ctou vsichni - broadcast */
char *Rnn_getMsg (CQhandle *h);			/* doda buffer se zpravou */
char *Rnn_getMsgNW (CQhandle *h);		/* doda buffer se zpravou, neni-li, neceka */
void Rnn_putBuffer (CQhandle *h, char *buffer); /* vrati buffer s prectenou zpravou */

/* Rnb = n ctenaru, zpravu ctou vsichni - bariera */
void Rnb_putBuffer (CQhandle *h, char *buffer); /* vrati buffer s prectenou zpravou */

CQ *CQ_init (long queuesize, long itemsize, short readers, short writers, short readertype,
	     unsigned long optimizations) {
  CQ *this;
  CQentry *e;
  CQsimpleentry *se;
  CQhandle *h;
  int size, mask, i;

  this = malloc (sizeof (CQ));
  this->optimizations = optimizations;
  size = 2;
  mask = 1;
  while (size < queuesize) {
    size *= 2;
    mask = (mask << 1) | 1;
  }
  this->queuesize = size;
  this->mask = mask;
  this->itemsize = itemsize;
  if (optimizations & OPT_CCA) {
    this->allocsize = ((itemsize + CBSIZE - 1) / CBSIZE) * CBSIZE;
  } else
    this->allocsize = itemsize;
#ifdef DEBUG
  if (optimizations & OPT_DEBUG) {
    printf ("queuesize = %ld,%d,   mask = %x\n", queuesize, size, mask);
    printf ("msgsize = %ld,  allocsize = %ld\n", itemsize, this->allocsize);
  }
#endif
  this->readers = readers;
  this->writers = writers;
  this->openreaders = 0;
  this->openwriters = 0;
  strncpy (this->queuetype, "WnRn1", 8);
  this->eof = 0;
  this->allocated = 0;
  this->queued = 0;
  this->head = 0;
  this->tail = 0;
  this->read = 0;
  this->deallocated = 0;

  this->buffers = malloc (this->queuesize * this->allocsize);
  this->simplequeue = 0;
  this->queue = malloc (this->queuesize * sizeof (CQentry));
  for (i = 0; i < this->queuesize; i++) {
    e = this->queue + i;
    e->buffer = &this->buffers[i * this->allocsize];
  }
  this->usecnt = 0;
  this->handles = malloc ((readers + writers) * sizeof (CQhandle));
  for (i = 0; i < readers + writers; i++) {
    h = this->handles + i;
    h->q = this;
    h->id = i;
    h->rdwr = 0;
    h->head = 0;
    h->tail = 0;
    h->rdcnt = 0;
    h->wrcnt = 0;
    h->stcnt = 0;
    h->errcnt = 0;
    h->trace[0] = 0;
  }

  this->open = &CQ_open;
  this->close = &CQ_close;
  this->free = &CQ_free;
  this->getBuffer = &Wn_getBuffer;
  this->getBufferNW = &Wn_getBufferNW;
  this->putMsg = &Wn_putMsg;
  this->getMsg = &Rn1_getMsg;
  this->getMsgNW = &Rn1_getMsgNW;
  this->putBuffer = &Rn1_putBuffer;
  this->readMsg = &CQ_readMsg;
  this->writeMsg = &CQ_writeMsg;
  this->isEof = &CQ_isEof;
  this->isEmpty = &CQ_isEmpty;
  this->isFull = &CQ_isFull;

  switch (readertype) {
  case RDR_1: break;
  case RDR_N: // rutiny pro W1Rnn a WnRnn
    this->getMsg = &Rnn_getMsg;
    this->getMsgNW = &Rnn_getMsgNW;
    this->putBuffer = &Rnn_putBuffer;
    strncpy (this->queuetype, "WnRnn", 8);
    break;
  case RDR_B: // rutiny pro W1Rnb a WnRnb
    this->getMsg = &Rnn_getMsg;
    this->getMsgNW = &Rnn_getMsgNW;
    this->putBuffer = &Rnb_putBuffer;
    strncpy (this->queuetype, "WnRnb", 8);
  }

  this->mutex = 0;
  this->mutexa = (pthread_mutex_t *)malloc (sizeof (pthread_mutex_t));
  pthread_mutex_init (this->mutexa, NULL);
  this->mutexw = (pthread_mutex_t *)malloc (sizeof (pthread_mutex_t));
  pthread_mutex_init (this->mutexw, NULL);
  this->mutexr = (pthread_mutex_t *)malloc (sizeof (pthread_mutex_t));
  pthread_mutex_init (this->mutexr, NULL);
  this->mutexd = (pthread_mutex_t *)malloc (sizeof (pthread_mutex_t));
  pthread_mutex_init (this->mutexd, NULL);
  this->notFull = (pthread_cond_t *)malloc (sizeof (pthread_cond_t));
  pthread_cond_init (this->notFull, NULL);
  this->notEmpty = (pthread_cond_t *)malloc (sizeof (pthread_cond_t));
  pthread_cond_init (this->notEmpty, NULL);
  this->barrier = (pthread_cond_t *)malloc (sizeof (pthread_cond_t));
  pthread_cond_init (this->barrier, NULL);

  return this;
}

CQhandle *CQ_open (CQ *q, long rdwr) { /* otevre frontu pro cteni ci zapis */
  CQhandle *h;

  if (rdwr == 0)
    __sync_fetch_and_add (&q->openreaders, 1);
  else
    __sync_fetch_and_add (&q->openwriters, 1);
  if ((q->openreaders > q->readers) || (q->openwriters > q->writers)) return 0;
  h = q->handles + __sync_fetch_and_add (&q->usecnt, 1);
  h->rdwr = rdwr;
  //    if (rdwr) Dprintf("W%ld:open %s\n","writer")
  //    else Dprintf("R%ld:open %s\n","reader")
  return h;
}

void CQ_close (CQhandle *h) { /* uzavre handle na frontu */
  CQ *q;

  q = h->q;
  if (h->rdwr == 0) {
    __sync_fetch_and_add (&q->openreaders, -1);
    //	Dprintf("R%ld:close %s\n","reader")
  } else {
    if (__sync_add_and_fetch (&q->openwriters, -1) == 0) {
      pthread_mutex_lock (q->mutexr); // lock object
      q->eof = 1;
      pthread_cond_broadcast (q->notEmpty); // inform waiting readers
      pthread_mutex_unlock (q->mutexr);     // lock object
    }
    //	Dprintf("W%ld:close %s\n","writer")
  }
}

void CQ_free (CQ *q) { /* zrusi frontu */
  if ((q->openreaders > 0) || (q->openwriters > 0)) return;
  free (q->buffers);
  if (q->simplequeue) free (q->simplequeue);
  if (q->queue) free (q->queue);
  free (q->handles);
  pthread_mutex_destroy (q->mutexa);
  free (q->mutexa);
  pthread_mutex_destroy (q->mutexw);
  free (q->mutexw);
  pthread_mutex_destroy (q->mutexr);
  free (q->mutexr);
  pthread_mutex_destroy (q->mutexd);
  free (q->mutexd);
  pthread_cond_destroy (q->notFull);
  free (q->notFull);
  pthread_cond_destroy (q->notEmpty);
  free (q->notEmpty);
  pthread_cond_destroy (q->barrier);
  free (q->barrier);
  free (q);
}

/* Wn = n pisaru */

char *Wn_getBuffer (CQhandle *h) { /* doda volny buffer pro zapis zpravy */
  CQ *q;
  CQentry *e;
  char *b;
  long index;
  int i = 0;

  q = h->q;
  // Tprintf("%s getBuffer","A");
  pthread_mutex_lock (q->mutexa); // lock object
  while ((q->allocated - q->deallocated) == q->queuesize) {
    // sprintf(h->trace, "a%d getBuffer a=%ld d=%ld h=%ld t=%ld", i++, q->allocated, q->deallocated,
    // q->head, q->tail);
    //	h->trace[0] = 'a';
    pthread_cond_wait (q->notFull, q->mutexa); // wait for a free entry
  }
  index = q->allocated & q->mask;
  e = q->queue + index;
  b = e->buffer;
  q->allocated++;
  pthread_mutex_unlock (q->mutexa); // release lock
				    //    h->trace[0] = ' ';
  return b;
}

char *Wn_getBufferNW (CQhandle *h) { /* doda volny buffer pro zapis zpravy, neni-li, neceka */
  CQ *q;
  CQentry *e;
  char *b;
  long index;

  q = h->q;
  // Tprintf("%s getBufferNW","A");
  pthread_mutex_lock (q->mutexa); // lock object
  if ((q->allocated - q->deallocated) == q->queuesize) {
    pthread_mutex_unlock (q->mutexa); // release lock
    return 0;
  }
  index = q->allocated & q->mask;
  e = q->queue + index;
  b = e->buffer;
  q->allocated += 1;
  pthread_mutex_unlock (q->mutexa); // release lock
				    //    h->trace[0] = ' ';
  return b;
}

void Wn_putMsg (CQhandle *h, char *buffer) { /* zaradi buffer se zpravou do fronty */
  CQ *q;
  CQentry *e;
  long index;

  q = h->q;
  // Tprintf("%s putMsg","W");
  pthread_mutex_lock (q->mutexw); // lock object
  index = q->head & q->mask;
  e = q->queue + index;
  e->buffer = buffer;
  e->counter = q->readers;
  q->head += 1;
  pthread_mutex_unlock (q->mutexw);  // release lock
  pthread_mutex_lock (q->mutexr);    // lock object
  pthread_cond_signal (q->notEmpty); // inform waiting readers
  pthread_mutex_unlock (q->mutexr);  // lock object
				     //    h->trace[0] = ' ';
  // sprintf(h->trace, " putMsg a=%ld d=%ld h=%ld t=%ld", q->allocated, q->deallocated, q->head,
  // q->tail);
  if (q->optimizations & OPT_CNT) h->wrcnt++;
}

/* Rn1 = n ctenaru, zpravu cte jen jeden z nich */

char *Rn1_getMsg (CQhandle *h) { /* doda buffer se zpravou */
  CQ *q;
  CQentry *e;
  char *b;
  long index;
  int i = 0;

  q = h->q;
  // Tprintf("%s getMsg","R");
  pthread_mutex_lock (q->mutexr); // lock object
  while (q->tail == q->head) {
    if (q->eof) {
      pthread_mutex_unlock (q->mutexr); // release lock
      return 0;
    }
    // sprintf(h->trace, "r%d getMsg a=%ld d=%ld h=%ld t=%ld", i++, q->allocated, q->deallocated,
    // q->head, q->tail);
    //    h->trace[0] = 'r';
    pthread_cond_wait (q->notEmpty, q->mutexr); // wait for a message
  }
  index = q->tail & q->mask;
  e = q->queue + index;
  b = e->buffer;
  q->tail++;
  pthread_mutex_unlock (q->mutexr); // release lock
  if (q->optimizations & OPT_CNT) h->rdcnt++;
  //    h->trace[0] = ' ';
  return b;
}

char *Rn1_getMsgNW (CQhandle *h) { /* doda buffer se zpravou, neni-li, neceka  */
  CQ *q;
  CQentry *e;
  char *b;
  long index;

  q = h->q;
  // Tprintf("%s getMsgNW","R");
  pthread_mutex_lock (q->mutexr); // lock object
  if ((q->tail) == q->head) {
    pthread_mutex_unlock (q->mutexr); // release lock
    return 0;
  }
  index = q->tail & q->mask;
  e = q->queue + index;
  b = e->buffer;
  q->tail++;
  pthread_mutex_unlock (q->mutexr); // release lock
  if (q->optimizations & OPT_CNT) h->rdcnt++;
  //    h->trace[0] = ' ';
  return b;
}

void Rn1_putBuffer (CQhandle *h, char *buffer) { /* vrati buffer s prectenou zpravou */
  CQ *q;
  CQentry *e;
  long index;

  q = h->q;
  // Tprintf("%s putBuffer","D");
  // sprintf(h->trace, "D putBuffer a=%ld d=%ld h=%ld t=%ld", q->allocated, q->deallocated, q->head,
  // q->tail);
  pthread_mutex_lock (q->mutexd); // lock object
  index = q->deallocated & q->mask;
  e = q->queue + index;
  e->buffer = buffer;
  q->deallocated++;
  pthread_mutex_unlock (q->mutexd); // release lock
  pthread_mutex_lock (q->mutexa);   // lock object
  pthread_cond_signal (q->notFull); // inform a waiting writer
  pthread_mutex_unlock (q->mutexa); // release lock
  //    h->trace[0] = ' ';
}

/* Rnn = n ctenaru, zpravu ctou vsichni - broadcast */

char *Rnn_getMsg (CQhandle *h) { /* doda buffer se zpravou */
  CQ *q;
  CQentry *e;
  char *b;
  long index;

  q = h->q;
  // Tprintf("%s getMsg","R");
  pthread_mutex_lock (q->mutexr); // lock object
  while ((h->tail == q->head)) {
    if (q->eof) {
      pthread_mutex_unlock (q->mutexr); // release lock
      return 0;
    }
    //    	h->trace[0] = 'r';
    pthread_cond_wait (q->notEmpty, q->mutexr); // wait for a message
  }
  index = h->tail & q->mask;
  e = q->queue + index;
  b = e->buffer;
  pthread_mutex_unlock (q->mutexr); // release lock
  if (q->optimizations & OPT_CNT) h->rdcnt++;
  //    h->trace[0] = ' ';
  return b;
}

char *Rnn_getMsgNW (CQhandle *h) { /* doda buffer se zpravou, neni-li, neceka  */
  CQ *q;
  CQentry *e;
  char *b;
  long index;

  q = h->q;
  // Tprintf("%s getMsgNW","R");
  pthread_mutex_lock (q->mutexr); // lock object
  if (h->tail == q->head) {
    pthread_mutex_unlock (q->mutexr); // release lock
    return 0;
  }
  index = h->tail & q->mask;
  e = q->queue + index;
  b = e->buffer;
  pthread_mutex_unlock (q->mutexr); // release lock
  if (q->optimizations & OPT_CNT) h->rdcnt++;
  //    h->trace[0] = ' ';
  return b;
}

void Rnn_putBuffer (CQhandle *h, char *buffer) { /* vrati buffer s prectenou zpravou */
  CQ *q;
  CQentry *e;
  long index;

  q = h->q;
  index = h->tail & q->mask;
  h->tail++;
  e = q->queue + index;
  // Tprintf("%s putBuffer","D");
  pthread_mutex_lock (q->mutexd); // lock object
  e->counter--;
  if (e->counter <= 0) {
    q->tail++;
    q->deallocated++;
    pthread_mutex_unlock (q->mutexd); // release lock
    pthread_mutex_lock (q->mutexa);   // release lock
    pthread_cond_signal (q->notFull); // inform a waiting writer
    pthread_mutex_unlock (q->mutexa); // release lock
    return;
  }
  pthread_mutex_unlock (q->mutexd); // release lock
  //    h->trace[0] = ' ';
}

/* Rnb = n ctenaru, zpravu ctou vsichni - bariera */

void Rnb_putBuffer (CQhandle *h, char *buffer) { /* vrati buffer s prectenou zpravou */
  CQ *q;
  CQentry *e;
  long index;

  q = h->q;
  index = h->tail & q->mask;
  h->tail++;
  e = q->queue + index;
  // Tprintf("%s putBuffer","D");
  pthread_mutex_lock (q->mutexd); // lock object
  e->counter--;
  if (e->counter <= 0) {
    q->tail++;
    q->deallocated++;
    pthread_cond_broadcast (q->barrier); // inform waiting readers
    pthread_mutex_unlock (q->mutexd);    // release lock
    pthread_mutex_lock (q->mutexa);      // release lock
    pthread_cond_signal (q->notFull);    // inform a waiting writer
    pthread_mutex_unlock (q->mutexa);    // release lock
  } else {
    while (h->tail != q->tail) {		 /* ceka, az zpravu precte posledni ctenar */
						 //	    h->trace[0] = 'd';
      pthread_cond_wait (q->barrier, q->mutexd); // wait for the last reader
    }
    pthread_mutex_unlock (q->mutexd); // release lock
  }
  //    h->trace[0] = ' ';
}

int CQ_readMsg (CQhandle *h, char *buffer) { /* precte zpravu do lokalniho bufferu */

  CQ *q;
  char *b;
  q = h->q;
  // Dprintf("R%ld:readMsg %s\n","getMsg")
  b = q->getMsg (h);
  if (b != 0) {
    if (q->optimizations & OPT_NTMRD) {
      /* non temporal move */
    } else {
      memmove (buffer, b, q->itemsize);
    }
    // Dprintf("R%ld:readMsg %s\n","putBuffer")
    q->putBuffer (h, b);
    // Dprintf("R%ld:readMsg %s\n","done")
    return 1;
  }
  return 0;
}

int CQ_writeMsg (CQhandle *h, char *buffer) { /* zapise zpravu z lokalniho bufferu */

  CQ *q;
  char *b;
  q = h->q;
  // Dprintf("W%ld:writeMsg %s\n","getBuffer")
  b = q->getBuffer (h);
  if (b != 0) {
    if (q->optimizations & OPT_NTMWD) {
      /* non temporal move */
    } else {
      memmove (b, buffer, q->itemsize);
    }
    // Dprintf("W%ld:writeMsg %s\n","putMsg")
    q->putMsg (h, b);
    // Dprintf("W%ld:writeMsg %s\n","done")
    return 1;
  }
  return 0;
}

int CQ_isEof (CQhandle *h) { /* testuje konec fronty */
  CQ *q;

  q = h->q;
  if (!q->eof) return 0;
  if (q->tail != q->head) return 0;
  return 1;
}

int CQ_isEmpty (CQhandle *h) { /* testuje prazdnost fronty */
  CQ *q;

  q = h->q;
  if (q->tail != q->head) return 0;
  return 1;
}

int CQ_isFull (CQhandle *h) { /* testuje plnost fronty */
  CQ *q;

  q = h->q;
  if (q->allocated - q->deallocated >= q->queuesize) return 1;
  return 0;
}
