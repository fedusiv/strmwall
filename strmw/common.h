#ifndef __COMMON_H__
#define __COMMON_H__

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <errno.h>

// Постарлся максимально сложить все числа в единое место с описанием

#define MAX_PACKET_SIZE 128
#define MIN_PACKET_SIZE 16
#define DATA_BUFS_AMOUNT 50 // amount of buffers to store packets for udp and tcp
#define APP_MESSAGES_AMOUNT 40 // amout of application messages
#define PACKET_START_SEQUENCE_SIZE 2
#define PACKET_START_SEQUENCE 0xBEEF
#define PACKET_CRC_SIZE 2
#define PACKET_LENGTH_DATA_SIZE 1
#define PACKET_META_SIZE PACKET_START_SEQUENCE_SIZE + PACKET_CRC_SIZE + PACKET_LENGTH_DATA_SIZE
#define UDP_RECIEVE_TIMEOUT 2 // seconds
#define UDP_BIND_PORT 53480 // порт по которому юдп производит отправку

#define TCP_RECEIVE_TIMEOUT 100// milliseconds
#define TCP_OVERHEAD_SIZE 4 // amount of bytes for symbol to insert before udp payload data
#define TCP_PACKET_SIZE MAX_PACKET_SIZE + TCP_OVERHEAD_SIZE
#define TCP_RECCONNECT_TIMEOUT 2 // in seconds
#define TCP_THREAD_SLEEP 100 // миллисекунды

#define LOG_ONE_PRINT_SIZE 512
#define FILENAME_SIZE 256

// Опкоды для сообщений между тредами
typedef enum{
  OP_CODE_UDP_ORDER,
  OP_CODE_UDP_STATUS,
  OP_CODE_TCP_STATUS,
}op_code_e;

typedef enum{
  ACCESS_BUF_REQUEST,
  ACCESS_BUF_FILL,
  ACCESS_BUF_SEND
}access_buf_e;

typedef enum{
  UDP_NO_ERR = 0,
  UDP_ERR_PROCESS,
  UDP_ERR_RECV,
  UDP_ERR_RECV_IGNOR,
  UDP_ERR_PCK_SIZE,
  UDP_ERR_IP,
  UDP_ERR_PORT,
  UDP_ERR_START_SEQ,
  UDP_ERR_CRC,
  UDP_ERR_IGNOR_START,
  UDP_ERR_IGNOR_END,
  UDP_ERR_MAX = 255
}udp_errors_e;

typedef enum{
    TCP_NO_ERR = 0,
    TCP_ERR_SOCKET,
    TCP_ERR_CONNECT_DONE,
    TCP_ERR_CONNECT_FAIL,
    TCP_ERR_KA,
    TCP_ERR_SOCKET_TIMEOUT,
    TCP_ERR_DISCONNECT,
    TCP_ERR_SEND,
    TCP_ERR_MAX = 255
}tcp_error_e;

#endif // __COMMON_H__