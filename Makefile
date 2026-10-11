CC ?= cc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -O2 -g
LDFLAGS =
LDLIBS = -lcurl -lm

SDL_CFLAGS := $(shell pkg-config --cflags sdl2 SDL2_ttf SDL2_image 2>/dev/null || sdl2-config --cflags 2>/dev/null)
SDL_LIBS   := $(shell pkg-config --libs sdl2 SDL2_ttf SDL2_image 2>/dev/null || sdl2-config --libs 2>/dev/null; pkg-config --libs SDL2_ttf 2>/dev/null; pkg-config --libs SDL2_image 2>/dev/null)

SRC = utils.c dom.c parser.c css.c color.c style.c search.c fetch.c layout.c gui.c image.c main.c
OBJ = $(SRC:.c=.o)
BIN = browser
GUI_BIN = browser-gui

LEGACY_SRC = browser.legacy.c

all: $(BIN) $(GUI_BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ $(OBJ) $(LDFLAGS) $(LDLIBS) $(SDL_LIBS)

$(GUI_BIN): $(OBJ)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ $(OBJ) $(LDFLAGS) $(LDLIBS) $(SDL_LIBS)

%.o: %.c
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -c $< -o $@

legacy: $(LEGACY_SRC)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $(BIN) $< $(LDFLAGS) $(LDLIBS) $(SDL_LIBS)

no-curl:
	$(CC) $(CFLAGS) -o $(BIN) $(SRC) -lm

no-sdl:
	$(CC) $(CFLAGS) -o $(BIN) $(SRC) $(LDFLAGS) -lcurl -lm

test: all
	./$(BIN)

test-query: all
	./$(BIN) --query "p.note"

test-gui: $(GUI_BIN)
	@echo "Launching GUI (close window to finish test)..."
	./$(GUI_BIN) --gui test.html || ./$(GUI_BIN) --gui about:blank

test-color: $(BIN)
	@echo "hsl(0 100% 50%) -> red" && ./$(BIN) --query "*" | head

clean:
	rm -f $(BIN) $(GUI_BIN) *.o
	rm -rf *.dSYM browser.dSYM $(BIN).dSYM $(GUI_BIN).dSYM

.PHONY: all legacy no-curl no-sdl test test-query test-gui test-color clean
