/*
 * Copyright (c) 2000-2007 Niels Provos <provos@citi.umich.edu>
 * Copyright (c) 2007-2012 Niels Provos and Nick Mathewson
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef _EVENT2_EVENT_STRUCT_H_
#define _EVENT2_EVENT_STRUCT_H_

/** @file event2/event_struct.h

  Structures used by event.h.  Using these structures directly WILL harm
  forward compatibility: be careful.

  No field declared in this file should be used directly in user code.  Except
  for historical reasons, these fields would not be exposed at all.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <event2/event-config.h>
#ifdef _EVENT_HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef _EVENT_HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

/* For int types. */
#include <event2/util.h>

/* For evkeyvalq */
#include <event2/keyvalq_struct.h>

#define EVLIST_TIMEOUT	0x01
#define EVLIST_INSERTED	0x02
#define EVLIST_SIGNAL	0x04
#define EVLIST_ACTIVE	0x08
#define EVLIST_INTERNAL	0x10
#define EVLIST_INIT	0x80

/* EVLIST_X_ Private space: 0x1000-0xf000 */
#define EVLIST_ALL	(0xf000 | 0x9f)

/* Fix so that people don't have to run with <sys/queue.h> */
#ifndef TAILQ_ENTRY
#define _EVENT_DEFINED_TQENTRY
#define TAILQ_ENTRY(type)						\
struct {								\
	struct type *tqe_next;	/* next element */			\
	struct type **tqe_prev;	/* address of previous next element */	\
}
#endif /* !TAILQ_ENTRY */

#ifndef TAILQ_HEAD //not used, why?
#define _EVENT_DEFINED_TQHEAD
#define TAILQ_HEAD(name, type)			\
struct name {					\
	struct type *tqh_first;			\
	struct type **tqh_last;			\
}
#endif

struct event_base; //forward declaration, because this file doesn't include Event-internal.h
struct event {
	TAILQ_ENTRY(event) ev_active_next; //double-linked queue for events which has been active
	TAILQ_ENTRY(event) ev_next; //the position in double-linked queue for I/O events?

	/* for managing timeouts */
	union {
		TAILQ_ENTRY(event) ev_next_with_common_timeout; //the position in double-linked queue for timeout events
		int min_heap_idx; //index in binary heap of timeout event
	} ev_timeout_pos; //programmer can choose use double-linked or binary heap to index timeout events

	evutil_socket_t ev_fd; // descriptor

	struct event_base *ev_base; //the event base which current event associate with

	union {
		/* used for io events */
		struct {
			TAILQ_ENTRY(event) ev_io_next; //queue for I/O events
			struct timeval ev_timeout;
		} ev_io;

		/* used by signal events */
		struct {
			TAILQ_ENTRY(event) ev_signal_next; //queue for signal events
			short ev_ncalls; //the times reactor invoke callback function
			/* Allows deletes in callback */
			short *ev_pncalls;
		} ev_signal;
	} _ev; //I/O and signal are alternative

	short ev_events; //the type of event, such as EV_TIMEOUT, EV_WIRTE etc,这是一个"|"的组合
	short ev_res;		/* result passed to event callback */　//current active event type,是组合吗？
	short ev_flags; //don't kown
	ev_uint8_t ev_pri;	/* smaller numbers are higher priority */ //the priority of current event
	ev_uint8_t ev_closure; //EVLIST_*
	struct timeval ev_timeout; //the timeout of current event

	/* allows us to adopt for different types of events */
	void (*ev_callback)(evutil_socket_t, short, void *arg); //callback function
	void *ev_arg; //parameter passed to callback fuction
};

TAILQ_HEAD (event_list, event);//this isn't declare, but I still don't understand，搞懂了，就是声明一个叫做event_list的结构

#ifdef _EVENT_DEFINED_TQENTRY
#undef TAILQ_ENTRY
#endif

#ifdef _EVENT_DEFINED_TQHEAD
#undef TAILQ_HEAD
#endif

#ifdef __cplusplus
}
#endif

#endif /* _EVENT2_EVENT_STRUCT_H_ */file
