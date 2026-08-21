#include "config.h"

#include <string.h>

int tcp_server(char *buffer)
{
    (void)buffer;
    return -1;
}

int tcp_connect(char *buffer)
{
    (void)buffer;
    return -1;
}

int tcp_accept(int descriptor)
{
    (void)descriptor;
    return -1;
}

int gethostname(char *name, int length)
{
    DWORD size = (DWORD)length;
    return GetComputerNameA(name, &size) ? 0 : -1;
}
