/* defs.h - MemTest-86 assembler/compiler definitions
 *
 * Released under version 2 of the Gnu Public License.
 * By Chris Brady, cbrady@sgi.com
 */

#define SETUPSECS	2		/* Number of setup sectors */

/*
 * Caution!! There is magic in the build process.  Read
 * README.build-process before you change anything.
 * Unlike earlier versions all of the settings are in defs.h
 * so the build process should be more robust.
 */
#define LOW_TEST_ADR	0x00002000		/* Final adrs for test code */

#define BOOTSEG		0xB000			/* Segment adrs for inital boot FM TOWNS=0xB000 */
#define INITSEG		0x9000			/* Segment adrs for relocated boot */
#define SETUPSEG	(INITSEG+0x20)		/* Segment adrs for relocated setup */
#define TSTLOAD		0x1000			/* Segment adrs for load of test */

#define KERNEL_CS	0x10			/* 32 bit segment adrs for code */
#define KERNEL_DS	0x18			/* 32 bit segment adrs for data */
#define REAL_CS		0x20			/* 16 bit segment adrs for code */
#define REAL_DS		0x28			/* 16 bit segment adrs for data */

// FM TOWNS I/O Port
#define	IO_FMR_GVRAMMASK			0x0FF81
#define	IO_FMR_GVRAMDISPMODE 		0x0FF82
#define	IO_FMR_GVRAMPAGESEL			0x0FF83
#define	IO_KVRAM_OR_ANKFONT			0x0FF99
#define	IO_FMR_VRAM_OR_MAINRAM		0x0404


