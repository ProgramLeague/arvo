default: arvo
clean :
	rm -rf arvo

arvo: main.c term.c typecheck.c telescope.c parser.c printing.c vernac.c context.c typing_context.c normalize.c  mpc/mpc.c dbg.c elaborate.c
	gcc -Wall -Wextra -Wno-format -g main.c term.c typecheck.c telescope.c parser.c vernac.c context.c typing_context.c normalize.c mpc/mpc.c printing.c dbg.c elaborate.c -o arvo -lm

.PHONY : default clean
