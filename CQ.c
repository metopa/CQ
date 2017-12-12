/*
 * CQ.c
 * verze 0.9.7	9.12.2017
 *
 * Circular queue routines
 * @author Jiri Kaspar
 */

#define SHARED volatile
#define ALIGNED __attribute__ ((aligned (64)))

#include "CQ.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef DEBUG
#define Dprintf(f, v)                                                                              \
  {                                                                                                \
    if (cq_opt & OPT_DEBUG) printf (f, h->id, v);                                        \
  }
#define Eprintf(f, v, w)                                                                           \
  {                                                                                                \
    if (cq_opt & OPT_DEBUG) printf (f, h->id, v, w);                                     \
  }
#define Fprintf(f, v, w, x)                                                                        \
  {                                                                                                \
    if (cq_opt & OPT_DEBUG) printf (f, h->id, v, w, x);                                  \
  }
#define Tprintf(f, v)                                                                              \
  {                                                                                                \
    if (cq_opt & OPT_TRACE) sprintf (h->trace, f, h->id, v);                             \
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

CQhandle *CQ_open (CQ *q, long rdwr);	// otevre frontu pro cteni ci zapis
void CQ_close (CQhandle *h);		     // uzavre handle na frontu
void CQ_free (CQ *q);			     // zrusi frontu
int CQ_readMsg (CQhandle *h, char *buffer);  // precte zpravu do lokalniho bufferu
int CQ_writeMsg (CQhandle *h, char *buffer); // zapise zpravu z lokalniho bufferu
int CQ_isEof (CQhandle *h);		     // testuje konec fronty
int CQ_isEmpty (CQhandle *h);		     // testuje prazdnost fronty
int CQ_isFull (CQhandle *h);		     // testuje plnost fronty

// Wn = n pisaru
char *Wn_getBuffer (CQhandle *h);	   // doda volny buffer pro zapis zpravy
char *Wn_getBufferNW (CQhandle *h);	 // doda volny buffer pro zapis zpravy, neni-li, neceka
void Wn_putMsg (CQhandle *h, char *buffer); // zaradi buffer se zpravou do fronty

// Rn1 = n ctenaru, zpravu cte jen jeden z nich
char *Rn1_getMsg (CQhandle *h);			// doda buffer se zpravou
char *Rn1_getMsgNW (CQhandle *h);		// doda buffer se zpravou, neni-li, neceka
void Rn1_putBuffer (CQhandle *h, char *buffer); // vrati buffer s prectenou zpravou

// W1 = 1 pisar
char *W1_getBuffer (CQhandle *h);	   // doda volny buffer pro zapis zpravy
char *W1_getBufferNW (CQhandle *h);	 // doda volny buffer pro zapis zpravy, neni-li, neceka
void W1_putMsg (CQhandle *h, char *buffer); // zaradi buffer se zpravou do fronty

// R11 = 1 ctenar
char *R11_getMsg (CQhandle *h);		       // doda buffer se zpravou
char *R11_getMsgNW (CQhandle *h);	      // doda buffer se zpravou, neni-li, neceka
void R1_putBuffer (CQhandle *h, char *buffer); // vrati buffer s prectenou zpravou

// W1R1 = 1 pisar, 1 ctenar
char *W1R1_getBuffer (CQhandle *h);	   // doda volny buffer pro zapis zpravy
char *W1R1_getBufferNW (CQhandle *h);	 // doda volny buffer pro zapis zpravy, neni-li, neceka
void W1R1_putMsg (CQhandle *h, char *buffer); // zaradi buffer se zpravou do fronty
char *W1R1_getMsg (CQhandle *h);	      // doda buffer se zpravou
char *W1R1_getMsgNW (CQhandle *h);	    // doda buffer se zpravou, neni-li, neceka
void W1R1_putBuffer (CQhandle *h, char *buffer); // vrati buffer s prectenou zpravou
int W1R1_isFull (CQhandle *h);			 // testuje plnost fronty

// Rnn = n ctenaru, zpravu ctou vsichni - broadcast
char *Rnn_getMsg (CQhandle *h);			// doda buffer se zpravou
char *Rnn_getMsgNW (CQhandle *h);		// doda buffer se zpravou, neni-li, neceka
void Rnn_putBuffer (CQhandle *h, char *buffer); // vrati buffer s prectenou zpravou

// Rnb = n ctenaru, zpravu ctou vsichni - bariera
void Rnb_putBuffer (CQhandle *h, char *buffer); // vrati buffer s prectenou zpravou

// work stealing schedulers
void CQ_idlereader (CQhandle *h); // scheduler - najde a obslouzi polozku z drivejsi fronty
void CQ_busywriter (CQhandle *h); // scheduler - najde a obslouzi polozku z dalsi fronty

CQ *CQ_init (long queuesize, long itemsize, short readers, short writers, short readertype,
	     unsigned long optimizations,
             void (*worker) (CQhandle *h_in, char *buffer, CQhandle *h_out)) {
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
  this->stage = 0;
  strncpy (this->queuetype, "WnRn1", 8);
  this->eof = 0;
  this->allocated = 0;
  this->queued = 0;
  this->head = 0;
  this->tail = 0;
  this->read = 0;
  this->deallocated = 0;
  this->stcnt = 0;
  this->injcnt = 0;
  this->errcnt = 0;

  this->buffers = malloc (this->queuesize * this->allocsize);
  if ((readers == 1) && (writers == 1) && (readertype == RDR_1) &&
      (worker == 0)) { // pro W1R1
    this->queue = 0;
    this->simplequeue = malloc (this->queuesize * sizeof (CQsimpleentry));
    for (i = 0; i < this->queuesize; i++) {
      se = this->simplequeue + i;
      se->buffer = &this->buffers[i * this->allocsize];
    }
  } else { // pro ostatni varianty
    this->simplequeue = 0;
    this->queue = malloc (this->queuesize * sizeof (CQentry));
    for (i = 0; i < this->queuesize; i++) {
      e = this->queue + i;
      e->buffer = &this->buffers[i * this->allocsize];
    }
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

  // WnRn1 je default
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

  this->prevqueue = 0;
  this->nextqueue = 0;
  this->worker = worker;
  this->idlereader = &CQ_idlereader;
  this->busywriter = &CQ_busywriter;

  if (readertype == RDR_1) {
    if ((readers == 1) && (writers == 1) && (worker == 0)) {
      // zjednodusene rutiny pro W1R1
      this->getBuffer = &W1R1_getBuffer;
      this->getBufferNW = &W1R1_getBufferNW;
      this->putMsg = &W1_putMsg;
      this->getMsg = &W1R1_getMsg;
      this->getMsgNW = &W1R1_getMsgNW;
      this->putBuffer = &R1_putBuffer;
      this->isFull = &W1R1_isFull;
      strncpy (this->queuetype, "W1R1", 8);
    } else if ((readers == 1) && (worker == 0)) {
      // zjednodusene rutiny pro WnR11
      this->getMsg = &R11_getMsg;
      this->getMsgNW = &R11_getMsgNW;
      this->putBuffer = &R1_putBuffer;
      strncpy (this->queuetype, "WnR11", 8);
    } else if (writers == 1) {
      // zjednodusene rutiny pro W1Rn1
      this->getBuffer = &W1_getBuffer;
      this->getBufferNW = &W1_getBufferNW;
      this->putMsg = &W1_putMsg;
      strncpy (this->queuetype, "W1Rn1", 8);
    }
  } else {
    this->worker = 0;
    this->getMsg = &Rnn_getMsg;
    this->getMsgNW = &Rnn_getMsgNW;
    if (readertype == RDR_B) {
      this->putBuffer = &Rnb_putBuffer;
      strncpy (this->queuetype, "WnRnb", 8);
    } else {
      this->putBuffer = &Rnn_putBuffer;
      strncpy (this->queuetype, "WnRnn", 8);
    }
    if (writers == 1) { // rutiny pro W1Rnn a W1Rnb
      this->getBuffer = &W1_getBuffer;
      this->getBufferNW = &W1_getBufferNW;
      this->putMsg = &W1_putMsg;
      if (readertype == RDR_B)
	strncpy (this->queuetype, "W1Rnb", 8);
      else
	strncpy (this->queuetype, "W1Rnn", 8);
    }
  }
  return this;
}

CQhandle *CQ_open (CQ *q, long rdwr) { // otevre frontu pro cteni ci zapis
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

void CQ_close (CQhandle *h) { // uzavre handle na frontu
  CQ *q;

  q = h->q;
  if (h->rdwr == 0) {
    __sync_fetch_and_add (&q->openreaders, -1);
    //	Dprintf("R%ld:close %s\n","reader")
  } else {
    if (__sync_add_and_fetch (&q->openwriters, -1) == 0) q->eof = 1;
    //	Dprintf("W%ld:close %s\n","writer")
  }
}

void CQ_free (CQ *q) { // zrusi frontu
  if ((q->openreaders > 0) || (q->openwriters > 0)) return;
  free (q->buffers);
  if (q->simplequeue) free (q->simplequeue);
  if (q->queue) free (q->queue);
  free (q->handles);
  free (q);
}

// Wn = n pisaru

char *Wn_getBuffer (CQhandle *h) { // doda volny buffer pro zapis zpravy
  CQ *q;
  CQentry *e;
  char *b;
  long old, new, index;
  long success;

  q = h->q;
  //    Dprintf("W%ld:getBuffer %s\n"," ")
  do {
    while ((old = q->allocated) - q->deallocated == q->queuesize) {
      // ~delsi cekani - na precteni zpravy ctenarem
      if (q->optimizations & OPT_WSTFW) q->busywriter (h);
    }
    index = old & q->mask;
    e = q->queue + index;
    b = e->buffer;
    new = old + 1;
    success = __sync_bool_compare_and_swap (&q->allocated, old, new);
  } while (!success);
  //    Fprintf("W%ld:getBuffer old=%ld new=%ld success= %ld\n", old, new, success)
  //    Fprintf("W%ld:getBuffer %ld %ld: %p\n", q->mask, index, b)
  return b;
}

char *Wn_getBufferNW (CQhandle *h) { // doda volny buffer pro zapis zpravy, neni-li, neceka
  CQ *q;
  CQentry *e;
  char *b;
  long old, new, index;
  long success;

  q = h->q;
  if ((old = q->allocated) - q->deallocated == q->queuesize) return 0;
  index = old & q->mask;
  e = q->queue + index;
  b = e->buffer;
  new = old + 1;
  success = __sync_bool_compare_and_swap (&q->allocated, old, new);
  if (!success) return 0;
  return b;
}

void Wn_putMsg (CQhandle *h, char *buffer) { // zaradi buffer se zpravou do fronty
  CQ *q;
  CQentry *e;
  long old, index;

  q = h->q;
  //    Dprintf("W%ld:putMsg %p\n", buffer)
  old = __sync_fetch_and_add (&q->queued, 1);
  index = old & q->mask;
  e = q->queue + index;
  e->buffer = buffer;
  e->counter = q->readers;
  while (q->head < old) { // ~kratke cekani - jen zapis pripravene zpravy jinym pisarem
  }
  q->head = old + 1;
  if (q->optimizations & OPT_CNT) h->wrcnt++;
  //    Dprintf("W%ld:putMsg %s\n", "done")
}

// Rn1 = n ctenaru, zpravu cte jen jeden z nich

char *Rn1_getMsg (CQhandle *h) { // doda buffer se zpravou
  CQ *q;
  CQentry *e;
  char *b;
  long old, new, index;
  long success;

  q = h->q;
  //    Dprintf("R%ld:getMsg  %s\n"," ")
  do {
    while ((old = q->tail) == q->head) {
      // ~delsi cekani - na zapis zpravy pisarem
      if (q->eof) return 0;
      if ((q->prevqueue) && (q->optimizations & OPT_WSTBW)) q->idlereader (h);
    }
    index = old & q->mask;
    e = q->queue + index;
    b = e->buffer;
    new = old + 1;
    success = __sync_bool_compare_and_swap (&q->tail, old, new);
  } while (!success);
  //    Dprintf("R%ld:getMsg %p\n",b)
  if (q->optimizations & OPT_CNT) h->rdcnt++;
  return b;
}

char *Rn1_getMsgNW (CQhandle *h) { // doda buffer se zpravou, neni-li, neceka
  CQ *q;
  CQentry *e;
  char *b;
  long old, new, index;
  long success;

  q = h->q;
  if ((old = q->tail) == q->head) return 0;
  index = old & q->mask;
  e = q->queue + index;
  b = e->buffer;
  new = old + 1;
  success = __sync_bool_compare_and_swap (&q->tail, old, new);
  if (!success) return 0;
  if (q->optimizations & OPT_CNT) h->rdcnt++;
  return b;
}

void Rn1_putBuffer (CQhandle *h, char *buffer) { // vrati buffer s prectenou zpravou
  CQ *q;
  CQentry *e;
  long old, index;

  q = h->q;
  //    Dprintf("R%ld:putBuffer %p\n", buffer)
  old = __sync_fetch_and_add (&q->read, 1);
  index = old & q->mask;
  e = q->queue + index;
  e->buffer = buffer;
  while (q->deallocated < old) { // ~kratke cekani - jen vraceni bufferu jinym ctenarem
  }
  q->deallocated = old + 1;
  //    Dprintf("R%ld:putBuffer %s\n","done")
}

// Rnn = n ctenaru, zpravu ctou vsichni - broadcast

char *Rnn_getMsg (CQhandle *h) { // doda buffer se zpravou
  CQ *q;
  CQentry *e;
  char *b;
  long old, new, index;
  long success;

  q = h->q;
  //    Dprintf("R%ld:getMsg  %s\n"," ")
  while ((old = h->tail) == q->head) {
    // ~delsi cekani - na zapis zpravy pisarem
    if (q->eof) return 0;
    if ((q->prevqueue) && (q->optimizations & OPT_WSTBW)) q->idlereader (h);
  }
  index = old & q->mask;
  e = q->queue + index;
  b = e->buffer;
  //    Dprintf("R%ld:getMsg %p\n",b)
  if (q->optimizations & OPT_CNT) h->rdcnt++;
  return b;
}

char *Rnn_getMsgNW (CQhandle *h) { // doda buffer se zpravou, neni-li, neceka
  CQ *q;
  CQentry *e;
  char *b;
  long old, new, index;
  long success;

  q = h->q;
  if ((old = h->tail) == q->head) return 0;
  index = old & q->mask;
  e = q->queue + index;
  b = e->buffer;
  if (q->optimizations & OPT_CNT) h->rdcnt++;
  return b;
}

void Rnn_putBuffer (CQhandle *h, char *buffer) { // vrati buffer s prectenou zpravou
  CQ *q;
  CQentry *e;
  long old, cnt, index;

  q = h->q;
  //    Dprintf("R%ld:putBuffer %p\n", buffer)
  index = h->tail & q->mask;
  h->tail += 1;
  e = q->queue + index;
  cnt = __sync_add_and_fetch (&e->counter, -1);
  if (cnt == 0) {
    q->tail += 1;
    q->deallocated += 1;
  }
  //    Dprintf("R%ld:putBuffer %s\n","done")
}

// Rnb = n ctenaru, zpravu ctou vsichni - bariera

void Rnb_putBuffer (CQhandle *h, char *buffer) { // vrati buffer s prectenou zpravou
  CQ *q;
  CQentry *e;
  long old, cnt, index;

  q = h->q;
  //    Dprintf("R%ld:putBuffer %p\n", buffer)
  index = h->tail & q->mask;
  h->tail += 1;
  e = q->queue + index;
  cnt = __sync_add_and_fetch (&e->counter, -1);
  if (cnt == 0) {
    q->tail += 1;
    q->deallocated += 1;
  } else
    while (h->tail != q->tail) { // ceka, az zpravu precte posledni ctenar
    }
  //    Dprintf("R%ld:putBuffer %s\n","done")
}

// W1 = 1 pisar

char *W1_getBuffer (CQhandle *h) { // doda volny buffer pro zapis zpravy
  CQ *q;
  CQentry *e;
  char *b;
  long old, index;

  q = h->q;
  //    Dprintf("W%ld:getBuffer %s\n"," ")
  while ((old = q->head) - q->deallocated == q->queuesize) {
    // ~delsi cekani - na precteni zpravy ctenarem
    if (q->optimizations & OPT_WSTFW) q->busywriter (h);
  }
  q->allocated += 1;
  index = old & q->mask;
  e = q->queue + index;
  b = e->buffer;
  e->counter = q->readers;
  return b;
}

char *W1_getBufferNW (CQhandle *h) { // doda volny buffer pro zapis zpravy, neni-li, neceka
  CQ *q;
  CQentry *e;
  char *b;
  long old, index;

  q = h->q;
  //    Dprintf("W%ld:getBuffer %s\n"," ")
  if ((old = q->head) - q->deallocated == q->queuesize) return 0;
  q->allocated += 1;
  index = old & q->mask;
  e = q->queue + index;
  b = e->buffer;
  e->counter = q->readers;
  return b;
}

void W1_putMsg (CQhandle *h, char *buffer) { // zaradi buffer se zpravou do fronty
  CQ *q;

  q = h->q;
  q->head += 1;
  if (q->optimizations & OPT_CNT) h->wrcnt++;
}

// R11 = 1 ctenar

char *R11_getMsg (CQhandle *h) { // doda buffer se zpravou
  CQ *q;
  CQentry *e;
  char *b;
  long old, index;

  q = h->q;
  //    Dprintf("R%ld:getMsg  %s\n"," ")
  while ((old = q->tail) == q->head) {
    // ~delsi cekani - na zapis zpravy pisarem
    if (q->eof) return 0;
    if ((q->prevqueue) && (q->optimizations & OPT_WSTBW)) q->idlereader (h);
  }
  index = old & q->mask;
  e = q->queue + index;
  b = e->buffer;
  //    Dprintf("R%ld:getMsg %p\n",b)
  if (q->optimizations & OPT_CNT) h->rdcnt++;
  return b;
}

char *R11_getMsgNW (CQhandle *h) { // doda buffer se zpravou, neni-li, neceka
  CQ *q;
  CQentry *e;
  char *b;
  long old, index;

  q = h->q;
  //    Dprintf("R%ld:getMsg  %s\n"," ")
  if ((old = q->tail) == q->head) return 0;
  index = old & q->mask;
  e = q->queue + index;
  b = e->buffer;
  //    Dprintf("R%ld:getMsg %p\n",b)
  if (q->optimizations & OPT_CNT) h->rdcnt++;
  return b;
}

void R1_putBuffer (CQhandle *h, char *buffer) { // vrati buffer s prectenou zpravou
  CQ *q;

  q = h->q;
  q->tail += 1;
  q->deallocated += 1;
  //    Dprintf("R%ld:putBuffer %s\n","done")
}

// W1R1 = 1 pisar, 1 ctenar

char *W1R1_getBuffer (CQhandle *h) { // doda volny buffer pro zapis zpravy
  CQ *q;
  CQsimpleentry *e;
  char *b;
  long old, index;

  q = h->q;
  e = q->simplequeue;
  //    Dprintf("W%ld:getBuffer %s\n"," ")
  while ((old = q->head) - q->tail == q->queuesize) {
    // ~delsi cekani - na precteni zpravy ctenarem
    if (q->optimizations & OPT_WSTFW) q->busywriter (h);
  }
  index = old & q->mask;
  e = e + index;
  b = e->buffer;
  //    Dprintf("W%ld:getBuffer %ld \n", old)
  //    Fprintf("W%ld:getBuffer %ld %ld: %p\n", q->mask, index, b)
  return b;
}

char *W1R1_getBufferNW (CQhandle *h) { // doda volny buffer pro zapis zpravy, neni-li, neceka
  CQ *q;
  CQsimpleentry *e;
  char *b;
  long old, index;

  q = h->q;
  e = q->simplequeue;
  //    Dprintf("W%ld:getBuffer %s\n"," ")
  if ((old = q->head) - q->tail == q->queuesize) return 0;
  index = old & q->mask;
  e = e + index;
  b = e->buffer;
  //    Dprintf("W%ld:getBuffer %ld \n", old)
  //    Fprintf("W%ld:getBuffer %ld %ld: %p\n", q->mask, index, b)
  return b;
}

char *W1R1_getMsg (CQhandle *h) { // doda buffer se zpravou
  CQ *q;
  CQsimpleentry *e;
  char *b;
  long old, index;

  q = h->q;
  e = q->simplequeue;
  //    Dprintf("R%ld:getMsg  %s\n"," ")
  while ((old = q->tail) == q->head) {
    // ~delsi cekani - na zapis zpravy pisarem
    if (q->eof) return 0;
    if ((q->prevqueue) && (q->optimizations & OPT_WSTBW)) q->idlereader (h);
  }
  index = old & q->mask;
  e = e + index;
  b = e->buffer;
  //    Dprintf("R%ld:getMsg %p\n",b)
  if (q->optimizations & OPT_CNT) h->rdcnt++;
  return b;
}

char *W1R1_getMsgNW (CQhandle *h) { // doda buffer se zpravou, neni-li, neceka
  CQ *q;
  CQsimpleentry *e;
  char *b;
  long old, index;

  q = h->q;
  e = q->simplequeue;
  //    Dprintf("R%ld:getMsg  %s\n"," ")
  if ((old = q->tail) == q->head) return 0;
  index = old & q->mask;
  e = e + index;
  b = e->buffer;
  if (q->optimizations & OPT_CNT) h->rdcnt++;
  return b;
}

int W1R1_isFull (CQhandle *h) { // testuje plnost fronty
  CQ *q;

  q = h->q;
  if (q->head - q->tail == q->queuesize) return 1;
  return 0;
}

int CQ_readMsg (CQhandle *h, char *buffer) { // precte zpravu do lokalniho bufferu
  CQ *q;

  char *b;
  q = h->q;
  b = q->getMsg (h);
  if (b) {
    if (q->optimizations & OPT_NTMRD) {
      // non temporal move
    } else {
      memmove (buffer, b, q->itemsize);
    }
    q->putBuffer (h, b);
    return 1;
  }
  return 0;
}

int CQ_writeMsg (CQhandle *h, char *buffer) { // zapise zpravu z lokalniho bufferu
  CQ *q;

  char *b;
  q = h->q;
  b = q->getBuffer (h);
  if (b) {
    if (q->optimizations & OPT_NTMWD) {
      // non temporal move
    } else {
      memmove (b, buffer, q->itemsize);
    }
    q->putMsg (h, b);
    return 1;
  }
  return 0;
}

int CQ_isEof (CQhandle *h) { // testuje konec fronty
  CQ *q;

  q = h->q;
  if (!q->eof) return 0;
  if (q->tail != q->head) return 0;
  return 1;
}

int CQ_isEmpty (CQhandle *h) { // testuje prazdnost fronty
  CQ *q;

  q = h->q;
  if (q->tail != q->head) return 0;
  return 1;
}

int CQ_isFull (CQhandle *h) { // testuje plnost fronty
  CQ *q;

  q = h->q;
  if (q->allocated - q->deallocated >= q->queuesize) return 1;
  return 0;
}

void CQ_idlereader (CQhandle *h) { // scheduler - najde a obslouzi polozku z drivejsi fronty
  char *b;
  CQ *q;
  CQhandle h_in, h_out;

  q = h->q->prevqueue;
  while (q) {
    if ((q->worker != 0) && (q->tail != q->head)) {
      h_in.q = q;
      h_in.rdwr = 0;
      h_in.id = -1;
      h_in.errcnt = 0;
      h_out.q = q->nextqueue;
      h_out.rdwr = 1;
      h_out.id = -2;
      h_out.wrcnt = 0;
      if ((b = q->getMsgNW (&h_in))) {
	q->worker (&h_in, b, &h_out);
	q->putBuffer (&h_in, b);
	if (q->optimizations & OPT_CNT) {
	  h->stcnt++;				// handle steal counter
          __sync_fetch_and_add (&q->stcnt, 1);  // queue steal counter
          if (h_in.errcnt) {
	    __sync_fetch_and_add (&q->errcnt, 1);  // queue error counter
	    h->errcnt += 1;
	  }
          if (h_out.q && h_out.wrcnt)
	    __sync_fetch_and_add (&h_out.q->injcnt, 1);  // queue inject counter
	}
	return;
      }
      if (q->eof) return;
    }
    q = q->prevqueue;
  }
}

void CQ_busywriter (CQhandle *h) { // scheduler - najde a obslouzi polozku z dalsi fronty
  char *b;
  CQ *q;
  CQhandle h_in, h_out;

  q = h->q;
  while (q) {
    if ((q->worker != 0) && (q->tail != q->head)) {
      h_in.q = q;
      h_in.rdwr = 0;
      h_in.id = -3;
      h_in.errcnt = 0;
      h_out.q = q->nextqueue;
      h_out.rdwr = 1;
      h_out.id = -4;
      h_out.wrcnt = 0;
      if (b = q->getMsgNW (&h_in)) {
	q->worker (&h_in, b, &h_out);
	q->putBuffer (&h_in, b);
        if (q->optimizations & OPT_CNT) {
          h->stcnt++;                           // handle steal counter
          __sync_fetch_and_add (&q->stcnt, 1);  // queue steal counter
	  if (h_in.errcnt) {
	    __sync_fetch_and_add (&q->errcnt, 1);  // queue error counter
	    h->errcnt += 1;
	  }
          if (h_out.q && h_out.wrcnt)
	    __sync_fetch_and_add (&h_out.q->injcnt, 1);  // queue inject counter
	}
	return;
      }
    }
    q = q->nextqueue;
  }
}
