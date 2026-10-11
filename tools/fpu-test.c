/*
 * Check that user space can run single-precision float instructions (the
 * F extension) and that the kernel keeps the float registers of each
 * process apart across context switches.
 *
 * Build for riscv32 with F instructions but the soft-float calling
 * convention, so it links against Alpine's soft-float musl:
 *   riscv32-alpine-linux-musl-gcc -static -O2 -march=rv32imafc -mabi=ilp32 -o fpu-test fpu-test.c
 *
 * Usage: fpu-test [processes]   (default 2)
 * Each process runs the same float loop five times with its own seed and
 * checks that every round gives the same bits. A process killed by SIGILL
 * means the F instructions are not available.
 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* noipa: the compiler must not merge the five calls with the same seed. */
static __attribute__((noipa)) unsigned int run(float seed)
{
	float x = seed, y = 1.0f;
	unsigned int bits;

	for (int i = 0; i < 20000000; i++) {
		x = x * 1.0000001f + 0.5f;
		if (x > 1000.0f)
			x -= 999.0f;
		y = y * 0.9999999f + x * 1e-6f;
	}
	memcpy(&bits, &y, sizeof(bits));
	return bits;
}

static int child(int p)
{
	float seed = 1.0f + p;
	unsigned int first = run(seed);

	for (int r = 1; r < 5; r++) {
		unsigned int again = run(seed);

		if (again != first) {
			printf("process %d, round %d: %08x, first round %08x\n",
			       p, r, again, first);
			return 1;
		}
	}
	printf("process %d: %08x in all 5 rounds\n", p, first);
	return 0;
}

int main(int argc, char **argv)
{
	int procs = argc > 1 ? atoi(argv[1]) : 2;
	int status, failed = 0;

	for (int p = 0; p < procs; p++) {
		pid_t pid = fork();

		if (pid < 0) {
			perror("fork");
			return 2;
		}
		if (pid == 0) {
			int ret = child(p);

			fflush(stdout);
			_exit(ret);
		}
	}
	while (wait(&status) > 0) {
		if (WIFSIGNALED(status)) {
			printf("a process died from signal %d (%s)\n",
			       WTERMSIG(status), strsignal(WTERMSIG(status)));
			failed++;
		} else if (WEXITSTATUS(status) != 0) {
			failed++;
		}
	}
	puts(failed ? "FAILED" : "OK");
	return failed != 0;
}
