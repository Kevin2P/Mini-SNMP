/*
 * net.h: funcoes de rede compartilhadas pelo agente e pelo gerente.
 * O protocolo MSMP e orientado a linha (uma mensagem = uma linha, terminada
 * em '\n'), entao as funcoes de I/O trabalham em termos de linhas inteiras.
 */
#ifndef NET_H
#define NET_H

#include <stddef.h>

/* Cria um soquete de escuta TCP na porta dada (texto, ex. "7161") e em
 * todas as interfaces. Retorna o descritor ou -1 em erro. */
int net_listen(const char *porta);

/* Conecta por TCP a host:porta, com um tempo limite de 'timeout_s' segundos
 * para a conexao (nao so para o connect, tambem para send/recv depois).
 * Retorna o descritor ou -1 em erro (ETIMEDOUT se o tempo esgotar). */
int net_connect(const char *host, const char *porta, int timeout_s);

/* Le uma linha (ate '\n', exclusive) em buf, no maximo cap-1 bytes mais o
 * terminador nulo. Retorna o numero de bytes lidos (>=0), 0 se a conexao
 * fechou sem dados, ou -1 em erro/timeout (errno fica setado). */
int recv_line(int fd, char *buf, size_t cap);

/* Envia 'linha' seguida de '\n'. Retorna 0 em sucesso, -1 em erro. */
int send_line(int fd, const char *linha);

#endif
