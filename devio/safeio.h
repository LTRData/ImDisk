#ifndef _SAFEIO_H
#define _SAFEIO_H

#ifdef __cplusplus
extern "C" {
#endif

int
safe_read(int fd, void *pdata, size_t size);

int
safe_write(int fd, const void *pdata, size_t size);

#ifdef __cplusplus
}
#endif

#endif
