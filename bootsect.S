/*
 *	 bootsect.s		Copyright (C) 1991, 1992 Linus Torvalds
 *
 * bootsect.s is loaded at 0x7c00 by the bios-startup routines, and moves
 * itself out of the way to address 0x90000, and jumps there.
 *
 * It then loads 'setup' directly after itself (0x90200), and the system
 * at 0x10000, using BIOS interrupts.
 *
 * The loader has been made as simple as possible, and continuos
 * read errors will result in a unbreakable loop. Reboot by hand. It
 * loads pretty fast by getting whole tracks at a time whenever possible.
 *
 * 1-Jan-96 Modified by Chris Brady for use as a boot loader for MemTest-86.
 */

#include "defs.h"

ROOT_DEV = 0

.code16
.section ".bootsect", "ax", @progbits
_boot:


# ld86 requires an entry symbol. This may as well be the usual one.
.globl	_main
_main:
	.ascii "IPL4"		/* FM TOWNS IPL Identifier */

.org 4
	movw	$BOOTSEG, %ax
	movw	%ax, %ds
	movw	$INITSEG, %ax
	movw	%ax, %es
	movw	$256, %cx
	subw	%si, %si
	subw	%di, %di

	push	%bx
	call	INITGRAPHICS
	call	VRAMMODE
	call	CLS
	pop		%bx	

	cld
	rep
	movsw
	ljmp	$INITSEG, $go - _boot

go:
	movw	%cs, %ax
	movw	$(0x4000-12), %dx	# 0x4000 is arbitrary value >= length of
					# bootsect + length of setup + room for stack
					# 12 is disk parm size

# bde - changed 0xff00 to 0x4000 to use debugger at 0x6400 up (bde).  We
# wouldn't have to worry about this if we checked the top of memory.  Also
# my BIOS can be configured to put the wini drive tables in high memory
# instead of in the vector table.  The old stack might have clobbered the
# drive table.

	movw	%ax, %ds
	movw	%ax, %es
	movw	%ax, %ss		# put stack at INITSEG:0x4000-12.
	movw	%dx, %sp



# load the setup-sectors directly after the bootblock.
# Note that 'es' is already set up.
	
load_setup:

	push	%bx
	movb	%bh, %al	# Unit number
	or		$0x20, %al	# Floppy Disk
	movb	%al, %cs:(DEVICEID)
	# FD Restore (AL=Device ID) >>
	movb	$0x03, %ah
	xor		%ch,%ch
	lcall	$0xfffb, $0x014
	# FD Restore (AL=Device ID) <<

	movw	$INITSEG, %ax
	movw	%ax,%ds	
	/*	Read Sector
		ah	0x05
		al	device-id
		cx	cylinder(track)
		dh	head
		dl	sector 	
		bx	read sector number
		ds:di buffer address
	*/
	movb	$0x05, %ah
	movb	%cs:(DEVICEID), %al	# device id
	movw	$0x0000, %cx	# Cylinder(track):0
	movw	$0x0002, %dx	# head 0, sector 2
	movw	$0x0001, %bx	# 1 sector
	movw	$0x0200, %di	# address = 512, in INITSEG
	lcall	$0xfffb, $0x0014
	jnb		ok_load_setup

READ_ERROR:
	movb	$0x58, %ah		# 'X'
	movw	0x01+0x01*(80*16), %di
	call DRAWFONT_TALL
	jmp READ_ERROR

ok_load_setup:


# Get disk drive parameters, specifically nr of sectors/track


/* It seems that there is no BIOS call to get the number of sectors.  Guess
 * 18 sectors if sector 18 can be read, 15 if sector 15 can be read.
 * Otherwise guess 9
 */

# Print some inane message

# ok, we've written the message, now
# we want to load the system (at 0x10000)

	movw	$TSTLOAD, %ax
	movw	%ax, %ds		# segment of 0x010000

	#The memtest body will be less than 192kbyte
	movw	$0x0000, %di	# address = 0, in TSTLOAD
	movw	$0x0000, %cx	# cylinder 0
	movw	$0x0004, %dx	# head 0 sector 4(0x0C00)
	movw	$0x0000, %ax	# counter

read_loop:
	pushw	%ax
	movb	$0x05, %ah
	movb	%cs:(DEVICEID), %al	# device id
	movw	$0x0001, %bx	# read sector number
	lcall	$0xfffb, $0x0014
	jnb		read_ok
	# error code
	movb	$0x58, %ah		# 'X'
	movw	0x01+0x01*(80*16), %di
	call DRAWFONT_TALL
	movb	%cl, %ah		# Cylinder
	add $0x30, %ah
	movw	0x01+0x02*(80*16), %di
	call DRAWFONT_TALL
	movb	%dh, %ah		# Head
	add $0x30, %ah
	movw	0x01+0x03*(80*16), %di
	call DRAWFONT_TALL
	movb	%dl, %ah		# Sector
	add $0x30, %ah
	movw	0x01+0x04*(80*16), %di
	call DRAWFONT_TALL

error_hlt:
	jmp error_hlt

read_ok:
	popw	%ax
	inc	%al
	add	$1, %dl
	cmpb	$9, %dl
	jl	check_next_seg
	sub	$8, %dl
	# head 0->1 1->0	
	not	%dh
	and	$0x01, %dh	
	# if head 1->0 then next cylinder
	cmpb	$0, %dh
	jne	check_next_seg
	inc	%cx

check_next_seg:
	add	$0x400, %di	# 1sector=1024byte
	cmpw	$0, %di
	jne	check_read_end
	movw	%ds, %bx
	add	$0x1000, %bx	# next segment
	movw	%bx, %ds
	movw	$0x0000, %di
	inc	%ah		# segment number 

check_read_end:
	cmpb	$3, %ah	/* 65536*3 */
	jle	read_loop
	

readok:

# after that (everyting loaded), we jump to
# the setup-routine loaded directly after
# the bootblock:

	ljmp	$SETUPSEG,$0

	
sectors:
	.word 0

msg1:
	.ascii "Loading"



/*
	FM TOWNS VRAM Text Drawing Functions
	from captainys's FD_IPL
*/

.intel_syntax noprefix
INITGRAPHICS:

	MOV		DX,IO_FMR_GVRAMDISPMODE
	MOV		AL,0x27  # Show all planes, write to Page 0 (Page 0 is upper half of VRAM Layer 0)
	OUT		DX,AL

	MOV		DX,IO_FMR_GVRAMMASK
	MOV		AL,0x0F  # Write to all planes
	OUT		DX,AL

	RET


VRAMMODE:
	PUSH	DX
	PUSH	AX
	MOV		AL,0
	JMP		SHORT SET_VRAM_OR_RAM_MODE

RAMMODE:
	PUSH	DX
	PUSH	AX
	MOV		AL,0x80

SET_VRAM_OR_RAM_MODE:
	MOV		DX,IO_FMR_VRAM_OR_MAINRAM
	OUT		DX,AL
	POP		AX
	POP		DX
	RET


CLS:
	PUSH	ES
	PUSH	DI
	PUSH	AX
	PUSH	CX

	AND		AL,0x0F
	MOV		DX,IO_FMR_GVRAMMASK
	OUT		DX,AL

	MOV		AX,0x0C000
	MOV		ES,AX
	MOV		DI,0
	MOV		CX,80*400/2
	MOV		AX,0
	REP STOSW

	POP		CX
	POP		AX
	POP		DI
	POP		ES
	RET

# DI:  VRAM ADDRESS
# AH:  ASCII Code
# C0000H must be in FM-R VRAM mode.
# BX preserved
# DS preserved
DRAWFONT_TALL:
	PUSH	DS

	MOV		DX,0x0C000
	MOV		DS,DX

	MOV		DX,IO_KVRAM_OR_ANKFONT
	MOV		AL,1
	OUT		DX,AL

	# ANK Font ROM CA000H-
	MOV		AL,8
	MUL		AH
	ADD		AX,0x0A000

	MOV		CX,8
	MOV		SI,AX
drawfont_tall_before:
	MOV		DH,DS:[SI]
	MOV		[DI],DH
	MOV		[DI+0x50],DH
	INC		SI
	ADD		DI,0x0A0
	LOOP	drawfont_tall_before

	MOV		DX,IO_KVRAM_OR_ANKFONT
	XOR		AL,AL
	OUT		DX,AL

	POP		DS
	RET

# Input
#   DS:SI     String
#   DI        VRAM Address
PRINT_TALL:
						PUSH	ES
						PUSH	SI
						PUSH	DI
						PUSH	AX
						PUSH	DX
						PUSH	CX

						MOV		AX,0x0C000
						MOV		ES,AX
						MOV		BX,SI
print_tall_b:
						MOV		SI,BX
						LODSB
						CMP		AL,0
						JE		print_tall_f
						MOV		AH,AL
						CALL	DRAWFONT_TALL
						ADD		DI,1-80*16
						ADD		BX,1
						JMP		print_tall_b
print_tall_f:
						POP		CX
						POP		DX
						POP		AX
						POP		DI
						POP		SI
						POP		ES
						RET

.att_syntax prefix

.org 497
setup_sects:
	.byte SETUPSECS
.org 500
syssize:
	.word _syssize

.org 504
DEVICEID:
	.byte	0

.org 510
boot_flag:
	.word 0xAA55

.org 1022	/* 1 sector is 1024 byte 
			   sector 1:bootsector 2:setup 4-:body	*/
	.word 0xFFFF
_eboot:
