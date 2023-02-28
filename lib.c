/* lib.c - MemTest-86  Version 3.0
 *
 * Released under version 2 of the Gnu Public License.
 * By Chris Brady, cbrady@sgi.com
 * ----------------------------------------------------
 * MemTest86+ V4.00 Specific code (GPL V2.0)
 * By Samuel DEMEULEMEESTER, memtest@memtest.org
 * http://www.canardplus.com - http://www.memtest.org
*/
 
#include "io.h"
//#include "serial.h"
#include "test.h"
#include "config.h"
#include "screen_buffer.h"
#include "defs.h"	// ADD
//#include "smp.h"

#define NULL 0

int slock = 0, lsr = 0;
short serial_cons = SERIAL_CONSOLE_DEFAULT;

#if SERIAL_TTY != 0 && SERIAL_TTY != 1
#error Bad SERIAL_TTY. Only ttyS0 and ttyS1 are supported.
#endif
short serial_tty = SERIAL_TTY;
const short serial_base_ports[] = {0x3f8, 0x2f8, 0x3e8, 0x2e8};

#if ((115200%SERIAL_BAUD_RATE) != 0)
#error Bad default baud rate
#endif
int serial_baud_rate = SERIAL_BAUD_RATE;
unsigned char serial_parity = 0;
unsigned char serial_bits = 8;

char buf[18];

struct ascii_map_str {
	int ascii;
	int keycode;
};

char *codes[] = {
	"  Divide",
	"   Debug",
	"     NMI",
	"  Brkpnt",
	"Overflow",
	"   Bound",
	"  Inv_Op",
	" No_Math",
	"Double_Fault",
	"Seg_Over",
	" Inv_TSS",
	"  Seg_NP",
	"Stack_Fault",
	"Gen_Prot",
	"Page_Fault",
	"   Resvd",
	"     FPE",
	"Alignment",
	" Mch_Chk",
	"SIMD FPE"
};

struct eregs {
	ulong ss;
	ulong ds;
	ulong esp;
	ulong ebp;
	ulong esi;
	ulong edi;
	ulong edx;
	ulong ecx;
	ulong ebx;
	ulong eax;
	ulong vect;
	ulong code;
	ulong eip;
	ulong cs;
	ulong eflag;
};

int memcmp(const void *s1, const void *s2, ulong count)
{
	const unsigned char *src1 = s1, *src2 = s2;
	int i;
	for(i = 0; i < count; i++) {
		if (src1[i] != src2[i]) {
			return (int)src1[i] - (int)src2[i];
		}
	}
	return 0;
}

void memcpy (void *dst, void *src, int len)
{
	char *s = (char*)src;
	char *d = (char*)dst;
	int i;

	if (len <= 0) {
		return;
	}
	for (i = 0 ; i < len; i++) {
		*d++ = *s++;
	} 
}
int strncmp(const char *s1, const char *s2, ulong n) {
	signed char res = 0;
	while (n) {
		res = *s1 - *s2;
		if (res != 0)
			return res;
		if (*s1 == '\0')
			return 0;
		++s1, ++s2;
		--n;
	}
	return res;
}

void *memmove(void *dest, const void *src, ulong n)
{
	long i;
	char *d = (char *)dest, *s = (char *)src;

	/* If src == dest do nothing */
	if (dest < src) {
		for(i = 0; i < n; i++) {
			d[i] = s[i];
		}
	}
	else if (dest > src) {
		for(i = n -1; i >= 0; i--) {
			d[i] = s[i];
		}
	}
	return dest;
}

char toupper(char c)
{
	if (c >= 'a' && c <= 'z')
		return c + 'A' -'a';
	else
		return c;
}

int isdigit(char c)
{
	return c >= '0' && c <= '9';
}

int isxdigit(char c)
{
	return isdigit(c) || (toupper(c) >= 'A' && toupper(c) <= 'F'); }

unsigned long simple_strtoul(const char *cp, char **endp, unsigned int base) {
	unsigned long result = 0, value;

	if (!base) {
		base = 10;
		if (*cp == '0') {
			base = 8;
			cp++;
			if (toupper(*cp) == 'X' && isxdigit(cp[1])) {
				cp++;
				base = 16;
			}
		}
	} else if (base == 16) {
		if (cp[0] == '0' && toupper(cp[1]) == 'X')
			cp += 2;
	}
	while (isxdigit(*cp) &&
		(value = isdigit(*cp) ? *cp-'0' : toupper(*cp)-'A'+10) < base) {
		result = result*base + value;
		cp++;
	}
	if (endp)
		*endp = (char *)cp;
	return result;
}

/*
	FM TOWNS VRAM char write
*/
void cput(int y, int x, char c)
{
	int i;
	unsigned short pos = y*(0x500)+x;	// 1 line = 0x50 * 16 line
	char *cvram = (char *)SCREEN_ADR+y*160+x*2;
	char *vram = (char *)0xC0000;
	char *cgram = (char *)(0xCA000+c*8);
	*cvram = c;
	outb(1, IO_KVRAM_OR_ANKFONT);	// Use ANK
	for (i=0; i<8; i++) {
		vram[pos] = vram[pos+0x50] = cgram[i];
		pos += 0xA0;
	}
	outb(0, IO_KVRAM_OR_ANKFONT);	// Use KANJI
}

/*
 * Scroll the error message area of the screen as needed
 * Starts at line LINE_SCROLL and ends at line 23
 */
void scroll(void)
{
	int i, j;
	char *s, tmp;

	/* Only scroll if at the bottom of the screen */
	if (v->msg_line < 23) {
		v->msg_line++;
	} else {
		/* If scroll lock is on, loop till it is cleared */
		while (slock) {
			check_input();
		}
#if 0
		for (i=LINE_SCROLL; i<23; i++) {
			s = (char *)(SCREEN_ADR + ((i+1) * 160));
			for (j=0; j<160; j+=2, s+=2) {
				*(s-160) = *s;
				tmp = get_scrn_buf(i+1, j/2);
				set_scrn_buf(i, j/2, tmp);
			}
		}
		/* Clear the newly opened line */
		s = (char *)(SCREEN_ADR + (23 * 160));
		for (j=0; j<80; j++) {
			*s = ' ';
			set_scrn_buf(23, j, ' ');
			s += 2;
		}
		tty_print_region(LINE_SCROLL, 0, 23, 79);
#else
		//FM TOWNS
		s = (char *)(VRAM_ADR + ((LINE_SCROLL+1) * 0x500));
		memcpy(s-1*0x500, s, 0x500*(23-LINE_SCROLL));
		/* Clear the newly opened line */
		for (j=0; j<80; j++) {
			cput(23, j, ' ');
		}
#endif
	}
}

/*
 * Clear scroll region
 */
void clear_scroll(void)
{
	int i;
	char *s;

#if 0
	s = (char*)(SCREEN_ADR+LINE_HEADER*160);
    for(i=0; i<80*(24-LINE_HEADER); i++) {
            *s++ = ' ';
            *s++ = 0x17;
    }
#else
	//FM TOWNS
	s = (char *)VRAM_ADR+LINE_HEADER*(0x500);
    for(i=0; i<(24-LINE_HEADER)*0x500; i++) {
		*s++ = '\0';
	}
#endif
}

/*
 * Print characters on screen
 */
void cprint(int y, int x, const char *text)
{
	register int i;
	for (i=0; text[i]; i++) {
		cput(y, x+i, text[i]);
	}
	tty_print_line(y, x, text);
}

void itoa(char s[], int n)
{
	int i, sign;

	if((sign = n) < 0)
		n = -n;
	i=0;
	do {
		s[i++] = n % 10 + '0';
	} while ((n /= 10) > 0);
	if(sign < 0)
		s[i++] = '-';
	s[i] = '\0';
	reverse(s);
}

void reverse(char s[])
{
	int c, i, j;
	for(j = 0; s[j] != 0; j++)
		;

	for(i=0, j = j - 1; i < j; i++, j--) {
		c = s[i];
		s[i] = s[j];
		s[j] = c;
	}
}

/*
 * Print a people friendly address
 */
void aprint(int y, int x, ulong page)
{
	/* page is in multiples of 4K */
	if ((page << 2) < 9999) {
		dprint(y, x, page << 2, 4, 0);
		cprint(y, x+4, "K");
	}
	else if ((page >>8) < 9999) {
		dprint(y, x, (page  + (1 << 7)) >> 8, 4, 0);
		cprint(y, x+4, "M");
	}
	else if ((page >>18) < 9999) {
		dprint(y, x, (page + (1 << 17)) >> 18, 4, 0);
		cprint(y, x+4, "G");
	}
	else {
		dprint(y, x, (page + (1 << 27)) >> 28, 4, 0);
		cprint(y, x+4, "T");
	}
}

/*
 * Print a decimal number on screen
 */
void dprint(int y, int x, ulong val, int len, int right)
{
	ulong j, k;
	int i, flag=0;

	if (val > 999999999 || len > 9) {
		return;
	}
	for(i=0, j=1; i<len-1; i++) {
		j *= 10;
	}
	if (!right) {
		for (i=0; j>0; j/=10) {
			k = val/j;
			if (k > 9) {
				j *= 100;
				continue;
			}
			if (flag || k || j == 1) {
				buf[i++] = k + '0';
				flag++;
			} else {
				buf[i++] = ' ';
			}
			val -= k * j;
		}
	} else {
		for(i=0; i<len; j/=10) {
			if (j) {
				k = val/j;
					if (k > 9) {
					j *= 100;
					len++;
					continue;
				}
				if (k == 0 && flag == 0) {
					continue;
				}
				buf[i++] = k + '0';
				val -= k * j;
			} else {
				if (flag == 0 &&  i < len-1) {
					buf[i++] = '0';
				} else {
					buf[i++] = ' ';
				}
			}
			flag++;
		}
	}
	buf[i] = 0;
	cprint(y,x,buf);
}


/*
 * Get_number of digits
 */
int getnum(ulong val)
{
	int len = 0;
	int i = 1;
	
	while(i <= val)
	{
		len++;
		i *= 10;
	}

	return len;
		
}

/*
 * Print a hex number on screen at least digits long
 */
void hprint2(int y,int x, unsigned long val, int digits)
{
        unsigned long j;
        int i, idx, flag = 0;

        for (i=0, idx=0; i<8; i++) {
                j = val >> (28 - (4 * i));
                j &= 0xf;
                if (j < 10) {
                        if (flag || j || i == 7) {
                                buf[idx++] = j + '0';
                                flag++;
                        } else {
                                buf[idx++] = '0';
                        }
                } else {
                        buf[idx++] = j + 'a' - 10;
                        flag++;
                }
        }
        if (digits > 8) {
                digits = 8;
        }
        if (flag > digits) {
                digits = flag;
        }
        buf[idx] = 0;
        cprint(y,x,buf + (idx - digits));
}

/*
 * Print a hex number on screen exactly digits long
 */
void hprint3(int y,int x, unsigned long val, int digits)
{
	unsigned long j;
	int i, idx, flag = 0;


	for (i=0, idx=0; i<digits; i++) {
		j = 0xf & val;
		val /= 16;

		if (j < 10) {
			if (flag || j || i == 7) {
				buf[digits - ++idx] = j + '0';
				flag++;
			} else {
				buf[digits - ++idx] = '0';
			}
		} else {
			buf[digits - ++idx] = j + 'a' - 10;
			flag++;
		}
	}
	buf[idx] = 0;
	cprint(y,x,buf);
}

/*
 * Print a hex number on screen
 */
void hprint(int y, int x, unsigned long val)
{
	return hprint2(y, x, val, 8);
}

/*
 * Print an address in 0000m0000k0000 notation
 */
void xprint(int y,int x, ulong val)
{
	ulong j;

	j = (val & 0xffc00000) >> 20;
	dprint(y, x, j, 4, 0);
	cprint(y, x+4, "m");
	j = (val & 0xffc00) >> 10;
	dprint(y, x+5, j, 4, 0);
	cprint(y, x+9, "k");
	j = val & 0x3ff;
	dprint(y, x+10, j, 4, 0);
}

/* Handle an interrupt */
void inter(struct eregs *trap_regs)
{
	int i, line;
	unsigned char *pp;
	ulong address = 0;

	/* Get the page fault address */
	if (trap_regs->vect == 14) {
		__asm__("movl %%cr2,%0":"=r" (address));
	}
#ifdef PARITY_MEM

	/* Check for a parity error */
	if (trap_regs->vect == 2) {
		parity_err(trap_regs->edi, trap_regs->esi);
		return;
	}
#endif

	/* clear scrolling region */
#if 0
	pp=(unsigned char *)(SCREEN_ADR+(2*80*(LINE_SCROLL-2)));
	for(i=0; i<2*80*(24-LINE_SCROLL-2); i++, pp+=2) {
		*pp = ' ';
	}
#else
	//FM TOWNS
	pp = (char *)VRAM_ADR+(LINE_SCROLL-2)*0x500;
 	for(i=0; i<(24-LINE_SCROLL-2)*0x500; i++) {
		*pp++ = '\0';
	}
#endif
	line = LINE_SCROLL-2;

	cprint(line, 0, "Unexpected Interrupt - Halting");
	cprint(line+2, 0, " Type: ");
	if (trap_regs->vect <= 19) {
		cprint(line+2, 7, codes[trap_regs->vect]);
	} else {
		hprint(line+2, 7, trap_regs->vect);
	}
	cprint(line+3, 0, "   PC: ");
	hprint(line+3, 7, trap_regs->eip);
	cprint(line+4, 0, "   CS: ");
	hprint(line+4, 7, trap_regs->cs);
	cprint(line+5, 0, "Eflag: ");
	hprint(line+5, 7, trap_regs->eflag);
	cprint(line+6, 0, " Code: ");
	hprint(line+6, 7, trap_regs->code);
	if (trap_regs->vect == 14) {
		/* Page fault address */
		cprint(line+7, 0, " Addr: ");
		hprint(line+7, 7, address);
	}

	cprint(line+2, 20, "eax: ");
	hprint(line+2, 25, trap_regs->eax);
	cprint(line+3, 20, "ebx: ");
	hprint(line+3, 25, trap_regs->ebx);
	cprint(line+4, 20, "ecx: ");
	hprint(line+4, 25, trap_regs->ecx);
	cprint(line+5, 20, "edx: ");
	hprint(line+5, 25, trap_regs->edx);
	cprint(line+6, 20, "edi: ");
	hprint(line+6, 25, trap_regs->edi);
	cprint(line+7, 20, "esi: ");
	hprint(line+7, 25, trap_regs->esi);
	cprint(line+8, 20, "ebp: ");
	hprint(line+8, 25, trap_regs->ebp);
	cprint(line+9, 20, "esp: ");
	hprint(line+9, 25, trap_regs->esp);
	cprint(line+7, 0, "   DS: ");
	hprint(line+7, 7, trap_regs->ds);
	cprint(line+8, 0, "   SS: ");
	hprint(line+8, 7, trap_regs->ss);
	cprint(line+1, 38, "Stack:");
	for (i=0; i<12; i++) {
		hprint(line+2+i, 38, trap_regs->esp+(4*i));
		hprint(line+2+i, 47, *(ulong*)(trap_regs->esp+(4*i)));
		hprint(line+2+i, 57, trap_regs->esp+(4*(i+12)));
		hprint(line+2+i, 66, *(ulong*)(trap_regs->esp+(4*(i+12))));
	}

	cprint(line+11, 0, "CS:EIP:                          ");
	pp = (unsigned char *)trap_regs->eip;
	for(i = 0; i < 10; i++) {
		hprint2(line+11, 8+(3*i), pp[i], 2);
	}

	while(1) {
		check_input();
	}
}

void set_cache(int val)
{
	extern struct cpu_ident cpu_id;
	/* 386's don't have a cache */
	if ((cpu_id.cpuid < 1) && (cpu_id.type == 3)) {
		cprint(LINE_INFO, COL_CACHE, "none");
		return;
	}
	switch(val) {
	case 0:
		cache_off();
		cprint(LINE_INFO, COL_CACHE, "off");
		break;
	case 1:
		cache_on();
		cprint(LINE_INFO, COL_CACHE, " on");
		break;
	}
}

// KeyScanCode Table TOWNS to 5150,XT(83)
char keymap[0x80] = {
/* [0x00]  			*/ 0,
/* [0x01] Esc		*/ 1,
/* [0x02] 1			*/ 2,
/* [0x03] 2			*/ 3,
/* [0x04] 3			*/ 4,
/* [0x05] 4			*/ 5,
/* [0x06] 5			*/ 6,
/* [0x07] 6			*/ 7,
/* [0x08] 7			*/ 8,
/* [0x09] 8			*/ 9,
/* [0x0A] 9			*/ 10,
/* [0x0B] 0			*/ 11,
/* [0x0C] -			*/ 12,
/* [0x0D] ^			*/ 0,
/* [0x0E] \			*/ 43,
/* [0x0F] BackSpace	*/ 14,
/* [0x10] Tab		*/ 15,
/* [0x11] Q			*/ 16,
/* [0x12] W			*/ 17,
/* [0x13] E			*/ 18,
/* [0x14] R			*/ 19,
/* [0x15] T			*/ 20,
/* [0x16] Y			*/ 21,
/* [0x17] U			*/ 22,
/* [0x18] I			*/ 23,
/* [0x19] O			*/ 24,
/* [0x1A] P			*/ 25,
/* [0x1B] @			*/ 0,
/* [0x1C] [			*/ 26,
/* [0x1D] Enter		*/ 28,
/* [0x1E] A			*/ 30,
/* [0x1F] S			*/ 31,
/* [0x20] D			*/ 32,
/* [0x21] F			*/ 33,
/* [0x22] G			*/ 34,
/* [0x23] H			*/ 35,
/* [0x24] J			*/ 36,
/* [0x25] K			*/ 37,
/* [0x26] L			*/ 38,
/* [0x27] ;			*/ 39,
/* [0x28] :			*/ 0,
/* [0x29] ]			*/ 27,
/* [0x2A] Z			*/ 44,
/* [0x2B] X			*/ 45,
/* [0x2C] C			*/ 46,
/* [0x2D] V			*/ 47,
/* [0x2E] B			*/ 48,
/* [0x2F] N			*/ 49,
/* [0x30] M			*/ 50,
/* [0x31] ,			*/ 51,
/* [0x32] .			*/ 52,
/* [0x33] /			*/ 53,
/* [0x34] \			*/ 0,
/* [0x35] Space		*/ 57,
/* [0x36] TK *		*/ 55,
/* [0x37] TK /		*/ 0,
/* [0x38] TK +		*/ 78,
/* [0x39] TK -		*/ 74,
/* [0x3A] TK 7		*/ 71,
/* [0x3B] TK 8		*/ 72,
/* [0x3C] TK 9		*/ 73,
/* [0x3D] TK =		*/ 0,
/* [0x3E] TK 4		*/ 75,
/* [0x3F] TK 5		*/ 76,
/* [0x40] TK 6		*/ 77,
/* [0x41] 			*/ 0,
/* [0x42] TK 1		*/ 79,
/* [0x43] TK 2		*/ 80,
/* [0x44] TK 3		*/ 81,
/* [0x45] TK Enter	*/ 0,
/* [0x46] TK 0		*/ 82,
/* [0x47] 			*/ 0,
/* [0x48] Insert	*/ 0,
/* [0x49] 			*/ 0,
/* [0x4A] TK 000	*/ 0,
/* [0x4B] Delete	*/ 0,
/* [0x4C] 			*/ 0,
/* [0x4D] Up		*/ 0,
/* [0x4E] Home		*/ 0,
/* [0x4F] Left		*/ 0,
/* [0x50] Down		*/ 0,
/* [0x51] Right		*/ 0,
/* [0x52] Ctrl		*/ 29,
/* [0x53] Shift		*/ 42,
/* [0x54] 			*/ 0,
/* [0x55] CAP		*/ 0,
/* [0x56] Hiragana	*/ 0,
/* [0x57] Muhenkan	*/ 0,
/* [0x58] Henkan	*/ 0,
/* [0x59] KanaHira	*/ 0,
/* [0x5A] Kanji		*/ 0,
/* [0x5B] F12		*/ 0,
/* [0x5C] 			*/ 0,
/* [0x5D] F1		*/ 59,
/* [0x5E] F2		*/ 60,
/* [0x5F] F3		*/ 61,
/* [0x60] F4		*/ 62,
/* [0x61] F5		*/ 63,
/* [0x62] F6		*/ 64,
/* [0x63] F7		*/ 65,
/* [0x64] F8		*/ 66,
/* [0x65] F9		*/ 67,
/* [0x66] F10		*/ 68,
/* [0x67] 			*/ 0,
/* [0x68]		 	*/ 0,
/* [0x69] 			*/ 0,
/* [0x6A] 			*/ 0,
/* [0x6B] CapsLock	*/ 58,
/* [0x6C] ScrLock	*/ 70,
/* [0x6D] NumLock	*/ 69,
/* [0x6E] PgUp		*/ 0,
/* [0x6F]			*/ 0,
/* [0x70] PgDn		*/ 0,
/* [0x71] Han/Zen	*/ 0,
/* [0x72] Cancel	*/ 0,
/* [0x73] Execute	*/ 0,
/* [0x74] F13		*/ 0,
/* [0x75] F14		*/ 0,
/* [0x76] F15		*/ 0,
/* [0x77] F16		*/ 0,
/* [0x78] F17	 	*/ 0,
/* [0x79] F18		*/ 0,
/* [0x7A] F19		*/ 0,
/* [0x7B] F20		*/ 0,
/* [0x7C] Pause		*/ 0,
/* [0x7D] PrintScr	*/ 0,
/* [0x7E] PgUp		*/ 0,
/* [0x7F]			*/ 0,
};

int get_key() {
	int c;

	c = inb(0x0602);
	if ((c & 0x01) == 0) {
		return(0);
	}
	c = inb(0x0600);
	if ((c & 0x80) == 1) {	// 0=key address 1=exinfo
		return(0);
	}
	return(keymap[c & 0x7F]);
}

void check_input(void)
{
	unsigned char c;

	if ((c = get_key())) {
		switch(c & 0x7f) {
		case 1:
			/* "ESC" key was pressed, bail out.  */
			cprint(LINE_RANGE, COL_MID+23, "Halting... ");

			//FM TOWNS Reset
			outb(0x01, 0x20);	
			break;
		case 46:
			/* c - Configure */
			get_config();
			// BEEP TEST
			beep(1000);
			beep(2000);
			beep(1000);
			beep(2000);	
			break;
		case 28:
			/* CR - clear scroll lock */
			slock = 0;
			footer();
			break;
		case 57:
			/* SP - set scroll lock */
			slock = 1;
			footer();
			break;
		case 0x26:
			/* ^L/L - redraw the display */
			tty_print_screen();
			break;
		}
	}
}

void footer()
{
	cprint(24, 0, "(ESC)Reboot  (c)configuration  (SP)scroll_lock  (CR)scroll_unlock");
	if (slock) {
		cprint(24, 74, "LOCKED");
	} else {
		cprint(24, 74, "      ");
	}
}

ulong getval(int x, int y, int result_shift)
{
	unsigned long val;
	int done;
	int c;
	int i, n;
	int base;
	int shift;
	char buf[16];

	for(i = 0; i < sizeof(buf)/sizeof(buf[0]); i++ ) {
		buf[i] = ' ';
	}
	buf[sizeof(buf)/sizeof(buf[0]) -1] = '\0';

	wait_keyup();
	done = 0;
	n = 0;
	base = 10;
	while(!done) {
		/* Read a new character and process it */
		c = get_key();
		switch(c) {
		case 0x26: /* ^L/L - redraw the display */
			tty_print_screen();
			break;
		case 0x1c: /* CR */
			/* If something has been entered we are done */
			if(n) done = 1;
			break;
		case 0x19: /* p */ buf[n] = 'p'; break;
		case 0x22: /* g */ buf[n] = 'g'; break;
		case 0x32: /* m */ buf[n] = 'm'; break;
		case 0x25: /* k */ buf[n] = 'k'; break;
		case 0x2d: /* x */
			/* Only allow 'x' after an initial 0 */
			if (n == 1 && (buf[0] == '0')) {
				buf[n] = 'x';
			}
			break;
		case 0x0e: /* BS */
			if (n > 0) {
				n -= 1;
				buf[n] = ' ';
			}
			break;
		/* Don't allow entering a number not in our current base */
		case 0x0B: if (base >= 1) buf[n] = '0'; break;
		case 0x02: if (base >= 2) buf[n] = '1'; break;
		case 0x03: if (base >= 3) buf[n] = '2'; break;
		case 0x04: if (base >= 4) buf[n] = '3'; break;
		case 0x05: if (base >= 5) buf[n] = '4'; break;
		case 0x06: if (base >= 6) buf[n] = '5'; break;
		case 0x07: if (base >= 7) buf[n] = '6'; break;
		case 0x08: if (base >= 8) buf[n] = '7'; break;
		case 0x09: if (base >= 9) buf[n] = '8'; break;
		case 0x0A: if (base >= 10) buf[n] = '9'; break;
		case 0x1e: if (base >= 11) buf[n] = 'a'; break;
		case 0x30: if (base >= 12) buf[n] = 'b'; break;
		case 0x2e: if (base >= 13) buf[n] = 'c'; break;
		case 0x20: if (base >= 14) buf[n] = 'd'; break;
		case 0x12: if (base >= 15) buf[n] = 'e'; break;
		case 0x21: if (base >= 16) buf[n] = 'f'; break;
		default:
			break;
		}
		/* Don't allow anything to be entered after a suffix */
		if (n > 0 && (
			(buf[n-1] == 'p') || (buf[n-1] == 'g') ||
			(buf[n-1] == 'm') || (buf[n-1] == 'k'))) {
			buf[n] = ' ';
		}
		/* If we have entered a character increment n */
		if (buf[n] != ' ') {
			n++;
		}
		buf[n] = ' ';
		/* Print the current number */
		cprint(x, y, buf);

		/* Find the base we are entering numbers in */
		base = 10;
		if ((buf[0] == '0') && (buf[1] == 'x')) {
			base = 16;
		}
		else if (buf[0] == '0') {
			base = 8;
		}
	}
	/* Compute our current shift */
	shift = 0;
	switch(buf[n-1]) {
	case 'g': /* gig */  shift = 30; break;
	case 'm': /* meg */  shift = 20; break;
	case 'p': /* page */ shift = 12; break;
	case 'k': /* kilo */ shift = 10; break;
	}
	shift -= result_shift;

	/* Compute our current value */
	val = simple_strtoul(buf, NULL, base);
	if (shift > 0) {
		if (shift >= 32) {
			val = 0xffffffff;
		} else {
			val <<= shift;
		}
	} else {
		if (-shift >= 32) {
			val = 0;
		}
		else {
			val >>= -shift;
		}
	}
	return val;
}

void ttyprint(int y, int x, const char *p)
{
}

void serial_echo_init(void)
{
	clear_screen_buf();
	return;
}

void serial_echo_print(const char *p)
{
}

/* Except for multi-character key sequences this mapping
 * table is complete.  So it should not need to be updated
 * when new keys are searched for.  However the key handling
 * should really be turned around and only in get_key should
 * we worry about the exact keycode that was pressed.  Everywhere
 * else we should switch on the character...
 */
struct ascii_map_str ser_map[] =
/*ascii keycode     ascii  keycode*/
{
  /* Special cases come first so I can leave
   * their "normal" mapping in the table,
   * without it being activated.
   */
  {  27,   0x01}, /* ^[/ESC -> ESC  */
  { 127,   0x0e}, /*    DEL -> BS   */
  {   8,   0x0e}, /* ^H/BS  -> BS   */
  {  10,   0x1c}, /* ^L/NL  -> CR   */
  {  13,   0x1c}, /* ^M/CR  -> CR   */
  {   9,   0x0f}, /* ^I/TAB -> TAB  */
  {  19,   0x39}, /* ^S     -> SP   */
  {  17,     28}, /* ^Q     -> CR   */

  { ' ',   0x39}, /*     SP -> SP   */
  { 'a',   0x1e},
  { 'A',   0x1e},
  {   1,   0x1e}, /* ^A      -> A */
  { 'b',   0x30},
  { 'B',   0x30},
  {   2,   0x30}, /* ^B      -> B */
  { 'c',   0x2e},
  { 'C',   0x2e},
  {   3,   0x2e}, /* ^C      -> C */
  { 'd',   0x20},
  { 'D',   0x20},
  {   4,   0x20}, /* ^D      -> D */
  { 'e',   0x12},
  { 'E',   0x12},
  {   5,   0x12}, /* ^E      -> E */
  { 'f',   0x21},
  { 'F',   0x21},
  {   6,   0x21}, /* ^F      -> F */
  { 'g',   0x22},
  { 'G',   0x22},
  {   7,   0x22}, /* ^G      -> G */
  { 'h',   0x23},
  { 'H',   0x23},
  {   8,   0x23}, /* ^H      -> H */
  { 'i',   0x17},
  { 'I',   0x17},
  {   9,   0x17}, /* ^I      -> I */
  { 'j',   0x24},
  { 'J',   0x24},
  {  10,   0x24}, /* ^J      -> J */
  { 'k',   0x25},
  { 'K',   0x25},
  {  11,   0x25}, /* ^K      -> K */
  { 'l',   0x26},
  { 'L',   0x26},
  {  12,   0x26}, /* ^L      -> L */
  { 'm',   0x32},
  { 'M',   0x32},
  {  13,   0x32}, /* ^M      -> M */
  { 'n',   0x31},
  { 'N',   0x31},
  {  14,   0x31}, /* ^N      -> N */
  { 'o',   0x18},
  { 'O',   0x18},
  {  15,   0x18}, /* ^O      -> O */
  { 'p',   0x19},
  { 'P',   0x19},
  {  16,   0x19}, /* ^P      -> P */
  { 'q',   0x10},
  { 'Q',   0x10},
  {  17,   0x10}, /* ^Q      -> Q */
  { 'r',   0x13},
  { 'R',   0x13},
  {  18,   0x13}, /* ^R      -> R */
  { 's',   0x1f},
  { 'S',   0x1f},
  {  19,   0x1f}, /* ^S      -> S */
  { 't',   0x14},
  { 'T',   0x14},
  {  20,   0x14}, /* ^T      -> T */
  { 'u',   0x16},
  { 'U',   0x16},
  {  21,   0x16}, /* ^U      -> U */
  { 'v',   0x2f},
  { 'V',   0x2f},
  {  22,   0x2f}, /* ^V      -> V */
  { 'w',   0x11},
  { 'W',   0x11},
  {  23,   0x11}, /* ^W      -> W */
  { 'x',   0x2d},
  { 'X',   0x2d},
  {  24,   0x2d}, /* ^X      -> X */
  { 'y',   0x15},
  { 'Y',   0x15},
  {  25,   0x15}, /* ^Y      -> Y */
  { 'z',   0x2c},
  { 'Z',   0x2c},
  {  26,   0x2c}, /* ^Z      -> Z */
  { '-',   0x0c},
  { '_',   0x0c},
  {  31,   0x0c}, /* ^_      -> _ */
  { '=',   0x0c},
  { '+',   0x0c},
  { '[',   0x1a},
  { '{',   0x1a},
  {  27,   0x1a}, /* ^[      -> [ */
  { ']',   0x1b},
  { '}',   0x1b},
  {  29,   0x1b}, /* ^]      -> ] */
  { ';',   0x27},
  { ':',   0x27},
  { '\'',  0x28},
  { '"',   0x28},
  { '`',   0x29},
  { '~',   0x29},
  { '\\',  0x2b},
  { '|',   0x2b},
  {  28,   0x2b}, /* ^\      -> \ */
  { ',',   0x33},
  { '<',   0x33},
  { '.',   0x34},
  { '>',   0x34},
  { '/',   0x35},
  { '?',   0x35},
  { '1',   0x02},
  { '!',   0x02},
  { '2',   0x03},
  { '@',   0x03},
  { '3',   0x04},
  { '#',   0x04},
  { '4',   0x05},
  { '$',   0x05},
  { '5',   0x06},
  { '%',   0x06},
  { '6',   0x07},
  { '^',   0x07},
  {  30,   0x07}, /* ^^      -> 6 */
  { '7',   0x08},
  { '&',   0x08},
  { '8',   0x09},
  { '*',   0x09},
  { '9',   0x0a},
  { '(',   0x0a},
  { '0',   0x0b},
  { ')',   0x0b},
  {   0,      0}
};

/*
 * Given an ascii character, return the keycode
 *
 * Uses ser_map definition above.
 *
 * It would be more efficient to use an array of 255 characters
 * and directly index into it.
 */
int ascii_to_keycode (int in)
{
	struct ascii_map_str *p;
	for (p = ser_map; p->ascii; p++) {
		if (in ==p->ascii)
			return p->keycode;
	}
	return 0;
}

/*
 * Call this when you want to wait for the user to lift the
 * finger off of a key.  It is a noop if you are using a
 * serial console.
 */
void wait_keyup( void ) {
	/* Check to see if someone lifted the keyboard key */
	while (1) {
		//if ((get_key() & 0x80) != 0) {
		if ((inb(0x0602) & 0x1) != 0) {
			return;
		}
		/* Trying to simulate waiting for a key release with
		 * the serial port is to nasty to let live.
		 * In particular some menus don't even display until
		 * you release the key that caused to to get there.
		 * With the serial port this results in double pressing
		 * or something worse for just about every key.
		 */
		if (serial_cons) {
			return;
		}
	}
}

