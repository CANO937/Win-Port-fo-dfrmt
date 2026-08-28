#pragma once


#include <types.h>

typedef struct {
    UINT8  boot_indicator;  // 0x00 = non avviabile
    UINT8  starting_chs[3]; // Geometria CHS iniziale {0x00, 0x02, 0x00}
    UINT8  os_type;         // Tipo di partizione (0xEE per GPT Protective)
    UINT8  ending_chs[3];   // Geometria CHS finale {0xFF, 0xFF, 0xFF}
    UINT32 starting_lba;    // LBA iniziale (solitamente 1)
    UINT32 size_in_lba;     // Numero di settori coperti
} __attribute__((packed)) MbrPartitionEntry;

typedef struct {
    UINT8  boot_code[440];           // Codice di boot (azzerato)
    UINT32 disk_signature;           // Firma disco (opzionale, 0)
    UINT16 reserved;                 // Solitamente 0x0000
    MbrPartitionEntry partitions[4]; // 4 record di partizione (64 byte)
    UINT16 boot_signature;           // Firma MBR obbligatoria (0xAA55)
} __attribute__((packed)) ProtectiveMbr;