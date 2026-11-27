CFLAGS ?= -O2 -g -Wall -Wextra -Wno-unused-parameter
pnv-proto-service: service.c
	$(CC) $(CFLAGS) -o $@ $< $(shell pkg-config --cflags --libs libnm)
clean:
	rm -f pnv-proto-service
