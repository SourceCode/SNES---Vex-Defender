;==LoRom==

.MEMORYMAP
  SLOTSIZE $8000
  DEFAULTSLOT 0
  SLOT 0 $8000
  SLOT 1 $0 $2000
  SLOT 2 $2000 $E000
  SLOT 3 $0 $10000
.ENDME

.ROMBANKSIZE $8000
.ROMBANKS 16                    ; 4 Mbits (512KB)

.SNESHEADER
  ID "SNES"

  NAME "VEX DEFENDER         "  ; 21 bytes exactly
  ;    "123456789012345678901"

  SLOWROM
  LOROM

  CARTRIDGETYPE $02             ; ROM + SRAM
  ROMSIZE $09                   ; 4 Megabits
  SRAMSIZE $01                  ; 2KB SRAM
  COUNTRY $01                   ; U.S.
  LICENSEECODE $00
  VERSION $00                   ; 1.00
.ENDSNES

.SNESNATIVEVECTOR
  COP EmptyHandler
  BRK EmptyHandler
  ABORT EmptyHandler
  NMI VBlank
  IRQ EmptyHandler
.ENDNATIVEVECTOR

.SNESEMUVECTOR
  COP EmptyHandler
  ABORT EmptyHandler
  NMI EmptyHandler
  RESET tcc__start
  IRQBRK EmptyHandler
.ENDEMUVECTOR
