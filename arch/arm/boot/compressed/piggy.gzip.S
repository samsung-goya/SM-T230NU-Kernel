	.section .piggydata,#alloc
	.p2align 2		@ goyawifi: 4-byte aligned payload, as Magisk and other zImage tools expect
	.globl	input_data
input_data:
	.incbin	"arch/arm/boot/compressed/piggy.gzip"
	.globl	input_data_end
input_data_end:
