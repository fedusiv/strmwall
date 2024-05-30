#ifndef __APP_MNGR_H__
#define __APP_MNGR_H__

#include <crc.h>
#include <common.h>

typedef struct {
  op_code_e op_code;
  uint8_t error;
  int data;
  double time;
}app_msg_t;

typedef struct app_node_t{
  app_msg_t* msg;
  struct app_node_t* next;
}app_node_t;

typedef struct{
  uint8_t* buf;
  int id;
}buf_request_t;

void* app_mngr_executor(void* data);
buf_request_t get_buf_operate(access_buf_e access);
void send_app_msg(app_msg_t* msg);
void init(char* filename, char* symbols);
int sniff_app_msg(void);
int get_app_msg(app_msg_t* msg);
void close_log(void);



#endif
