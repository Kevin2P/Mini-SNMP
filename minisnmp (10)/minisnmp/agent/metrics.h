/*
 * metrics.h: coleta das metricas locais do agente, lidas de /proc (Linux).
 * Mesma fonte de dados que ferramentas reais (top, sar, ifconfig) usam, e
 * que um agente SNMP real leria via o nucleo do sistema operacional.
 */
#ifndef METRICS_H
#define METRICS_H

/* Uso de CPU em % (0 a 100), medido como a fracao de tempo NAO ocioso
 * entre esta chamada e a anterior (na primeira chamada do processo, faz
 * uma amostra curta e sincrona para ja devolver um valor valido). */
double metrica_cpu_pct(void);

/* Uso de memoria em % (0 a 100): 100 * (1 - MemAvailable / MemTotal). */
double metrica_mem_pct(void);

/* Tempo ligado, em segundos inteiros, de /proc/uptime. */
long metrica_uptime_s(void);

/* Bytes recebidos/enviados, somados em todas as interfaces exceto 'lo'.
 * Contadores cumulativos desde o boot (mesma semantica do ifInOctets /
 * ifOutOctets do SNMP real). */
unsigned long long metrica_net_in_bytes(void);
unsigned long long metrica_net_out_bytes(void);

#endif
