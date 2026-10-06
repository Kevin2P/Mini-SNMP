/*
 * metrics.c: implementacao da coleta de metricas (ver metrics.h). Cada
 * funcao publica corresponde a um objeto da MIB simplificada e le o
 * arquivo /proc correspondente a cada chamada, para sempre devolver o
 * valor atual (nao um valor em cache).
 */
#include "metrics.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

/* CPU */

/* Le a linha "cpu" agregada de /proc/stat e soma os 8 campos de tempo
 * (em "jiffies") em dois totais: ocioso (idle + iowait) e geral. O uso
 * de CPU e sempre uma fracao entre duas leituras (ver metrica_cpu_pct),
 * nunca um valor absoluto de uma leitura so. */
static int ler_cpu_totais(unsigned long long *ocioso, unsigned long long *total)
{
    FILE *f = fopen("/proc/stat", "r");
    if (!f)
        return -1;
    char rotulo[8];
    unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
    int n = fscanf(f, "%7s %llu %llu %llu %llu %llu %llu %llu %llu",
                   rotulo, &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal);
    fclose(f);
    if (n < 8)
        return -1;
    *ocioso = idle + iowait;
    *total = user + nice + system + idle + iowait + irq + softirq + steal;
    return 0;
}

double metrica_cpu_pct(void)
{
    static unsigned long long ult_ocioso = 0, ult_total = 0;
    static int tem_amostra = 0;

    unsigned long long ocioso, total;
    if (ler_cpu_totais(&ocioso, &total) < 0)
        return -1.0;

    if (!tem_amostra) {
        /* primeira chamada do processo: tira uma amostra curta e sincrona,
         * para nao devolver um valor sem sentido na primeira consulta */
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 150000000L }; /* 150 ms */
        nanosleep(&ts, NULL);
        unsigned long long ocioso2, total2;
        if (ler_cpu_totais(&ocioso2, &total2) < 0)
            return -1.0;
        ult_ocioso = ocioso2;
        ult_total = total2;
        tem_amostra = 1;
        unsigned long long d_total = total2 - total, d_ocioso = ocioso2 - ocioso;
        return d_total == 0 ? 0.0 : 100.0 * (double)(d_total - d_ocioso) / (double)d_total;
    }

    unsigned long long d_total = total - ult_total, d_ocioso = ocioso - ult_ocioso;
    ult_ocioso = ocioso;
    ult_total = total;
    if (d_total == 0)
        return 0.0;
    double pct = 100.0 * (double)(d_total - d_ocioso) / (double)d_total;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return pct;
}

/* Memoria */

double metrica_mem_pct(void)
{
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f)
        return -1.0;
    unsigned long long total = 0, disponivel = 0;
    char linha[256];
    while (fgets(linha, sizeof linha, f)) {
        unsigned long long kb;
        if (sscanf(linha, "MemTotal: %llu kB", &kb) == 1)
            total = kb;
        else if (sscanf(linha, "MemAvailable: %llu kB", &kb) == 1)
            disponivel = kb;
    }
    fclose(f);
    if (total == 0)
        return -1.0;
    return 100.0 * (double)(total - disponivel) / (double)total;
}

/* Uptime */

long metrica_uptime_s(void)
{
    FILE *f = fopen("/proc/uptime", "r");
    if (!f)
        return -1;
    double segundos = 0;
    int n = fscanf(f, "%lf", &segundos);
    fclose(f);
    return n == 1 ? (long)segundos : -1;
}

/* Rede */

/* Soma bytes recebidos/enviados de cada linha de /proc/net/dev (formato
 * "iface: rx_bytes rx_pacotes ... tx_bytes tx_pacotes ..."), pulando as
 * 2 linhas de cabecalho e a interface "lo" (trafego local, que nao e
 * trafego de rede de verdade). */
static int ler_net_totais(unsigned long long *in, unsigned long long *out)
{
    FILE *f = fopen("/proc/net/dev", "r");
    if (!f)
        return -1;
    char linha[256];
    if (!fgets(linha, sizeof linha, f) || !fgets(linha, sizeof linha, f)) {
        fclose(f);
        return -1; /* arquivo mais curto que o esperado: sem as 2 linhas de cabecalho */
    }
    unsigned long long soma_in = 0, soma_out = 0;
    while (fgets(linha, sizeof linha, f)) {
        char iface[32];
        unsigned long long rx_bytes, rx_resto[7], tx_bytes;
        char *dois_pontos = strchr(linha, ':');
        if (!dois_pontos)
            continue;
        *dois_pontos = ' ';
        int n = sscanf(linha, "%31s %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                       iface, &rx_bytes, &rx_resto[0], &rx_resto[1], &rx_resto[2],
                       &rx_resto[3], &rx_resto[4], &rx_resto[5], &rx_resto[6], &tx_bytes);
        if (n < 10)
            continue;
        if (strcmp(iface, "lo") == 0)
            continue; /* loopback nao conta como trafego de rede */
        soma_in += rx_bytes;
        soma_out += tx_bytes;
    }
    fclose(f);
    *in = soma_in;
    *out = soma_out;
    return 0;
}

unsigned long long metrica_net_in_bytes(void)
{
    unsigned long long in, out;
    return ler_net_totais(&in, &out) == 0 ? in : 0;
}

unsigned long long metrica_net_out_bytes(void)
{
    unsigned long long in, out;
    return ler_net_totais(&in, &out) == 0 ? out : 0;
}
