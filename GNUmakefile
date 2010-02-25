
# Common
prefix=     /usr/local
bindir=     $(prefix)/bin

CFLAGS+= -Wall -Wextra -Werror -Wsign-conversion -Wshadow -Wno-unused
CFLAGS+= -g

LDFLAGS= -g

# OSS specific
include /etc/oss.conf

# Target: mixoss
mixoss_SRC= mixoss.c
mixoss_OBJ= $(subst .c,.o,$(mixoss_SRC))
mixoss_BIN= $(subst .o,,$(mixoss_OBJ))

$(mixoss_BIN): CFLAGS+=  -I$(OSSLIBDIR)/include/sys
$(mixoss_BIN): LDFLAGS+=
$(mixoss_BIN): LDLIBS+=

# Rules
all: $(mixoss_BIN)

$(mixoss_BIN): $(mixoss_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

clean:
	$(RM) $(mixoss_BIN) $(mixoss_OBJ)

install: all
	mkdir -p $(bindir)
	install -m 755 $(mixoss_BIN) $(bindir)

uninstall:
	$(RM) $(addprefix $(bindir)/,$(mixoss_BIN))

tags:
	ctags -o tags -a $(wildcard *.[hc])

.PHONY: all clean install uninstall tags

