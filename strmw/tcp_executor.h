#ifndef __TCP_EXECUTOR_H__
#define __TCP_EXECUTOR_H__

#include <common.h>

void* tcp_executor(void* data);
int parse_tcp_args(char *argv[]);

#endif // __TCP_EXECUTOR_H__