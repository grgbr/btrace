#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <inttypes.h>
#include <unistd.h>
#include <limits.h>
#include <signal.h>
#include <execinfo.h>
#include <string.h>
#include <elfutils/libdwfl.h>
#include <errno.h>

#define CONFIG_BACKTRACE_NR (256U)

static int                   btrace_pid;
static Dwfl                 *btrace_dwarf;
static void                 *btrace_frames[CONFIG_BACKTRACE_NR];
static char                  btrace_line[LINE_MAX];
static char                 *btrace_debug_info;
static const Dwfl_Callbacks  btrace_dwarf_callbacks = {
  .find_debuginfo = dwfl_standard_find_debuginfo,
  .find_elf       = dwfl_linux_proc_find_elf,
  .debuginfo_path = &btrace_debug_info
};

static void
btrace_write(const char *msg, size_t count)
{
	do {
		ssize_t ret;

		ret = write(STDERR_FILENO, msg, count);
		if (ret < 0) {
			if (errno != EAGAIN)
				return;
		}
		else {
			msg += ret;
			count -= ret;
		}
	} while (count);
}

static void
btrace_dump(void)
{
	const char *msg;
	int         nr;
	int         f;

	msg = "[fatal]btrace: dump\n";
	btrace_write(msg, strlen(msg));

	if (dwfl_linux_proc_report(btrace_dwarf, btrace_pid)) {
		fprintf(stderr,
		        "[fatal]btrace: process reporting failed: %s\n",
		        dwfl_errmsg(-1));
		return;
	}
	dwfl_report_end(btrace_dwarf, NULL, NULL);

	nr = backtrace(btrace_frames, CONFIG_BACKTRACE_NR);
	f = nr;
	while (f--) {
		GElf_Addr    addr = (uintptr_t)btrace_frames[f];
		Dwfl_Module *mod;             /* module, i.e. library, binary,
		                                 ... */
		const char  *mod_name = NULL; /* module name */
		GElf_Sym     sym;             /* symbol descriptor. */
		const char  *sym_name = NULL; /* symbol name */
		GElf_Off     off;             /* offset from symbol's first
		                                 instruction address. */
		Dwfl_Line   *line;            /* source infos related to
		                                 address. */
		int          len;

		mod = dwfl_addrmodule(btrace_dwarf, addr);
		if (mod) {
			mod_name = dwfl_module_info(mod, NULL, NULL, NULL, NULL,
			                            NULL, NULL, NULL);
			sym_name = dwfl_module_addrinfo(mod, addr, &off, &sym,
			                                NULL, NULL, NULL);
		}

		if (sym_name)
			len = snprintf(btrace_line,
			               sizeof(btrace_line),
			               "%3u#  [%p]  %s+%#" PRIx64 "\n"
			               "      %*s  %s+%#" PRIx64 "\n",
			               nr - (f + 1),
			               (void *)((uintptr_t)addr),
			               sym_name,
			               off,
			               (__WORDSIZE / CHAR_BIT) * 2,
			               " ",
			               mod_name,
			               sym.st_value);
		else if (mod_name)
			len = snprintf(btrace_line,
			               sizeof(btrace_line),
			               "%3u#  [%p]\n"
			               "      %*s  %s\n",
			               nr - (f + 1),
			               (void *)((uintptr_t)addr),
			               (__WORDSIZE / CHAR_BIT) * 2,
			               " ",
			               mod_name);
		else
			len = snprintf(btrace_line,
			               sizeof(btrace_line),
			               "%3u#  [%p]\n",
			               nr - (f + 1),
			               (void *)((uintptr_t)addr));

		/*
		 * Lets throw in some source/line info if we can find it.
		 * Note: safely ignored if mod == NULL.
		 */
		line = dwfl_module_getsrc(mod, addr);
		if (line) {
			int         lnno; /* line number within source file. */
			const char *path; /* source file path */

			path = dwfl_lineinfo(line, NULL, &lnno, NULL, NULL,
			                     NULL);
			if (path)
				len += snprintf(&btrace_line[len],
				                sizeof(btrace_line) - len,
				                "      %*s  %s:%d\n",
				                (__WORDSIZE / CHAR_BIT) * 2,
				                " ",
				                path,
				                lnno);
		}

		btrace_write(btrace_line, len);
	}

	dwfl_end(btrace_dwarf);
}

static void
btrace_handle_fault(int signal)
{
	const char  prolog[] = "[fatal]btrace: received ";
	const char *sigstr;
	bool        exit;

	/*
	 * We cannot use strsignal() here since it is not async signal safe. See
	 * «Async-signal-safe functions» section of signal(7) man page.
	 */
	switch (signal) {
	case SIGSEGV:
		sigstr = "Segmentation fault signal (11)\n";
		exit = true;
		break;

	case SIGABRT:
		/*
		 * As stated in abort(3) man page, abort() first unblocks the
		 * SIGABRT signal, and then raises that signal for the calling
		 * process. This results in the abnormal termination of the
		 * process unless the SIGABRT signal is caught and the signal
		 * handler does not return (see longjmp(3)).  As a consequence,
		 * there is no need for explicit exit.
		 */
		sigstr = "Abort signal (6)\n";
		exit = false;
		break;

	default:
		sigstr = "unknown";
		exit = true;
	}

	fsync(STDOUT_FILENO);
	fsync(STDERR_FILENO);

	btrace_write(prolog, sizeof(prolog) - 1);
	btrace_write(sigstr, strlen(sigstr));

	btrace_dump();

	fsync(STDERR_FILENO);

	if (exit)
		_exit(EXIT_FAILURE);
}

static void __attribute__((constructor))
btrace_init(void)
{
	btrace_pid = getpid();

	btrace_dwarf = dwfl_begin(&btrace_dwarf_callbacks);
	if (!btrace_dwarf) {
		fprintf(stderr,
		        "[warning]btrace: DWARF initialization failed: %s\n",
		        dwfl_errmsg(-1));
		return;
	}

	signal(SIGSEGV, btrace_handle_fault);
	signal(SIGABRT, btrace_handle_fault);
}
