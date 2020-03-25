#include "imdsksys.h"

void * __CRTDECL operator new(size_t Size)
{
    return operator_new(Size, 0);
}

void * __CRTDECL operator new[](size_t Size)
{
    return operator_new(Size, 0);
}

void * __CRTDECL operator new(size_t Size, UCHAR FillByte)
{
    return operator_new(Size, FillByte);
}

void __CRTDECL operator delete(void * Ptr)
{
    operator_delete(Ptr);
}

void __CRTDECL operator delete(void * Ptr, size_t)
{
    operator_delete(Ptr);
}

void __CRTDECL operator delete[](void * Ptr)
{
    operator_delete(Ptr);
}

