/*
 * manager.c: gerente do Mini-SNMP. Le uma lista de agentes de um arquivo
 * de configuracao, consulta cada um periodicamente ("GET ALL"), detecta
 * falhas (timeout ou recusa de conexao) e mostra um painel no terminal.
 * Tambem grava um log de eventos e um historico de metricas em CSV.
 *
 * Uso: ./manager [-f config] [-i intervalo_s] [-n rodadas] [-l log.txt] [-c metricas.csv]
 *   -f config     arquivo com uma linha "id host porta" por agente
 *                 (padrao: tres agentes locais em 127.0.0.1:7161-7163)
 *   -i intervalo  segundos entre rodadas de consulta (padrao: 5)
 *   -n rodadas    numero de rodadas; 0 = infinito (padrao: 0)
 *   -l log.txt    arquivo de log de eventos (padrao: manager.log)
 *   -c metricas.csv  historico de metricas em CSV (padrao: metricas.csv)
 */
#include "../common/common.h"
#include "../common/net.h"

#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    char id[NOME_MAX + 1];
    char host[64];
    char porta[8];
    int ativo;                 /* 1 = UP na ultima consulta, 0 = DOWN */
    int falhas_consecutivas;
    char valores[MIB_TAMANHO][VALOR_MAX];
} Agente;

static Agente g_agentes[MAX_AGENTES];
static int g_n_agentes = 0;
static FILE *g_log = NULL;
static FILE *g_csv = NULL;
static volatile sig_atomic_t g_encerrar = 0;

static void ao_sinal(int sig) { (void)sig; g_encerrar = 1; }

static void logar(const char *fmt, ...)
{
    time_t agora = time(NULL);
    struct tm tmv;
    localtime_r(&agora, &tmv);
    char ts[32];
    strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &tmv);

    va_list ap;
    va_start(ap, fmt);
    char msg[512];
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);

    printf("%s %s\n", ts, msg);
    if (g_log) {
        fprintf(g_log, "%s %s\n", ts, msg);
        fflush(g_log);
    }
}

static int carregar_config(const char *caminho)
{
    FILE *f = fopen(caminho, "r");
    if (!f)
        return -1;
    char linha[256];
    while (fgets(linha, sizeof linha, f) && g_n_agentes < MAX_AGENTES) {
        if (linha[0] == '#' || linha[0] == '\n')
            continue;
        Agente *a = &g_agentes[g_n_agentes];
        if (sscanf(linha, "%32s %63s %7s", a->id, a->host, a->porta) == 3) {
            a->ativo = 0;
            a->falhas_consecutivas = 0;
            g_n_agentes++;
        }
    }
    fclose(f);
    return g_n_agentes;
}

static void config_padrao(void)
{
    const char *padrao[][3] = {
        { "nodeA", "127.0.0.1", "7161" },
        { "nodeB", "127.0.0.1", "7162" },
        { "nodeC", "127.0.0.1", "7163" },
    };
    g_n_agentes = 3;
    for (int i = 0; i < 3; i++) {
        Agente *a = &g_agentes[i];
        strcpy(a->id, padrao[i][0]);
        strcpy(a->host, padrao[i][1]);
        strcpy(a->porta, padrao[i][2]);
        a->ativo = 0;
        a->falhas_consecutivas = 0;
    }
}

/* Consulta um agente ("GET ALL"); devolve 0 em sucesso (com 'a->valores'
 * preenchido) ou -1 em falha (timeout, recusa, resposta incompleta). */
static int consultar(Agente *a)
{
    int fd = net_connect(a->host, a->porta, TIMEOUT_RESPOSTA_S);
    if (fd < 0)
        return -1;

    if (send_line(fd, "GET ALL") < 0) {
        close(fd);
        return -1;
    }

    size_t recebidos = 0;
    for (;;) {
        char linha[LINHA_MAX];
        int n = recv_line(fd, linha, sizeof linha);
        if (n <= 0) {
            close(fd);
            return -1; /* timeout ou conexao caiu no meio da resposta */
        }
        if (strcmp(linha, "END") == 0)
            break;
        char oid[OID_MAX], valor[VALOR_MAX];
        if (sscanf(linha, "RESPONSE %15s %63s", oid, valor) == 2) {
            for (size_t i = 0; i < MIB_TAMANHO; i++) {
                if (strcmp(MIB[i].oid, oid) == 0) {
                    snprintf(a->valores[i], VALOR_MAX, "%s", valor);
                    recebidos++;
                }
            }
        }
    }
    close(fd);
    return recebidos == MIB_TAMANHO ? 0 : -1;
}

static void gravar_csv_cabecalho(void)
{
    if (!g_csv)
        return;
    fprintf(g_csv, "timestamp,agente,status");
    for (size_t i = 0; i < MIB_TAMANHO; i++)
        fprintf(g_csv, ",%s", MIB[i].nome);
    fprintf(g_csv, "\n");
    fflush(g_csv);
}

static void gravar_csv_linha(const char *ts, Agente *a)
{
    if (!g_csv)
        return;
    fprintf(g_csv, "%s,%s,%s", ts, a->id, a->ativo ? "UP" : "DOWN");
    for (size_t i = 0; i < MIB_TAMANHO; i++)
        fprintf(g_csv, ",%s", a->ativo ? a->valores[i] : "");
    fprintf(g_csv, "\n");
    fflush(g_csv);
}

static void imprimir_painel(void)
{
    printf("\n%-8s %-6s", "AGENTE", "STATUS");
    for (size_t i = 0; i < MIB_TAMANHO; i++)
        printf(" %10s", MIB[i].nome);
    printf("\n");
    for (int i = 0; i < g_n_agentes; i++) {
        Agente *a = &g_agentes[i];
        printf("%-8s %-6s", a->id, a->ativo ? "UP" : "DOWN");
        for (size_t j = 0; j < MIB_TAMANHO; j++)
            printf(" %10s", a->ativo ? a->valores[j] : "-");
        printf("\n");
    }
    printf("\n");
}

static void rodada(void)
{
    time_t agora = time(NULL);
    struct tm tmv;
    localtime_r(&agora, &tmv);
    char ts[32];
    strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &tmv);

    for (int i = 0; i < g_n_agentes; i++) {
        Agente *a = &g_agentes[i];
        int estava_ativo = a->ativo;
        int ok = consultar(a) == 0;
        a->ativo = ok;
        if (ok) {
            a->falhas_consecutivas = 0;
            if (!estava_ativo)
                logar("%s: voltou a responder (UP)", a->id);
        } else {
            a->falhas_consecutivas++;
            if (estava_ativo)
                logar("%s: parou de responder (DOWN): timeout ou conexao recusada", a->id);
        }
        gravar_csv_linha(ts, a);
    }
    imprimir_painel();
}

int main(int argc, char **argv)
{
    const char *config = NULL, *log_path = "manager.log", *csv_path = "metricas.csv";
    int intervalo = INTERVALO_PADRAO_S, rodadas = 0;

    int opt;
    while ((opt = getopt(argc, argv, "f:i:n:l:c:")) != -1) {
        switch (opt) {
        case 'f': config = optarg; break;
        case 'i': intervalo = atoi(optarg); break;
        case 'n': rodadas = atoi(optarg); break;
        case 'l': log_path = optarg; break;
        case 'c': csv_path = optarg; break;
        default:
            fprintf(stderr, "uso: %s [-f config] [-i intervalo_s] [-n rodadas] [-l log.txt] [-c metricas.csv]\n", argv[0]);
            return 1;
        }
    }

    if (config) {
        if (carregar_config(config) <= 0) {
            fprintf(stderr, "manager: nao foi possivel ler '%s'\n", config);
            return 1;
        }
    } else {
        config_padrao();
    }

    g_log = fopen(log_path, "a");
    g_csv = fopen(csv_path, "w");
    gravar_csv_cabecalho();

    signal(SIGINT, ao_sinal);
    signal(SIGTERM, ao_sinal);

    logar("gerente iniciado: %d agente(s), intervalo de %ds", g_n_agentes, intervalo);
    for (int i = 0; i < g_n_agentes; i++)
        logar("  %s em %s:%s", g_agentes[i].id, g_agentes[i].host, g_agentes[i].porta);

    int r = 0;
    while (!g_encerrar && (rodadas == 0 || r < rodadas)) {
        rodada();
        r++;
        if (rodadas != 0 && r >= rodadas)
            break;
        sleep((unsigned)intervalo);
    }

    logar("gerente encerrado apos %d rodada(s)", r);
    if (g_log) fclose(g_log);
    if (g_csv) fclose(g_csv);
    return 0;
}
