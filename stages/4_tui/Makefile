# pmview: user-space TUI for the pmon v2.0 module (stage 3)
#   make        build ./pmview
#   make clean
pmview: pmview.c
	$(CC) -O2 -Wall -Wextra -o $@ $< -lncursesw

clean:
	rm -f pmview

.PHONY: clean
