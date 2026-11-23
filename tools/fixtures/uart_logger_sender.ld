ENTRY(probe_entry)
SECTIONS
{
    .text 0x40100000 : { *(.literal*) *(.text*) }
    .rodata 0x3ffe9000 : { *(.rodata*) }
}
