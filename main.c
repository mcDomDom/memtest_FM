/* main.c - MemTest-86  Version 3.2
 *
 * Released under version 2 of the Gnu Public License.
 * By Chris Brady
 * ----------------------------------------------------
 * MemTest86+ V4.00 Specific code (GPL V2.0)
 * By Samuel DEMEULEMEESTER, sdemeule@memtest.org
 * http://www.canardpc.com - http://www.memtest.org
 */

#include "test.h"
#include "defs.h"
#include "config.h"
#undef TEST_TIMES
#define DEFTESTS 9

extern void bzero();

const struct tseq tseq[] = {
	{1,  5,   4, 0, "[Address test, walking ones]          "},
	{1,  6,   4, 0, "[Address test, own address]           "},
	{1,  0,   4, 0, "[Moving inversions, ones & zeros]     "},
	{1,  1,   2, 0, "[Moving inversions, 8 bit pattern]    "},
	{1, 10,  50, 0, "[Moving inversions, random pattern]   "},
	{1,  7,  80, 0, "[Block move, 80 moves]                "},
	{1,  2,   2, 0, "[Moving inversions, 32 bit pattern]   "},
	{1,  9,  30, 0, "[Random number sequence]              "},
  {1, 11,   6, 0, "[Modulo 20, Random pattern]           "},
	{1,  8,   1, 0, "[Bit fade test, 90 min, 2 patterns]   "},
	{0,  0,   0, 0, NULL}
};

char firsttime = 0;
char cmdline_parsed = 0;

struct vars variables = {};
struct vars * const v = &variables;

volatile ulong *p = 0;
ulong p1 = 0, p2 = 0, p0 = 0;
int segs = 0, bail = 0;
int test_ticks;
int nticks;
ulong high_test_adr = 0x200000;

static int window = 0;
static int c_iter;
static struct pmap windows[] =
{
	{ 0, 0x080000 },
	{ 0, 0 },

	{ 0x080000, 0x100000 },
	{ 0x100000, 0x180000 },
	{ 0x180000, 0x200000 },

	{ 0x200000, 0x280000 },
	{ 0x280000, 0x300000 },

	{ 0x300000, 0x380000 },
	{ 0x380000, 0x400000 },

	{ 0x400000, 0x480000 },
	{ 0x480000, 0x500000 },

	{ 0x500000, 0x580000 },
	{ 0x580000, 0x600000 },

	{ 0x600000, 0x680000 },
	{ 0x680000, 0x700000 },

	{ 0x700000, 0x780000 },
	{ 0x780000, 0x800000 },

	{ 0x800000, 0x880000 },
	{ 0x880000, 0x900000 },

	{ 0x900000, 0x980000 },
	{ 0x980000, 0xA00000 },

	{ 0xA00000, 0xA80000 },
	{ 0xA80000, 0xB00000 },

	{ 0xB00000, 0xB80000 },
	{ 0xB80000, 0xC00000 },

	{ 0xC00000, 0xC80000 },
	{ 0xC80000, 0xD00000 },

	{ 0xD00000, 0xD80000 },
	{ 0xD80000, 0xE00000 },

	{ 0xE00000, 0xE80000 },
	{ 0xE80000, 0xF00000 },

	{ 0xF00000, 0xF80000 },
	{ 0xF80000, 0x1000000 },
};


#if (LOW_TEST_ADR > (640*1024))
#error LOW_TEST_ADR must be below 640K
#endif

static int find_ticks_for_test(int ch, int test);
static int compute_segments(int win);
void find_ticks_for_pass(void);

static void __run_at(unsigned long addr)
{
	/* Copy memtest86+ code */
	memmove((void *)addr, &_start, _end - _start);
	/* Jump to the start address */
	p = (ulong *)(addr + startup_32 - _start);
	goto *p;
}

static unsigned long run_at_addr = 0xffffffff;
static void run_at(unsigned long addr)
{
	unsigned long start;
	unsigned long len;

	run_at_addr = addr;

	start = (unsigned long) &_start;
	len = _end - _start;
	if (	((start < addr) && ((start + len) >= addr)) ||
		((addr < start) &&  ((addr + len) >= start))) {
		/* Handle overlap by doing an extra relocation */
		if (addr + len < high_test_adr) {
			__run_at(high_test_adr);
		}
		else if (start + len < addr) {
			__run_at(LOW_TEST_ADR);
		}
	}
	__run_at(run_at_addr);
}

/* command line passing using the 'old' boot protocol */
#define MK_PTR(seg,off) ((void*)(((unsigned long)(seg) << 4) + (off)))
#define OLD_CL_MAGIC_ADDR ((unsigned short*) MK_PTR(INITSEG,0x20))
#define OLD_CL_MAGIC 0xA33F 
#define OLD_CL_OFFSET_ADDR ((unsigned short*) MK_PTR(INITSEG,0x22))

static void parse_command_line(void)
{
	char *cmdline;

	if (cmdline_parsed)
		return;

	if (*OLD_CL_MAGIC_ADDR != OLD_CL_MAGIC)
		return;

	unsigned short offset = *OLD_CL_OFFSET_ADDR;
	cmdline = MK_PTR(INITSEG, offset);

	/* skip leading spaces */
	while (*cmdline == ' ')
		cmdline++;

	while (*cmdline) {
		/*
		if (!strncmp(cmdline, "console=", 8)) {
			cmdline += 8;
			serial_console_setup(cmdline);
		}
		*/

		/* go to the next parameter */
		while (*cmdline && *cmdline != ' ')
			cmdline++;
		while (*cmdline == ' ')
			cmdline++;
	}

	cmdline_parsed = 1;
}

void do_test(void)
{
	int i = 0, j = 0;
	unsigned long chunks;
	unsigned long lo, hi;

	parse_command_line();

	/* If we have a partial relocation finish it */
	if (run_at_addr == (unsigned long)&_start) {
		run_at_addr = 0xffffffff;
	} else if (run_at_addr != 0xffffffff) {
		__run_at(run_at_addr);
	}

	/* If first time, initialize test */
	if (firsttime == 0) {
		if ((ulong)&_start != LOW_TEST_ADR) {
			restart();
		}
		
		init();
		
		find_ticks_for_pass();
		
		windows[0].start = 
			( LOW_TEST_ADR + (_end - _start) + 4095) >> 12;

		/* Set relocation address at 16Mb if there is enough memory */
		if (v->pmap[v->msegs-1].end > 0x1100) {
			high_test_adr = 0x01000000;
		}
		windows[1].end = (high_test_adr >> 12);
		firsttime = 1;
	}
	bail = 0;

	/* Find the memory areas I am going to test */
	compute_segments(window);
	if (segs == 0) {
		goto skip_window;
	}
	/* Now map in the window... */
	if (map_page(v->map[0].pbase_addr) < 0) {
		goto skip_window;
	}

	if ((ulong)&_start > LOW_TEST_ADR) {
		/* Relocated so we need to test all selected lower memory */
		v->map[0].start = mapping(v->plim_lower);
		
#ifdef USB_WAR
 /* We must not touch test below 0x500 memory beacuase
  * BIOS USB support clobbers location 0x410 and 0x4e0
  */
	if ((ulong)v->map[0].start < 0x500) {
    v->map[0].start = (ulong*)0x500;
	}
#endif

		cprint(LINE_RANGE, COL_MID+28, " Relocated");
	} else {
		cprint(LINE_RANGE, COL_MID+28, "          ");
	}

	/* Update display of memory segments being tested */
	lo = page_of(v->map[0].start);
	hi = page_of(v->map[segs -1].end);
	aprint(LINE_RANGE, COL_MID+9, lo);
	cprint(LINE_RANGE, COL_MID+14, " - ");
	aprint(LINE_RANGE, COL_MID+17, hi);
	aprint(LINE_RANGE, COL_MID+23, v->selected_pages);


#ifdef TEST_TIMES
	{
		ulong l, h, t;

		asm __volatile__ (
			"rdtsc\n\t"
			"subl %%ebx,%%eax\n\t"
			"sbbl %%ecx,%%edx\n\t"
			:"=a" (l), "=d" (h)
			:"b" (v->snapl), "c" (v->snaph)
		);

		cprint(20, 5, ":  :");
		t = h * ((unsigned)0xffffffff / v->clks_msec) / 1000;
		t += (l / v->clks_msec) / 1000;
		i = t % 60;
		dprint(20, 10, i%10, 1, 0);
		dprint(20, 9, i/10, 1, 0);
		t /= 60;
		i = t % 60;
		dprint(20, +7, i % 10, 1, 0);
		dprint(20, +6, i / 10, 1, 0);
		t /= 60;
		dprint(20, 0, t, 5, 0);

		asm __volatile__ ("rdtsc":"=a" (v->snapl),"=d" (v->snaph));
	}
#endif
	/* Now setup the test parameters based on the current test number */
	/* Figure out the next test to run */
	if (v->testsel >= 0) {
		v->test = v->testsel;
	}
	
	if (v->pass == 0) {
		c_iter = tseq[v->test].iter/2;
	} else {
		c_iter = tseq[v->test].iter;
	}
	
	dprint(LINE_TST, COL_MID+6, v->test, 2, 1);
	cprint(LINE_TST, COL_MID+9, tseq[v->test].msg);
	set_cache(tseq[v->test].cache);

	/* Compute the number of SPINSZ memory segments */
	chunks = 0;
	for(i = 0; i < segs; i++) {
		unsigned long len;
		len = v->map[i].end - v->map[i].start;
		chunks += (len + SPINSZ -1)/SPINSZ;
	}
	test_ticks = find_ticks_for_test(chunks, v->test);
	nticks = 0;
	v->tptr = 0;
	cprint(1, COL_MID+8, "                                         ");
	switch(tseq[v->test].pat) {

	/* Now do the testing according to the selected pattern */
	case 0:	/* Moving inversions, all ones and zeros (test #2) */
		p1 = 0;
		p2 = ~p1;
		movinv1(c_iter,p1,p2);
		BAILOUT;
	
		/* Switch patterns */
		p2 = p1;
		p1 = ~p2;
		movinv1(c_iter,p1,p2);
		BAILOUT;
		break;
		
	case 1: /* Moving inversions, 8 bit walking ones and zeros (test #3) */
		p0 = 0x80;
		for (i=0; i<8; i++, p0=p0>>1) {
			p1 = p0 | (p0<<8) | (p0<<16) | (p0<<24);
			p2 = ~p1;
			movinv1(c_iter,p1,p2);
			BAILOUT;
	
			/* Switch patterns */
			p2 = p1;
			p1 = ~p2;
			movinv1(c_iter,p1,p2);
			BAILOUT
		}
		break;

	case 2: /* Moving inversions, 32 bit shifting pattern (test #6) */
		for (i=0, p1=1; p1; p1=p1<<1, i++) {
			movinv32(c_iter,p1, 1, 0x80000000, 0, i);
			BAILOUT
			movinv32(c_iter,~p1, 0xfffffffe,
				0x7fffffff, 1, i);
			BAILOUT
		}
		break;

	case 3: /* Modulo 20 check, all ones and zeros (unused) */
		p1=0;
		for (i=0; i<MOD_SZ; i++) {
			p2 = ~p1;
			modtst(i, c_iter, p1, p2);
			BAILOUT

			/* Switch patterns */
			p2 = p1;
			p1 = ~p2;
			modtst(i, c_iter, p1,p2);
			BAILOUT
		}
		break;

	case 4: /* Modulo 20 check, 8 bit pattern (unused) */
		p0 = 0x80;
		for (j=0; j<8; j++, p0=p0>>1) {
			p1 = p0 | (p0<<8) | (p0<<16) | (p0<<24);
			for (i=0; i<MOD_SZ; i++) {
				p2 = ~p1;
				modtst(i, c_iter, p1, p2);
				BAILOUT

				/* Switch patterns */
				p2 = p1;
				p1 = ~p2;
				modtst(i, c_iter, p1, p2);
				BAILOUT
			}
		}
		break;
	case 5: /* Address test, walking ones (test #0) */
		addr_tst1();
		BAILOUT;
		break;

	case 6: /* Address test, own address (test #1) */
		addr_tst2();
		BAILOUT;
		break;

	case 7: /* Block move (test #5) */
		block_move(c_iter);
		BAILOUT;
		break;
	case 8: /* Bit fade test (test #9) */
		if (window == 0 ) {
			bit_fade();
		}
		BAILOUT;
		break;
	case 9: /* Random Data Sequence (test #7) */
		for (i=0; i < c_iter; i++) {
			movinvr();
			BAILOUT;
		}
		break;
	case 10: /* Random Data (test #4) */
		for (i=0; i < c_iter; i++) {
			p1 = rand();
			p2 = ~p1;
			movinv1(2,p1,p2);
			BAILOUT;
		}
		break;

	case 11: /* Modulo 20 check, Random pattern (test #8) */
		for (j=0; j<c_iter; j++) {
			p1 = rand();
			for (i=0; i<MOD_SZ; i++) {
				p2 = ~p1;
				modtst(i, 2, p1, p2);
				BAILOUT

				/* Switch patterns */
				p2 = p1;
				p1 = ~p2;
				modtst(i, 2, p1, p2);
				BAILOUT
			}
		}
		break;
	}
 skip_window:
	if (bail) {
		goto bail_test;
	}
	/* Rever to the default mapping and enable the cache */
	paging_off();
	set_cache(1);
	window++;
	if (window >= sizeof(windows)/sizeof(windows[0])) {
		window = 0;
	}
	/* We finished the test so clear the pattern */
	cprint(LINE_PAT, COL_PAT, "            ");
	if (window != 0) {
		/* Relocate and run the high copy if:
		 * - The window overwrites us.
		 *   The lower limit is less than START_ADR
		 * - There is more than 1 meg of memory
		 */
		if (windows[window].start < 
			((ulong)&_start + (_end - _start)) >> 12) {
			if (v->pmap[v->msegs-1].end > 
				(((high_test_adr + (_end - _start)) >> 12)+1)) {
				/* We need the high copy and we have enough
				 * memory so use it.
				 */
				run_at(high_test_adr);
			} else { 
				/* We can't use this window so skip it */
				goto skip_window;
			}
		} else {
			/* We don't need the high copy for this test */
			run_at(LOW_TEST_ADR);
		}
	}
	else {
		/* We have run this test in all of the windows
		 * advance to the next test.
		 */
	skip_test:
		v->test++;
	bail_test:
		/* Revert to the default mapping
		 * and enable the cache.
		 */
		paging_off();
		set_cache(1);
		check_input();
		window = 0;
		cprint(LINE_PAT, COL_PAT-3, "   ");
		/* If this was the last test then we finished a pass */
		if (v->test >= 9 || v->testsel >= 0) {
			v->pass++;
			dprint(LINE_INFO, COL_PASS, v->pass, 5, 0);
			v->test = 0;
			v->total_ticks = 0;
			v->pptr = 0;
			cprint(0, COL_MID+8,
				"                                         ");
			if (v->ecount == 0 && v->testsel < 0) {
			    cprint(LINE_MSG+5, 0,
				"              *****Pass complete, no errors, press Esc to exit*****            ");
				if(BEEP_END_NO_ERROR) {
					beep(1000);
					beep(2000);
					beep(1000);
					beep(2000);
				}
			}
		}
		
		/* We always start a pass with the low copy */
		run_at(LOW_TEST_ADR);
	}
}

void restart()
{
	int x, y;

	/* clear variables */
	firsttime = 0;
	v->test = 0;
	v->pass = 0;
	v->msg_line = 0;
	v->ecount = 0;
	v->ecc_ecount = 0;

	/* Clear the screen */
	for(y=0; y<25; y++) {
		for(x=0; x<80; x++) cput(y, x, ' ');
	}
	run_at(LOW_TEST_ADR);
}

void find_ticks_for_pass(void)
{
	int i, j, chunks;

	v->pptr = 0;

	/* Compute the number of SPINSZ memory segments in one pass */
	chunks = 0;
	for(j = 0; j < sizeof(windows)/sizeof(windows[0]); j++) {
		compute_segments(j);
		for(i = 0; i < segs; i++) {
			unsigned long len;
			len = v->map[i].end - v->map[i].start;
			chunks += (len + SPINSZ -1)/SPINSZ;
		}
	}
	compute_segments(window);
	window = 0;
	for (v->pass_ticks=0, i=0; ((i<DEFTESTS) && (DEFTESTS != NULL)); i++) {

		/* Test to see if this test is selected for execution */
		if (v->testsel >= 0) {
			if (i != v->testsel) {
				continue;
			}
		}
		v->pass_ticks += find_ticks_for_test(chunks, i);
	}
}


static int find_ticks_for_test(int ch, int test)
{
	int ticks=0, c;

	/* Set the number of iterations. We only do half of the iterations */
        /* on the first pass */
	if (v->pass == 0 && FIRST_PASS_HALF_ITERATIONS) {
		c = tseq[test].iter/2;
	} else {
		c = tseq[test].iter;
	}

	switch(tseq[test].pat) {
	case 0: /* Moving inversions, all ones and zeros (test #2) */
		ticks = 2 + 4 * c;
		break;
	case 1: /* Moving inversions, 8 bit walking ones and zeros (test #3) */
		ticks = 24 + 24 * c;
		break;
	case 2: /* Moving inversions, 32 bit shifting pattern, very long */
		ticks = (1 + c * 2) * 80;
		break;
	case 3: /* Modulo 20 check, all ones and zeros (unused) */
		ticks = (2 + c) * 40;
		break;
	case 4: /* Modulo 20 check, 8 bit pattern (unused) */
		ticks = (2 + c) * 40 * 8;
		break;
	case 5: /* Address test, walking ones (test #0) */
		ticks = 4;
		break;
	case 6: /* Address test, own address (test #1) */
		ticks = 2;
		break;
	case 7: /* Block move (test #5) */
		ticks = 2 + c;
		break;
	case 8: /* Bit fade test (test #9) */
		ticks = 1;
		break;
	case 9: /* Random Data Sequence (test #7) */
		ticks = 3 * c;
		break;
	case 10: /* Random Data (test #4) */
		ticks = c + 4 * c;
		break;
	case 11: /* Modulo 20 check, Random pattern (test #8) */
		ticks = 4 * 40 * c;
		break;
	}

	return ticks*ch;
}

static int compute_segments(int win)
{
	unsigned long wstart, wend;
	int i;

	/* Compute the window I am testing memory in */
	wstart = windows[win].start;
	wend = windows[win].end;
	segs = 0;

	/* Now reduce my window to the area of memory I want to test */
	if (wstart < v->plim_lower) {
		wstart = v->plim_lower;
	}
	if (wend > v->plim_upper) {
		wend = v->plim_upper;
	}
	if (wstart >= wend) {
		return(0);
	}
	/* List the segments being tested */
	for (i=0; i< v->msegs; i++) {
		unsigned long start, end;
		start = v->pmap[i].start;
		end = v->pmap[i].end;
		if (start <= wstart) {
			start = wstart;
		}
		if (end >= wend) {
			end = wend;
		}
#if 0
		cprint(LINE_SCROLL+(2*i), 0, " (");
		hprint(LINE_SCROLL+(2*i), 2, start);
		cprint(LINE_SCROLL+(2*i), 10, ", ");
		hprint(LINE_SCROLL+(2*i), 12, end);
		cprint(LINE_SCROLL+(2*i), 20, ") ");

		cprint(LINE_SCROLL+(2*i), 22, "r(");
		hprint(LINE_SCROLL+(2*i), 24, wstart);
		cprint(LINE_SCROLL+(2*i), 32, ", ");
		hprint(LINE_SCROLL+(2*i), 34, wend);
		cprint(LINE_SCROLL+(2*i), 42, ") ");

		cprint(LINE_SCROLL+(2*i), 44, "p(");
		hprint(LINE_SCROLL+(2*i), 46, v->plim_lower);
		cprint(LINE_SCROLL+(2*i), 54, ", ");
		hprint(LINE_SCROLL+(2*i), 56, v->plim_upper);
		cprint(LINE_SCROLL+(2*i), 64, ") ");

		cprint(LINE_SCROLL+(2*i+1),  0, "w(");
		hprint(LINE_SCROLL+(2*i+1),  2, windows[win].start);
		cprint(LINE_SCROLL+(2*i+1), 10, ", ");
		hprint(LINE_SCROLL+(2*i+1), 12, windows[win].end);
		cprint(LINE_SCROLL+(2*i+1), 20, ") ");

		cprint(LINE_SCROLL+(2*i+1), 22, "m(");
		hprint(LINE_SCROLL+(2*i+1), 24, v->pmap[i].start);
		cprint(LINE_SCROLL+(2*i+1), 32, ", ");
		hprint(LINE_SCROLL+(2*i+1), 34, v->pmap[i].end);
		cprint(LINE_SCROLL+(2*i+1), 42, ") ");

		cprint(LINE_SCROLL+(2*i+1), 44, "i=");
		hprint(LINE_SCROLL+(2*i+1), 46, i);
		
		cprint(LINE_SCROLL+(2*i+2), 0, 
			"                                        "
			"                                        ");
		cprint(LINE_SCROLL+(2*i+3), 0, 
			"                                        "
			"                                        ");
#endif
		if ((start < end) && (start < wend) && (end > wstart)) {
			v->map[segs].pbase_addr = start;
			v->map[segs].start = mapping(start);
			v->map[segs].end = emapping(end);
#if 0
			cprint(LINE_SCROLL+(2*i+1), 54, " sg: ");
			hprint(LINE_SCROLL+(2*i+1), 61, sg);
#endif
			segs++;
		}
	}
	return (segs);
}

