OUTPUT_ARCH(i8086)
MEMORY { MBR (rx) : org = 0x7c00 , len = 0x200 }
SECTIONS {
	ENTRY(mbr)
	.main     0x7c00        : { *(main) }
	.igot     0x0000        : { *(.igot.plt) }
}
