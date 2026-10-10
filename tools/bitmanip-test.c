/*
 * Check which RISC-V bit-manipulation extensions the CPU implements:
 * Zba (address generation), Zbb (basic bit manipulation), Zbc (carry-less
 * multiplication) and Zbs (single-bit instructions).
 *
 * The program itself is built for rv32imac. Each instruction under test
 * sits alone in its own inline assembly, enabled there with ".option arch",
 * so a missing extension raises SIGILL at exactly that instruction and the
 * program reports it and goes on with the next one.
 *
 * Build it static, so it does not depend on the libraries on the stick:
 *   riscv32-alpine-linux-musl-gcc -static -O2 -march=rv32imac -mabi=ilp32 -o bitmanip-test bitmanip-test.c
 *
 * For every instruction it prints "ok", "SIGILL" (not implemented), or
 * "wrong" with the value it got and the value expected, then one line per
 * extension. It exits with 0 only when all four extensions work.
 */
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* rd = insn rs1, rs2 */
#define RR(fn, ext, insn)						\
static uint32_t fn(uint32_t a, uint32_t b)				\
{									\
	uint32_t r;							\
									\
	__asm__ volatile(".option push\n\t"				\
			 ".option arch, +" ext "\n\t"			\
			 insn " %0, %1, %2\n\t"				\
			 ".option pop"					\
			 : "=r"(r) : "r"(a), "r"(b));			\
	return r;							\
}

/* rd = insn rs1 (b is not used) */
#define R1(fn, ext, insn)						\
static uint32_t fn(uint32_t a, uint32_t b)				\
{									\
	uint32_t r;							\
									\
	(void)b;							\
	__asm__ volatile(".option push\n\t"				\
			 ".option arch, +" ext "\n\t"			\
			 insn " %0, %1\n\t"				\
			 ".option pop"					\
			 : "=r"(r) : "r"(a));				\
	return r;							\
}

/* rd = insn rs1, imm (b only records the immediate for the table) */
#define RI(fn, ext, insn, imm)						\
static uint32_t fn(uint32_t a, uint32_t b)				\
{									\
	uint32_t r;							\
									\
	(void)b;							\
	__asm__ volatile(".option push\n\t"				\
			 ".option arch, +" ext "\n\t"			\
			 insn " %0, %1, " #imm "\n\t"			\
			 ".option pop"					\
			 : "=r"(r) : "r"(a));				\
	return r;							\
}

RR(t_sh1add, "zba", "sh1add")
RR(t_sh2add, "zba", "sh2add")
RR(t_sh3add, "zba", "sh3add")

RR(t_andn, "zbb", "andn")
RR(t_orn, "zbb", "orn")
RR(t_xnor, "zbb", "xnor")
R1(t_clz, "zbb", "clz")
R1(t_ctz, "zbb", "ctz")
R1(t_cpop, "zbb", "cpop")
RR(t_max, "zbb", "max")
RR(t_maxu, "zbb", "maxu")
RR(t_min, "zbb", "min")
RR(t_minu, "zbb", "minu")
R1(t_sext_b, "zbb", "sext.b")
R1(t_sext_h, "zbb", "sext.h")
R1(t_zext_h, "zbb", "zext.h")
RR(t_rol, "zbb", "rol")
RR(t_ror, "zbb", "ror")
RI(t_rori, "zbb", "rori", 4)
R1(t_orc_b, "zbb", "orc.b")
R1(t_rev8, "zbb", "rev8")

RR(t_clmul, "zbc", "clmul")
RR(t_clmulh, "zbc", "clmulh")
RR(t_clmulr, "zbc", "clmulr")

RR(t_bset, "zbs", "bset")
RR(t_bclr, "zbs", "bclr")
RR(t_binv, "zbs", "binv")
RR(t_bext, "zbs", "bext")
RI(t_bseti, "zbs", "bseti", 31)
RI(t_bclri, "zbs", "bclri", 28)
RI(t_binvi, "zbs", "binvi", 1)
RI(t_bexti, "zbs", "bexti", 2)

/* The expected values, computed with base instructions only. */
static uint32_t clz(uint32_t x)
{
	uint32_t n = 0;

	while (n < 32 && !(x & 0x80000000u)) {
		x <<= 1;
		n++;
	}
	return n;
}

static uint32_t ctz(uint32_t x)
{
	uint32_t n = 0;

	while (n < 32 && !(x & 1)) {
		x >>= 1;
		n++;
	}
	return n;
}

static uint32_t cpop(uint32_t x)
{
	uint32_t n = 0;

	for (; x; x >>= 1)
		n += x & 1;
	return n;
}

/* Carry-less product: shifted copies of a combined with XOR, not addition. */
static uint64_t clmul64(uint32_t a, uint32_t b)
{
	uint64_t p = 0;

	for (int i = 0; i < 32; i++)
		if (b >> i & 1)
			p ^= (uint64_t)a << i;
	return p;
}

static uint32_t orc_b(uint32_t x)
{
	uint32_t r = 0;

	for (int i = 0; i < 32; i += 8)
		if (x >> i & 0xff)
			r |= 0xffu << i;
	return r;
}

static const char *const exts[] = { "zba", "zbb", "zbc", "zbs" };

struct test {
	const char *ext;
	const char *insn;
	uint32_t (*run)(uint32_t a, uint32_t b);
	uint32_t a, b, want;
};

static sigjmp_buf env;
static unsigned int passed[4], total[4];

static void on_sigill(int sig)
{
	(void)sig;
	siglongjmp(env, 1);
}

static int ext_index(const char *ext)
{
	for (int i = 0; i < 4; i++)
		if (!strcmp(ext, exts[i]))
			return i;
	return 0;
}

int main(void)
{
	/* a has a different value in every byte; n is negative as int32_t. */
	const uint32_t a = 0x12345678, b = 0x0f0f0f0f, n = 0xf0f0f0f0;
	const struct test tests[] = {
		{ "zba", "sh1add", t_sh1add, a, b, (a << 1) + b },
		{ "zba", "sh2add", t_sh2add, a, b, (a << 2) + b },
		{ "zba", "sh3add", t_sh3add, a, b, (a << 3) + b },

		{ "zbb", "andn", t_andn, a, b, a & ~b },
		{ "zbb", "orn", t_orn, a, b, a | ~b },
		{ "zbb", "xnor", t_xnor, a, b, ~(a ^ b) },
		{ "zbb", "clz", t_clz, 0x00f00000, 0, clz(0x00f00000) },
		{ "zbb", "ctz", t_ctz, 0x00f00000, 0, ctz(0x00f00000) },
		{ "zbb", "cpop", t_cpop, a, 0, cpop(a) },
		{ "zbb", "max", t_max, a, n, a },
		{ "zbb", "maxu", t_maxu, a, n, n },
		{ "zbb", "min", t_min, a, n, n },
		{ "zbb", "minu", t_minu, a, n, a },
		{ "zbb", "sext.b", t_sext_b, 0x12345680, 0, 0xffffff80 },
		{ "zbb", "sext.h", t_sext_h, 0x12348000, 0, 0xffff8000 },
		{ "zbb", "zext.h", t_zext_h, a, 0, a & 0xffff },
		{ "zbb", "rol", t_rol, a, 8, a << 8 | a >> 24 },
		{ "zbb", "ror", t_ror, a, 8, a >> 8 | a << 24 },
		{ "zbb", "rori", t_rori, a, 4, a >> 4 | a << 28 },
		{ "zbb", "orc.b", t_orc_b, 0x12005600, 0, orc_b(0x12005600) },
		{ "zbb", "rev8", t_rev8, a, 0, 0x78563412 },

		{ "zbc", "clmul", t_clmul, a, b, (uint32_t)clmul64(a, b) },
		{ "zbc", "clmulh", t_clmulh, a, b, (uint32_t)(clmul64(a, b) >> 32) },
		{ "zbc", "clmulr", t_clmulr, a, b, (uint32_t)(clmul64(a, b) >> 31) },

		{ "zbs", "bset", t_bset, a, 5, a | 1u << 5 },
		{ "zbs", "bclr", t_bclr, a, 4, a & ~(1u << 4) },
		{ "zbs", "binv", t_binv, a, 0, a ^ 1u },
		{ "zbs", "bext", t_bext, a, 3, a >> 3 & 1 },
		{ "zbs", "bseti", t_bseti, a, 31, a | 1u << 31 },
		{ "zbs", "bclri", t_bclri, a, 28, a & ~(1u << 28) },
		{ "zbs", "binvi", t_binvi, a, 1, a ^ 1u << 1 },
		{ "zbs", "bexti", t_bexti, a, 2, a >> 2 & 1 },
	};
	struct sigaction sa;
	int missing = 0;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_sigill;
	sigaction(SIGILL, &sa, NULL);

	for (unsigned int i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
		const struct test *t = &tests[i];
		int e = ext_index(t->ext);
		uint32_t got;

		total[e]++;
		/* 1: restore the signal mask, so the next SIGILL is caught too. */
		if (sigsetjmp(env, 1)) {
			printf("%s %-7s SIGILL\n", t->ext, t->insn);
			continue;
		}
		got = t->run(t->a, t->b);
		if (got == t->want) {
			printf("%s %-7s ok\n", t->ext, t->insn);
			passed[e]++;
		} else {
			printf("%s %-7s wrong: %08x, expected %08x\n",
			       t->ext, t->insn, (unsigned int)got,
			       (unsigned int)t->want);
		}
	}

	for (int e = 0; e < 4; e++) {
		printf("%s: %u of %u instructions ok\n", exts[e], passed[e],
		       total[e]);
		if (passed[e] != total[e])
			missing = 1;
	}
	puts(missing ? "NOT ALL PRESENT" : "ALL PRESENT");
	return missing;
}
