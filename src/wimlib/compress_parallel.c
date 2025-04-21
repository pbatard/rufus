/*
 * compress_parallel.c
 *
 * Compress chunks of data (parallel version).
 */

/*
 * Copyright (C) 2013-2023 Eric Biggers
 *
 * This file is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option) any
 * later version.
 *
 * This file is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this file; if not, see https://www.gnu.org/licenses/.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "wimlib/assert.h"
#include "wimlib/chunk_compressor.h"
#include "wimlib/error.h"
#include "wimlib/list.h"
#include "wimlib/threads.h"
#include "wimlib/util.h"

struct message_queue {
	struct list_head list;
	struct mutex lock;
	struct condvar msg_avail_cond;
	struct condvar space_avail_cond;
	bool terminating;
};

struct compressor_thread_data {
	struct thread thread;
	struct message_queue *chunks_to_compress_queue;
	struct message_queue *compressed_chunks_queue;
	struct wimlib_compressor *compressor;
};

#define MAX_CHUNKS_PER_MSG 16

struct message {
	u8 *uncompressed_chunks[MAX_CHUNKS_PER_MSG];
	u8 *compressed_chunks[MAX_CHUNKS_PER_MSG];
	u32 uncompressed_chunk_sizes[MAX_CHUNKS_PER_MSG];
	u32 compressed_chunk_sizes[MAX_CHUNKS_PER_MSG];
	size_t num_filled_chunks;
	size_t num_alloc_chunks;
	struct list_head list;
	bool complete;
	struct list_head submission_list;
};

struct parallel_chunk_compressor {
	struct chunk_compressor base;

	struct message_queue chunks_to_compress_queue;
	struct message_queue compressed_chunks_queue;
	struct compressor_thread_data *thread_data;
	unsigned num_thread_data;
	unsigned num_started_threads;

	struct message *msgs;
	size_t num_messages;

	struct list_head available_msgs;
	struct list_head submitted_msgs;
	struct message *next_submit_msg;
	struct message *next_ready_msg;
	size_t next_chunk_idx;
};



static int
message_queue_init(struct message_queue *q)
{
	if (!mutex_init(&q->lock))
		goto err;
	if (!condvar_init(&q->msg_avail_cond))
		goto err_destroy_lock;
	if (!condvar_init(&q->space_avail_cond))
		goto err_destroy_msg_avail_cond;
	INIT_LIST_HEAD(&q->list);
	return 0;

err_destroy_msg_avail_cond:
	condvar_destroy(&q->msg_avail_cond);
err_destroy_lock:
	mutex_destroy(&q->lock);
err:
	return WIMLIB_ERR_NOMEM;
}

static void
message_queue_destroy(struct message_queue *q)
{
	if (q->list.next != NULL) {
		mutex_destroy(&q->lock);
		condvar_destroy(&q->msg_avail_cond);
		condvar_destroy(&q->space_avail_cond);
	}
}

static void
message_queue_put(struct message_queue *q, struct message *msg)
{
	mutex_lock(&q->lock);
	list_add_tail(&msg->list, &q->list);
	condvar_signal(&q->msg_avail_cond);
	mutex_unlock(&q->lock);
}

static struct message *
message_queue_get(struct message_queue *q)
{
	struct message *msg;

	mutex_lock(&q->lock);
	while (list_empty(&q->list) && !q->terminating)
		condvar_wait(&q->msg_avail_cond, &q->lock);
	if (!q->terminating) {
		msg = list_entry(q->list.next, struct message, list);
		list_del(&msg->list);
	} else
		msg = NULL;
	mutex_unlock(&q->lock);
	return msg;
}

static void
message_queue_terminate(struct message_queue *q)
{
	mutex_lock(&q->lock);
	q->terminating = true;
	condvar_broadcast(&q->msg_avail_cond);
	mutex_unlock(&q->lock);
}

static int
init_message(struct message *msg, size_t num_chunks, u32 out_chunk_size)
{
	msg->num_alloc_chunks = num_chunks;
	for (size_t i = 0; i < num_chunks; i++) {
		msg->compressed_chunks[i] = MALLOC(out_chunk_size - 1);
		msg->uncompressed_chunks[i] = MALLOC(out_chunk_size);
		if (msg->compressed_chunks[i] == NULL ||
		    msg->uncompressed_chunks[i] == NULL)
			return WIMLIB_ERR_NOMEM;
	}
	return 0;
}

static void
destroy_message(struct message *msg)
{
	for (size_t i = 0; i < msg->num_alloc_chunks; i++) {
		FREE(msg->compressed_chunks[i]);
		FREE(msg->uncompressed_chunks[i]);
	}
}

static void
free_messages(struct message *msgs, size_t num_messages)
{
	if (msgs) {
		for (size_t i = 0; i < num_messages; i++)
			destroy_message(&msgs[i]);
		FREE(msgs);
	}
}

static struct message *
allocate_messages(size_t count, size_t chunks_per_msg, u32 out_chunk_size)
{
	struct message *msgs;

	msgs = CALLOC(count, sizeof(struct message));
	if (msgs == NULL)
		return NULL;
	for (size_t i = 0; i < count; i++) {
		if (init_message(&msgs[i], chunks_per_msg, out_chunk_size)) {
			free_messages(msgs, count);
			return NULL;
		}
	}
	return msgs;
}

static void
compress_chunks(struct message *msg, struct wimlib_compressor *compressor)
{

	for (size_t i = 0; i < msg->num_filled_chunks; i++) {
		wimlib_assert(msg->uncompressed_chunk_sizes[i] != 0);
		msg->compressed_chunk_sizes[i] =
			wimlib_compress(msg->uncompressed_chunks[i],
					msg->uncompressed_chunk_sizes[i],
					msg->compressed_chunks[i],
					msg->uncompressed_chunk_sizes[i] - 1,
					compressor);
	}
}

static void *
compressor_thread_proc(void *arg)
{
	struct compressor_thread_data *params = arg;
	struct message *msg;

	while ((msg = message_queue_get(params->chunks_to_compress_queue)) != NULL) {
		compress_chunks(msg, params->compressor);
		message_queue_put(params->compressed_chunks_queue, msg);
	}
	return NULL;
}

static void
parallel_chunk_compressor_destroy(struct chunk_compressor *_ctx)
{
	struct parallel_chunk_compressor *ctx = (struct parallel_chunk_compressor *)_ctx;
	unsigned i;

	if (ctx == NULL)
		return;

	if (ctx->num_started_threads != 0) {
		message_queue_terminate(&ctx->chunks_to_compress_queue);

		for (i = 0; i < ctx->num_started_threads; i++)
			thread_join(&ctx->thread_data[i].thread);
	}

	message_queue_destroy(&ctx->chunks_to_compress_queue);
	message_queue_destroy(&ctx->compressed_chunks_queue);

	if (ctx->thread_data != NULL)
		for (i = 0; i < ctx->num_thread_data; i++)
			wimlib_free_compressor(ctx->thread_data[i].compressor);

	FREE(ctx->thread_data);

	free_messages(ctx->msgs, ctx->num_messages);

	FREE(ctx);
}

static void
submit_compression_msg(struct parallel_chunk_compressor *ctx)
{
	struct message *msg = ctx->next_submit_msg;

	msg->complete = false;
	list_add_tail(&msg->submission_list, &ctx->submitted_msgs);
	message_queue_put(&ctx->chunks_to_compress_queue, msg);
	ctx->next_submit_msg = NULL;
}

static void *
parallel_chunk_compressor_get_chunk_buffer(struct chunk_compressor *_ctx)
{
	struct parallel_chunk_compressor *ctx = (struct parallel_chunk_compressor *)_ctx;
	struct message *msg;

	if (ctx->next_submit_msg) {
		msg = ctx->next_submit_msg;
	} else {
		if (list_empty(&ctx->available_msgs))
			return NULL;

		msg = list_entry(ctx->available_msgs.next, struct message, list);
		list_del(&msg->list);
		ctx->next_submit_msg = msg;
		msg->num_filled_chunks = 0;
	}

	return msg->uncompressed_chunks[msg->num_filled_chunks];
}

static void
parallel_chunk_compressor_signal_chunk_filled(struct chunk_compressor *_ctx, u32 usize)
{
	struct parallel_chunk_compressor *ctx = (struct parallel_chunk_compressor *)_ctx;
	struct message *msg;

	wimlib_assert(usize > 0);
	wimlib_assert(usize <= ctx->base.out_chunk_size);
	wimlib_assert(ctx->next_submit_msg);

	msg = ctx->next_submit_msg;
	msg->uncompressed_chunk_sizes[msg->num_filled_chunks] = usize;
	if (++msg->num_filled_chunks == msg->num_alloc_chunks)
		submit_compression_msg(ctx);
}

static bool
parallel_chunk_compressor_get_compression_result(struct chunk_compressor *_ctx,
						 const void **cdata_ret, u32 *csize_ret,
						 u32 *usize_ret)
{
	struct parallel_chunk_compressor *ctx = (struct parallel_chunk_compressor *)_ctx;
	struct message *msg;

	if (ctx->next_submit_msg)
		submit_compression_msg(ctx);

	if (ctx->next_ready_msg) {
		msg = ctx->next_ready_msg;
	} else {
		if (list_empty(&ctx->submitted_msgs))
			return false;

		while (!(msg = list_entry(ctx->submitted_msgs.next,
					  struct message,
					  submission_list))->complete)
			message_queue_get(&ctx->compressed_chunks_queue)->complete = true;

		ctx->next_ready_msg = msg;
		ctx->next_chunk_idx = 0;
	}

	if (msg->compressed_chunk_sizes[ctx->next_chunk_idx]) {
		*cdata_ret = msg->compressed_chunks[ctx->next_chunk_idx];
		*csize_ret = msg->compressed_chunk_sizes[ctx->next_chunk_idx];
	} else {
		*cdata_ret = msg->uncompressed_chunks[ctx->next_chunk_idx];
		*csize_ret = msg->uncompressed_chunk_sizes[ctx->next_chunk_idx];
	}
	*usize_ret = msg->uncompressed_chunk_sizes[ctx->next_chunk_idx];

	if (++ctx->next_chunk_idx == msg->num_filled_chunks) {
		list_del(&msg->submission_list);
		list_add_tail(&msg->list, &ctx->available_msgs);
		ctx->next_ready_msg = NULL;
	}
	return true;
}

int
new_parallel_chunk_compressor(int out_ctype, u32 out_chunk_size,
			      unsigned num_threads, u64 max_memory,
			      struct chunk_compressor **compressor_ret)
{
	u64 approx_mem_required;
	size_t chunks_per_msg;
	size_t msgs_per_thread;
	struct parallel_chunk_compressor *ctx;
	unsigned i;
	int ret;
	unsigned desired_num_threads;

	wimlib_assert(out_chunk_size > 0);

	if (num_threads == 0)
		num_threads = get_available_cpus();

	if (num_threads == 1)
		return -1;

	if (max_memory == 0)
		max_memory = get_available_memory();

	desired_num_threads = num_threads;

	if (out_chunk_size < ((u32)1 << 23)) {
		/* Relatively small chunks.  Use 2 messages per thread, each
		 * with at least 2 chunks.  Use more chunks per message if there
		 * are lots of threads and/or the chunks are very small.  */
		chunks_per_msg = 2;
		chunks_per_msg += num_threads * (65536 / out_chunk_size) / 16;
		chunks_per_msg = max(chunks_per_msg, 2);
		chunks_per_msg = min(chunks_per_msg, MAX_CHUNKS_PER_MSG);
		msgs_per_thread = 2;
	} else {
		/* Big chunks: Just have one buffer per thread --- more would
		 * just waste memory.  */
		chunks_per_msg = 1;
		msgs_per_thread = 1;
	}
	for (;;) {
		approx_mem_required =
			(u64)chunks_per_msg *
			(u64)msgs_per_thread *
			(u64)num_threads *
			(u64)out_chunk_size
			+ out_chunk_size
			+ 1000000
			+ num_threads * wimlib_get_compressor_needed_memory(out_ctype,
									    out_chunk_size,
									    0);
		if (approx_mem_required <= max_memory)
			break;

		if (chunks_per_msg > 1)
			chunks_per_msg--;
		else if (msgs_per_thread > 1)
			msgs_per_thread--;
		else if (num_threads > 1)
			num_threads--;
		else
			break;
	}

	if (num_threads < desired_num_threads) {
		WARNING("Wanted to use %u threads, but limiting to %u "
			"to fit in available memory!",
			desired_num_threads, num_threads);
	}

	if (num_threads == 1)
		return -2;

	ret = WIMLIB_ERR_NOMEM;
	ctx = CALLOC(1, sizeof(*ctx));
	if (ctx == NULL)
		goto err;

	ctx->base.out_ctype = out_ctype;
	ctx->base.out_chunk_size = out_chunk_size;
	ctx->base.destroy = parallel_chunk_compressor_destroy;
	ctx->base.get_chunk_buffer = parallel_chunk_compressor_get_chunk_buffer;
	ctx->base.signal_chunk_filled = parallel_chunk_compressor_signal_chunk_filled;
	ctx->base.get_compression_result = parallel_chunk_compressor_get_compression_result;

	ctx->num_thread_data = num_threads;

	ret = message_queue_init(&ctx->chunks_to_compress_queue);
	if (ret)
		goto err;

	ret = message_queue_init(&ctx->compressed_chunks_queue);
	if (ret)
		goto err;

	ret = WIMLIB_ERR_NOMEM;
	ctx->thread_data = CALLOC(num_threads, sizeof(ctx->thread_data[0]));
	if (ctx->thread_data == NULL)
		goto err;

	for (i = 0; i < num_threads; i++) {
		struct compressor_thread_data *dat;

		dat = &ctx->thread_data[i];

		dat->chunks_to_compress_queue = &ctx->chunks_to_compress_queue;
		dat->compressed_chunks_queue = &ctx->compressed_chunks_queue;
		ret = wimlib_create_compressor(out_ctype, out_chunk_size,
					       WIMLIB_COMPRESSOR_FLAG_DESTRUCTIVE,
					       &dat->compressor);
		if (ret)
			goto err;
	}

	for (ctx->num_started_threads = 0;
	     ctx->num_started_threads < num_threads;
	     ctx->num_started_threads++)
	{
		if (!thread_create(&ctx->thread_data[ctx->num_started_threads].thread,
				   compressor_thread_proc,
				   &ctx->thread_data[ctx->num_started_threads]))
		{
			ret = WIMLIB_ERR_NOMEM;
			if (ctx->num_started_threads >= 2)
				break;
			goto err;
		}
	}

	ctx->base.num_threads = ctx->num_started_threads;

	ret = WIMLIB_ERR_NOMEM;
	ctx->num_messages = ctx->num_started_threads * msgs_per_thread;
	ctx->msgs = allocate_messages(ctx->num_messages,
				      chunks_per_msg, out_chunk_size);
	if (ctx->msgs == NULL)
		goto err;

	INIT_LIST_HEAD(&ctx->available_msgs);
	for (size_t i = 0; i < ctx->num_messages; i++)
		list_add_tail(&ctx->msgs[i].list, &ctx->available_msgs);

	INIT_LIST_HEAD(&ctx->submitted_msgs);

	*compressor_ret = &ctx->base;
	return 0;

err:
	if (ctx)
		parallel_chunk_compressor_destroy(&ctx->base);
	return ret;
}
