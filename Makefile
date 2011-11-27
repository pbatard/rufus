TARGETNAME=rufus

CC     = gcc
RC     = windres
STRIP  = strip
CFLAGS = -std=gnu99 -Wall -Wundef -Wunused -Wstrict-prototypes -Werror-implicit-function-declaration -Wno-pointer-sign -Wshadow -O2 -Wl,--subsystem,windows -DWINVER=0x501 -D_WIN32_IE=0x501
LIBS   = -lsetupapi -lole32 -lgdi32

RUFUS_SRC = rufus.c stdlg.c msdos.c

.PHONY: all clean

all: $(TARGETNAME)

$(TARGETNAME): $(RUFUS_SRC) $(TARGETNAME)_rc.o
	@echo "[CCLD]  $@"
	@$(CC) -o $@ $(CFLAGS) $^ $(TARGETNAME)_rc.o $(LIBS)
	@$(STRIP) $(TARGETNAME).exe

$(TARGETNAME)_rc.o: $(TARGETNAME).rc
	@echo "[RC]    $@"
	@$(RC) -i $< -o $@

clean:
	rm -f *.exe *.o
