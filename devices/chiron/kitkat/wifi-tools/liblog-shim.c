/* liblog-shim.c — native (ALOG) logging redirected to stderr, dependency free.
 *
 * The 4.4.247 kernel this port boots has no Android "logger" driver and
 * there is no logd in the KitKat tree, so the /dev/log devices never exist and
 * every vendor blob that logs through liblog (cnss-daemon, the audio HAL, ...)
 * is completely silent — which makes on-device bring-up guesswork. Preloading
 * this over liblog prints the same text on stderr, where the keeper scripts
 * already redirect it.
 *
 * Built with -nostdlib and raw syscalls on purpose: a glibc-linked .so carries
 * SHT_GNU_verneed entries, and Android's linker refuses to bind them against
 * bionic ("cannot locate symbol stderr"), so the shim must reference nothing.
 */

#include <stdarg.h>
#include <stddef.h>

#if defined(__aarch64__)

#define NR_write 64
#define NR_getpid 172

static long syscall2(long nr, long a0, long a1)
{
	register long x8 __asm__("x8") = nr;
	register long x0 __asm__("x0") = a0;
	register long x1 __asm__("x1") = a1;

	__asm__ volatile("svc #0"
			 : "+r"(x0)
			 : "r"(x8), "r"(x1)
			 : "memory", "cc", "x2", "x3", "x4", "x5", "x6", "x7");
	return x0;
}

static long syscall3(long nr, long a0, long a1, long a2)
{
	register long x8 __asm__("x8") = nr;
	register long x0 __asm__("x0") = a0;
	register long x1 __asm__("x1") = a1;
	register long x2 __asm__("x2") = a2;

	__asm__ volatile("svc #0"
			 : "+r"(x0)
			 : "r"(x8), "r"(x1), "r"(x2)
			 : "memory", "cc", "x3", "x4", "x5", "x6", "x7");
	return x0;
}

#elif defined(__arm__)

/* ARM EABI (legacy) numbers, not the __NR_* asm-generic ones. */
#define NR_write 4
#define NR_getpid 20

static long syscall2(long nr, long a0, long a1)
{
	register long r0 __asm__("r0") = a0;
	register long r1 __asm__("r1") = a1;
	register long r7 __asm__("r7") = nr;

	__asm__ volatile("swi 0"
			 : "+r"(r0)
			 : "r"(r1), "r"(r7)
			 : "memory", "cc", "r2", "r3");
	return r0;
}

static long syscall3(long nr, long a0, long a1, long a2)
{
	register long r0 __asm__("r0") = a0;
	register long r1 __asm__("r1") = a1;
	register long r2 __asm__("r2") = a2;
	register long r7 __asm__("r7") = nr;

	__asm__ volatile("swi 0"
			 : "+r"(r0)
			 : "r"(r1), "r"(r2), "r"(r7)
			 : "memory", "cc", "r3");
	return r0;
}

#else
#error "liblog-shim only knows the aarch64 and arm syscall conventions"
#endif

#define OUT_MAX 1024

struct out {
	char *p;
	char *end;
};

static void putch(struct out *o, char c)
{
	if (o->p < o->end)
		*o->p++ = c;
}

static void puts_n(struct out *o, const char *s, int n)
{
	while (n-- > 0)
		putch(o, *s++);
}

/* Shift-and-subtract division: the compiler otherwise emits calls to
 * __aeabi_uidiv, and the shim must stay free of undefined symbols. */
static unsigned long udiv(unsigned long n, unsigned long d, unsigned long *rem)
{
	unsigned long q = 0, r = 0;
	int bits = (int)sizeof(unsigned long) * 8;
	int i;

	for (i = bits - 1; i >= 0; i--) {
		r = (r << 1) | ((n >> i) & 1u);
		if (r >= d) {
			r -= d;
			q |= 1ul << i;
		}
	}
	*rem = r;
	return q;
}

static void put_num(struct out *o, unsigned long val, int base, int upper, int width)
{
	char tmp[24];
	const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
	int n = 0;
	int pad;
	unsigned long rem;

	if (val == 0)
		tmp[n++] = '0';
	while (val) {
		val = udiv(val, (unsigned long)base, &rem);
		tmp[n++] = digits[rem];
	}
	for (pad = width - n; pad > 0; pad--)
		putch(o, '0');
	while (n > 0)
		putch(o, tmp[--n]);
}

/* Prints the ALOG record itself; %pK (the kernel-style pointer format the QC
 * sources use) is handled so it does not leak a literal 'K' into the line. */
static int render(struct out *o, const char *fmt, va_list ap)
{
	char *start = o->p;

	while (*fmt) {
		char c = *fmt++;
		unsigned long val;
		int width = 0, longsz = 0, base = 10, upper = 0, alt = 0;
		int digits, pad;
		const char *s;

		if (c != '%') {
			putch(o, c);
			continue;
		}

		for (;;) {
			c = *fmt++;
			if (c == '-')
				;	/* left alignment is meaningless here */
			else if (c == '0')
				alt = 1;
			else if (c == '#')
				alt = 2;
			else if (c == 'l' || c == 'z' || c == 'q' || c == 'j')
				longsz = 1;
			else if (c == 'h')
				;
			else if (c >= '0' && c <= '9')
				width = width * 10 + (c - '0');
			else
				break;
		}

		if (c == '%') {
			putch(o, '%');
			continue;
		}

		switch (c) {
		case 's':
			s = va_arg(ap, const char *);
			if (!s)
				s = "(null)";
			digits = 0;
			while (s[digits])
				digits++;
			for (pad = width - digits; pad > 0; pad--)
				putch(o, ' ');
			puts_n(o, s, digits);
			break;
		case 'c':
			putch(o, (char)va_arg(ap, int));
			break;
		case 'p':
			val = (unsigned long)va_arg(ap, void *);
			if (*fmt == 'K')
				fmt++;
			puts_n(o, "0x", 2);
			put_num(o, val, 16, 0, 16);
			break;
		case 'd':
		case 'i':
			val = longsz ? (unsigned long)va_arg(ap, long)
				     : (unsigned long)(int)va_arg(ap, int);
			if ((long)val < 0) {
				putch(o, '-');
				val = (unsigned long)(-(long)val);
			}
			put_num(o, val, 10, 0, width);
			break;
		case 'u':
			val = longsz ? va_arg(ap, unsigned long)
				     : (unsigned long)va_arg(ap, unsigned int);
			put_num(o, val, 10, 0, width);
			break;
		case 'x':
		case 'X':
			val = longsz ? va_arg(ap, unsigned long)
				     : (unsigned long)va_arg(ap, unsigned int);
			base = 16;
			upper = (c == 'X');
			if (alt && val)
				puts_n(o, "0x", 2);
			put_num(o, val, base, upper, alt == 1 ? width : width);
			break;
		default:
			putch(o, '%');
			if (width)
				put_num(o, (unsigned long)width, 10, 0, 0);
			putch(o, c);
			break;
		}
	}

	return (int)(o->p - start);
}

static const char *const lvl_tag[] = { "??", "??", "VV", "DD", "II", "WW", "EE", "AA", "SS" };

static int log_to_stderr(int prio, const char *tag, const char *fmt, va_list ap)
{
	static char buf[OUT_MAX];
	struct out o;
	int n;

	o.p = buf;
	o.end = buf + sizeof(buf) - 1;

	puts_n(&o, lvl_tag[(prio >= 0 && prio <= 8) ? prio : 0], 2);
	putch(&o, '/');
	{
		const char *t = tag ? tag : "?";
		while (*t && o.p < o.end)
			putch(&o, *t++);
	}
	putch(&o, ':');
	{
		long pid = syscall2(NR_getpid, 0, 0);
		unsigned long u = pid < 0 ? 0 : (unsigned long)pid;
		putch(&o, ' ');
		putch(&o, '(');
		put_num(&o, u, 10, 0, 0);
		puts_n(&o, ") ", 2);
	}

	n = render(&o, fmt, ap);
	putch(&o, '\n');
	*o.p = '\0';

	syscall3(NR_write, 2, (long)buf, (long)(o.p - buf));
	return n;
}

int __android_log_print(int prio, const char *tag, const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = log_to_stderr(prio, tag, fmt, ap);
	va_end(ap);
	return n;
}

int __android_log_vprint(int prio, const char *tag, const char *fmt, va_list ap)
{
	return log_to_stderr(prio, tag, fmt, ap);
}

int __android_log_write(int prio, const char *tag, const char *msg)
{
	return __android_log_print(prio, tag, "%s", msg ? msg : "(null)");
}

int __android_log_buf_print(int buf_id, int prio, const char *tag, const char *fmt, ...)
{
	va_list ap;
	int n;

	(void)buf_id;
	va_start(ap, fmt);
	n = log_to_stderr(prio, tag, fmt, ap);
	va_end(ap);
	return n;
}

int __android_log_buf_write(int buf_id, int prio, const char *tag, const char *msg)
{
	return __android_log_buf_print(buf_id, prio, tag, "%s", msg ? msg : "(null)");
}

int __android_log_is_loggable(int prio, const char *tag, int def)
{
	(void)tag;
	return prio <= 4 ? 1 : def;
}

void __android_log_close(void) { }
