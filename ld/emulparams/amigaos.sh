. ${srcdir}/emulparams/elf32ppccommon.sh
. ${srcdir}/emulparams/plt_unwind.sh

TEMPLATE_NAME=elf
EXTRA_EM_FILE=ppc32elf
SCRIPT_NAME=amigaos
OUTPUT_FORMAT="elf32-powerpc-amigaos"
MAXPAGESIZE="CONSTANT (MAXPAGESIZE)"
COMMONPAGESIZE="CONSTANT (COMMONPAGESIZE)"
DATA_SEGMENT_ALIGN="ALIGN(${SEGMENT_SIZE})"
ALIGNMENT=16
ARCH=powerpc
MACHINE=
GENERATE_SHLIB_SCRIPT=yes
TEXT_START_ADDR=0x01000000
SHLIB_TEXT_START_ADDR=0x10000000
unset WRITABLE_RODATA
DATA_START_SYMBOLS="_DATA_BASE_ = .;"
SDATA_START_SYMBOLS="_SDA_BASE_ = . + 0x8000;"
DATA_GOT=
SDATA_GOT=
TEXT_PLT=yes
SEPARATE_GOTPLT=0
unset BSS_PLT
unset DATA_PLT
GOT=".got          ${RELOCATING-0} : SPECIAL { *(.got) }"
PLT=".plt          ${RELOCATING-0} :  { *(.plt) }"
# GOTPLT="${PLT}"
OTHER_TEXT_SECTIONS="*(.glink)"
DYNAMIC_LINK=false