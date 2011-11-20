TARGETNAME=rufus

CC     = gcc
RC     = windres
STRIP  = strip
CFLAGS = -std=gnu99 -Wall -Wundef -Wunused -Wstrict-prototypes -Werror-implicit-function-declaration -Wno-pointer-sign -Wshadow -O2 -Wl,--subsystem,windows
LIBS   = -lsetupapi -lole32

.PHONY: all clean

all: $(TARGETNAME)

$(TARGETNAME): $(TARGETNAME).c $(TARGETNAME)_rc.o
	@echo "[CCLD]  $@"
	@$(CC) -o $@ $(CFLAGS) $^ $(TARGETNAME)_rc.o $(LIBS)
	@$(STRIP) $(TARGETNAME).exe

$(TARGETNAME)_rc.o: $(TARGETNAME).rc
	@echo "[RC]    $@"
	@$(RC) -i $< -o $@

clean:
	rm -f *.exe *.o
