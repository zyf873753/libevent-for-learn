/*
 * Copyright (c) 2008-2012 Niels Provos, Nick Mathewson
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

#include "event2/event-config.h"

#ifndef _EVENT_DISABLE_THREAD_SUPPORT

#include "event2/thread.h"

#include <stdlib.h>
#include <string.h>

#include "log-internal.h"
#include "mm-internal.h"
#include "util-internal.h"
#include "evthread-internal.h"

#ifdef EVTHREAD_EXPOSE_STRUCTS　//如果定义了暴露结构的宏，则GloBAL不定义为任何关键字，而全局变量不指明static，就是extern，所以为暴露出去的
#define GLOBAL
#else
#define GLOBAL static
#endif

/* globals */
GLOBAL int _evthread_lock_debugging_enabled = 0;
GLOBAL struct evthread_lock_callbacks _evthread_lock_fns = {
	0, 0, NULL, NULL, NULL, NULL
};
GLOBAL unsigned long (*_evthread_id_fn)(void) = NULL;
GLOBAL struct evthread_condition_callbacks _evthread_cond_fns = {
	0, NULL, NULL, NULL, NULL
};

//以下两个函数和上面的全局函数是一样的，除了返回值（如果定义了EVTHREAD_EXPOSE_STRUCTS，就不一样）
/* Used for debugging */
static struct evthread_lock_callbacks _original_lock_fns = {
	0, 0, NULL, NULL, NULL, NULL
};
static struct evthread_condition_callbacks _original_cond_fns = {
	0, NULL, NULL, NULL, NULL
};

//设置获取当前线程id的函数
void
evthread_set_id_callback(unsigned long (*id_fn)(void))
{
	_evthread_id_fn = id_fn;
}

//设置互斥锁的所有相关函数
int
evthread_set_lock_callbacks(const struct evthread_lock_callbacks *cbs)
{
	struct evthread_lock_callbacks *target =
	    _evthread_lock_debugging_enabled
	    ? &_original_lock_fns : &_evthread_lock_fns;

	if (!cbs) {
		if (target->alloc)
			event_warnx("Trying to disable lock functions after "
			    "they have been set up will probaby not work.");
		memset(target, 0, sizeof(_evthread_lock_fns));
		return 0;
	}
	if (target->alloc) {　//一旦设置了，就不能在更改
		/* Uh oh; we already had locking callbacks set up.*/
		if (target->lock_api_version == cbs->lock_api_version &&
			target->supported_locktypes == cbs->supported_locktypes &&
			target->alloc == cbs->alloc &&
			target->free == cbs->free &&
			target->lock == cbs->lock &&
			target->unlock == cbs->unlock) {
			/* no change -- allow this. */
			return 0;
		}
		event_warnx("Can't change lock callbacks once they have been "
		    "initialized.");
		return -1;
	}
	if (cbs->alloc && cbs->free && cbs->lock && cbs->unlock) {
		memcpy(target, cbs, sizeof(_evthread_lock_fns));　//target是局部变量，把cbs的内容复制过来有什么用？
		return event_global_setup_locks_(1);//不懂？
	} else {
		return -1;
	}
}

//设置条件变量的所有相关函数
int
evthread_set_condition_callbacks(const struct evthread_condition_callbacks *cbs)
{
	struct evthread_condition_callbacks *target =
	    _evthread_lock_debugging_enabled
	    ? &_original_cond_fns : &_evthread_cond_fns;

	if (!cbs) {
		if (target->alloc_condition)
			event_warnx("Trying to disable condition functions "
			    "after they have been set up will probaby not "
			    "work.");
		memset(target, 0, sizeof(_evthread_cond_fns));
		return 0;
	}
	if (target->alloc_condition) {
		/* Uh oh; we already had condition callbacks set up.*/
		if (target->condition_api_version == cbs->condition_api_version &&
			target->alloc_condition == cbs->alloc_condition &&
			target->free_condition == cbs->free_condition &&
			target->signal_condition == cbs->signal_condition &&
			target->wait_condition == cbs->wait_condition) {
			/* no change -- allow this. */
			return 0;
		}
		event_warnx("Can't change condition callbacks once they "
		    "have been initialized.");
		return -1;
	}
	if (cbs->alloc_condition && cbs->free_condition &&
	    cbs->signal_condition && cbs->wait_condition) {
		memcpy(target, cbs, sizeof(_evthread_cond_fns));//?
	}
	if (_evthread_lock_debugging_enabled) {
		_evthread_cond_fns.alloc_condition = cbs->alloc_condition;
		_evthread_cond_fns.free_condition = cbs->free_condition;
		_evthread_cond_fns.signal_condition = cbs->signal_condition;
	}
	return 0;
}


/*以下多个以debug开头的结构和函数都是用以调试*/

//此结构用于调试
struct debug_lock {
	unsigned locktype;
	unsigned long held_by;//此锁目前被哪个线程占用，只能记录一个线程id，那对于读写锁怎么办？
	/* XXXX if we ever use read-write locks, we will need a separate
	 * lock to protect count. */
	int count;//用于统计递归深度，还是读写锁同时读的次数？是该所的拥有者的数量，应该用于读写锁，注意只是拥有者，不一定加锁
	void *lock;
};

//创建锁
static void * //之所以返回void，是因为可能是互斥锁，也可能是读写锁，虽然目前不支持读写锁
debug_lock_alloc(unsigned locktype)
{
	struct debug_lock *result = mm_malloc(sizeof(struct debug_lock));
	if (!result)
		return NULL;
	if (_original_lock_fns.alloc) {
		if (!(result->lock = _original_lock_fns.alloc(
				locktype|EVTHREAD_LOCKTYPE_RECURSIVE))) {
			mm_free(result);
			return NULL;
		}
	} else {
		result->lock = NULL;
	}
	result->locktype = locktype;
	result->count = 0;
	result->held_by = 0;
	return result;
}

//释放锁
static void
debug_lock_free(void *lock_, unsigned locktype)
{
	struct debug_lock *lock = lock_;
	//这两个断言一旦不成立就会终止程序(abort，见断言宏的实现)，因为这只为调试用，所以可以终止程序
	EVUTIL_ASSERT(lock->count == 0);
	EVUTIL_ASSERT(locktype == lock->locktype);
	if (_original_lock_fns.free) {
		_original_lock_fns.free(lock->lock,
		    lock->locktype|EVTHREAD_LOCKTYPE_RECURSIVE);
	}
	lock->lock = NULL;
	lock->count = -100;
	mm_free(lock);
}

//将锁的拥有者设为自己，注意并没有加锁（看下一个函数，该函数是加锁成功后调用的）
static void
evthread_debug_lock_mark_locked(unsigned mode, struct debug_lock *lock)
{
	++lock->count;
	if (!(lock->locktype & EVTHREAD_LOCKTYPE_RECURSIVE))//如果创建锁时没有指明使用递归锁，实现也会把它设为递归，但是lock->locktype却没有记录为递归，见上面的alloc函数，？
		EVUTIL_ASSERT(lock->count == 1);　//如果没有使用递归锁，那么此时count必须是１
	if (_evthread_id_fn) {
		unsigned long me;
		me = _evthread_id_fn();
		if (lock->count > 1)
			EVUTIL_ASSERT(lock->held_by == me);
		lock->held_by = me; 
	}
}

//加锁
static int
debug_lock_lock(unsigned mode, void *lock_)
{
	struct debug_lock *lock = lock_;
	int res = 0;
	if (lock->locktype & EVTHREAD_LOCKTYPE_READWRITE)
		EVUTIL_ASSERT(mode & (EVTHREAD_READ|EVTHREAD_WRITE));
	else
		EVUTIL_ASSERT((mode & (EVTHREAD_READ|EVTHREAD_WRITE)) == 0);//如果不是读写锁，就不应该设置EVTHREAD_READ和EVTHREAD_WRITE两个模式
	if (_original_lock_fns.lock)
		res = _original_lock_fns.lock(mode, lock->lock);
	if (!res) {
		evthread_debug_lock_mark_locked(mode, lock);
	}
	return res;
}

//
static void
evthread_debug_lock_mark_unlocked(unsigned mode, struct debug_lock *lock)
{
	if (lock->locktype & EVTHREAD_LOCKTYPE_READWRITE)
		EVUTIL_ASSERT(mode & (EVTHREAD_READ|EVTHREAD_WRITE));
	else
		EVUTIL_ASSERT((mode & (EVTHREAD_READ|EVTHREAD_WRITE)) == 0);
	if (_evthread_id_fn) {
		EVUTIL_ASSERT(lock->held_by == _evthread_id_fn());//首先必须是拥有该所的线程，然后才可以解锁
		if (lock->count == 1)
			lock->held_by = 0;
	}
	--lock->count;
	EVUTIL_ASSERT(lock->count >= 0);
}

//解锁
static int
debug_lock_unlock(unsigned mode, void *lock_)
{
	struct debug_lock *lock = lock_;
	int res = 0;
	evthread_debug_lock_mark_unlocked(mode, lock);
	if (_original_lock_fns.unlock)
		res = _original_lock_fns.unlock(mode, lock->lock);
	return res;
}

//条件变量等待
static int
debug_cond_wait(void *_cond, void *_lock, const struct timeval *tv)
{
	int r;
	struct debug_lock *lock = _lock;
	EVUTIL_ASSERT(lock);
	EVLOCK_ASSERT_LOCKED(_lock);//_lock必须处于加锁状态
	evthread_debug_lock_mark_unlocked(0, lock);//由于wait会解锁，所以之前要解除原来的加锁标记
	r = _original_cond_fns.wait_condition(_cond, lock->lock, tv);
	evthread_debug_lock_mark_locked(0, lock);//再次设置加锁标记
	return r;
}

//开启debug模式，上面的debug函数都在此处用到
void
evthread_enable_lock_debuging(void)
{
	//注意此处初始化结构中的函数指针，所用的函数都是debug的
	struct evthread_lock_callbacks cbs = {
		EVTHREAD_LOCK_API_VERSION,
		EVTHREAD_LOCKTYPE_RECURSIVE,
		debug_lock_alloc,
		debug_lock_free,
		debug_lock_lock,
		debug_lock_unlock
	};
	if (_evthread_lock_debugging_enabled)　//定义在本文件开始处，在此函数中设置，如果等于１，说明已经开启debug模式，所以直接返回
		return;
	//不是debug模式吗？以下３句没搞懂
	memcpy(&_original_lock_fns, &_evthread_lock_fns,
	    sizeof(struct evthread_lock_callbacks));
	memcpy(&_evthread_lock_fns, &cbs,
	    sizeof(struct evthread_lock_callbacks));

	memcpy(&_original_cond_fns, &_evthread_cond_fns,
	    sizeof(struct evthread_condition_callbacks));
	_evthread_cond_fns.wait_condition = debug_cond_wait;
	_evthread_lock_debugging_enabled = 1;

	/* XXX return value should get checked. */
	event_global_setup_locks_(0);//？
}

//判断当前线程是否已经hold了指定的锁
int
_evthread_is_debug_lock_held(void *lock_)
{
	struct debug_lock *lock = lock_;
	if (! lock->count)
		return 0;
	if (_evthread_id_fn) {
		unsigned long me = _evthread_id_fn();
		if (lock->held_by != me)
			return 0;
	}
	return 1;
}

//获得debug_lock结构中存放的真正锁
void *
_evthread_debug_get_real_lock(void *lock_)
{
	struct debug_lock *lock = lock_;
	return lock->lock;
}

//在四种不同情况下，创建锁
void *
evthread_setup_global_lock_(void *lock_, unsigned locktype, int enable_locks)
{
	/* there are four cases here:
	   1) we're turning on debugging; locking is not on.
	   2) we're turning on debugging; locking is on.
	   3) we're turning on locking; debugging is not on.
	   4) we're turning on locking; debugging is on. */

	if (!enable_locks && _original_lock_fns.alloc == NULL) {
		/* Case 1: allocate a debug lock. */
		EVUTIL_ASSERT(lock_ == NULL);
		return debug_lock_alloc(locktype);
	} else if (!enable_locks && _original_lock_fns.alloc != NULL) {
		/* Case 2: wrap the lock in a debug lock. */
		struct debug_lock *lock;
		EVUTIL_ASSERT(lock_ != NULL);

		if (!(locktype & EVTHREAD_LOCKTYPE_RECURSIVE)) {
			/* We can't wrap it: We need a recursive lock */
			_original_lock_fns.free(lock_, locktype);
			return debug_lock_alloc(locktype);
		}
		lock = mm_malloc(sizeof(struct debug_lock));
		if (!lock) {
			_original_lock_fns.free(lock_, locktype);
			return NULL;
		}
		lock->lock = lock_;
		lock->locktype = locktype;
		lock->count = 0;
		lock->held_by = 0;
		return lock;
	} else if (enable_locks && ! _evthread_lock_debugging_enabled) {
		/* Case 3: allocate a regular lock */
		EVUTIL_ASSERT(lock_ == NULL);
		return _evthread_lock_fns.alloc(locktype);
	} else {
		/* Case 4: Fill in a debug lock with a real lock */
		struct debug_lock *lock = lock_;
		EVUTIL_ASSERT(enable_locks &&
		              _evthread_lock_debugging_enabled);
		EVUTIL_ASSERT(lock->locktype == locktype);
		EVUTIL_ASSERT(lock->lock == NULL);
		lock->lock = _original_lock_fns.alloc(
			locktype|EVTHREAD_LOCKTYPE_RECURSIVE);
		if (!lock->lock) {
			lock->count = -200;
			mm_free(lock);
			return NULL;
		}
		return lock;
	}
}

//以下就是一些对锁和条件变量的操作，和上面一样，不过上面是要维护一个结构，所以会多一些
#ifndef EVTHREAD_EXPOSE_STRUCTS
//获得当前线程id
unsigned long
_evthreadimpl_get_id()
{
	return _evthread_id_fn ? _evthread_id_fn() : 1;//evthread_id_fn函数设置了，就意味着支持多线程
}
void *
_evthreadimpl_lock_alloc(unsigned locktype)
{
	return _evthread_lock_fns.alloc ?
	    _evthread_lock_fns.alloc(locktype) : NULL;
}
void
_evthreadimpl_lock_free(void *lock, unsigned locktype)
{
	if (_evthread_lock_fns.free)
		_evthread_lock_fns.free(lock, locktype);
}
int
_evthreadimpl_lock_lock(unsigned mode, void *lock)
{
	if (_evthread_lock_fns.lock)
		return _evthread_lock_fns.lock(mode, lock);
	else
		return 0;
}
int
_evthreadimpl_lock_unlock(unsigned mode, void *lock)
{
	if (_evthread_lock_fns.unlock)
		return _evthread_lock_fns.unlock(mode, lock);
	else
		return 0;
}
void *
_evthreadimpl_cond_alloc(unsigned condtype)
{
	return _evthread_cond_fns.alloc_condition ?
	    _evthread_cond_fns.alloc_condition(condtype) : NULL;
}
void
_evthreadimpl_cond_free(void *cond)
{
	if (_evthread_cond_fns.free_condition)
		_evthread_cond_fns.free_condition(cond);
}
int
_evthreadimpl_cond_signal(void *cond, int broadcast)
{
	if (_evthread_cond_fns.signal_condition)
		return _evthread_cond_fns.signal_condition(cond, broadcast);
	else
		return 0;
}
int
_evthreadimpl_cond_wait(void *cond, void *lock, const struct timeval *tv)
{
	if (_evthread_cond_fns.wait_condition)
		return _evthread_cond_fns.wait_condition(cond, lock, tv);
	else
		return 0;
}
int
_evthreadimpl_is_lock_debugging_enabled(void)
{
	return _evthread_lock_debugging_enabled;
}

int
_evthreadimpl_locking_enabled(void)
{
	return _evthread_lock_fns.lock != NULL;
}
#endif

#endif
