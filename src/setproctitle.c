/* ==========================================================================
 * setproctitle.c - Linux/Darwin setproctitle.
 * --------------------------------------------------------------------------
 * Copyright (C) 2010  William Ahern
 * Copyright (C) 2013  Salvatore Sanfilippo
 * Copyright (C) 2013  Stam He
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to permit
 * persons to whom the Software is furnished to do so, subject to the
 * following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
 * NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 * ==========================================================================
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stddef.h>	/* NULL size_t */
#include <stdarg.h>	/* va_list va_start va_end */
#include <stdlib.h>	/* malloc(3) setenv(3) clearenv(3) setproctitle(3) getprogname(3) */
#include <stdio.h>	/* vsnprintf(3) snprintf(3) */

#include <string.h>	/* strlen(3) strchr(3) strdup(3) memset(3) memcpy(3) */

#include <errno.h>	/* errno program_invocation_name program_invocation_short_name */

#if !defined(HAVE_SETPROCTITLE)
#if (defined __NetBSD__ || defined __FreeBSD__ || defined __OpenBSD__ || defined __DragonFly__)
#define HAVE_SETPROCTITLE 1
#else
#define HAVE_SETPROCTITLE 0
#endif
#endif


#if !HAVE_SETPROCTITLE
#if (defined __linux || defined __APPLE__)

#ifdef __GLIBC__
#define HAVE_CLEARENV
#endif

// 环境变量
extern char **environ;

// SPT变量
static struct {
	// 原argv[0]
	/* original value */
	const char *arg0;

	// base = argv首地址
	// end = argv尾地址(扩展到env后)
	/* title space available */
	char *base, *end;

	// argv[0]尾地址(\0位置)
	 /* pointer to original nul character within base */
	char *nul;

	_Bool reset;
	int error;
} SPT;


#ifndef SPT_MIN
#define SPT_MIN(a, b) (((a) < (b))? (a) : (b))
#endif

static inline size_t spt_min(size_t a, size_t b) {
	return SPT_MIN(a, b);
} /* spt_min() */

/**
 * 清空env
 */
/*
 * For discussion on the portability of the various methods, see
 * http://lists.freebsd.org/pipermail/freebsd-stable/2008-June/043136.html
 */
int spt_clearenv(void) {
#ifdef HAVE_CLEARENV
	return clearenv();
#else
	extern char **environ;
	static char **tmp;

	// 分配新的内存
	if (!(tmp = malloc(sizeof *tmp)))
		return errno;

	tmp[0]  = NULL;
	environ = tmp;

	return 0;
#endif
} /* spt_clearenv() */

/**
 * 拷贝环境变量
 * 先使用envcopy保存environ指向的地址，
 * 调用spt_clearenv使environ为NULL，这时envcopy指向的内容依然有效，
 * 解析envcopy，调用setenv函数。
 * 
 * @param envc 环境变量数量
 * @param oldenv 环境变量
 * @return 0 成功
 */
static int spt_copyenv(int envc, char *oldenv[]) {
	extern char **environ;
	char **envcopy = NULL;
	char *eq;
	int i, error;
	int envsize;

	// 环境变量已经被修改 直接返回
	if (environ != oldenv)
		return 0;

	// 拷贝oldenv数组(仅数组)
	/* Copy environ into envcopy before clearing it. Shallow copy is
	 * enough as clearenv() only clears the environ array.
	 */
	envsize = (envc + 1) * sizeof(char *);
	envcopy = malloc(envsize);
	if (!envcopy)
		return ENOMEM;
	memcpy(envcopy, oldenv, envsize);

	// 清空SPT中的env
	/* Note that the state after clearenv() failure is undefined, but we'll
	 * just assume an error means it was left unchanged.
	 */
	if ((error = spt_clearenv())) {
		environ = oldenv;
		free(envcopy);
		return error;
	}

	/* Set environ from envcopy */
	for (i = 0; envcopy[i]; i++) {
		// 查找 =
		if (!(eq = strchr(envcopy[i], '=')))
			continue;

		*eq = '\0';
		// 设置env
		error = (0 != setenv(envcopy[i], eq + 1, 1))? errno : 0;
		*eq = '=';

		// 失败
		/* On error, do our best to restore state */
		if (error) {
#ifdef HAVE_CLEARENV
			/* We don't assume it is safe to free environ, so we
			 * may leak it. As clearenv() was shallow using envcopy
			 * here is safe.
			 */
			environ = envcopy;
#else
			free(envcopy);
			free(environ);  /* Safe to free, we have just alloc'd it */
			environ = oldenv;
#endif
			return error;
		}
	}

	free(envcopy);
	return 0;
} /* spt_copyenv() */

/**
 * 拷贝参数
 * 
 * @param argc 参数个数
 * @param argv 参数值
 * @return 0 成功
 */
static int spt_copyargs(int argc, char *argv[]) {
	char *tmp;
	int i;

	for (i = 1; i < argc || (i >= argc && argv[i]); i++) {
		if (!argv[i])
			continue;

		if (!(tmp = strdup(argv[i])))
			return errno;

		argv[i] = tmp;
	}

	return 0;
} /* spt_copyargs() */

/**
 * 初始化SPT
 * 
 * Linux进程名称是通过命令行参数argv[0]来标识。
 * Linux环境变量参数信息，表示进程执行需要的所有环境变量信息。通过全局变量Char **environ;可以访问环境变量。
 * 命令行参数argv和环境变量信息environ是在一块连续的内存中表示的，并且environ紧跟在argv后面。
 * 
 * 目的：
 * 将argv和environ相关的数据拷贝到新的内存中
 * 原内存用于设置新的进程名称
 * 
 * @param argc 命令行参数个数
 * @param argv 命令行参数
 */
/* Initialize and populate SPT to allow a future setproctitle()
 * call.
 *
 * As setproctitle() basically needs to overwrite argv[0], we're
 * trying to determine what is the largest contiguous block
 * starting at argv[0] we can use for this purpose.
 *
 * As this range will overwrite some or all of the argv and environ
 * strings, a deep copy of these two arrays is performed.
 */
void spt_init(int argc, char *argv[]) {
	// 环境变量指针
        char **envp = environ;
	char *base, *end, *nul, *tmp;
	int i, error, envc;

	// base指向argv[0]的指针
	if (!(base = argv[0]))
		return;
	// nul指向argv[0]的末尾\0
	/* We start with end pointing at the end of argv[0] */
	nul = &base[strlen(base)];
	end = nul + 1;

	// 尽可能向后扩展end
	// i < argc 表示 命令行参数
	// i >= argc && argv[i] 非命令行参数 但有指针 
	/* Attempt to extend end as far as we can, while making sure
	 * that the range between base and end is only allocated to
	 * argv, or anything that immediately follows argv (presumably
	 * envp).
	 */
	for (i = 0; i < argc || (i >= argc && argv[i]); i++) {
		if (!argv[i] || argv[i] < end)
			continue;

		if (end >= argv[i] && end <= argv[i] + strlen(argv[i]))
			end = argv[i] + strlen(argv[i]) + 1;
	}

	/* In case the envp array was not an immediate extension to argv,
	 * scan it explicitly.
	 */
	for (i = 0; envp[i]; i++) {
		if (envp[i] < end)
			continue;

		if (end >= envp[i] && end <= envp[i] + strlen(envp[i]))
			end = envp[i] + strlen(envp[i]) + 1;
	}
	// 环境变量数量
	envc = i;

	// 拷贝argv[0] 原进程名
	/* We're going to deep copy argv[], but argv[0] will still point to
	 * the old memory for the purpose of updating the title so we need
	 * to keep the original value elsewhere.
	 */
	if (!(SPT.arg0 = strdup(argv[0])))
		goto syerr;

#if __GLIBC__
	// program_invocation_name和program_invocation_short_name为glibc提供的取程序名全局变量
	// 拷贝program_invocation_name
	if (!(tmp = strdup(program_invocation_name)))
		goto syerr;

	// 修改program_invocation_name指向拷贝内存
	program_invocation_name = tmp;

	// 拷贝program_invocation_short_name
	if (!(tmp = strdup(program_invocation_short_name)))
		goto syerr;

	// 修改program_invocation_short_name指向拷贝的内存
	program_invocation_short_name = tmp;
#elif __APPLE__
	// 拷贝程序名称
	if (!(tmp = strdup(getprogname())))
		goto syerr;

	// 设置程序名称到新的内存
	setprogname(tmp);
#endif
    /* Now make a full deep copy of the environment and argv[] */
	// 拷贝环境变量
	if ((error = spt_copyenv(envc, envp)))
		goto error;

	// 拷贝参数 从argv[1]开始拷贝
	if ((error = spt_copyargs(argc, argv)))
		goto error;

	SPT.nul  = nul;
	SPT.base = base;
	SPT.end  = end;

	return;
syerr:
	error = errno;
error:
	SPT.error = error;
} /* spt_init() */


#ifndef SPT_MAXTITLE
#define SPT_MAXTITLE 255
#endif

/**
 * 设置进程名
 * 
 * @param fmt 格式
 * @param 参数
 */
void setproctitle(const char *fmt, ...) {
	char buf[SPT_MAXTITLE + 1]; /* use buffer in case argv[0] is passed */
	va_list ap;
	char *nul;
	int len, error;

	if (!SPT.base)
		return;

	// buf 进程名
	// len 进程名长度
	if (fmt) {
		// 格式化新进程名
		va_start(ap, fmt);
		len = vsnprintf(buf, sizeof buf, fmt, ap);
		va_end(ap);
	} else {
		// 使用原进程名
		len = snprintf(buf, sizeof buf, "%s", SPT.arg0);
	}

	if (len <= 0)
		{ error = errno; goto error; }

	// 重置SPT内存
	if (!SPT.reset) {
		// 未重置 则重置全部内存
		memset(SPT.base, 0, SPT.end - SPT.base);
		SPT.reset = 1;
	} else {
		// 已重置 则重置 min(buf大小, 原内存大小)
		memset(SPT.base, 0, spt_min(sizeof buf, SPT.end - SPT.base));
	}

	// 如果新进程名过长  则截断
	len = spt_min(len, spt_min(sizeof buf, SPT.end - SPT.base) - 1);
	// 拷贝内存
	memcpy(SPT.base, buf, len);
	// 获得新的\0位置
	nul = &SPT.base[len];

	// 添加末尾'.'字符
	if (nul < SPT.nul) {
		*SPT.nul = '.';
	} else if (nul == SPT.nul && &nul[1] < SPT.end) {
		*SPT.nul = ' ';
		*++nul = '\0';
	}

	return;
error:
	SPT.error = error;
} /* setproctitle() */


#endif /* __linux || __APPLE__ */
#endif /* !HAVE_SETPROCTITLE */

#ifdef SETPROCTITLE_TEST_MAIN
int main(int argc, char *argv[]) {
	spt_init(argc, argv);

	printf("SPT.arg0: [%p] '%s'\n", SPT.arg0, SPT.arg0);
	printf("SPT.base: [%p] '%s'\n", SPT.base, SPT.base);
	printf("SPT.end: [%p] (%d bytes after base)'\n", SPT.end, (int) (SPT.end - SPT.base));
	return 0;
}
#endif
