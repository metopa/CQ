/*
 * CQpipeline.h
 * verze 0.9.7	9.12.2017
 *
 *  Circular queue interface
 * @author Jiri Kaspar
 * @author Viacheslav Kroilov
 *
 * pred include je treba definovat hodnoty SHARED a ALIGNED
 *
 */
#ifndef CQ_PIPELINE_INCLUDE
#define CQ_PIPELINE_INCLUDE

#include "CQ.h"

#ifdef __cplusplus
extern "C" {
namespace cq {
namespace c {
#endif

struct _CQPipelineCtx;

typedef void(* pipeline_worker_func_t)(struct _CQPipelineCtx* ctx,
									   int thread, int stage, int index,
									   CQhandle* input, CQhandle* output);
typedef void(* pipeline_help_func_t)(struct _CQPipelineCtx* ctx);
typedef void(* pipeline_stat_func_t)(struct _CQPipelineCtx* ctx);
typedef int(* pipeline_unknown_cl_opt_handler_t)(struct _CQPipelineCtx* ctx,
												 int argc, char** argv);
typedef void(* pipeline_stealer_func_t)(struct _CQPipelineCtx* ctx, CQhandle* h_in,
										char* buffer, CQhandle* h_out);

typedef struct {
	unsigned int id;
	struct _CQPipelineCtx* ctx;
} CQPipelineThreadCtx;

typedef struct _CQPipelineCtx {
	unsigned int num_threads;            // pocet obsluznych vlaken
	unsigned int cq_cpus[MAX_THREADS];        // cisla pouzitych cpu
	unsigned int cq_stages[MAX_THREADS];        // pocty vlaken pro jednotlive faze pipeline
	unsigned int num_stages;                // pocet fazi v pipeline

	CQPipelineThreadCtx thread_ctx[MAX_THREADS];

	unsigned int cq_queuetypes[MAX_THREADS];        // typy front pro jednotlive faze pipeline
	unsigned int cq_queuesizes[MAX_THREADS];        // delky front
	unsigned long int cq_optimizations[MAX_THREADS]; // volby front
	unsigned int cq_msgsizes[MAX_THREADS];        // delky zprav
	unsigned long int cq_opt;            // zvolene optimalizace
	unsigned int cq_tracestart;            // cas pocatku detailnich vypisu
	unsigned int cq_traceint;            // interval mezi vypisy

	struct timespec cq_stop_time, cq_start_time;

	volatile unsigned int atomic_barrier;

	pthread_t cq_threads[MAX_THREADS];    // vlakna
	CQ* cq_queues[MAX_THREADS];        // fronty
	CQhandle* cq_inputs[MAX_THREADS];    // vstupy
	CQhandle* cq_outputs[MAX_THREADS];    // vystupy

	pipeline_worker_func_t worker_func;
	pipeline_help_func_t help_func;
	pipeline_stat_func_t stat_func;
	pipeline_unknown_cl_opt_handler_t unknown_opt_handler;

	pipeline_stealer_func_t stealer_func[MAX_THREADS];

	void* cq_user_data;  // ukazatel na doplnkovy argumenty pro workery
} CQPipelineCtx;

void CQ_help();


CQPipelineCtx* CQPipeline_create(int argc, char** argv,
								 pipeline_worker_func_t, pipeline_help_func_t,
								 pipeline_stat_func_t, pipeline_unknown_cl_opt_handler_t,
								 void* user_data);

void CQPipeline_start(CQPipelineCtx* ctx);
void CQPipeline_join(CQPipelineCtx* ctx);
void CQPipeline_stats(CQPipelineCtx* ctx);
void CQPipeline_destroy(CQPipelineCtx* ctx);


#ifdef __cplusplus
} // namespace c
} // namespace cq
} // extern "C"
#endif

#endif //CQ_PIPELINE_INCLUDE
