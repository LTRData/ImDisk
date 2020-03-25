#ifdef _WIN32

typedef int safeio_size_t;
typedef int safeio_ssize_t;
typedef __int64 off_t_64;

#define ULL_FMT       "%I64u"
#define SLL_FMT       "%I64i"

#else

typedef size_t safeio_size_t;
typedef ssize_t safeio_ssize_t;
typedef off_t off_t_64;
typedef int SOCKET;

#define ULL_FMT   "%llu"
#define SLL_FMT   "%lli"

#define INVALID_SOCKET (-1)

#define _lseeki64 lseek
#define stricmp strcasecmp
#define strnicmp strncasecmp

#ifndef __cdecl
#define __cdecl
#endif

#endif
