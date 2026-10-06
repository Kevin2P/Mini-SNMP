/*
 * net.c: implementacao dos sockets TCP e da E/S por linha usados pelo
 * protocolo MSMP. Sem estado proprio; cada funcao recebe o descritor com
 * que deve trabalhar. Usado tanto pelo agente (net_listen, send_line,
 * recv_line) quanto pelo gerente (net_connect, send_line, recv_line).
 */
#include "net.h"
#include "common.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* Cria e vincula o soquete de escuta (ver net.h). getaddrinfo() com
 * AI_PASSIVE resolve para "todas as interfaces"; percorremos os resultados
 * ate um bind() dar certo, para funcionar tanto em maquinas so IPv4 quanto
 * com pilha dupla. */
int net_listen(const char *porta)
{
    struct addrinfo hints, *res, *p;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, porta, &hints, &res) != 0)
        return -1;

    int fd = -1;
    for (p = res; p; p = p->ai_next) {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0)
            continue;
        int um = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &um, sizeof um);
        if (bind(fd, p->ai_addr, p->ai_addrlen) == 0)
            break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0)
        return -1;
    if (listen(fd, 16) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/* Conecta com um tempo limite de verdade (ver net.h). connect() sozinho,
 * em um soquete bloqueante, pode ficar preso por minutos se o outro lado
 * nao responder (por exemplo, atras de um firewall que descarta pacotes);
 * por isso a conexao e feita em modo nao bloqueante e aguardada com
 * select(), que respeita 'timeout_s'. */
int net_connect(const char *host, const char *porta, int timeout_s)
{
    struct addrinfo hints, *res, *p;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, porta, &hints, &res) != 0) {
        errno = EINVAL;
        return -1;
    }

    int fd = -1;
    for (p = res; p; p = p->ai_next) {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0)
            continue;

        struct timeval tv = { .tv_sec = timeout_s, .tv_usec = 0 };
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);

        /* connect() nao respeita SO_SNDTIMEO em todo sistema; usamos o
         * soquete em modo nao bloqueante soh para a fase de conexao. */
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        int r = connect(fd, p->ai_addr, p->ai_addrlen);
        if (r < 0 && errno == EINPROGRESS) {
            fd_set wset;
            FD_ZERO(&wset);
            FD_SET(fd, &wset);
            struct timeval sel_tv = { .tv_sec = timeout_s, .tv_usec = 0 };
            r = select(fd + 1, NULL, &wset, NULL, &sel_tv);
            if (r > 0) {
                int err = 0;
                socklen_t len = sizeof err;
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
                r = err == 0 ? 0 : -1;
            } else {
                r = -1;
                errno = ETIMEDOUT;
            }
        }
        fcntl(fd, F_SETFL, flags); /* volta ao modo bloqueante */

        if (r == 0)
            break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

/* Le byte a byte ate '\n' (descartado) ou ate 'cap'-1 bytes, o que vier
 * primeiro; ignora '\r', para aceitar tanto quem manda so '\n' quanto
 * quem manda '\r\n'. Byte a byte custa pouco aqui porque as linhas do
 * MSMP sao curtas (no maximo VALOR_MAX bytes) e o kernel ja faz o
 * buffering do lado do soquete. */
int recv_line(int fd, char *buf, size_t cap)
{
    size_t n = 0;
    while (n + 1 < cap) {
        char c;
        ssize_t r = recv(fd, &c, 1, 0);
        if (r == 0)
            return n == 0 ? 0 : (int)n; /* fechou: devolve o que tiver */
        if (r < 0)
            return -1; /* erro ou timeout (SO_RCVTIMEO) */
        if (c == '\n')
            break;
        if (c != '\r')
            buf[n++] = c;
    }
    buf[n] = '\0';
    return (int)n;
}

/* Acrescenta '\n' a 'linha' e envia tudo, repetindo send() ate esgotar o
 * buffer (um send() pode escrever menos do que foi pedido). MSG_NOSIGNAL
 * troca o SIGPIPE, que mataria o processo se o outro lado ja tiver
 * fechado a conexao, por um simples retorno de erro (EPIPE). */
int send_line(int fd, const char *linha)
{
    size_t len = strlen(linha);
    char tmp[LINHA_MAX];
    if (len + 2 > sizeof tmp)
        return -1;
    memcpy(tmp, linha, len);
    tmp[len] = '\n';
    size_t total = len + 1, sent = 0;
    while (sent < total) {
        ssize_t w = send(fd, tmp + sent, total - sent, MSG_NOSIGNAL);
        if (w < 0)
            return -1;
        sent += (size_t)w;
    }
    return 0;
}
