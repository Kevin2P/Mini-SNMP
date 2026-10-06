/*
 * agent.c: agente do Mini-SNMP. Escuta em uma porta TCP e responde a
 * requisicoes GET do gerente com o valor atual de uma metrica (ou de
 * todas, com "GET ALL"). Cada conexao atende uma unica requisicao, e
 * fecha; o mesmo padrao sem estado usado pelo SNMP real sobre UDP,
 * so que aqui sobre TCP para simplificar (ver Secao 8 do relatorio).
 *
 * Uso: ./agent <id> [porta]
 */
#include "../common/common.h"
#include "../common/net.h"
#include "metrics.h"

#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static char g_id[NOME_MAX + 1] = "agente";

static void logar(const char *fmt, ...)
{
    time_t agora = time(NULL);
    struct tm tmv;
    localtime_r(&agora, &tmv);
    char ts[32];
    strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &tmv);

    va_list ap;
    va_start(ap, fmt);
    fprintf(stdout, "%s [%s] ", ts, g_id);
    vfprintf(stdout, fmt, ap);
    fprintf(stdout, "\n");
    fflush(stdout);
    va_end(ap);
}

/* devolve o valor textual de um OID, ou NULL se o OID nao existe */
static int valor_de(const char *oid, char *saida, size_t cap)
{
    if (strcmp(oid, "1.1") == 0) {
        double v = metrica_cpu_pct();
        snprintf(saida, cap, "%.1f", v);
    } else if (strcmp(oid, "1.2") == 0) {
        double v = metrica_mem_pct();
        snprintf(saida, cap, "%.1f", v);
    } else if (strcmp(oid, "1.3") == 0) {
        snprintf(saida, cap, "%ld", metrica_uptime_s());
    } else if (strcmp(oid, "2.1") == 0) {
        snprintf(saida, cap, "%llu", metrica_net_in_bytes());
    } else if (strcmp(oid, "2.2") == 0) {
        snprintf(saida, cap, "%llu", metrica_net_out_bytes());
    } else {
        return -1;
    }
    return 0;
}

static void atender(int cfd)
{
    char linha[LINHA_MAX];
    int n = recv_line(cfd, linha, sizeof linha);
    if (n <= 0) {
        logar("conexao encerrada sem requisicao valida");
        return;
    }

    char cmd[8] = "", oid[OID_MAX] = "";
    if (sscanf(linha, "%7s %15s", cmd, oid) < 2 || strcmp(cmd, "GET") != 0) {
        send_line(cfd, "ERROR requisicao invalida");
        logar("requisicao invalida: \"%s\"", linha);
        return;
    }

    if (strcmp(oid, "ALL") == 0) {
        for (size_t i = 0; i < MIB_TAMANHO; i++) {
            char valor[VALOR_MAX], resp[LINHA_MAX];
            valor_de(MIB[i].oid, valor, sizeof valor);
            snprintf(resp, sizeof resp, "RESPONSE %s %s", MIB[i].oid, valor);
            send_line(cfd, resp);
        }
        send_line(cfd, "END");
        logar("GET ALL -> %zu metricas", MIB_TAMANHO);
        return;
    }

    char valor[VALOR_MAX];
    if (valor_de(oid, valor, sizeof valor) < 0) {
        send_line(cfd, "ERROR oid desconhecido");
        logar("GET %s -> ERROR oid desconhecido", oid);
        return;
    }
    char resp[LINHA_MAX];
    snprintf(resp, sizeof resp, "RESPONSE %s %s", oid, valor);
    send_line(cfd, resp);
    logar("GET %s -> %s", oid, valor);
}

int main(int argc, char **argv)
{
    signal(SIGPIPE, SIG_IGN);

    if (argc < 2) {
        fprintf(stderr, "uso: %s <id> [porta]\n", argv[0]);
        return 1;
    }
    strncpy(g_id, argv[1], NOME_MAX);
    g_id[NOME_MAX] = '\0';
    const char *porta = argc >= 3 ? argv[2] : "7161";

    int lfd = net_listen(porta);
    if (lfd < 0) {
        fprintf(stderr, "%s: nao foi possivel escutar na porta %s\n", g_id, porta);
        return 1;
    }
    /* "aquece" a amostra de CPU aqui, fora do atendimento de qualquer
     * cliente: a primeira leitura precisa de 2 amostras (ver metrics.c),
     * entao sem isto o primeiro GET de cada agente recem-iniciado custaria
     * ~150ms a mais que os seguintes (foi o que o experimento de latencia
     * mostrou antes desta correcao). */
    metrica_cpu_pct();
    logar("agente escutando na porta %s (%zu metricas na MIB)", porta, MIB_TAMANHO);

    for (;;) {
        int cfd = accept(lfd, NULL, NULL);
        if (cfd < 0)
            continue;
        struct timeval tv = { .tv_sec = TIMEOUT_RESPOSTA_S, .tv_usec = 0 };
        setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        atender(cfd);
        close(cfd);
    }
}
