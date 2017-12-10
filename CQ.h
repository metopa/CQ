/*
 * cq.h
 * verze 0.9.7	9.12.2017
 *
 *  Circular queue interface
 * @author Jiri Kaspar
 *
 * pred include je treba definovat hodnoty SHARED a ALLIGNED
 *
 */

#ifndef CQ_INCLUDE
#define CQ_INCLUDE

#ifdef __cplusplus
extern "C" {
#endif

#include <pthread.h>

// velikost cache bloku
#define CBSIZE 64

#ifndef MAX_THREADS
#define MAX_THREADS 32
#endif

/* typy optimalizaci:
    Prefetch dalsi polozky - pro ctenare/pisare pointer/data
	Prefetch read pointer
	Prefetch read data
	Prefetch write pointer
	Prefetch write data
    Flush hotove polozky - pro ctenare/pisare pointer/data
	Flush read pointer
	Flush read data
	Flush write pointer
	Flush write data
    Non temporal move polozky - pro ctenare/pisare pointer/data
	Non temporal move read pointer
	Non temporal move read data
	Non temporal move write pointer
	Non temporal move write data
    Lazy pointer update
	Lazy pointer update read
	Lazy pointer update write
    Cache conscitious allocation
    Work stealing
	Forward work stealing
	Backward work stealing
    Trace
    Counters
 */
#define OPT_PRERP 1
#define OPT_PRERD 2
#define OPT_PREWP 4
#define OPT_PREWD 8
#define OPT_PRE 0xf
#define OPT_FLURP 0x10
#define OPT_FLURD 0x20
#define OPT_FLUWP 0x40
#define OPT_FLUWD 0x80
#define OPT_FLU 0xf0
#define OPT_NTMRP 0x100
#define OPT_NTMRD 0x200
#define OPT_NTMWP 0x400
#define OPT_NTMWD 0x800
#define OPT_NTM 0xf00
#define OPT_LPUR 0x1000
#define OPT_LPUW 0x2000
#define OPT_CCA 0x4000
#define OPT_DEBUG 0x8000
#define OPT_WSTFW 0x10000
#define OPT_WSTBW 0x20000
#define OPT_WST 0x30000
#define OPT_TRACE 0x40000
#define OPT_CNT 0x80000

#define OPT_NONE 0
#define OPT_ALL 0xfffff

// typy zpracovani zprav - jen jeden ctenar/broadcast/bariera
#define RDR_1 0
#define RDR_N 1
#define RDR_B 2

typedef ALIGNED struct { // lokalni struktura - pro kazdeho ctenare/pisare
  struct CQ *q;		 // fronta - sdilena struktura
  long rdwr;		 // 0 = ctenar, 1 = pisar
  long id;		 // id ctenare/pisare
  long cpu;		 // cislo vlakna/cpu
  long head;		 // lokalni ukazatel za posledni zapsanou polozku
  long tail;		 // lokalni ukazatel na prvni neprectenou polozku
  SHARED long rdcnt;     // logovani: citac prectenych zprav
  SHARED long wrcnt;     // logovani: citac zapsanych zprav
  SHARED long stcnt;     // logovani: citac ukradenych zprav
  SHARED long errcnt;    // logovani: citac poskozenych zprav
  char trace[128];       // trasovaci buffer pro zaznamenani postupu vlakna
} CQhandle;

typedef ALIGNED struct { // polozka fronty
  char *buffer;		 // ukazatel na buffer pro ulozeni zpravy
  SHARED long counter;   // pocet ctenaru, kteri jeste zpravu necetli
} CQentry;

typedef struct { // polozka fronty pro W1R1
  char *buffer;  // ukazatel na buffer pro ulozeni zpravy
} CQsimpleentry;

typedef struct CQ {			 // fronta - sdilena struktura
  long queuesize;			 // delka fronty
  long mask;				 // maska pro ziskani indexu z ukazatele
  long itemsize;			 // delka zpravy
  long allocsize;			 // delka zpravy v celych cache blocich
  short readers, writers;		 // predpokladany pocet ctenaru/pisaru
  SHARED short openreaders, openwriters; // aktualni pocet ctenaru/pisaru
  long optimizations;			 // zapnute optimalizace
  long stage;				 // faze zpracovani v pipeline
  char queuetype[8]; // typ fronty: W1R1, W1Rn1, W1Rnn, W1Rnb, WnR1, WnRn1, WnRnn,WnRnb
#ifdef MUTEX
  pthread_mutex_t *mutex;			      // lock
  pthread_mutex_t *mutexa, *mutexw, *mutexr, *mutexd; // locks: allocate, write, read, deallocate
  pthread_cond_t *notFull, *notEmpty, *barrier;       //
#endif
  CQhandle *(*open) (struct CQ *q, long rdwr); 	// otevre frontu pro cteni ci zapis
  void (*close) (CQhandle *h);			// uzavre handle na frontu
  void (*free) (struct CQ *q);			// zrusi frontu
  char *(*getBuffer) (CQhandle *h);		// doda volny buffer pro zapis zpravy
  char *(*getBufferNW) (CQhandle *h);		// doda volny buffer pro zapis zpravy, neni-li, neceka
  void (*putMsg) (CQhandle *h, char *buffer);	// zaradi buffer se zpravou do fronty
  char *(*getMsg) (CQhandle *h);		// doda buffer se zpravou
  char *(*getMsgNW) (CQhandle *h);		// doda buffer se zpravou, neni-li, neceka
  void (*putBuffer) (CQhandle *h, char *buffer); // vrati buffer s prectenou zpravou
  int (*readMsg) (CQhandle *h, char *buffer);	// precte zpravu do lokalniho bufferu
  int (*writeMsg) (CQhandle *h, char *buffer);	// zapise zpravu z lokalniho bufferu
  int (*isEof) (CQhandle *h);			// testuje konec fronty
  int (*isEmpty) (CQhandle *h);			// testuje prazdnost fronty
  int (*isFull) (CQhandle *h);			// testuje plnost fronty
  struct CQ *prevqueue;				// razeni front pro work stealing - queue empty
  struct CQ *nextqueue;				// razeni front pro work stealing - queue full
  void (*idlereader) (CQhandle *h); // scheduler - najde a obslouzi polozku z drivejsi fronty
  void (*busywriter) (CQhandle *h); // scheduler - najde a obslouzi polozku z dalsi fronty
  void (*worker) (CQhandle *h_in, char *buffer,
		  CQhandle *h_out); // zpracuje zpravu - pro work stealing
  char *buffers;		    // alokovane buffery
  CQentry *queue;		    // fronta bufferu
  CQsimpleentry *simplequeue;       // fronta bufferu pro variantu W1R1
  CQhandle *handles;		    //
  long usecnt;			    //
  SHARED long eof;		    // konec souboru - zadne dalsi zpravy jiz nebudou zapsany
  SHARED long ALIGNED allocated;    // ukazatel na prvni volny buffer pro zapis
  SHARED long ALIGNED queued;	// ukazatel za posledni zafrontovanou polozku, 
				//tj. prvni nezafrontovanou
  SHARED long ALIGNED head; 	// ukazatel za posledni zapsanou polozku, tj. prvni nezapsanou
  SHARED long ALIGNED tail; 	// ukazatel na prvni neprectenou polozku
  SHARED long ALIGNED read; 	// ukazatel za posledni zafrontovanou vracenou polozku, 
				// tj. prvni nezafrontovanou
  SHARED long ALIGNED
      deallocated; // ukazatel za posledni vraceny buffer, tj. na posledni zapisovany nebo obsazeny
  SHARED long stcnt;	// logovani: citac ukradenych zprav
  SHARED long injcnt;	// logovani: citac injectovanych zprav
  SHARED long errcnt;	// logovani: citac poskozenych zprav
} CQ;

CQ *CQ_init (long queuesize, long itemsize, short readers, short writers, short readertype,
	     unsigned long optimizations,
	     void (*worker) (CQhandle *h_in, char *buffer, CQhandle *h_out)
	    );

void CQ_help ();

void appl_help ();
void appl_init (int argc, char **argv);
void appl_run (int thread, int stage, int index, CQhandle *input, CQhandle *output);
void appl_stat ();

extern unsigned int num_threads;			// pocet obsluznych vlaken
extern unsigned int cq_cpus[MAX_THREADS];		// cisla pouzitych cpu
extern unsigned int cq_stages[MAX_THREADS];		// pocty vlaken pro jednotlive faze pipeline
extern unsigned int num_stages;				// pocet fazi v pipeline
extern void (*cq_workers [MAX_THREADS]) (CQhandle *h_in,
				     char *buffer, 
				     CQhandle *h_out);	// pole ukazatelu na workery
extern unsigned int cq_queuetypes[MAX_THREADS];		// typy front pro jednotlive faze pipeline
extern unsigned int cq_queuesizes[MAX_THREADS];		// delky front
extern unsigned long int cq_optimizations[MAX_THREADS]; // volby front
extern unsigned int cq_msgsizes[MAX_THREADS];		// delky zprav
extern unsigned long int cq_opt;			// zvolene optimalizace
extern unsigned int cq_tracestart;			// cas pocatku detailnich vypisu
extern unsigned int cq_traceint;			// interval mezi vypisy

extern pthread_t cq_threads[MAX_THREADS];	// vlakna
extern CQ *cq_queues[MAX_THREADS];		// fronty
extern CQhandle *cq_inputs[MAX_THREADS];	// vstupy
extern CQhandle *cq_outputs[MAX_THREADS];	// vystupy

#ifdef __cplusplus
}
#endif

#endif
