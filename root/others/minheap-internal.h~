/*
 * Copyright (c) 2007-2012 Niels Provos and Nick Mathewson
 *
 * Copyright (c) 2006 Maxim Yegorushkin <maxim.yegorushkin@gmail.com>
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
#ifndef _MIN_HEAP_H_
#define _MIN_HEAP_H_

#include "event2/event-config.h"
#include "event2/event.h"
#include "event2/event_struct.h"
#include "event2/util.h"
#include "util-internal.h"
#include "mm-internal.h"

typedef struct min_heap
{
	struct event** p;
	unsigned n, a;//n是当前堆中已用的空间大小，a是当前堆中总大小
} min_heap_t;

static inline void	     min_heap_ctor(min_heap_t* s);
static inline void	     min_heap_dtor(min_heap_t* s);
static inline void	     min_heap_elem_init(struct event* e);
static inline int	     min_heap_elt_is_top(const struct event *e);
static inline int	     min_heap_elem_greater(struct event *a, struct event *b);
static inline int	     min_heap_empty(min_heap_t* s);
static inline unsigned	     min_heap_size(min_heap_t* s);
static inline struct event*  min_heap_top(min_heap_t* s);
static inline int	     min_heap_reserve(min_heap_t* s, unsigned n);
static inline int	     min_heap_push(min_heap_t* s, struct event* e);
static inline struct event*  min_heap_pop(min_heap_t* s);
static inline int	     min_heap_erase(min_heap_t* s, struct event* e);
static inline void	     min_heap_shift_up_(min_heap_t* s, unsigned hole_index, struct event* e);
static inline void	     min_heap_shift_down_(min_heap_t* s, unsigned hole_index, struct event* e);

//事件a所设置的timeout是否比b大
int min_heap_elem_greater(struct event *a, struct event *b)
{
	return evutil_timercmp(&a->ev_timeout, &b->ev_timeout, >);
}

void min_heap_ctor(min_heap_t* s) { s->p = 0; s->n = 0; s->a = 0; }//初始化min_heap_t结构
void min_heap_dtor(min_heap_t* s) { if (s->p) mm_free(s->p); }//释放minheap_t中的的事件，但是p是二重指针，可以这样释放吗？
void min_heap_elem_init(struct event* e) { e->ev_timeout_pos.min_heap_idx = -1; }//ev_timeout_pos是联合，可以是堆，也可以是队列，此处初始化堆的索引-1
int min_heap_empty(min_heap_t* s) { return 0u == s->n; }//检查堆是否为空
unsigned min_heap_size(min_heap_t* s) { return s->n; }//返回堆的大小
struct event* min_heap_top(min_heap_t* s) { return s->n ? *s->p : 0; }

int min_heap_push(min_heap_t* s, struct event* e)
{
	if (min_heap_reserve(s, s->n + 1))//加１是为了在Min_heap_reserve中判断是否大于n->a，当然如果不加１，在min_heap_reserve中判断是否等于n->a也是同样的效果
		return -1;
	min_heap_shift_up_(s, s->n++, e);//注意此处传进函数的形参是s->n没有加１
	return 0;
}

struct event* min_heap_pop(min_heap_t* s)
{
	if (s->n)//首先要判断当前堆中是否有元素，如果没有直接返回0
	{
		struct event* e = *s->p;//取走堆中第一个元素，也就是最小二叉堆中的最小元素
		min_heap_shift_down_(s, 0u, s->p[--s->n]);
		e->ev_timeout_pos.min_heap_idx = -1;//由于该事件已经从timeout的二叉堆中取出（在二叉堆中已不存在），所以它在堆中的索引应当设为-1
		return e;
	}
	return 0;//为了移植，不应该返回NULL才对吗？
}

int min_heap_elt_is_top(const struct event *e)
{
	return e->ev_timeout_pos.min_heap_idx == 0;
}

//删除堆中指定元素
int min_heap_erase(min_heap_t* s, struct event* e)
{
	if (-1 != e->ev_timeout_pos.min_heap_idx)
	{
		struct event *last = s->p[--s->n];
		unsigned parent = (e->ev_timeout_pos.min_heap_idx - 1) / 2;
		/* we replace e with the last element in the heap.  We might need to
		   shift it upward if it is less than its parent, or downward if it is
		   greater than one or both its children. Since the children are known
		   to be less than the parent, it can't need to shift both up and
		   down. */
		if (e->ev_timeout_pos.min_heap_idx > 0 && min_heap_elem_greater(s->p[parent], last))　//首先如果要删除的event在堆首，那么只有下沉，然后如果last小于该event的父亲，那么就上浮，否则就下沉
			min_heap_shift_up_(s, e->ev_timeout_pos.min_heap_idx, last);
		else
			min_heap_shift_down_(s, e->ev_timeout_pos.min_heap_idx, last);
		e->ev_timeout_pos.min_heap_idx = -1;
		return 0;
	}
	return -1;
}

//检测当前堆大小，如果不够，则重新分配空间
int min_heap_reserve(min_heap_t* s, unsigned n)
{
	if (s->a < n)
	{
		struct event** p;
		unsigned a = s->a ? s->a * 2 : 8;//s->a初始化为８，以后都是每次空间不够就增加一倍空间
		if (a < n)
			a = n;//如果指定的n够大，超过了当前s->a的两倍，则直接将堆大小设为n
		if (!(p = (struct event**)mm_realloc(s->p, a * sizeof *p)))//调用realloc重新分配空间
			return -1;
		s->p = p;
		s->a = a;
	}
	return 0;
}

//上浮，push时，hole_index为要插入的位置，其实是堆最后一个元素的下一个位置，e为要插入的元素
void min_heap_shift_up_(min_heap_t* s, unsigned hole_index, struct event* e)
{
    unsigned parent = (hole_index - 1) / 2;　//此处之所以要减去１再除以２，是因为堆索引是从0开始的，即0就有存放数据，而<算法第４版>中的堆是从1开始索引的
    while (hole_index && min_heap_elem_greater(s->p[parent], e))
    {
	(s->p[hole_index] = s->p[parent])->ev_timeout_pos.min_heap_idx = hole_index;
	hole_index = parent;
	parent = (hole_index - 1) / 2;
    }
    (s->p[hole_index] = e)->ev_timeout_pos.min_heap_idx = hole_index; //在event结构中存放了自己在二叉堆中的索引
}

//下沉，pop时，hole_index为0，e为堆的最后一个元素
void min_heap_shift_down_(min_heap_t* s, unsigned hole_index, struct event* e)
{
    unsigned min_child = 2 * (hole_index + 1);//索引从0开始，所以要加１
    while (min_child <= s->n)
	{
	//优先级'==' > '||' > '-='
	min_child -= min_child == s->n || min_heap_elem_greater(s->p[min_child], s->p[min_child - 1]);//如果min_child==s->n说明当前的已越界，所以min_child-1。如果没有越界，即min_child!=s->n，那么在检查两个儿子谁更小，如果左儿子小，那么就min_child-1
	if (!(min_heap_elem_greater(e, s->p[min_child])))//如果e小于hole_index的任一儿子，那么直接将hole_index处设为e
	    break;
	(s->p[hole_index] = s->p[min_child])->ev_timeout_pos.min_heap_idx = hole_index;//注意这种方法，先赋值，再改变存放在事件里在该堆的索引
	hole_index = min_child;
	min_child = 2 * (hole_index + 1);
	}
    (s->p[hole_index] = e)->ev_timeout_pos.min_heap_idx = hole_index;
}

#endif /* _MIN_HEAP_H_ */
