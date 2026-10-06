/*
 * common.h: constantes do protocolo e da MIB simplificada, usadas pelo
 * agente e pelo gerente.
 *
 * Protocolo MSMP (Mini-SNMP Management Protocol): texto simples sobre TCP,
 * uma linha por mensagem, terminada em '\n'. Inspirado no SNMP real (GET,
 * RESPONSE, identificadores de objeto no estilo OID), mas sem ASN.1/BER:
 * aqui os valores trafegam como texto legivel, o que facilita depurar e
 * capturar com ferramentas como o Wireshark ou o 'nc'.
 *
 *   Requisicao:  GET <oid>\n          (oid = "ALL" pede a MIB inteira)
 *   Resposta:    RESPONSE <oid> <valor>\n   (uma linha por objeto)
 *                END\n                      (fecha uma resposta de "ALL")
 *                ERROR <motivo>\n            (oid invalido, etc.)
 */
#ifndef COMMON_H
#define COMMON_H

#define PROTO_NAME     "MSMP"      /* Mini-SNMP Management Protocol */
#define PROTO_VERSION  "1.0"

#define PORTA_PADRAO     7161      /* porta TCP padrao de um agente */
#define LINHA_MAX        256       /* tamanho maximo de uma linha do protocolo */
#define OID_MAX          16        /* tamanho maximo de um OID, ex.: "2.2" */
#define VALOR_MAX         64       /* tamanho maximo do valor textual de uma metrica */
#define NOME_MAX          32       /* tamanho maximo do nome/id de um agente */

#define TIMEOUT_RESPOSTA_S   2     /* o gerente espera no maximo isto por uma resposta */
#define INTERVALO_PADRAO_S   5     /* intervalo padrao entre rodadas de consulta */
#define MAX_AGENTES          64    /* agentes que o gerente consegue monitorar */

/*
 * MIB simplificada (ver arquitetura no enunciado):
 *   1.0  Sistema
 *     1.1  CPU         uso de CPU, em % (0-100)
 *     1.2  Memoria      uso de memoria, em % (0-100)
 *     1.3  Uptime       tempo ligado, em segundos
 *   2.0  Rede
 *     2.1  TrafegoIn    bytes recebidos (contador cumulativo, desde o boot)
 *     2.2  TrafegoOut   bytes enviados  (contador cumulativo, desde o boot)
 */
typedef struct {
    const char *oid;      /* identificador, ex. "1.1" */
    const char *nome;     /* nome legivel, ex. "CPU" */
    const char *unidade;  /* unidade, so para exibicao */
} ObjetoMIB;

static const ObjetoMIB MIB[] = {
    { "1.1", "CPU",        "%"     },
    { "1.2", "Memoria",    "%"     },
    { "1.3", "Uptime",     "s"     },
    { "2.1", "TrafegoIn",  "bytes" },
    { "2.2", "TrafegoOut", "bytes" },
};
#define MIB_TAMANHO (sizeof(MIB) / sizeof(MIB[0]))

#endif
