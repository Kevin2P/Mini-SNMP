CC      = gcc
CFLAGS  = -std=gnu11 -O2 -Wall -Wextra -Wpedantic
LDFLAGS =

COMMON_SRC = common/net.c
AGENT_SRC  = agent/agent.c agent/metrics.c $(COMMON_SRC)
MANAGER_SRC = manager/manager.c $(COMMON_SRC)

.PHONY: all clean test

all: bin/agent bin/manager

bin/agent: $(AGENT_SRC) common/common.h common/net.h agent/metrics.h
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ $(AGENT_SRC) $(LDFLAGS)

bin/manager: $(MANAGER_SRC) common/common.h common/net.h
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ $(MANAGER_SRC) $(LDFLAGS)

clean:
	rm -rf bin *.log *.csv tests/*.log tests/*.csv

test: all
	bash tests/run_tests.sh
