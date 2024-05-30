#include <app_mngr.h>
#include <bits/pthreadtypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <fcntl.h>

pthread_mutex_t mutex_buf, mutex_comm;

static char log_filename[FILENAME_SIZE];
static int log_socket;
static char log_message[LOG_ONE_PRINT_SIZE];
struct timeval start_time;
static app_node_t * root_node;
static uint8_t buffers[DATA_BUFS_AMOUNT][TCP_PACKET_SIZE];
static struct {
  int fill_id; // fill id is for udp
  int read_id; // read id is for tcp
}ring_buf_stat;

// Строки для вывода
const char* udp_error_messages[] = {
    [UDP_NO_ERR] = "Received msg",
    [UDP_ERR_PROCESS] = "Keep in process. critical.",
    [UDP_ERR_RECV] = "Error in receiving",
    [UDP_ERR_RECV_IGNOR] = "Received message will be ignored",
    [UDP_ERR_PCK_SIZE] = "Packet size error",
    [UDP_ERR_IP] = "Received from wrong IP",
    [UDP_ERR_PORT] = "Received from wrong PORT",
    [UDP_ERR_START_SEQ] = "Start sequence error",
    [UDP_ERR_CRC] = "CRC error",
    [UDP_ERR_IGNOR_START] = "Request to Ignore UDP messages",
    [UDP_ERR_IGNOR_END] = "Request to STOP ignoring udp messages",
};

const char* tcp_error_messages[] = {
    [TCP_NO_ERR] = "Transieved msg",
    [TCP_ERR_SOCKET] = "Can not create socket",
    [TCP_ERR_CONNECT_DONE] = "Connected",
    [TCP_ERR_CONNECT_FAIL] = "Connection fail",
    [TCP_ERR_KA] = "Keep Alive setup fail",
    [TCP_ERR_DISCONNECT] = "Disconnection",
    [TCP_ERR_SOCKET_TIMEOUT] = "Problem with setting socket timeout",
    [TCP_ERR_SEND] = "Send fail. Disconnection"
};

const char* source_error_messages[] = {
  [OP_CODE_UDP_STATUS] = "UDP",
  [OP_CODE_TCP_STATUS] = "TCP"
};

// Получение указателя на текущий буффер для взаимодействия 
buf_request_t get_buf_operate(access_buf_e access){
    buf_request_t res;
    pthread_mutex_lock(&mutex_buf); // blocking
    // Вначале УДП просит отправить только указатель, чтобы чем-то оперировать
    if(access == ACCESS_BUF_REQUEST){
        // requesting pointer to available for fill data.
        res.buf = buffers[ring_buf_stat.fill_id]+TCP_OVERHEAD_SIZE;
        res.id = ring_buf_stat.fill_id;
    }else if(access == ACCESS_BUF_FILL){
        // Но в как только он всё заполнил, то теперь можно и увеличить каунтер
        // mark, that data is filled and can be taken to tcp
        if(++ring_buf_stat.fill_id >= DATA_BUFS_AMOUNT){
        ring_buf_stat.fill_id = 0;
        }
    }else{
        // С ТСП проще, его задача догонять удп буффер и всё
        // Либо оповещать, что данных новых нет
        // give buff to tcp to send
        if(ring_buf_stat.fill_id != ring_buf_stat.read_id){
        res.buf = buffers[ring_buf_stat.read_id];
        res.id = ring_buf_stat.read_id;
        if(++ring_buf_stat.read_id>= DATA_BUFS_AMOUNT){
            ring_buf_stat.read_id= 0;
        }
        }else{
        // there is no available data to be sent 
        res.buf = NULL;
        }
    }
    pthread_mutex_unlock(&mutex_buf);

    return res;
}

// Я решил, что работа с лог файлом тоже должна быть немного с рекконектом, поэтому лог файл и работа с ним
void create_log_file(){
    log_socket= open(log_filename, O_CREAT | O_WRONLY| O_TRUNC | O_APPEND, 0644);
    if(log_socket == -1){
        printf("[LOGERROR]! Can not use file: %s\n", log_filename);
        perror("Failed to open log file");
    }
}
// Аккуратно закрыть перед концом
void close_log(){
    if(log_socket != -1){
        close(log_socket);
    }
}

void elapsed_time(double * elapsed){
    struct timeval current;
    gettimeofday(&current, NULL);
    *elapsed = (current.tv_sec - start_time.tv_sec) +
                (double)(current.tv_usec - start_time.tv_usec)
                / 1000000.0;
}

void init(char* filename, char* symbols){
    gettimeofday(&start_time, NULL);
    ring_buf_stat.fill_id = 0;
    ring_buf_stat.read_id = 0;
    root_node = malloc(sizeof(app_node_t));
    root_node->next=NULL;
    // Здесь просто перевожу имя файла в контекст app_mngr.c, чтобы он был ответственнен за него
    strcpy(log_filename, filename);
    create_log_file();
    // Заполнение указанными символами буферов.
    // Теперь тсп таск не будет каждый делать одно и тоже
    for (int i = 0; i < DATA_BUFS_AMOUNT; ++i) {
        memcpy(buffers[i], symbols, sizeof(uint8_t)*TCP_OVERHEAD_SIZE);
    }
}

void send_app_msg(app_msg_t* msg){
    app_msg_t * nmsg;
    app_node_t * cur_node;
    app_node_t * pri_node;
    // Это функция создания и помещения сообщения для коммуникации между потоками
    // Особо ничего хитрого
    nmsg = malloc(sizeof(app_msg_t));
    memcpy(nmsg, msg ,sizeof(app_msg_t));
    elapsed_time(&nmsg->time);

    pri_node = NULL;
    cur_node = root_node;
    pthread_mutex_lock(&mutex_comm);
    while(cur_node->next != NULL){
        if(cur_node->next->msg->op_code > msg->op_code){ // Это кусок для приоритета
            // current message in hight priority
            pri_node = cur_node->next;
            break;
        }
        cur_node = cur_node->next;
    }
    cur_node->next = malloc(sizeof(app_node_t));
    cur_node->next->msg = nmsg;
    cur_node->next->next= pri_node;
    pthread_mutex_unlock(&mutex_comm);
}

// Проверяет, нужно ли в удп отправить приказ о том, чтобы начать игнорить или возобновить отправку сообщений
void send_udp_order(tcp_error_e err){
    app_msg_t msg;
    udp_errors_e u_e;

    if(err == TCP_ERR_CONNECT_DONE){
        u_e = UDP_ERR_IGNOR_END;
    }else if(err == TCP_ERR_DISCONNECT || err == TCP_ERR_SEND){
        u_e = UDP_ERR_IGNOR_START;
    }else{
        return; // exit function
    }

    msg.op_code = OP_CODE_UDP_ORDER;
    msg.error = u_e;
    send_app_msg(&msg);

}
// Чтение, получение сообщений из очереди
// копирует сообщение в указанные контейнер и удаляет из очереди
int get_app_msg(app_msg_t* msg){
    int res = -1;
    app_node_t * cur_node;
    pthread_mutex_lock(&mutex_comm);
    cur_node = root_node->next;
    if(NULL != cur_node){
      // there is msg to proccess
      memcpy(msg,cur_node->msg,sizeof(app_msg_t)); // copy content
      free(cur_node->msg);  // free memory
      root_node->next = cur_node->next; // removed node from list
      free(cur_node); // free memory
      res = 0;
    }
    pthread_mutex_unlock(&mutex_comm);
    return res;
}
// Тут он только проверяет (нюхает), есть ли что-то для удп сервера, т.е. высокоприоритетное
// Понятно, что реализация и такая фукнция немного хардкод вариант для текущей задачи.
int sniff_app_msg(){
    int res = -1;
    app_node_t * cur_node;
    pthread_mutex_lock(&mutex_comm);
    cur_node = root_node->next;
    if(NULL != cur_node){
        if(cur_node->msg->op_code == OP_CODE_UDP_ORDER){
            res = 0;
        }
    }
    pthread_mutex_unlock(&mutex_comm);
    return res;
}
// Вывод, логирование
// Из хитрого тут только как это всё компануется в строку, а так, просто описание, того, как форматируется стриг
void log_status(app_msg_t* content){
    size_t offset, log_msg_size, remain;
    uint8_t buf_data[TCP_PACKET_SIZE];
    uint8_t* buf_pnt;
    uint8_t buf_len;
    const char** error_msg;

    if(content->op_code == OP_CODE_TCP_STATUS){
        error_msg = tcp_error_messages;
    }else{
        error_msg = udp_error_messages;
    }
    log_msg_size = sizeof(log_message);
    if(content->error!= 0){ // Все сообщения со статусами
        offset = snprintf(log_message,log_msg_size ,
            "[ %10.6f] %s: %s errno: %d\n\r", 
            content->time,
            source_error_messages[content->op_code],
            error_msg[content->error],
            content->data);
    }else{ // сообщения для которых используется контент из буфферов. Получение и отправка
        offset = snprintf(log_message,log_msg_size ,
            "[ %10.6f] %s: %s: ", 
            content->time,
            source_error_messages[content->op_code],
            error_msg[0]);
        pthread_mutex_lock(&mutex_buf);
        buf_len = *(buffers[content->data]+6);
        if(buf_len > MAX_PACKET_SIZE){
            buf_len = MAX_PACKET_SIZE;
        }
        memcpy(&buf_data, buffers[content->data], buf_len+TCP_OVERHEAD_SIZE);
        pthread_mutex_unlock(&mutex_buf);
        if(content->op_code == OP_CODE_TCP_STATUS){
            buf_pnt = buf_data;
            buf_len+=TCP_OVERHEAD_SIZE;
        }else{
            buf_pnt = buf_data+TCP_OVERHEAD_SIZE;
        }
        remain = log_msg_size - offset;
        for (size_t i = 0; i < buf_len; ++i) {
            offset += snprintf(log_message+ offset,remain, "%02X ", buf_pnt[i]);
            remain = log_msg_size - offset;
        }
        log_message[offset++]='\n';
        log_message[offset]='\0';
    }
    printf("%s",log_message); // это вывод в консоль. считаю, что он нужен. если нет, то одним удаление строки всё исчезнет

    if(log_socket == -1){
        // Если что-то не так с сокетом для файловой системы (открытый или созданный файл)
        // программа будет пытаться открыть файл снова
        create_log_file();
    }
    if(write(log_socket, log_message,offset) == -1){
        perror("Failed to log write to file");
        close(log_socket);
        create_log_file();
    }
}

void* app_mngr_executor(void* data){

    app_msg_t msg_content;
    while(1){
        if(sniff_app_msg() == 0){
            // there is order for udp for ignore or release ignore
            usleep(10);
            continue;   // let udp task to take it
        }

        if(get_app_msg(&msg_content)==-1){
          continue; // no msg
          usleep(10); // give sometime to more improtant threads
        }else{
          // we have content to parse
          switch (msg_content.op_code) {
            case OP_CODE_TCP_STATUS:
                send_udp_order(msg_content.error);
                log_status(&msg_content);
                break;
            case OP_CODE_UDP_STATUS:
              log_status(&msg_content);
              break;
            case OP_CODE_UDP_ORDER:
              break;
            default:
              break;
          }
        }
  }
  
}

