# Тестовое задание
tg: @fedusiv

Постарался оставить как можно больше комментариев, чтобы объяснить почему использовал именно такое решение.
По ходу разработки я писал на английском комменты, уже перед отправкой вносил на русском объяснения.

## Как билдить
чтобы сбилдить достаточно запустить
./build.sh
всё на симэйке, должно сработать (у меня сработало)
Если надо будет (надеюсь нет)
./build.sh -c - компиляция (удобно мне,пользовался, чтобы не перекомпилировать каждый раз)
./build.sh -d - сбилдить для дебага

билдиться два проекта. udp_test (билдится вместе с основной прогой) - я написал для себя небольшую программу, чтобы отправлять данные.
spam.sh тоже для этого. udp_test бинарник после билда будет лежать в udp_test директории


## как запустить
удп и тсп идут в формате айпи:порт (так было в задании написано)
1. udp ip:port откуда ждать сообщение
2. tcp ip:port куда отправлять сообщения
3. путь к файлу для логов
4. 4 символа
   Usage:\n \
        <udp_ip>:<udp_port>\n \
        <tcp_ip>:tcp_port>\n \
        <log_file_path> (to create to use)\n \
        <4 symbols to attach for tcp>\n \
        Example:\n \
        ./app 127.0.0.1:34555 127.0.0.1:54322 /home/if/log1.file 1235";
сам удп биндится на порт 53480

для тсп сервера я использовал ncat -l 54322 | hexdump -C
для отправки удп пакетов использовал ./udp_test 5 127.0.0.1 53480 34555
udp_test не имеет проверку на аргументы, поэтому тут важно жестко вводить как в примере.
<test_id> <ip> <port_to> <port_from>
./spam.sh <число> просто запускает указанное количество раз
там необходимо в ручную менять порты и прочее, указывается только кол-во раз запустить


## Про архитектура
Хочу сказать про основную архитектуру. Имеется три таска: юдп, тсп и менеджер.
Они обмениваются через сообщения, которые хранятся в связанном списке, хранятся там по приоритету.
ТСП и УДП таски отправляют состояния в менеджер, тот решает что с ними делать, в основном он просто их логирует, но когда дисконнет у тсп, формирует команда для удп таска, для включения игнора.
Следующим важным моментом стоит отметить буферы данных. Я решил, что сразу аллоцирую буферы данных и в первые четыре байта впишу последовательность вводимых байт. Небольшое сохранение времени.
Так же массив этих буфферов, работает как кольцевой буффер, юдп заполняет, как только он заполнил, айди у кольцевого буфера увеличивается на один и тсп может брать его в оборот.
На основную функцию удп и тсп таск не имеют буферов, только те, общие, которые уже аллоцированы.
У ТСП только есть буфер для чтения. Как бы тсп не игнорировал данные, читать их необходимо, в случае, если сервер будет что-то отправлять, чтобы у нас внутренние штуки не переполнились.

ТСП игнорирует принимаемые данные. Т.е. он может работать с сервером, который отправляет и который молчит.
Нужно реализовать и то и то. Основная проблема для меня - это определение коннекта. Обычно я использовал бы самый простой способ - хартбит. Но у нас нет общения. Поэтому сделал следующие:
1. Включил кипэлайв для сокета и установил минмальное время. Это не самый действенный вариант, так как в этом плане кипэлайв имеет ограничения, которые есть в системе (в линуксе)
2. Проверка коннект, через getsockopt.
3. Самый действенный вариант recv.
Установив таймаут на сокет и делать recv. Тогда у нас будет сон, чтобы делать контекст свич для других тасков, а так же, если мы получаем 0 при recv, соединение значит разорвано.

Решил не делать всё в одном файле, разделил немного по модулям, кроме удп. удп оставил в мэйне, там его не много, не мешается для чтения. (надеюсь)

Некоторые функции не декларировал отдельно, составлено, так что либо функция используется внешне, поэтому есть декларация, а для внутреннего пользования в файле, сохраняется структура, что можно и без этого.
