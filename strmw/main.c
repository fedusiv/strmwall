#include <signal.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <common.h>
#include <app_mngr.h>
#include <tcp_executor.h>

struct sockaddr_in udp_sock_addr;
pthread_t udp_thread, tcp_thread, app_mngr_thread;

const char app_run_example[] = " \
        Usage:\n \
        <udp_ip>:<udp_port>\n \
        <tcp_ip>:tcp_port>\n \
        <log_file_path> (to create to use)\n \
        <4 symbols to attach for tcp>\n \
        Example:\n \
        ./app 127.0.0.1:34555 127.0.0.1:54322 /home/if/log1.file 1235";

// Проверка пакета на пригодность
udp_errors_e datagram_verification(uint8_t *data){
    /*
     Структура пакета 
     байты 0 - 1 - старт слово, комбинация из двух байт
     байт 2 - длина отправляемого пакета. так как максимум 128 байт, то одного байта, чтобы хранить длинну достаточно
     байт 3 и далее до двух последних - это данные
     последний и предпослений байт - это crc16_ccit
    */

    /*
        В задаче не было указано напрямую, но проверка на пригодность пакету нужна.
        Поэтому были решение сделать простую схему.
        Стартовое слово, длинна пакета и срс в конце, чтобы проверить, что всё корректно
    */
    uint16_t start_sequence, crc;
    uint8_t len;

    /*
        Как видно, здесь используется напрямую работа с байтами
        Можно было бы завести структуру и приводить указатель буфера к ней и проверять.
        Но так как структура пакета не сложная, мне было быстрее и проще реализовать именно так

        P.S. Это единственная часть кода, где много захардкоженых чисел, совершенно этим не горжусь
    */
    start_sequence = data[0] << 8 | data[1];
    if (start_sequence != PACKET_START_SEQUENCE){
        return UDP_ERR_START_SEQ;
    }
    len = data[2];
    crc = data[len - 2] << 8 | data[len - 1];
    len -= PACKET_META_SIZE;
    if (crc != crc16_ccitt(&data[3], len)){
        return UDP_ERR_CRC;
    }

    return UDP_NO_ERR;
}

void *udp_executor(void *data){
    int udp_socket;
    struct timeval timeout;
    struct sockaddr_in bind_addr, recv_addr;
    socklen_t sock_len;
    size_t buf_len;
    int recv_len;
    buf_request_t buf_info; // информация о буфере с которым оперировать
    udp_errors_e udp_res;   // результат последней проведённой операции
    app_msg_t msg;
    uint8_t ignore; // флаг показывающий требуется игнор полученных сообщений или нет

    msg.op_code = OP_CODE_UDP_STATUS; // always will send udp status
    udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_socket < 0){
        pthread_exit(NULL);
    }

    // Установить таймаут для сокета
    timeout.tv_sec = UDP_RECIEVE_TIMEOUT; // секунды
    timeout.tv_usec = 0; // миллисекуyнды
    if (setsockopt(udp_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        perror("udp socket timeout failed");
        close(udp_socket);
        pthread_exit(NULL);
    }

    memset(&bind_addr, 0, sizeof(bind_addr));
    memset(&recv_addr, 0, sizeof(recv_addr));

    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind_addr.sin_port = htons(UDP_BIND_PORT);

    if (bind(udp_socket, (const struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0){
        perror("udp_socket bind failed");
        pthread_exit(NULL);
    }
    sock_len = sizeof(recv_addr);
    char ip_str[256];
    udp_res = UDP_NO_ERR;
    ignore = 1; // by default ignore. when tcp will connect ignore will be removed
    /*
        Далее идёт основной цикл для юдп.
        Основное в юдп - это чтение.
        Если что-то читается, то дальше понять можно ли читать, приемлимый ли контент и по результату оповестить в лог
        Так как сокет с таймаутом, что чтение может быть пустое, это случай, когда никакой реакции не требуется.
        Сокет с таймаутом, чтобы реакция на отключение тсп коннект и включение игнора было быстрее.
    */
    while (1){
        if(udp_res == UDP_ERR_PROCESS){
            udp_res = UDP_ERR_PROCESS; // mark, that in the proccess
            // basically do nothing, repeat cycle
        }else if (udp_res != UDP_NO_ERR){
            // report about problem
            msg.data = errno;
            msg.error = udp_res;
            send_app_msg(&msg);
        }else{  // UDP_NO_ERR
            // запрос на получение указателя на новый буфер куда складывать данные
            buf_info = get_buf_operate(ACCESS_BUF_REQUEST);
        }
        udp_res = UDP_ERR_PROCESS; // mark, that in the proccess
        recv_len = recvfrom(udp_socket, buf_info.buf, MAX_PACKET_SIZE+1, 0, (struct sockaddr *)&recv_addr, &sock_len);
        if(sniff_app_msg() == 0){
            // there is special order for udp task
            get_app_msg(&msg);
            if(msg.error == UDP_ERR_IGNOR_START){
                ignore = 1;
            }
            if(msg.error == UDP_ERR_IGNOR_END){
                ignore = 0;
            }
            msg.op_code = OP_CODE_UDP_STATUS;
            msg.data = 0;
            send_app_msg(&msg); // notify about request
        }
        if (recv_len < 0){
            if(errno != EAGAIN){    // if it's timeout issue we can ignore
                udp_res = UDP_ERR_RECV;
            }
            continue;
        }
        if(ignore){
            udp_res = UDP_ERR_RECV_IGNOR;
            continue;
        }
        if (recv_len < MIN_PACKET_SIZE || recv_len > MAX_PACKET_SIZE){
            udp_res = UDP_ERR_PCK_SIZE;
            continue;
        }
        if (recv_addr.sin_addr.s_addr != udp_sock_addr.sin_addr.s_addr){
            udp_res = UDP_ERR_IP;
            continue;
        }
        if (ntohs(recv_addr.sin_port) != ntohs(udp_sock_addr.sin_port)){
            udp_res = UDP_ERR_PORT;
            continue;
        }
        udp_res = datagram_verification(buf_info.buf);
        if (udp_res == UDP_NO_ERR){
            get_buf_operate(ACCESS_BUF_FILL); // mark, that buffer can be used
            msg.data = buf_info.id;
            msg.error = udp_res;
            // report about success
            send_app_msg(&msg);
        }
    }

    close(udp_socket);
    pthread_exit(NULL);
    return data;
}

int run_executors(){
    pthread_create(&udp_thread, NULL, udp_executor, NULL);
    pthread_create(&tcp_thread, NULL, tcp_executor, NULL);
    pthread_create(&app_mngr_thread, NULL, app_mngr_executor, NULL);

    pthread_join(udp_thread, NULL);
    pthread_join(tcp_thread, NULL);
    pthread_join(app_mngr_thread, NULL);

    return 0;
}

int parse_udp_args(char *argv[]){
    char *udp_addr, *udp_ip, *udp_port;
    int port_num;

    udp_addr = argv[1];
    udp_ip = strtok(udp_addr, ":");
    udp_port = strtok(NULL, ":");

    if (NULL == udp_ip){
        printf("Entered wrong UDP Addr format\n");
        return 1;
    }
    if (inet_pton(AF_INET, udp_ip, &udp_sock_addr.sin_addr) != 1){
        printf("Non valid UDP Addr\n");
        return 1;
    }
    if (NULL == udp_port){
        printf("Entered wrong UDP Addr format for PORT\n");
        return 1;
    }
    port_num = atoi(udp_port);
    if(port_num == 0){
        printf("Non valid UDP Port\n");
        return 1;
    }
    udp_sock_addr.sin_port = ntohs(port_num);

    printf("UDP Addr: %s:%s\n", udp_ip, udp_port);

    return 0;
}

int parse_log_file(char *argv[]){
    int fd;
    int res;
    if (strlen(argv[3]) >= FILENAME_SIZE){
        printf("Filename is too big. Try to find in someother place");
        return 1;
    }
    fd = open(argv[3], O_CREAT | O_WRONLY | O_TRUNC, 0644); // Юзер может читать/писать, остальные только читать
    if (fd != -1){
        res = 0;
        printf("Log file %s is taken successfully Content will be overwritten.\n", argv[3]);
    }else{
        res = 1;
        printf("Failed to use file %s\n", argv[3]);
        perror("Failed");
    }

    if (fd != -1){
        close(fd);
    }
    return res;
}

int parse_symbols(char *argv[]){
    int len;
    int res;

    len = strlen(argv[4]);
    if (len != TCP_OVERHEAD_SIZE){
        printf("Amount of symbols need to be equal %d", TCP_OVERHEAD_SIZE);
        res = 1;
    }else{
        res = 0;
    }
    return res;
}

int parse_args(int argc, char *argv[]){
    if (argc != 5){
        printf("no required amount of args provided. exit app\n");
        return 1;
    }
    if (parse_udp_args(argv)){
        printf("Problem with UDP args. exit app\n");
        return 1;
    }
    if (parse_tcp_args(argv)){
        printf("Problem with TCP args. exit app\n");
        return 1;
    }
    if (parse_log_file(argv)){
        printf("Problems with log file args. exit app\n");
        return 1;
    }
    if (parse_symbols(argv)){
        printf("Problems with symbols  args. exit app\n");
        return 1;
    }

    return 0;
}

void sigint_handler(int sig){
    close_log();
    printf("Ctrl+C captured. Exit app\n");
    exit(1);
}

int main(int argc, char *argv[]){
    signal(SIGPIPE, SIG_IGN); // сигнал, чтобы самому обрабатывать ошибку отправки для тсп
    signal(SIGINT, sigint_handler); // самому обрабатывать ctrl c
    if(parse_args(argc, argv)){ // парсинг аргументов, фактически проверка их§
        printf("%s\n", app_run_example);
        exit(1);
    }
    init(argv[3], argv[4]); // вызов инициализации, аллокация необходимой памяти
    run_executors(); // запуск тредов
}
