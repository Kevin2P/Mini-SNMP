# Mini-SNMP

**Sistema de gerenciamento de rede simplificado, inspirado no SNMP**

Gerente ↔ Agentes sobre sockets TCP · protocolo próprio (MSMP) · métricas reais via `/proc`

Autores: Kevin and Christian

## Visão geral

O sistema tem dois programas independentes, em C, que conversam por sockets TCP:

- **`agent`** representa um nó monitorado. Escuta em uma porta e responde com o
  valor atual de suas métricas, lidas do sistema operacional no instante da consulta.
- **`manager`** lê uma lista de agentes e os consulta periodicamente, mostrando um
  painel no terminal, gravando um log de eventos e um histórico de métricas em CSV.

```
 Agente A (porta 7161) ┐
 Agente B (porta 7162) ├──  TCP, uma conexão por consulta  ──►  Gerente
 Agente C (porta 7163) ┘        (painel · log · CSV)
```

Cada nó monitorado é um processo `agent` independente; nada no protocolo impede
rodar cada um em uma máquina diferente, bastando apontar o IP certo no arquivo
de configuração do gerente.

## Início rápido

```bash
git clone <URL_DO_REPOSITORIO>
cd minisnmp
make
```

Em três terminais:

```bash
./bin/agent nodeA 7161
./bin/agent nodeB 7162
./bin/agent nodeC 7163
```

Em um quarto terminal:

```bash
cat > agentes.conf << EOF
nodeA 127.0.0.1 7161
nodeB 127.0.0.1 7162
nodeC 127.0.0.1 7163
EOF
./bin/manager -f agentes.conf -i 5
```

Sem `-f`, o gerente já usa esse mesmo padrão de 3 agentes locais. Outras opções:
`-i <s>` intervalo entre rodadas (padrão 5), `-n <rodadas>` número de rodadas
(padrão 0 = infinito), `-l <arquivo>` log de eventos, `-c <arquivo>` histórico em
CSV. Ctrl+C encerra o gerente a qualquer momento.

## Protocolo (MSMP)

**M**ini-**SNMP** **M**anagement **P**rotocol: texto simples sobre TCP, uma
mensagem por linha, inspirado no SNMP real (GET, RESPONSE, OIDs), mas sem
ASN.1/BER, para ficar fácil de testar com `nc` ou um script de poucas linhas.

| Mensagem | Sentido | Significado |
|---|---|---|
| `GET <oid>` | gerente → agente | pede o valor de uma métrica |
| `GET ALL` | gerente → agente | pede todas as métricas da MIB |
| `RESPONSE <oid> <valor>` | agente → gerente | valor de uma métrica |
| `END` | agente → gerente | fecha a resposta a `GET ALL` |
| `ERROR <motivo>` | agente → gerente | requisição inválida ou OID desconhecido |

Exemplo real, capturado com um agente rodando:

```
> GET ALL
< RESPONSE 1.1 0.0
< RESPONSE 1.2 5.3
< RESPONSE 1.3 499
< RESPONSE 2.1 31348
< RESPONSE 2.2 19171
< END
```

Tempo limite de 2 s para conectar e para cada leitura: se o agente não
responder a tempo (mesmo com a conexão aberta), a consulta falha e ele é
marcado `DOWN`.

## MIB simplificada

| OID | Nome | Unidade | Fonte |
|---|---|---|---|
| 1.1 | CPU | % | `/proc/stat` |
| 1.2 | Memoria | % | `/proc/meminfo` |
| 1.3 | Uptime | s | `/proc/uptime` |
| 2.1 | TrafegoIn | bytes | `/proc/net/dev` |
| 2.2 | TrafegoOut | bytes | `/proc/net/dev` |

Agrupadas em dois ramos, como no diagrama do enunciado: `1.0 Sistema` e
`2.0 Rede`. `TrafegoIn`/`TrafegoOut` são contadores cumulativos desde o boot,
a mesma semântica de `ifInOctets`/`ifOutOctets` do SNMP real.

## O gerente em ação

Painel exibido a cada rodada:

```
AGENTE   STATUS        CPU    Memoria     Uptime  TrafegoIn TrafegoOut
nodeA    UP            0.0        5.3         25      52663      18462
nodeB    UP            0.0        5.3         25      52663      18462
nodeC    UP            1.0        5.3         27      52663      18462
```

Log real de uma queda e recuperação (`nodeB` encerrado com `kill -9`, sem
aviso, e reiniciado em seguida):

```
2026-10-06 19:09:03 gerente iniciado: 3 agente(s), intervalo de 1s
2026-10-06 19:09:03 nodeA: voltou a responder (UP)
2026-10-06 19:09:03 nodeB: voltou a responder (UP)
2026-10-06 19:09:03 nodeC: voltou a responder (UP)
2026-10-06 19:09:06 nodeB: parou de responder (DOWN): timeout ou conexao recusada
2026-10-06 19:09:09 nodeB: voltou a responder (UP)
```

`nodeA` e `nodeC` continuam sendo consultados normalmente enquanto `nodeB`
está fora: a falha de um agente não afeta o monitoramento dos demais.

## Como testar

Passo a passo com os comandos exatos, para reproduzir no terminal. Os passos 1
a 3 usam os três agentes do [Início rápido](#início-rápido); os passos 4 e 5
sobem os próprios processos de que precisam.

**1. Testar o protocolo manualmente, com `nc`** (em qualquer terminal, com o
`nodeA` rodando):

```bash
echo "GET 1.1" | nc -w 2 127.0.0.1 7161        # uma métrica: RESPONSE 1.1 <valor>
echo "GET ALL" | nc -w 2 127.0.0.1 7161        # a MIB inteira, termina em END
echo "GET 9.9" | nc -w 2 127.0.0.1 7161        # OID que não existe: ERROR oid desconhecido
echo "XYZ"     | nc -w 2 127.0.0.1 7161        # requisição malformada: ERROR requisicao invalida
```

**2. Rodar o gerente e ver o painel, o log e o histórico:**

```bash
./bin/manager -f agentes.conf -i 2 -n 3
cat manager.log      # cada evento, com data e hora
cat metricas.csv      # uma linha por agente, por rodada
```

**3. Simular a queda de um agente.** Se ele está no próprio terminal, basta
Ctrl+C. De outro terminal, sem precisar anotar o PID:

```bash
kill -9 $(pgrep -f "bin/agent nodeB")
```

Rode o gerente de novo (passo 2): o painel mostra `nodeB` como `DOWN`, com
`-` no lugar das métricas, e o log registra `parou de responder (DOWN)`.
Suba o `nodeB` de novo (`./bin/agent nodeB 7162`) e rode o gerente uma vez
mais: ele volta a aparecer `UP`.

**4. Testes automatizados** (sobem e derrubam os próprios agentes de teste,
em portas separadas das do passo 1 a 3):

```bash
make test
```

Verifica o protocolo, a consulta a vários agentes ao mesmo tempo e a
detecção de queda/recuperação; termina com `TODOS OS TESTES PASSARAM`.

**5. Experimentos quantitativos** (latência, escalabilidade, overhead e
tempo de detecção de falha; os mesmos números usados no relatório):

```bash
python3 tests/experiments.py
```

Imprime um resumo no terminal e grava `tests/resultados_*.csv`.

## Resultados

Números reais, medidos pelos experimentos acima:

| Métrica | Valor |
|---|---|
| Latência média de uma consulta (200 amostras) | 0,118 ms |
| Duração de uma rodada completa com 20 agentes | < 5 ms |
| Overhead do protocolo por consulta | 101 bytes (8 de requisição + 93 de resposta) |
| Tempo de detecção de um agente que caiu | 0,27 ms |
| Tempo de detecção quando a conexão fica aberta mas não responde | ~2 s (o limite configurado) |

Detalhes, metodologia e a comparação conceitual com o SNMP real estão no
relatório técnico.
