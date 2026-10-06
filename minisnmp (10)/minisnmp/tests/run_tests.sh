#!/bin/bash
# Testes funcionais automatizados do Mini-SNMP. Sobe agentes de teste,
# confere as respostas do protocolo e o comportamento do gerente.
set -u
cd "$(dirname "$0")/.."

PASS=0
FAIL=0
PIDS=()

matar_tudo() {
    for p in "${PIDS[@]}"; do kill "$p" 2>/dev/null; done
}
trap matar_tudo EXIT

ok() { PASS=$((PASS+1)); echo "  ok   $1"; }
no() { FAIL=$((FAIL+1)); echo "  FALHOU $1"; }

secao() { echo; echo "=== $1 ==="; }

subir_agente() {
    ./bin/agent "$1" "$2" > "/tmp/mini_test_$2.log" 2>&1 &
    PIDS+=("$!")
}

secao "func: protocolo GET (um agente, porta 7301)"
subir_agente teste 7301
sleep 0.4

RESP=$(python3 - << 'EOF'
import socket
c = socket.create_connection(("127.0.0.1", 7301), timeout=2)
c.sendall(b"GET 1.1\n")
print(c.recv(256).decode().strip())
c.close()
EOF
)
case "$RESP" in
    "RESPONSE 1.1 "*) ok "GET 1.1 (CPU) devolve RESPONSE 1.1 <valor>" ;;
    *) no "GET 1.1 devolveu '$RESP'" ;;
esac

RESP=$(python3 - << 'EOF'
import socket
c = socket.create_connection(("127.0.0.1", 7301), timeout=2)
c.sendall(b"GET 9.9\n")
print(c.recv(256).decode().strip())
c.close()
EOF
)
[ "$RESP" = "ERROR oid desconhecido" ] && ok "GET de OID inexistente devolve ERROR" || no "GET 9.9 devolveu '$RESP'"

RESP=$(python3 - << 'EOF'
import socket
c = socket.create_connection(("127.0.0.1", 7301), timeout=2)
c.sendall(b"ALGOCOISA\n")
print(c.recv(256).decode().strip())
c.close()
EOF
)
[ "$RESP" = "ERROR requisicao invalida" ] && ok "requisicao malformada devolve ERROR" || no "requisicao malformada devolveu '$RESP'"

N_LINHAS=$(python3 - << 'EOF'
import socket
c = socket.create_connection(("127.0.0.1", 7301), timeout=2)
c.sendall(b"GET ALL\n")
c.settimeout(2)
buf = b""
while b"END\n" not in buf:
    d = c.recv(4096)
    if not d: break
    buf += d
c.close()
print(len(buf.decode().strip().split("\n")))
EOF
)
[ "$N_LINHAS" = "6" ] && ok "GET ALL devolve as 5 metricas da MIB + END" || no "GET ALL devolveu $N_LINHAS linhas (esperado 6)"

secao "func: valores dentro da faixa esperada"
RESULTADO=$(python3 - << 'EOF'
import socket
c = socket.create_connection(("127.0.0.1", 7301), timeout=2)
c.sendall(b"GET ALL\n")
c.settimeout(2)
buf = b""
while b"END\n" not in buf:
    buf += c.recv(4096)
c.close()
vals = {}
for l in buf.decode().strip().split("\n"):
    if l.startswith("RESPONSE"):
        _, oid, v = l.split()
        vals[oid] = float(v)
bom = 0 <= vals["1.1"] <= 100 and 0 <= vals["1.2"] <= 100 and vals["1.3"] >= 0 and vals["2.1"] >= 0 and vals["2.2"] >= 0
print("ok" if bom else "falhou")
EOF
)
[ "$RESULTADO" = "ok" ] && ok "CPU e Memoria em 0..100; Uptime e trafego >= 0" || no "algum valor fora da faixa esperada"

secao "concorrencia: dois agentes, manager consulta os dois"
subir_agente nodeX 7302
subir_agente nodeY 7303
sleep 0.4
cat > /tmp/mini_test_cfg.conf << CFG
nodeX 127.0.0.1 7302
nodeY 127.0.0.1 7303
CFG
rm -f /tmp/mini_test_mgr.log /tmp/mini_test_mgr.csv
./bin/manager -f /tmp/mini_test_cfg.conf -i 1 -n 1 -l /tmp/mini_test_mgr.log -c /tmp/mini_test_mgr.csv > /tmp/mini_test_mgr.out 2>&1
UPS=$(grep -c ",UP," /tmp/mini_test_mgr.csv)
[ "$UPS" = "2" ] && ok "manager marca os 2 agentes como UP na mesma rodada" || no "esperado 2 linhas UP no CSV, obtido $UPS"

secao "falha: agente derrubado e detectado; depois volta"
subir_agente nodeZ 7304
sleep 0.3
./bin/manager -f <(echo "nodeZ 127.0.0.1 7304") -i 1 -n 3 -l /tmp/mini_test_mgr2.log -c /tmp/mini_test_mgr2.csv > /tmp/mini_test_mgr2.out 2>&1 &
MPID=$!
PIDS+=("$MPID")
sleep 0.3
PID_Z=${PIDS[-2]}
kill -9 "$PID_Z" 2>/dev/null
wait "$MPID" 2>/dev/null
DOWN=$(grep -c "parou de responder" /tmp/mini_test_mgr2.log)
[ "$DOWN" -ge 1 ] && ok "manager detecta e registra a queda do agente (DOWN)" || no "nenhuma linha de DOWN no log"

secao "limites: nome/porta invalidos na linha de comando do agente"
OUT=$(./bin/agent 2>&1)
echo "$OUT" | grep -q "uso:" && ok "agente sem argumentos imprime a mensagem de uso e sai" || no "mensagem de uso ausente"

echo
echo "resultado: $PASS passaram, $FAIL falharam"
[ "$FAIL" -eq 0 ] && echo "TODOS OS TESTES PASSARAM"
[ "$FAIL" -eq 0 ]
