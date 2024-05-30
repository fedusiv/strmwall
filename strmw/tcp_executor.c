#include <tcp_executor.h>
#include <app_mngr.h>

struct sockaddr_in tcp_dest_addr;

int tcp_socket_timeot(int sockfd){
    struct timeval timeout;
    timeout.tv_sec = 0; // секунды
    timeout.tv_usec = TCP_RECEIVE_TIMEOUT; // миллисекугды
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        return -1;
    }
    return 0;
}
// Как и писал несколько методов проверки соединения
// Кип Элайв
int tcp_ka_config(int sockfd){
    int optval;
    socklen_t optlen = sizeof(optval);
    // Enable TCP keepalive
    optval = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, &optval, optlen) < 0){
        return -1;
    }

    // Set keepalive interval (time between probes)
    optval = 1; // 1 seconds
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPINTVL, &optval, optlen) < 0){
        return -1;
    }

    // Set keepalive probe count (number of probes before declaring the connection dead)
    optval = 3; // 1 probes
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPCNT, &optval, optlen) < 0){
        return -1;
    }

    // Set keepalive time (idle time before sending probes)
    optval = 1; // 1 seconds
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPIDLE, &optval, optlen) < 0){
        return -1;
    }
    return 0;
}
// проверка через getsockopt
int tcp_is_connected(int sockfd){
    int error = 0;
    int res;

    socklen_t len = sizeof(error);
    res = getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &len);
    if (res != 0 || error != 0){
        return -1;
    }
    return 0;
}
// Функия реконнект или создания коннекта
int tcp_connect(){
    int sockfd;
    int res;

    app_msg_t msg;
    msg.op_code = OP_CODE_TCP_STATUS;

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        msg.error = TCP_ERR_SOCKET;
        res = -1;
        goto tcp_connect_finilize;
    }

    if (connect(sockfd, (struct sockaddr *)&tcp_dest_addr, sizeof(tcp_dest_addr)) < 0){
        res = -1;
        msg.error = TCP_ERR_CONNECT_FAIL;
        goto tcp_connect_finilize;
    }

    if( tcp_ka_config(sockfd) == -1){
        res = -1;
        msg.error = TCP_ERR_KA;
        goto tcp_connect_finilize;
    }

    if(tcp_socket_timeot(sockfd) == -1)
    {
        res = -1;
        msg.error = TCP_ERR_KA;
        goto tcp_connect_finilize;
    }
    res = 0;
// Да здесь goto, решил, что ничего здесь с этим страшного нет.
tcp_connect_finilize:
    msg.data = errno;
    if(res == -1){
        if(sockfd == -1){
            close(sockfd);
        }
    }else{
        msg.error = TCP_ERR_CONNECT_DONE;
        msg.data = 0;
        res = sockfd;
    }
    send_app_msg(&msg);
    return res;
}

void *tcp_executor(void *data){
    int sockfd;
    buf_request_t buf_info;
    int send_len;
    int send_res;
    app_msg_t msg;
    uint8_t read_buffer[MAX_PACKET_SIZE];
    int recv_len;
    
    msg.op_code = OP_CODE_TCP_STATUS;

    sockfd = -1; // mark, that there is no connection in the beginning
    while (1){
        // Коннект. Функция, которая пытается сделать коннект к серверу
        if(sockfd == -1){
            while ((sockfd = tcp_connect()) < 0){
                sleep(TCP_RECCONNECT_TIMEOUT);
            }
        }

        buf_info = get_buf_operate(ACCESS_BUF_SEND); // Узнать, если бы буфер, требующий отправки

        // Дальше код - это детекция коннекта.
        // Если нам ничего не отправить, значит мы не можем понять через отправку, что происходит,
        // тогда проверяет через getsockopt, потом засыпаем на время таймаут для получения данных
        // и по итогу получаем информацию о состоянии соединения
        if(NULL == buf_info.buf){
            if(tcp_is_connected(sockfd)){
                sockfd = -1; // no connection to server
            }else{
                recv_len = recv(sockfd, read_buffer, MAX_PACKET_SIZE - 1, 0); // it works as sleep and also clear all incoming trash
                if(recv_len == 0){
                    // connection is closed. need to execute reconnect
                    sockfd = -1;
                }
            }
            if(sockfd == -1){
                // detected disconnect. report about it
                msg.error = TCP_ERR_DISCONNECT;
                msg.data = errno;
                send_app_msg(&msg);
            }
            continue;
        }
        // Функция отправки данных
        // Здесь своя система детекции отключения
        send_len = *(buf_info.buf + TCP_OVERHEAD_SIZE + PACKET_START_SEQUENCE_SIZE) + TCP_OVERHEAD_SIZE;
        send_res = write(sockfd, buf_info.buf, send_len);
        if(send_res < 0){
            msg.error = TCP_ERR_SEND;
            msg.data = errno;
            close(sockfd);
            sockfd = -1;
        }else{
            msg.error = TCP_NO_ERR;
            msg.data = buf_info.id;
        }
        send_app_msg(&msg);
    }

    close(sockfd);
    pthread_exit(NULL);
}

int parse_tcp_args(char *argv[])
{
    char *tcp_addr, *tcp_ip, *tcp_port;
    int port_num;

    tcp_addr = argv[2];
    tcp_ip = strtok(tcp_addr, ":");
    tcp_port = strtok(NULL, ":");

    if(NULL == tcp_ip){
        printf("Entered wrong TCP Addr format\n");
        return 1;
    }
    if(inet_pton(AF_INET, tcp_ip, &tcp_dest_addr.sin_addr) != 1){
        printf("Non valid TCP Addr\n");
        return 1;
    }
    if(NULL == tcp_port){
        printf("Entered wrong TCP Addr format for PORT\n");
        return 1;
    }
    port_num = atoi(tcp_port);
    if(port_num == 0){
        printf("Non valid TCP Port\n");
        return 1;
    }
    tcp_dest_addr.sin_port = ntohs(port_num);
    tcp_dest_addr.sin_family = AF_INET;

    printf("TCP Addr: %s:%s\n", tcp_ip, tcp_port);

    return 0;
}
