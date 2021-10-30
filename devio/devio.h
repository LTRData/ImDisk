#define DEVIO_VERSION "3.10"

#define MULTI_CONTAINER_DELIMITER ":::"

typedef safeio_ssize_t(__cdecl dllread_decl)(void *handle,
    void *buf,
    safeio_size_t size,
    off_t_64 offset);

typedef dllread_decl *dllread_proc;

typedef safeio_ssize_t(__cdecl dllwrite_decl)(void *handle,
    void *buf,
    safeio_size_t size,
    off_t_64 offset);

typedef dllwrite_decl *dllwrite_proc;

typedef int(__cdecl dllclose_decl)(void *handle);

typedef dllclose_decl *dllclose_proc;

typedef void * (__cdecl dllopen_decl)(const char *file,
    int read_only,
    dllread_proc *dllread,
    dllwrite_proc *dllwrite,
    dllclose_proc *dllclose,
    off_t_64 *size);

typedef dllopen_decl *dllopen_proc;
