#pragma once

#define STDC_HEADERS 1
#define HAVE_LOCALE_H 1
#define HAVE_SETLOCALE 1
#define HAVE_LIMITS_H 1
#define HAVE_STRCHR 1
#define HAVE_MEMCPY 1
#define HAVE_STRING_H 1
#define HAVE_STRERROR 1
#define HAVE_ERRNO_DECLARATION 1
#define ENABLE_NLS 0
#define NFGVMIN 1
#define RETSIGTYPE void
#define PACKAGE "lrzsz"
#define VERSION "0.12.20"
#define LOCALEDIR ""
#define LRZSZ_ATTRIB_UNUSED
#define LRZSZ_ATTRIB_REGPARM(x)
#define LRZSZ_ATTRIB_SECTION(x)
#define LRZSZ_ATTRIB_NORET
#define LRZSZ_ATTRIB_CONST
#define LRZSZ_ATTRIB_PRINTF(x,y)

#ifdef _MSC_VER
#include <windows.h>
#include <io.h>
#include <process.h>
#define alloca _alloca
typedef unsigned short mode_t;
typedef unsigned long speed_t;
typedef int pid_t;
#define read _read
#define write _write
#define close _close
#define unlink _unlink
#define lstat _stat
#define S_ISLNK(mode) 0
#define popen _popen
#define pclose _pclose
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#define getuid() 0
#define geteuid() 0
#define sleep(seconds) Sleep((seconds) * 1000)
#define SIGALRM 14
#define SIGQUIT 3
#define SIGPIPE 13
#define SIGHUP 1
#define stat _stat
#define fstat _fstat
#define fileno _fileno
#define O_WRONLY _O_WRONLY
#define O_CREAT _O_CREAT
#define O_EXCL _O_EXCL
#define S_IFMT _S_IFMT
#define S_IFDIR _S_IFDIR
#define S_IFREG _S_IFREG
#define S_IFCHR _S_IFCHR
#define S_IFIFO _S_IFIFO
#define S_IFBLK 0
#define alarm(seconds) 0
#elif defined(_WIN32)
/* MinGW already provides POSIX-compatible pid_t/mode_t/stat/read/write/etc.
 * via its own headers, unlike MSVC. Only the handful of POSIX signals, the
 * termios speed_t type, and symlink-aware lstat are genuinely missing.
 * WIN32_LEAN_AND_MEAN keeps windows.h from pulling in the legacy winsock1
 * header, whose dllimport'd gethostname() would otherwise clash with the
 * plain gethostname() this project defines in windows_stubs.c. */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
typedef unsigned long speed_t;
#define lstat _stat
#define S_ISLNK(mode) 0
#define getuid() 0
#define geteuid() 0
#define sleep(seconds) Sleep((seconds) * 1000)
#define SIGALRM 14
#define SIGQUIT 3
#define SIGPIPE 13
#define SIGHUP 1
#define alarm(seconds) 0
#endif
