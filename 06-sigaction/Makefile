PROG = sigaction
FLAGS += -Wall

${PROG}: ${PROG}.c
	gcc $< -o $@ ${FLAGS}

strace: rel
	strace -ff ./{$PROG}

debug: deb

clean:
	rm -f ./${PROG}
