/* A simple event-driven programming library. Originally I wrote this code
 * for the Jim's event-loop (Jim is a Tcl interpreter) but later translated
 * it in form of a library for easy reuse.
 *
 * Copyright (c) 2006-2012, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * 事件驱动
 */

#ifndef __AE_H__
#define __AE_H__

#include <time.h>

/**
 * 事件执行状态
 */
// 成功
#define AE_OK 0
// 失败
#define AE_ERR -1

/**
 * 文件事件状态
 */
// 未设置
#define AE_NONE 0       /* No events registered. */
// 可读
#define AE_READABLE 1   /* Fire when descriptor is readable. */
// 可写
#define AE_WRITABLE 2   /* Fire when descriptor is writable. */
// 先执行WRITABLE，再执行READABLE
#define AE_BARRIER 4    /* With WRITABLE, never fire the event if the
                           READABLE event already fired in the same event
                           loop iteration. Useful when you want to persist
                           things to disk before sending replies, and want
                           to do that in a group fashion. */
/*
 * 事件处理器flags
 */
// 文件事件
#define AE_FILE_EVENTS (1<<0)
// 时间事件
#define AE_TIME_EVENTS (1<<1)
// 所有事件
#define AE_ALL_EVENTS (AE_FILE_EVENTS|AE_TIME_EVENTS)
// 不阻塞，也不进行等待
#define AE_DONT_WAIT (1<<2)
// 在处理事件前要执行
#define AE_CALL_BEFORE_SLEEP (1<<3)
// 在处理事件后要执行
#define AE_CALL_AFTER_SLEEP (1<<4)

// 决定时间事件是否要持续执行的flag
#define AE_NOMORE -1
// 删除事件flag
#define AE_DELETED_EVENT_ID -1

/* Macros */
#define AE_NOTUSED(V) ((void) V)

// 事件处理器的状态
struct aeEventLoop;

/**
 * 事件接口
 */
/* Types and data structures */
typedef void aeFileProc(struct aeEventLoop *eventLoop, int fd, void *clientData, int mask);
typedef int aeTimeProc(struct aeEventLoop *eventLoop, long long id, void *clientData);
typedef void aeEventFinalizerProc(struct aeEventLoop *eventLoop, void *clientData);
typedef void aeBeforeSleepProc(struct aeEventLoop *eventLoop);

/**
 * 文件事件
 */
/* File event structure */
typedef struct aeFileEvent {
    // 事件状态掩码
    int mask; /* one of AE_(READABLE|WRITABLE|BARRIER) */
    // 读事件处理器
    aeFileProc *rfileProc;
    // 写事件处理器
    aeFileProc *wfileProc;
    // 多路复用库的私有数据
    void *clientData;
} aeFileEvent;

/**
 * 时间事件
 */
/* Time event structure */
typedef struct aeTimeEvent {
    // 时间事件的唯一标识符
    long long id; /* time event identifier. */
    // 事件的到达时间
    long when_sec; /* seconds */
    long when_ms; /* milliseconds */
    // 事件处理函数
    aeTimeProc *timeProc;
    // 事件释放函数
    aeEventFinalizerProc *finalizerProc;
    // 多路复用库的私有数据
    void *clientData;
    // 前后指针，形成链表
    struct aeTimeEvent *prev;
    struct aeTimeEvent *next;
    // 引用计数
    int refcount; /* refcount to prevent timer events from being
  		   * freed in recursive time event calls. */
} aeTimeEvent;

/**
 * 已就绪事件
 */
/* A fired event */
typedef struct aeFiredEvent {
    // 已就绪文件描述符
    int fd;
    // 事件状态掩码
    int mask;
} aeFiredEvent;

/**
 * 事件处理器的状态
 */
/* State of an event based program */
typedef struct aeEventLoop {
    // 目前已注册的最大描述符
    int maxfd;   /* highest file descriptor currently registered */
    // 追踪的最大描述符
    int setsize; /* max number of file descriptors tracked */
    // 用于生成时间事件id
    long long timeEventNextId;
    // 最后一次执行时间事件的时间
    time_t lastTime;     /* Used to detect system clock skew */
    // 已注册的文件事件
    aeFileEvent *events; /* Registered events */
    // 已就绪的文件事件
    aeFiredEvent *fired; /* Fired events */
    // 时间事件
    aeTimeEvent *timeEventHead;
    // 事件处理器的开关
    int stop;
    // 多路复用库的私有数据
    void *apidata; /* This is used for polling API specific data */
    // 在处理事件前要执行的函数
    aeBeforeSleepProc *beforesleep;
    // 在处理事件后要执行的函数
    aeBeforeSleepProc *aftersleep;
    // TODO
    int flags;
} aeEventLoop;

/* Prototypes */
// 创建事件处理器
aeEventLoop *aeCreateEventLoop(int setsize);
// 删除事件处理器
void aeDeleteEventLoop(aeEventLoop *eventLoop);
// 停止事件处理器
void aeStop(aeEventLoop *eventLoop);
// 根据mask参数的值，监听fd文件的状态，当fd可用时，执行proc函数
int aeCreateFileEvent(aeEventLoop *eventLoop, int fd, int mask,
        aeFileProc *proc, void *clientData);
// 将fd从mask指定的文件监听队列中删除
void aeDeleteFileEvent(aeEventLoop *eventLoop, int fd, int mask);
// 获取给定fd正在监听的事件类型
int aeGetFileEvents(aeEventLoop *eventLoop, int fd);
// 创建时间事件
long long aeCreateTimeEvent(aeEventLoop *eventLoop, long long milliseconds,
        aeTimeProc *proc, void *clientData,
        aeEventFinalizerProc *finalizerProc);
// 删除给定id的时间事件
int aeDeleteTimeEvent(aeEventLoop *eventLoop, long long id);
// 处理所有已到达的时间事件，以及所有已就绪的文件事件。
int aeProcessEvents(aeEventLoop *eventLoop, int flags);
// 在给定毫秒内等待，直到fd变成可写、可读或异常
int aeWait(int fd, int mask, long long milliseconds);
// 事件处理器的主循环
void aeMain(aeEventLoop *eventLoop);
// 返回所使用的多路复用库的名称
char *aeGetApiName(void);
// 设置处理事件前需要被执行的函数
void aeSetBeforeSleepProc(aeEventLoop *eventLoop, aeBeforeSleepProc *beforesleep);
// 设置处理事件后需要被执行的函数
void aeSetAfterSleepProc(aeEventLoop *eventLoop, aeBeforeSleepProc *aftersleep);
// 返回当前事件槽大小
int aeGetSetSize(aeEventLoop *eventLoop);
// 调整事件槽的大小
int aeResizeSetSize(aeEventLoop *eventLoop, int setsize);
// 设置事件处理器不阻塞等待
void aeSetDontWait(aeEventLoop *eventLoop, int noWait);

#endif
