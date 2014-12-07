/*	$OpenBSD: select.c,v 1.2 2002/06/25 15:50:15 mickey Exp $	*/

/*
 * Copyright 2000-2007 Niels Provos <provos@citi.umich.edu>
 * Copyright 2007-2012 Niels Provos and Nick Mathewson
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

#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#undef WIN32_LEAN_AND_MEAN
#endif
#include <sys/types.h>
#ifdef _EVENT_HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#include <sys/queue.h>
#ifdef _EVENT_HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _EVENT_HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <errno.h>
#ifdef _EVENT_HAVE_FCNTL_H
#include <fcntl.h>
#endif

#include "event2/event.h"//event.c也include了signal.h文件，一般头文件都不include自己写的文件，而由c文件来包括，signal.c和event.c可以互相对方的头文件
#include "event2/event_struct.h"
#include "event-internal.h"
#include "event2/util.h"
#include "evsignal-internal.h"
#include "log-internal.h"
#include "evmap-internal.h"
#include "evthread-internal.h"

/*
  signal.c

  This is the signal-handling implementation we use for backends that don't
  have a better way to do signal handling.  It uses sigaction() or signal()
  to set a signal handler, and a socket pair to tell the event base when

  Note that I said "the event base" : only one event base can be set up to use
  this at a time.  For historical reasons and backward compatibility, if you
  add an event for a signal to event_base A, then add an event for a signal
  (any signal!) to event_base B, event_base B will get informed about the
  signal, but event_base A won't.                                                  //目前的缺陷

  It would be neat to change this behavior in some future version of Libevent.
  kqueue already does something far more sensible.  We can make all backends
  on Linux do a reasonable thing using signalfd.                                      //使用linux的signalfd
*/

#ifndef WIN32
/* Windows wants us to call our signal handlers as __cdecl.  Nobody else
 * expects you to do anything crazy like this. */
#define __cdecl
#endif

static int evsig_add(struct event_base *, evutil_socket_t, short, short, void *);
static int evsig_del(struct event_base *, evutil_socket_t, short, short, void *);

static const struct eventop evsigops = { //signal的后端只要这几项？
	"signal",
	NULL,
	evsig_add,
	evsig_del,
	NULL,
	NULL,
	0, 0, 0
};

#ifndef _EVENT_DISABLE_THREAD_SUPPORT
/* Lock for evsig_base and evsig_base_n_signals_added fields. */
static void *evsig_base_lock = NULL;//如果不支持多线程，则不用定义锁
#endif
/* The event base that's currently getting informed about signals. */ 
static struct event_base *evsig_base = NULL;　　　　　　　//针对信号有一个单独的全局event_base
/* A copy of evsig_base->sigev_n_signals_added. */
static int evsig_base_n_signals_added = 0;
static evutil_socket_t evsig_base_fd = -1;

static void __cdecl evsig_handler(int sig);//__cdecl？

#define EVSIGBASE_LOCK() EVLOCK_LOCK(evsig_base_lock, 0)
#define EVSIGBASE_UNLOCK() EVLOCK_UNLOCK(evsig_base_lock, 0)

void
evsig_set_base(struct event_base *base)
{
	EVSIGBASE_LOCK();
	evsig_base = base;
	evsig_base_n_signals_added = base->sig.ev_n_signals_added;
	evsig_base_fd = base->sig.ev_signal_pair[0];//0用来信号处理函数发送给base，1已用base接收信号
	EVSIGBASE_UNLOCK();
}

//接收信号处理函数发过来的信号，激活base
/* Callback for when the signal handler write a byte to our signaling socket */
static void
evsig_cb(evutil_socket_t fd, short what, void *arg)
{
	static char signals[1024];//用来存放信号处理函数传过来的的信号？
	ev_ssize_t n;
	int i;
	int ncaught[NSIG];//NSIG在哪定义？
	struct event_base *base;

	base = arg;

	memset(&ncaught, 0, sizeof(ncaught));

	while (1) {
		n = recv(fd, signals, sizeof(signals), 0);
		if (n == -1) {
			int err = evutil_socket_geterror(fd);//windows平台这是函数，而linux平台这是宏函数
			if (! EVUTIL_ERR_RW_RETRIABLE(err))　//判断是否可重读写，即检查EINTR和EAGAIN
				event_sock_err(1, fd, "%s: recv", __func__);
			break;
		} else if (n == 0) {
			/* XXX warn? */
			break;
		}
		for (i = 0; i < n; ++i) {
			ev_uint8_t sig = signals[i];
			if (sig < NSIG)
				ncaught[sig]++;//记录传进来的信号
		}
	}

	EVBASE_ACQUIRE_LOCK(base, th_base_lock);//base虽然是在本函数中定义的，貌似在栈上，但是他指向的地址是从外部传过来的，所以需要加锁
	for (i = 0; i < NSIG; ++i) {
		if (ncaught[i])
			evmap_signal_active(base, i, ncaught[i]);//激活base中等待的信号
	}
	EVBASE_RELEASE_LOCK(base, th_base_lock);
}

//初始化base->sig成员
int
evsig_init(struct event_base *base)
{
	/*
	 * Our signal handler is going to write to one end of the socket
	 * pair to wake up our event loop.  The event loop then scans for
	 * signals that got delivered.　//这感觉就是上一个函数的功能
	 */
	if (evutil_socketpair(
		    AF_UNIX, SOCK_STREAM, 0, base->sig.ev_signal_pair) == -1) {
#ifdef WIN32
		/* Make this nonfatal on win32, where sometimes people
		   have localhost firewalled. */
		event_sock_warn(-1, "%s: socketpair", __func__);
#else
		event_sock_err(1, -1, "%s: socketpair", __func__);
#endif
		return -1;
	}

	evutil_make_socket_closeonexec(base->sig.ev_signal_pair[0]);//两个都设置执行时关闭
	evutil_make_socket_closeonexec(base->sig.ev_signal_pair[1]);
	base->sig.sh_old = NULL; //？
	base->sig.sh_old_max = 0;

	evutil_make_socket_nonblocking(base->sig.ev_signal_pair[0]);//设置为非阻塞
	evutil_make_socket_nonblocking(base->sig.ev_signal_pair[1]);

	event_assign(&base->sig.ev_signal, base, base->sig.ev_signal_pair[1],//注册了上一个函数evsig_cb为回调函数
		EV_READ | EV_PERSIST, evsig_cb, base);

	base->sig.ev_signal.ev_flags |= EVLIST_INTERNAL;//sig为evsig_info类型，ev_signal为event类型。设置为内部，一直没搞懂是啥
	event_priority_set(&base->sig.ev_signal, 0);//初始优先级为0

	base->evsigsel = &evsigops;//设置信号处理的后端

	return 0;
}

/* Helper: set the signal handler for evsignal to handler in base, so that
 * we can restore the original handler when we clear the current one. */
int
_evsig_set_handler(struct event_base *base,
    int evsignal, void (__cdecl *handler)(int))
{
#ifdef _EVENT_HAVE_SIGACTION
	struct sigaction sa;
#else
	ev_sighandler_t sh;
#endif
	struct evsig_info *sig = &base->sig;
	void *p;

	//重置sig->sh_old的空间
	/*
	 * resize saved signal handler array up to the highest signal number.
	 * a dynamic array is used to keep footprint on the low side.
	 */
	if (evsignal >= sig->sh_old_max) {　
		int new_max = evsignal + 1;//信号是从零开始的？
		event_debug(("%s: evsignal (%d) >= sh_old_max (%d), resizing",
			    __func__, evsignal, sig->sh_old_max));
		p = mm_realloc(sig->sh_old, new_max * sizeof(*sig->sh_old));
		if (p == NULL) {
			event_warn("realloc");
			return (-1);
		}

		memset((char *)p + sig->sh_old_max * sizeof(*sig->sh_old),//只把新加的空间清零
		    0, (new_max - sig->sh_old_max) * sizeof(*sig->sh_old));

		sig->sh_old_max = new_max;
		sig->sh_old = p;
	}

	
	//为什么有分配一次？上面的realloc难道没有分配？
	/* allocate space for previous handler out of dynamic array */
	sig->sh_old[evsignal] = mm_malloc(sizeof *sig->sh_old[evsignal]);
	if (sig->sh_old[evsignal] == NULL) {
		event_warn("malloc");
		return (-1);
	}

	/* save previous handler and setup new handler */
#ifdef _EVENT_HAVE_SIGACTION
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = handler;
	sa.sa_flags |= SA_RESTART;
	sigfillset(&sa.sa_mask);//当执行信号处理函数时，屏蔽所有信号

	if (sigaction(evsignal, &sa, sig->sh_old[evsignal]) == -1) {//把信号evsignal旧的信号处理函数存放在sig->sh_old[evsignal]上
		event_warn("sigaction");
		mm_free(sig->sh_old[evsignal]);
		sig->sh_old[evsignal] = NULL;
		return (-1);
	}
#else
	if ((sh = signal(evsignal, handler)) == SIG_ERR) {
		event_warn("signal");
		mm_free(sig->sh_old[evsignal]);
		sig->sh_old[evsignal] = NULL;
		return (-1);
	}
	*sig->sh_old[evsignal] = sh;
#endif

	return (0);
}

//给base添加监听的信号
static int
evsig_add(struct event_base *base, evutil_socket_t evsignal, short old, short events, void *p)
{
	struct evsig_info *sig = &base->sig;
	(void)p;//？

	EVUTIL_ASSERT(evsignal >= 0 && evsignal < NSIG);

	/* catch signals if they happen quickly */
	EVSIGBASE_LOCK();
	if (evsig_base != base && evsig_base_n_signals_added) {
		event_warnx("Added a signal to event base %p with signals "
		    "already added to event_base %p.  Only one can have "
		    "signals at a time with the %s backend.  The base with "
		    "the most recently added signal or the most recent "
		    "event_base_loop() call gets preference; do "
		    "not rely on this behavior in future Libevent versions.",
		    base, evsig_base, base->evsel->name);
	}
	evsig_base = base;//正如上面的警告，每次只能有一个signal的base
	evsig_base_n_signals_added = ++sig->ev_n_signals_added;//先加后赋值
	evsig_base_fd = base->sig.ev_signal_pair[0];
	EVSIGBASE_UNLOCK();

	event_debug(("%s: %d: changing signal handler", __func__, (int)evsignal));
	if (_evsig_set_handler(base, (int)evsignal, evsig_handler) == -1) {
		goto err;
	}


	if (!sig->ev_signal_added) {//如果siginfo结构中未标识已添加信号事件，那就标识
		if (event_add(&sig->ev_signal, NULL))
			goto err;
		sig->ev_signal_added = 1;
	}

	return (0);

err:
	EVSIGBASE_LOCK();
	--evsig_base_n_signals_added;
	--sig->ev_n_signals_added;
	EVSIGBASE_UNLOCK();
	return (-1);
}

//恢复指定信号的信号处理函数，将sig->sh_old[evsignal]设为NULL
int
_evsig_restore_handler(struct event_base *base, int evsignal)
{
	int ret = 0;
	struct evsig_info *sig = &base->sig;
#ifdef _EVENT_HAVE_SIGACTION
	struct sigaction *sh;
#else
	ev_sighandler_t *sh;
#endif

	/* restore previous handler */
	sh = sig->sh_old[evsignal];
	sig->sh_old[evsignal] = NULL;//将旧的sigaction只为NULL，有sh释放
#ifdef _EVENT_HAVE_SIGACTION
	if (sigaction(evsignal, sh, NULL) == -1) {//不记录新的信号处理函数
		event_warn("sigaction");
		ret = -1;
	}
#else
	if (signal(evsignal, *sh) == SIG_ERR) {
		event_warn("signal");
		ret = -1;
	}
#endif

	mm_free(sh);

	return ret;
}

//删除base中指定的信号，并且恢复之前的信号处理函数
static int
evsig_del(struct event_base *base, evutil_socket_t evsignal, short old, short events, void *p)
{
	EVUTIL_ASSERT(evsignal >= 0 && evsignal < NSIG);

	event_debug(("%s: "EV_SOCK_FMT": restoring signal handler",
		__func__, EV_SOCK_ARG(evsignal)));

	EVSIGBASE_LOCK();
	--evsig_base_n_signals_added;
	--base->sig.ev_n_signals_added;
	EVSIGBASE_UNLOCK();

	return (_evsig_restore_handler(base, (int)evsignal));
}

//信号处理函数
static void __cdecl
evsig_handler(int sig)
{
	int save_errno = errno;
#ifdef WIN32
	int socket_errno = EVUTIL_SOCKET_ERROR();
#endif
	ev_uint8_t msg;

	if (evsig_base == NULL) {
		event_warnx(
			"%s: received signal %d, but have no base configured",
			__func__, sig);
		return;
	}

#ifndef _EVENT_HAVE_SIGACTION
	signal(sig, evsig_handler);
#endif

	/* Wake up our notification mechanism */
	msg = sig;
	send(evsig_base_fd, (char*)&msg, 1, 0);//将激活的信号发送给base
	errno = save_errno;
#ifdef WIN32
	EVUTIL_SET_SOCKET_ERROR(socket_errno);
#endif
}

void
evsig_dealloc(struct event_base *base)
{
	int i = 0;
	if (base->sig.ev_signal_added) {
		event_del(&base->sig.ev_signal);//删除信号事件
		base->sig.ev_signal_added = 0;
	}
	/* debug event is created in evsig_init/event_assign even when
	 * ev_signal_added == 0, so unassign is required */
	event_debug_unassign(&base->sig.ev_signal);

	//恢复所有的信号处理函数
	for (i = 0; i < NSIG; ++i) {
		if (i < base->sig.sh_old_max && base->sig.sh_old[i] != NULL)
			_evsig_restore_handler(base, i);
	}
	EVSIGBASE_LOCK();
	if (base == evsig_base) {
		evsig_base = NULL;
		evsig_base_n_signals_added = 0;
		evsig_base_fd = -1;
	}
	EVSIGBASE_UNLOCK();

	//关闭socketpair描述府
	if (base->sig.ev_signal_pair[0] != -1) {
		evutil_closesocket(base->sig.ev_signal_pair[0]);
		base->sig.ev_signal_pair[0] = -1;
	}
	if (base->sig.ev_signal_pair[1] != -1) {
		evutil_closesocket(base->sig.ev_signal_pair[1]);
		base->sig.ev_signal_pair[1] = -1;
	}
	base->sig.sh_old_max = 0;

	/* per index frees are handled in evsig_del() */
	if (base->sig.sh_old) {
		mm_free(base->sig.sh_old);
		base->sig.sh_old = NULL;
	}
}

//?
#ifndef _EVENT_DISABLE_THREAD_SUPPORT
int
evsig_global_setup_locks_(const int enable_locks)
{
	EVTHREAD_SETUP_GLOBAL_LOCK(evsig_base_lock, 0);
	return 0;
}
#endif
