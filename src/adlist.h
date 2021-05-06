/* adlist.h - A generic doubly linked list implementation
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
 * 双端链表
 */

#ifndef __ADLIST_H__
#define __ADLIST_H__

/* Node, List, and Iterator are the only data structures used currently. */

/**
 * 双端链表节点
 */
typedef struct listNode {
    struct listNode *prev;
    struct listNode *next;
    void *value;
} listNode;

/**
 * 链表迭代器
 */
typedef struct listIter {
    listNode *next;
    int direction;
} listIter;

/**
 * 双端链表
 */
typedef struct list {
    listNode *head;
    listNode *tail;
    // 节点值复制函数
    void *(*dup)(void *ptr);
    // 节点值释放函数
    void (*free)(void *ptr);
    // 节点值比较函数
    int (*match)(void *ptr, void *key);
    // 链表所包含的节点数量
    unsigned long len;
} list;

/* Functions implemented as macros */
// 返回给定链表所包含的节点数量
#define listLength(l) ((l)->len)
// 返回给定链表的表头节点
#define listFirst(l) ((l)->head)
// 返回给定链表的表尾节点
#define listLast(l) ((l)->tail)
// 返回给定节点的前置节点
#define listPrevNode(n) ((n)->prev)
// 返回给定节点的后置节点
#define listNextNode(n) ((n)->next)
// 返回给定节点的值
#define listNodeValue(n) ((n)->value)

// 将链表 l 的值复制函数设置为 m
#define listSetDupMethod(l,m) ((l)->dup = (m))
// 将链表 l 的值释放函数设置为 m
#define listSetFreeMethod(l,m) ((l)->free = (m))
// 将链表的对比函数设置为 m
#define listSetMatchMethod(l,m) ((l)->match = (m))

// 返回给定链表的值复制函数
#define listGetDupMethod(l) ((l)->dup)
// 返回给定链表的值释放函数
#define listGetFreeMethod(l) ((l)->free)
// 返回给定链表的值对比函数
#define listGetMatchMethod(l) ((l)->match)

/* Prototypes */
// 创建链表
list *listCreate(void);
// 释放链表
void listRelease(list *list);
// 清空链表
void listEmpty(list *list);
// 链表头插入
list *listAddNodeHead(list *list, void *value);
// 链表尾插入
list *listAddNodeTail(list *list, void *value);
// 链表插入
list *listInsertNode(list *list, listNode *old_node, void *value, int after);
// 删除节点
void listDelNode(list *list, listNode *node);
// 创建迭代器
listIter *listGetIterator(list *list, int direction);
// 迭代器下一个节点
listNode *listNext(listIter *iter);
// 释放迭代器
void listReleaseIterator(listIter *iter);
// 复制链表
list *listDup(list *orig);
// 搜索链表
listNode *listSearchKey(list *list, void *key);
// 获得索引指定的节点
listNode *listIndex(list *list, long index);
// 重置迭代器，从头节点开始
void listRewind(list *list, listIter *li);
// 重置迭代器，从尾节点开始
void listRewindTail(list *list, listIter *li);
// 旋转链表，将尾节点插入头节点前
void listRotateTailToHead(list *list);
// 旋转链表，将头节点插入尾节点后
void listRotateHeadToTail(list *list);
// 合并两个链表
void listJoin(list *l, list *o);

/**
 * 迭代器进行迭代的方向
 */
/* Directions for iterators */
// 从表头向表尾进行迭代
#define AL_START_HEAD 0
// 从表尾到表头进行迭代
#define AL_START_TAIL 1

#endif /* __ADLIST_H__ */
