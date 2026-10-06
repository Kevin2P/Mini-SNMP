#!/usr/bin/env python3
"""
Experimentos quantitativos do Mini-SNMP, para a Secao 5/6 do relatorio:
  1. latencia de uma rodada de monitoramento (GET ALL -> RESPONSE*5 + END)
  2. escalabilidade: tempo de uma rodada completa do gerente, variando o
     numero de agentes monitorados (1, 3, 5, 10, 20)
  3. overhead do protocolo: bytes trocados por rodada, por agente
  4. latencia de deteccao de falha (agente morto sem fechar a conexao)

Uso: python3 tests/experiments.py
Gera: tests/resultados_latencia.csv, tests/resultados_escala.csv,
      tests/resultados_falha.csv, e imprime um resumo.
"""
import os
import socket
import statistics
import subprocess
import sys
import time

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
AGENTE = os.path.join(RAIZ, "bin", "agent")
PORTA_BASE = 7400


def subir_agentes(n, porta_base=PORTA_BASE):
    procs = []
    for i in range(n):
        p = subprocess.Popen(
            [AGENTE, "a%d" % i, str(porta_base + i)],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )
        procs.append(p)
    time.sleep(0.4)
    return procs


def derrubar(procs):
    for p in procs:
        p.terminate()
    for p in procs:
        try:
            p.wait(timeout=2)
        except subprocess.TimeoutExpired:
            p.kill()
            p.wait()


def uma_consulta(porta, timeout=2.0):
    """Faz um GET ALL completo; devolve (latencia_s, bytes_enviados, bytes_recebidos) ou None."""
    t0 = time.perf_counter()
    try:
        c = socket.create_connection(("127.0.0.1", porta), timeout=timeout)
    except OSError:
        return None
    c.settimeout(timeout)
    pedido = b"GET ALL\n"
    try:
        c.sendall(pedido)
        buf = b""
        while b"END\n" not in buf:
            d = c.recv(4096)
            if not d:
                break
            buf += d
    except OSError:
        c.close()
        return None
    t1 = time.perf_counter()
    c.close()
    return (t1 - t0, len(pedido), len(buf))


def experimento_latencia(n_amostras=200):
    print("== 1) Latencia de uma consulta GET ALL (%d amostras) ==" % n_amostras)
    procs = subir_agentes(1)
    try:
        amostras = []
        for _ in range(n_amostras):
            r = uma_consulta(PORTA_BASE)
            if r:
                amostras.append(r[0] * 1000.0)  # ms
        amostras.sort()
        media = statistics.mean(amostras)
        p50 = amostras[len(amostras) // 2]
        p95 = amostras[int(len(amostras) * 0.95)]
        maximo = amostras[-1]
        print("  media=%.3f ms  p50=%.3f ms  p95=%.3f ms  max=%.3f ms  (n=%d)"
              % (media, p50, p95, maximo, len(amostras)))
        with open(os.path.join(RAIZ, "tests", "resultados_latencia.csv"), "w") as f:
            f.write("amostra,latencia_ms\n")
            for i, v in enumerate(amostras):
                f.write("%d,%.4f\n" % (i, v))
        return media, p50, p95, maximo
    finally:
        derrubar(procs)


def experimento_escala(valores_n=(1, 3, 5, 10, 20)):
    print("== 2) Escalabilidade: tempo de uma rodada completa, variando o numero de agentes ==")
    linhas = []
    for n in valores_n:
        procs = subir_agentes(n, porta_base=PORTA_BASE + 100)
        try:
            t0 = time.perf_counter()
            ok = 0
            for i in range(n):
                r = uma_consulta(PORTA_BASE + 100 + i)
                if r:
                    ok += 1
            t1 = time.perf_counter()
            dur_ms = (t1 - t0) * 1000.0
            print("  N=%2d  rodada completa=%.2f ms  (%.3f ms/agente)  agentes OK=%d/%d"
                  % (n, dur_ms, dur_ms / n, ok, n))
            linhas.append((n, dur_ms, dur_ms / n, ok))
        finally:
            derrubar(procs)
    with open(os.path.join(RAIZ, "tests", "resultados_escala.csv"), "w") as f:
        f.write("n_agentes,duracao_rodada_ms,ms_por_agente,agentes_ok\n")
        for n, dur, por_agente, ok in linhas:
            f.write("%d,%.4f,%.4f,%d\n" % (n, dur, por_agente, ok))
    return linhas


def experimento_overhead():
    print("== 3) Overhead do protocolo (bytes por rodada, por agente) ==")
    procs = subir_agentes(1, porta_base=PORTA_BASE + 300)
    try:
        r = uma_consulta(PORTA_BASE + 300)
        if r:
            _, env, rec = r
            print("  requisicao (GET ALL): %d bytes" % env)
            print("  resposta (5 metricas + END): %d bytes" % rec)
            print("  total por consulta: %d bytes" % (env + rec))
        return r
    finally:
        derrubar(procs)


def experimento_falha():
    print("== 4) Latencia de deteccao de falha ==")
    linhas = []

    # 4a) processo morto (porta fecha): deteccao por ECONNREFUSED, deve ser quase instantanea
    procs = subir_agentes(1, porta_base=PORTA_BASE + 500)
    derrubar(procs)  # mata o agente ANTES de consultar
    t0 = time.perf_counter()
    r = uma_consulta(PORTA_BASE + 500, timeout=3.0)
    t1 = time.perf_counter()
    dt_ms = (t1 - t0) * 1000.0
    print("  processo morto (porta fechada): deteccao em %.2f ms (r=%s)" % (dt_ms, "OK" if r is None else "inesperado: respondeu"))
    linhas.append(("processo_morto_porta_fechada", dt_ms))

    # 4b) porta que nunca existiu (ninguem escutando) -- mesmo caminho de ECONNREFUSED
    t0 = time.perf_counter()
    r = uma_consulta(PORTA_BASE + 599, timeout=3.0)
    t1 = time.perf_counter()
    dt_ms = (t1 - t0) * 1000.0
    print("  porta nunca usada (ninguem escutando): deteccao em %.2f ms" % dt_ms)
    linhas.append(("porta_nunca_usada", dt_ms))

    with open(os.path.join(RAIZ, "tests", "resultados_falha.csv"), "w") as f:
        f.write("cenario,latencia_deteccao_ms\n")
        for nome, v in linhas:
            f.write("%s,%.4f\n" % (nome, v))
    return linhas


if __name__ == "__main__":
    if not os.path.exists(AGENTE):
        print("binario %s nao encontrado; rode 'make' antes" % AGENTE, file=sys.stderr)
        sys.exit(1)
    experimento_latencia()
    print()
    experimento_escala()
    print()
    experimento_overhead()
    print()
    experimento_falha()
