/*
 * Copyright (c) 2008-2012 Niels Provos and Nick Mathewson
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
#ifndef _EVENT2_THREAD_H_
#define _EVENT2_THREAD_H_

/** @file event2/thread.h

  Functions for multi-threaded applications using Libevent.

  When using a multi-threaded application in which multiple threads
  add and delete events from a single event base, Libevent needs to
  lock its data structures.

  Like the memory-management function hooks, all of the threading functions   线程函数必须在创建event_base之前设置
  _must_ be set up before an event_base is created if you want the base to
  use them.

  Most programs will either be using Windows threads or Posix threads.  You
  can configure Libevent to use one of these event_use_windows_threads() or
  event_use_pthreads() respectively.  If you're using another threading　　　　　　　　　　　　　　　　　　　　　　　有单独为unix和windows线程函数，
  library, you'll need to configure threading functions manually using　　　　　　　　　　　　　　　　　　　　　　　　也可以自己指定
  evthread_set_lock_callbacks() and evthread_set_condition_callbacks().　　　　　　　　　　　　　
  
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <event2/event-config.h>//这个文件在哪？

/**
   @name Flags passed to lock functions　以下三个宏就是下面evthread_lock_callbacks结构提中的Lock函数的Mode参数

   @{
*/
/** A flag passed to a locking callback when the lock was allocated as a
 * read-write lock, and we want to acquire or release the lock for writing. */
#define EVTHREAD_WRITE	0x04　//为读写锁设置
/** A flag passed to a locking callback when the lock was allocated as a
 * read-write lock, and we want to acquire or release the lock for reading. */
#define EVTHREAD_READ	0x08　//为读写锁设置
/** A flag passed to a locking callback when we don't want to block waiting
 * for the lock; if we can't get the lock immediately, we will instead
 * return nonzero from the locking callback. */
#define EVTHREAD_TRY    0x10　//这个应该是互斥锁和读写锁通用的
/**@}*/

#if !defined(_EVENT_DISABLE_THREAD_SUPPORT) || defined(_EVENT_IN_DOXYGEN)

#define EVTHREAD_LOCK_API_VERSION 1 //当前线程锁的版本

/**
   @name Types of locks//以下２个宏是下面evthread_lock_callbacks结构中各个函数的locktype参数

   @{*/
/** A recursive lock is one that can be acquired multiple times at once by the
 * same thread.  No other process can allocate the lock until the thread that
 * has been holding it has unlocked it as many times as it locked it. */
#define EVTHREAD_LOCKTYPE_RECURSIVE 1　//互斥锁的递归属性
/* A read-write lock is one that allows multiple simultaneous readers, but
 * where any one writer excludes all other writers and readers. */
#define EVTHREAD_LOCKTYPE_READWRITE 2//这个是属性吗？读写锁
/**@}*/

/** This structure describes the interface a threading library uses for
 * locking.   It's used to tell evthread_set_lock_callbacks() how to use
 * locking on this platform.
 */
struct evthread_lock_callbacks {
	/** The current version of the locking API.  Set this to
	 * EVTHREAD_LOCK_API_VERSION */
	int lock_api_version;　//当前锁API的版本，就是前面定义的宏EVTHREAD_LOCK_API_VERSION
	/** Which kinds of locks does this version of the locking API
	 * support?  A bitfield of EVTHREAD_LOCKTYPE_RECURSIVE and
	 * EVTHREAD_LOCKTYPE_READWRITE.
	 *
	 * (Note that RECURSIVE locks are currently mandatory, and　
	 * READWRITE locks are not currently used.)　//可知互斥锁的递归属性在Libevent是强制的，而读写锁目前没有使用，所以直接把递归属性看作就是互斥锁
	 **/
	unsigned supported_locktypes;
	/** Function to allocate and initialize new lock of type 'locktype'.
	 * Returns NULL on failure. */
	void *(*alloc)(unsigned locktype);
	/** Funtion to release all storage held in 'lock', which was created
	 * with type 'locktype'. */
	void (*free)(void *lock, unsigned locktype);
	/** Acquire an already-allocated lock at 'lock' with mode 'mode'.
	 * Returns 0 on success, and nonzero on failure. */
	int (*lock)(unsigned mode, void *lock);
	/** Release a lock at 'lock' using mode 'mode'.  Returns 0 on success,
	 * and nonzero on failure. */
	int (*unlock)(unsigned mode, void *lock);
};

/** Sets a group of functions that Libevent should use for locking.
 * For full information on the required callback API, see the
 * documentation for the individual members of evthread_lock_callbacks.
 *
 * Note that if you're using Windows or the Pthreads threading library, you
 * probably shouldn't call this function; instead, use
 * evthread_use_windows_threads() or evthread_use_posix_threads() if you can.　
 */
int evthread_set_lock_callbacks(const struct evthread_lock_callbacks *);　//如果使用windows和unix的pthread线程库不应该调用此函数

#define EVTHREAD_CONDITION_API_VERSION 1　//但前线程条件变量的版本

struct timeval;//前向声明，本文件没有include任何头文件，所以无法使用timeval这个结构，这可能就是前向声明的作用吧

/** This structure describes the interface a threading library uses for
 * condition variables.  It's used to tell evthread_set_condition_callbacks
 * how to use locking on this platform.
 */　//本结构和前面的锁结构差不多
struct evthread_condition_callbacks {
	/** The current version of the conditions API.  Set this to
	 * EVTHREAD_CONDITION_API_VERSION */
	int condition_api_version;
	/** Function to allocate and initialize a new condition variable.
	 * Returns the condition variable on success, and NULL on failure.
	 * The 'condtype' argument will be 0 with this API version.
	 */
	void *(*alloc_condition)(unsigned condtype);
	/** Function to free a condition variable. */
	void (*free_condition)(void *cond);
	/** Function to signal a condition variable.  If 'broadcast' is 1, all
	 * threads waiting on 'cond' should be woken; otherwise, only on one
	 * thread is worken.  Should return 0 on success, -1 on failure.
	 * This function will only be called while holding the associated
	 * lock for the condition.
	 */
	int (*signal_condition)(void *cond, int broadcast);
	/** Function to wait for a condition variable.  The lock 'lock'
	 * will be held when this function is called; should be released
	 * while waiting for the condition to be come signalled, and
	 * should be held again when this function returns.
	 * If timeout is provided, it is interval of seconds to wait for
	 * the event to become signalled; if it is NULL, the function
	 * should wait indefinitely.
	 *
	 * The function should return -1 on error; 0 if the condition
	 * was signalled, or 1 on a timeout. */
	int (*wait_condition)(void *cond, void *lock,
	    const struct timeval *timeout);
};

/** Sets a group of functions that Libevent should use for condition variables.
 * For full information on the required callback API, see the
 * documentation for the individual members of evthread_condition_callbacks.
 *
 * Note that if you're using Windows or the Pthreads threading library, you
 * probably shouldn't call this function; instead, use
 * evthread_use_windows_threads() or evthread_use_pthreads() if you can.//和锁一样，如果使用windows和unix的库，不应该调用此函数
 */
int evthread_set_condition_callbacks(
	const struct evthread_condition_callbacks *);

/**
   Sets the function for determining the thread id.

   @param base the event base for which to set the id function
   @param id_fn the identify function Libevent should invoke to
     determine the identity of a thread.
*/
//设置使用什么函数来获得当前线程id
void evthread_set_id_callback(
    unsigned long (*id_fn)(void));

#if (defined(WIN32) && !defined(_EVENT_DISABLE_THREAD_SUPPORT)) || defined(_EVENT_IN_DOXYGEN)
/** Sets up Libevent for use with Windows builtin locking and thread ID
    functions.  Unavailable if Libevent is not built for Windows.

    @return 0 on success, -1 on failure. */
int evthread_use_windows_threads(void);//使用windows的线程库
/**
   Defined if Libevent was built with support for evthread_use_windows_threads()
*/
#define EVTHREAD_USE_WINDOWS_THREADS_IMPLEMENTED 1
#endif

#if defined(_EVENT_HAVE_PTHREADS) || defined(_EVENT_IN_DOXYGEN)
/** Sets up Libevent for use with Pthreads locking and thread ID functions.
    Unavailable if Libevent is not build for use with pthreads.  Requires
    libraries to link against Libevent_pthreads as well as Libevent.

    @return 0 on success, -1 on failure. */
int evthread_use_pthreads(void);　//使用unix的pthread线程库
/** Defined if Libevent was built with support for evthread_use_pthreads() */
#define EVTHREAD_USE_PTHREADS_IMPLEMENTED 1　//如果使用unix的pthread线程库，就定义这个宏，应该是用来判断是否使用Phread线程库

#endif

/** Enable debugging wrappers around the current lock callbacks.  If Libevent
 * makes one of several common locking errors, exit with an assertion failure.
 *
 * If you're going to call this function, you must do so before any locks are　　
 * allocated.
 **/
void evthread_enable_lock_debuging(void);　//没搞懂

#endif /* _EVENT_DISABLE_THREAD_SUPPORT */

struct event_base;　//前向声明
/** Make sure it's safe to tell an event base to wake up from another thread
    or a signal handler.

    @return 0 on success, -1 on failure.
 */
int evthread_make_base_notifiable(struct event_base *base);

#ifdef __cplusplus
}
#endif

#endif /* _EVENT2_THREAD_H_ */
