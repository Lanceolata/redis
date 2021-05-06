/* anet.c -- Basic TCP socket stuff made a bit less boring
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

#include "fmacros.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <netdb.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>

#include "anet.h"

/**
 * 打印错误信息
 * 
 * @param err 错误信息输出
 * @param fmt 错误信息格式
 * @param ap 变长参数
 */
static void anetSetError(char *err, const char *fmt, ...)
{
    va_list ap;

    if (!err) return;
    va_start(ap, fmt);
    vsnprintf(err, ANET_ERR_LEN, fmt, ap);
    va_end(ap);
}

/**
 * 将fd设置为非阻塞模式(O_NONBLOCK)
 * 
 * @param err 错误信息输出
 * @param fd 文件描述符
 * @param non_block 1 非阻塞 0 阻塞
 * @return 成功 0 失败 -1
 */
int anetSetBlock(char *err, int fd, int non_block) {
    int flags;

    // 获得当前mask
    /* Set the socket blocking (if non_block is zero) or non-blocking.
     * Note that fcntl(2) for F_GETFL and F_SETFL can't be
     * interrupted by a signal. */
    if ((flags = fcntl(fd, F_GETFL)) == -1) {
        anetSetError(err, "fcntl(F_GETFL): %s", strerror(errno));
        return ANET_ERR;
    }

    // 设置阻塞模式
    if (non_block)
        flags |= O_NONBLOCK;
    else
        flags &= ~O_NONBLOCK;

    // 设置mask
    if (fcntl(fd, F_SETFL, flags) == -1) {
        anetSetError(err, "fcntl(F_SETFL,O_NONBLOCK): %s", strerror(errno));
        return ANET_ERR;
    }
    return ANET_OK;
}

/**
 * 将fd设置为非阻塞模式
 * 
 * @param err 错误信息输出
 * @param fd 文件描述符
 * @param non_block 1 非阻塞 0 阻塞
 * @return 成功 0 失败 -1
 */
int anetNonBlock(char *err, int fd) {
    return anetSetBlock(err,fd,1);
}

/**
 * 将fd设置为阻塞模式
 * 
 * @param err 错误信息输出
 * @param fd 文件描述符
 * @param non_block 1 非阻塞 0 阻塞
 * @return 成功 0 失败 -1
 */
int anetBlock(char *err, int fd) {
    return anetSetBlock(err,fd,0);
}

/**
 * 设置心跳选项
 * 
 * @param err 错误信息输出
 * @param fd 文件描述符
 * @param interval 发送心跳间隔
 * @return 成功 0 失败 -1
 */
/* Set TCP keep alive option to detect dead peers. The interval option
 * is only used for Linux as we are using Linux-specific APIs to set
 * the probe send time, interval, and count. */
int anetKeepAlive(char *err, int fd, int interval)
{
    int val = 1;

    // 设置SO_KEEPALIVE
    if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &val, sizeof(val)) == -1)
    {
        anetSetError(err, "setsockopt SO_KEEPALIVE: %s", strerror(errno));
        return ANET_ERR;
    }

#ifdef __linux__
    /* Default settings are more or less garbage, with the keepalive time
     * set to 7200 by default on Linux. Modify settings to make the feature
     * actually useful. */

    /* Send first probe after interval. */
    val = interval;
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &val, sizeof(val)) < 0) {
        anetSetError(err, "setsockopt TCP_KEEPIDLE: %s\n", strerror(errno));
        return ANET_ERR;
    }

    /* Send next probes after the specified interval. Note that we set the
     * delay as interval / 3, as we send three probes before detecting
     * an error (see the next setsockopt call). */
    val = interval/3;
    if (val == 0) val = 1;
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &val, sizeof(val)) < 0) {
        anetSetError(err, "setsockopt TCP_KEEPINTVL: %s\n", strerror(errno));
        return ANET_ERR;
    }

    /* Consider the socket in error state after three we send three ACK
     * probes without getting a reply. */
    val = 3;
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &val, sizeof(val)) < 0) {
        anetSetError(err, "setsockopt TCP_KEEPCNT: %s\n", strerror(errno));
        return ANET_ERR;
    }
#else
    ((void) interval); /* Avoid unused var warning for non Linux systems. */
#endif

    return ANET_OK;
}

/**
 * 设置TCP_NODELAY
 * 
 * @param err 错误信息输出
 * @param fd 文件描述符
 * @param val 值
 * @return 成功 0 失败 -1
 */
static int anetSetTcpNoDelay(char *err, int fd, int val)
{
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val)) == -1)
    {
        anetSetError(err, "setsockopt TCP_NODELAY: %s", strerror(errno));
        return ANET_ERR;
    }
    return ANET_OK;
}

/**
 * 开启TCP_NODELAY
 * 
 * @param err 错误信息输出
 * @param fd 文件描述符
 * @return 成功 0 失败 -1
 */
int anetEnableTcpNoDelay(char *err, int fd)
{
    return anetSetTcpNoDelay(err, fd, 1);
}

/**
 * 关闭TCP_NODELAY
 * 
 * @param err 错误信息输出
 * @param fd 文件描述符
 * @return 成功 0 失败 -1
 */
int anetDisableTcpNoDelay(char *err, int fd)
{
    return anetSetTcpNoDelay(err, fd, 0);
}

/**
 * 设置SO_SNDBUF
 * 
 * @param err 错误信息输出
 * @param fd 文件描述符
 * @param buffsize buffer大小
 * @return 成功 0 失败 -1
 */
int anetSetSendBuffer(char *err, int fd, int buffsize)
{
    if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buffsize, sizeof(buffsize)) == -1)
    {
        anetSetError(err, "setsockopt SO_SNDBUF: %s", strerror(errno));
        return ANET_ERR;
    }
    return ANET_OK;
}

/**
 * 设置SO_KEEPALIVE
 * 
 * @param err 错误信息输出
 * @param fd 文件描述符
 * @return 成功 0 失败 -1
 */
int anetTcpKeepAlive(char *err, int fd)
{
    int yes = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(yes)) == -1) {
        anetSetError(err, "setsockopt SO_KEEPALIVE: %s", strerror(errno));
        return ANET_ERR;
    }
    return ANET_OK;
}

/**
 * 设置SO_SNDTIMEO
 * 
 * @param err 错误信息输出
 * @param fd 文件描述符
 * @param ms 超时时间
 * @return 成功 0 失败 -1
 */
/* Set the socket send timeout (SO_SNDTIMEO socket option) to the specified
 * number of milliseconds, or disable it if the 'ms' argument is zero. */
int anetSendTimeout(char *err, int fd, long long ms) {
    struct timeval tv;

    tv.tv_sec = ms/1000;
    tv.tv_usec = (ms%1000)*1000;
    if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == -1) {
        anetSetError(err, "setsockopt SO_SNDTIMEO: %s", strerror(errno));
        return ANET_ERR;
    }
    return ANET_OK;
}

/**
 * 设置SO_RCVTIMEO
 * 
 * @param err 错误信息输出
 * @param fd 文件描述符
 * @param ms 超时时间
 * @return 成功 0 失败 -1
 */
/* Set the socket receive timeout (SO_RCVTIMEO socket option) to the specified
 * number of milliseconds, or disable it if the 'ms' argument is zero. */
int anetRecvTimeout(char *err, int fd, long long ms) {
    struct timeval tv;

    tv.tv_sec = ms/1000;
    tv.tv_usec = (ms%1000)*1000;
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == -1) {
        anetSetError(err, "setsockopt SO_RCVTIMEO: %s", strerror(errno));
        return ANET_ERR;
    }
    return ANET_OK;
}

/**
 * 主机名到地址解析
 * 
 * @param err 错误信息
 * @param host 主机名
 * @param ipbuf ip地址 输出
 * @param ipbuf_len ipbuf长度 输出
 * @param flags 标识 ANET_IP_ONLY时，格式化IP地址(host已经是IP地址)
 */
/* anetGenericResolve() is called by anetResolve() and anetResolveIP() to
 * do the actual work. It resolves the hostname "host" and set the string
 * representation of the IP address into the buffer pointed by "ipbuf".
 *
 * If flags is set to ANET_IP_ONLY the function only resolves hostnames
 * that are actually already IPv4 or IPv6 addresses. This turns the function
 * into a validating / normalizing function. */
int anetGenericResolve(char *err, char *host, char *ipbuf, size_t ipbuf_len,
                       int flags)
{
    struct addrinfo hints, *info;
    int rv;

    memset(&hints,0,sizeof(hints));
    // 主机名字符串是IP地址。由于主机名已经是IP地址，因此不需要DNS解析。
    if (flags & ANET_IP_ONLY) hints.ai_flags = AI_NUMERICHOST;
    // 返回适用于指定主机名和服务名且适合任何协议族的地址
    // AF_INET IPV4
    // AF_INET6 IPV6
    hints.ai_family = AF_UNSPEC;
    // TCP协议
    hints.ai_socktype = SOCK_STREAM;  /*     */

    // 主机名到地址解析
    if ((rv = getaddrinfo(host, NULL, &hints, &info)) != 0) {
        anetSetError(err, "%s", gai_strerror(rv));
        return ANET_ERR;
    }
    if (info->ai_family == AF_INET) {
        // IPV4
        struct sockaddr_in *sa = (struct sockaddr_in *)info->ai_addr;
        // 将数值格式转化为点分十进制的ip地址格式
        inet_ntop(AF_INET, &(sa->sin_addr), ipbuf, ipbuf_len);
    } else {
        // IPV6
        struct sockaddr_in6 *sa = (struct sockaddr_in6 *)info->ai_addr;
        // 将数值格式转化为点分十进制的ip地址格式
        inet_ntop(AF_INET6, &(sa->sin6_addr), ipbuf, ipbuf_len);
    }

    // 释放addrinfo
    freeaddrinfo(info);
    return ANET_OK;
}

/**
 * 主机名到地址解析
 * 
 * @param err 错误信息
 * @param host 主机名
 * @param ipbuf ip地址 输出
 * @param ipbuf_len ipbuf长度 输出
 */
int anetResolve(char *err, char *host, char *ipbuf, size_t ipbuf_len) {
    return anetGenericResolve(err,host,ipbuf,ipbuf_len,ANET_NONE);
}

/**
 * 主机名到地址解析
 * 
 * @param err 错误信息
 * @param host 主机名(IP)
 * @param ipbuf ip地址 输出
 * @param ipbuf_len ipbuf长度 输出
 */
int anetResolveIP(char *err, char *host, char *ipbuf, size_t ipbuf_len) {
    return anetGenericResolve(err,host,ipbuf,ipbuf_len,ANET_IP_ONLY);
}

/**
 * 设置SO_REUSEADDR
 * 允许服务器bind一个地址，即使这个地址当前已经存在已建立的连接
 * 服务器启动后，有客户端连接并已建立，如果服务器主动关闭，那么和客户端的连接会处于TIME_WAIT状态，此时再次启动服务器，就会bind不成功，报：Address already in use。
 * 服务器父进程监听客户端，当和客户端建立链接后，fork一个子进程专门处理客户端的请求，如果父进程停止，因为子进程还和客户端有连接，所以再次启动父进程，也会报Address already in use。
 * 
 * @param err 错误信息输出
 * @param fd 文件描述符
 * @return 成功 0 失败 -1
 */
static int anetSetReuseAddr(char *err, int fd) {
    int yes = 1;
    /* Make sure connection-intensive things like the redis benchmark
     * will be able to close/open sockets a zillion of times */
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
        anetSetError(err, "setsockopt SO_REUSEADDR: %s", strerror(errno));
        return ANET_ERR;
    }
    return ANET_OK;
}

/**
 * 创建socket
 * 
 * @param err 错误信息输出
 * @param domain 地址描述
 * @return socket描述符
 */
static int anetCreateSocket(char *err, int domain) {
    int s;
    if ((s = socket(domain, SOCK_STREAM, 0)) == -1) {
        anetSetError(err, "creating socket: %s", strerror(errno));
        return ANET_ERR;
    }

    /* Make sure connection-intensive things like the redis benchmark
     * will be able to close/open sockets a zillion of times */
    if (anetSetReuseAddr(err,s) == ANET_ERR) {
        close(s);
        return ANET_ERR;
    }
    return s;
}

/**
 * TCP连接
 * 
 * @param err 错误信息输出
 * @param addr 地址
 * @param port 端口
 * @param source_addr 源地址
 * @param flags 标识
 * @return 
 */
#define ANET_CONNECT_NONE 0
#define ANET_CONNECT_NONBLOCK 1
#define ANET_CONNECT_BE_BINDING 2 /* Best effort binding. */
static int anetTcpGenericConnect(char *err, const char *addr, int port,
                                 const char *source_addr, int flags)
{
    int s = ANET_ERR, rv;
    char portstr[6];  /* strlen("65535") + 1; */
    struct addrinfo hints, *servinfo, *bservinfo, *p, *b;

    // 端口号
    snprintf(portstr,sizeof(portstr),"%d",port);
    // 地址
    memset(&hints,0,sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    // 解析addr为 ip和端口 列表
    if ((rv = getaddrinfo(addr,portstr,&hints,&servinfo)) != 0) {
        anetSetError(err, "%s", gai_strerror(rv));
        return ANET_ERR;
    }
    // 遍历 创建socket并connect
    // socket / connect 失败时，尝试下一个
    for (p = servinfo; p != NULL; p = p->ai_next) {
        /* Try to create the socket and to connect it.
         * If we fail in the socket() call, or on connect(), we retry with
         * the next entry in servinfo. */
        // 创建socket
        if ((s = socket(p->ai_family,p->ai_socktype,p->ai_protocol)) == -1)
            continue;
        // 设置SO_REUSEADDR
        if (anetSetReuseAddr(err,s) == ANET_ERR) goto error;
        // 设置非阻塞
        if (flags & ANET_CONNECT_NONBLOCK && anetNonBlock(err,s) != ANET_OK)
            goto error;
        if (source_addr) {
            int bound = 0;
            // 解析源IP和端口
            /* Using getaddrinfo saves us from self-determining IPv4 vs IPv6 */
            if ((rv = getaddrinfo(source_addr, NULL, &hints, &bservinfo)) != 0)
            {
                anetSetError(err, "%s", gai_strerror(rv));
                goto error;
            }
            // s 绑定 b
            // socket绑定到源地址
            for (b = bservinfo; b != NULL; b = b->ai_next) {
                if (bind(s,b->ai_addr,b->ai_addrlen) != -1) {
                    bound = 1;
                    break;
                }
            }
            // 释放bservinfo
            freeaddrinfo(bservinfo);
            // 绑定失败
            if (!bound) {
                anetSetError(err, "bind: %s", strerror(errno));
                goto error;
            }
        }
        // 连接s
        if (connect(s,p->ai_addr,p->ai_addrlen) == -1) {
            /* If the socket is non-blocking, it is ok for connect() to
             * return an EINPROGRESS error here. */
            if (errno == EINPROGRESS && flags & ANET_CONNECT_NONBLOCK)
                goto end;
            close(s);
            s = ANET_ERR;
            continue;
        }

        /* If we ended an iteration of the for loop without errors, we
         * have a connected socket. Let's return to the caller. */
        goto end;
    }
    if (p == NULL)
        anetSetError(err, "creating socket: %s", strerror(errno));

error:
    if (s != ANET_ERR) {
        close(s);
        s = ANET_ERR;
    }

end:
    freeaddrinfo(servinfo);

    /* Handle best effort binding: if a binding address was used, but it is
     * not possible to create a socket, try again without a binding address. */
    if (s == ANET_ERR && source_addr && (flags & ANET_CONNECT_BE_BINDING)) {
        return anetTcpGenericConnect(err,addr,port,NULL,flags);
    } else {
        return s;
    }
}

/**
 * 创建TCP链接
 * 
 * @param err 错误信息输出
 * @param addr 地址
 * @param port 端口
 * @return 套接口
 */
int anetTcpConnect(char *err, const char *addr, int port)
{
    return anetTcpGenericConnect(err,addr,port,NULL,ANET_CONNECT_NONE);
}

/**
 * 创建TCP链接，非阻塞
 * 
 * @param err 错误信息输出
 * @param addr 地址
 * @param port 端口
 * @return 套接口
 */
int anetTcpNonBlockConnect(char *err, const char *addr, int port)
{
    return anetTcpGenericConnect(err,addr,port,NULL,ANET_CONNECT_NONBLOCK);
}

/**
 * 创建TCP链接，绑定地址，非阻塞
 * 
 * @param err 错误信息输出
 * @param addr 地址
 * @param port 端口
 * @param source_addr 源地址
 * @return 套接口
 */
int anetTcpNonBlockBindConnect(char *err, const char *addr, int port,
                               const char *source_addr)
{
    return anetTcpGenericConnect(err,addr,port,source_addr,
            ANET_CONNECT_NONBLOCK);
}

/**
 * 创建TCP链接，绑定地址，非阻塞
 * 无法绑定时，使用不绑定重试
 * 
 * @param err 错误信息输出
 * @param addr 地址
 * @param port 端口
 * @param source_addr 源地址
 * @return 套接口
 */
int anetTcpNonBlockBestEffortBindConnect(char *err, const char *addr, int port,
                                         const char *source_addr)
{
    return anetTcpGenericConnect(err,addr,port,source_addr,
            ANET_CONNECT_NONBLOCK|ANET_CONNECT_BE_BINDING);
}

/**
 * 创建本地通信连接
 * 
 * @param err 错误信息输出
 * @param path 路径
 * @param flags flags
 * @return 套接口
 */
int anetUnixGenericConnect(char *err, const char *path, int flags)
{
    int s;
    struct sockaddr_un sa;

    // 创建socket
    if ((s = anetCreateSocket(err,AF_LOCAL)) == ANET_ERR)
        return ANET_ERR;

    // 设置本地通信
    sa.sun_family = AF_LOCAL;
    strncpy(sa.sun_path,path,sizeof(sa.sun_path)-1);
    // 设置非阻塞
    if (flags & ANET_CONNECT_NONBLOCK) {
        if (anetNonBlock(err,s) != ANET_OK) {
            close(s);
            return ANET_ERR;
        }
    }
    // 建立连接
    if (connect(s,(struct sockaddr*)&sa,sizeof(sa)) == -1) {
        if (errno == EINPROGRESS &&
            flags & ANET_CONNECT_NONBLOCK)
            return s;

        anetSetError(err, "connect: %s", strerror(errno));
        close(s);
        return ANET_ERR;
    }
    return s;
}

/**
 * 创建本地通信连接
 * 
 * @param err 错误信息输出
 * @param path 路径
 * @return 套接口
 */
int anetUnixConnect(char *err, const char *path)
{
    return anetUnixGenericConnect(err,path,ANET_CONNECT_NONE);
}

/**
 * 创建本地通信连接，非阻塞
 * 
 * @param err 错误信息输出
 * @param path 路径
 * @return 套接口
 */
int anetUnixNonBlockConnect(char *err, const char *path)
{
    return anetUnixGenericConnect(err,path,ANET_CONNECT_NONBLOCK);
}

/**
 * 读数据，读count长度
 * 
 * @param fd 文件描述符
 * @param buf buffer
 * @param count 读长度
 * @return 读长度 失败时返回-1
 */
/* Like read(2) but make sure 'count' is read before to return
 * (unless error or EOF condition is encountered) */
int anetRead(int fd, char *buf, int count)
{
    ssize_t nread, totlen = 0;
    while(totlen != count) {
        nread = read(fd,buf,count-totlen);
        if (nread == 0) return totlen;
        if (nread == -1) return -1;
        totlen += nread;
        buf += nread;
    }
    return totlen;
}

/**
 * 写数据，写count长度
 * 
 * @param fd 文件描述符
 * @param buf buffer
 * @param count 写长度
 * @return 写长度 失败时返回-1
 */
/* Like write(2) but make sure 'count' is written before to return
 * (unless error is encountered) */
int anetWrite(int fd, char *buf, int count)
{
    ssize_t nwritten, totlen = 0;
    while(totlen != count) {
        nwritten = write(fd,buf,count-totlen);
        if (nwritten == 0) return totlen;
        if (nwritten == -1) return -1;
        totlen += nwritten;
        buf += nwritten;
    }
    return totlen;
}

/**
 * 监听连接请求
 * 
 * @param err 错误信息输出
 * @param s 套接口
 * @param sa 地址
 * @param len 地址长度
 * @param backlog
 * @return 0 成功 -1 失败
 */
static int anetListen(char *err, int s, struct sockaddr *sa, socklen_t len, int backlog) {
    // 绑定地址
    if (bind(s,sa,len) == -1) {
        anetSetError(err, "bind: %s", strerror(errno));
        close(s);
        return ANET_ERR;
    }

    // 监听套接口
    if (listen(s, backlog) == -1) {
        anetSetError(err, "listen: %s", strerror(errno));
        close(s);
        return ANET_ERR;
    }
    return ANET_OK;
}

/**
 * 设置IPV6
 * 
 * @param err 错误信息输出
 * @param s 套接口
 * @return 0 成功 -1 失败
 */
static int anetV6Only(char *err, int s) {
    int yes = 1;
    if (setsockopt(s,IPPROTO_IPV6,IPV6_V6ONLY,&yes,sizeof(yes)) == -1) {
        anetSetError(err, "setsockopt: %s", strerror(errno));
        close(s);
        return ANET_ERR;
    }
    return ANET_OK;
}

/**
 * TCP Server
 * 
 * @param err 错误信息输出
 * @param port 端口
 * @param bindaddr 绑定地址
 * @param af ai_family
 * @param backlog 连接请求队列
 * @return 套接口
 */
static int _anetTcpServer(char *err, int port, char *bindaddr, int af, int backlog)
{
    int s = -1, rv;
    char _port[6];  /* strlen("65535") */
    struct addrinfo hints, *servinfo, *p;

    // 端口字符串
    snprintf(_port,6,"%d",port);
    memset(&hints,0,sizeof(hints));
    hints.ai_family = af;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;    /* No effect if bindaddr != NULL */

    // 解析addr为 ip和端口 列表
    if ((rv = getaddrinfo(bindaddr,_port,&hints,&servinfo)) != 0) {
        anetSetError(err, "%s", gai_strerror(rv));
        return ANET_ERR;
    }
    // 遍历列表 创建socket并监听
    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((s = socket(p->ai_family,p->ai_socktype,p->ai_protocol)) == -1)
            continue;

        if (af == AF_INET6 && anetV6Only(err,s) == ANET_ERR) goto error;
        if (anetSetReuseAddr(err,s) == ANET_ERR) goto error;
        if (anetListen(err,s,p->ai_addr,p->ai_addrlen,backlog) == ANET_ERR) s = ANET_ERR;
        goto end;
    }
    if (p == NULL) {
        anetSetError(err, "unable to bind socket, errno: %d", errno);
        goto error;
    }

error:
    if (s != -1) close(s);
    s = ANET_ERR;
end:
    freeaddrinfo(servinfo);
    return s;
}

/**
 * 创建TCP Server
 * 
 * @param err 错误信息输出
 * @param port 端口
 * @param bindaddr 绑定地址
 * @param backlog 连接请求队列
 * @return 套接口
 */
int anetTcpServer(char *err, int port, char *bindaddr, int backlog)
{
    return _anetTcpServer(err, port, bindaddr, AF_INET, backlog);
}

/**
 * 创建TCP Server IPV6
 * 
 * @param err 错误信息输出
 * @param port 端口
 * @param bindaddr 绑定地址
 * @param backlog 连接请求队列
 * @return 套接口
 */
int anetTcp6Server(char *err, int port, char *bindaddr, int backlog)
{
    return _anetTcpServer(err, port, bindaddr, AF_INET6, backlog);
}

/**
 * 进程通信服务
 * 
 * @param err 错误信息输出
 * @param path 路径
 * @param perm 路径权限
 * @param backlog 连接请求队列
 * @return 套接口
 */
int anetUnixServer(char *err, char *path, mode_t perm, int backlog)
{
    int s;
    struct sockaddr_un sa;

    // 创建套接口
    if ((s = anetCreateSocket(err,AF_LOCAL)) == ANET_ERR)
        return ANET_ERR;

    memset(&sa,0,sizeof(sa));
    sa.sun_family = AF_LOCAL;
    strncpy(sa.sun_path,path,sizeof(sa.sun_path)-1);
    // 监听
    if (anetListen(err,s,(struct sockaddr*)&sa,sizeof(sa),backlog) == ANET_ERR)
        return ANET_ERR;
    // 权限
    if (perm)
        chmod(sa.sun_path, perm);
    return s;
}

/**
 * 接受连接
 * 
 * @param err 错误信息输出
 * @param s 套接口
 * @param sa 地址
 * @param len 地址长度
 * @return 新的套接口
 */
static int anetGenericAccept(char *err, int s, struct sockaddr *sa, socklen_t *len) {
    int fd;
    while(1) {
        fd = accept(s,sa,len);
        if (fd == -1) {
            if (errno == EINTR)
                continue;
            else {
                anetSetError(err, "accept: %s", strerror(errno));
                return ANET_ERR;
            }
        }
        break;
    }
    return fd;
}

/**
 * 接受TCP连接
 * 
 * @param err 错误信息输出
 * @param s 套接口
 * @param ip ip地址(返回值)
 * @param ip_len ip地址长度(返回值)
 * @param port 端口号(返回值)
 * @return 新的套接口
 */
int anetTcpAccept(char *err, int s, char *ip, size_t ip_len, int *port) {
    int fd;
    struct sockaddr_storage sa;
    socklen_t salen = sizeof(sa);
    // 接受连接
    if ((fd = anetGenericAccept(err,s,(struct sockaddr*)&sa,&salen)) == -1)
        return ANET_ERR;

    if (sa.ss_family == AF_INET) {
        struct sockaddr_in *s = (struct sockaddr_in *)&sa;
        if (ip) inet_ntop(AF_INET,(void*)&(s->sin_addr),ip,ip_len);
        if (port) *port = ntohs(s->sin_port);
    } else {
        struct sockaddr_in6 *s = (struct sockaddr_in6 *)&sa;
        if (ip) inet_ntop(AF_INET6,(void*)&(s->sin6_addr),ip,ip_len);
        if (port) *port = ntohs(s->sin6_port);
    }
    return fd;
}

/**
 * 接受Unix连接
 * 
 * @param err 错误信息输出
 * @param s 套接口
 * @return 新的套接口
 */
int anetUnixAccept(char *err, int s) {
    int fd;
    struct sockaddr_un sa;
    socklen_t salen = sizeof(sa);
    if ((fd = anetGenericAccept(err,s,(struct sockaddr*)&sa,&salen)) == -1)
        return ANET_ERR;

    return fd;
}

/**
 * 获取与套接口相连的端地址
 * 
 * @param fd 套接口
 * @param ip ip地址
 * @param ip_len ip地址长度
 * @param port 端口
 * @return 0 成功 -1 失败
 */
int anetPeerToString(int fd, char *ip, size_t ip_len, int *port) {
    struct sockaddr_storage sa;
    socklen_t salen = sizeof(sa);

    // 获取与套接口相连的端地址
    if (getpeername(fd,(struct sockaddr*)&sa,&salen) == -1) goto error;
    if (ip_len == 0) goto error;

    if (sa.ss_family == AF_INET) {
        struct sockaddr_in *s = (struct sockaddr_in *)&sa;
        if (ip) inet_ntop(AF_INET,(void*)&(s->sin_addr),ip,ip_len);
        if (port) *port = ntohs(s->sin_port);
    } else if (sa.ss_family == AF_INET6) {
        struct sockaddr_in6 *s = (struct sockaddr_in6 *)&sa;
        if (ip) inet_ntop(AF_INET6,(void*)&(s->sin6_addr),ip,ip_len);
        if (port) *port = ntohs(s->sin6_port);
    } else if (sa.ss_family == AF_UNIX) {
        if (ip) strncpy(ip,"/unixsocket",ip_len);
        if (port) *port = 0;
    } else {
        goto error;
    }
    return 0;

error:
    if (ip) {
        if (ip_len >= 2) {
            ip[0] = '?';
            ip[1] = '\0';
        } else if (ip_len == 1) {
            ip[0] = '\0';
        }
    }
    if (port) *port = 0;
    return -1;
}

/**
 * 格式化地址
 * 
 * @param buf 输出buffer
 * @param buf_len buffer的长度
 * @param ip ip地址
 * @param port 端口
 * @return 0 成功
 */
/* Format an IP,port pair into something easy to parse. If IP is IPv6
 * (matches for ":"), the ip is surrounded by []. IP and port are just
 * separated by colons. This the standard to display addresses within Redis. */
int anetFormatAddr(char *buf, size_t buf_len, char *ip, int port) {
    return snprintf(buf,buf_len, strchr(ip,':') ?
           "[%s]:%d" : "%s:%d", ip, port);
}

/**
 * 从socket中解析ip和port，并格式化
 * 
 * @param fd 套接口
 * @param buf 输出buffer
 * @param buf_len buffer的长度
 * @return 0 成功
 */
/* Like anetFormatAddr() but extract ip and port from the socket's peer. */
int anetFormatPeer(int fd, char *buf, size_t buf_len) {
    char ip[INET6_ADDRSTRLEN];
    int port;

    anetPeerToString(fd,ip,sizeof(ip),&port);
    return anetFormatAddr(buf, buf_len, ip, port);
}

/**
 * 获取套接字的名字
 * 
 * @param fd 套接口
 * @param ip ip地址
 * @param ip_len ip地址长度
 * @param port 端口好
 * @return 0 成功 -1 失败
 */
int anetSockName(int fd, char *ip, size_t ip_len, int *port) {
    struct sockaddr_storage sa;
    socklen_t salen = sizeof(sa);

    // 获取一个套接字的名字
    if (getsockname(fd,(struct sockaddr*)&sa,&salen) == -1) {
        if (port) *port = 0;
        ip[0] = '?';
        ip[1] = '\0';
        return -1;
    }
    if (sa.ss_family == AF_INET) {
        struct sockaddr_in *s = (struct sockaddr_in *)&sa;
        if (ip) inet_ntop(AF_INET,(void*)&(s->sin_addr),ip,ip_len);
        if (port) *port = ntohs(s->sin_port);
    } else {
        struct sockaddr_in6 *s = (struct sockaddr_in6 *)&sa;
        if (ip) inet_ntop(AF_INET6,(void*)&(s->sin6_addr),ip,ip_len);
        if (port) *port = ntohs(s->sin6_port);
    }
    return 0;
}

/**
 * 从套接口解析本地ip和port，并格式化
 * 
 * @param fd 套接口
 * @param fmt 输出buffer
 * @param fmt 输出buffer长度
 * @return 0 成功 -1 失败
 */
int anetFormatSock(int fd, char *fmt, size_t fmt_len) {
    char ip[INET6_ADDRSTRLEN];
    int port;

    anetSockName(fd,ip,sizeof(ip),&port);
    return anetFormatAddr(fmt, fmt_len, ip, port);
}
