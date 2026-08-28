#pragma once
#include <types.h>

// FAT Entry Constants
#define EXFAT_FAT_FREE          0x00000000
#define EXFAT_FAT_BAD           0xFFFFFFF7
#define EXFAT_FAT_EOF           0xFFFFFFFF

#define EXFAT_ENTRY_BITMAP      0x81
#define EXFAT_ENTRY_UPCASE      0x82
#define EXFAT_ENTRY_LABEL       0x83
#define EXFAT_ENTRY_FILE        0x85

#pragma pack(push, 1)
typedef struct {
    UINT8  JumpBoot[3]; 
    UINT8  FileSystemName[8];     // "EXFAT   "
    UINT8  MustBeZero[53];
    UINT64 PartitionOffset;       // LBA di inizio partizione
    UINT64 VolumeLength;          // Settori totali
    UINT32 FatOffset;             // Offset FAT in settori
    UINT32 FatLength;             // Lunghezza FAT in settori
    UINT32 ClusterHeapOffset;     // Offset Cluster Heap in settori
    UINT32 ClusterCount;          // Numero totali di cluster
    UINT32 RootDirectoryCluster;  // Primo cluster della Root Dir (di solito 2)
    UINT32 VolumeSerialNumber;    // ID Seriale unico
    UINT16 FileSystemRevision;    // 0x0100 (v1.00)
    UINT16 VolumeFlags;           // 0x0000
    UINT8  BytesPerSectorShift;   // 9 per 512B, 12 per 4096B
    UINT8  SectorsPerClusterShift;// log2(settori per cluster)
    UINT8  NumberOfFats;          // 1 (TexFAT usa 2)
    UINT8  DriveSelect;           // 0x80
    UINT8  PercentInUse;          // 0..100
    UINT8  Reserved[7];
    UINT8  BootCode[390];
    UINT16 BootSignature;         // 0xAA55
} EXFAT_VBR;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
    UINT8  EntryType;             // 0x83
    UINT8  CharacterCount;        // N. caratteri in UTF-16
    UINT16 VolumeLabel[11];       // Nome volume (UTF-16LE)
    UINT8  Reserved[8];
} EXFAT_ENTRY_LABEL_STRUCT;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
    UINT8  EntryType;             // 0x81
    UINT8  BitmapFlags;           // 0x00 per la primaria
    UINT8  Reserved[18];
    UINT32 FirstCluster;          // Solitamente Cluster 3
    UINT64 DataLength;            // Dimensione in Byte
} EXFAT_ENTRY_BITMAP_STRUCT;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
    UINT8  EntryType;             // 0x82
    UINT8  Reserved1[3];
    UINT32 TableChecksum;         // Checksum della tabella delle maiuscole
    UINT8  Reserved2[12];
    UINT32 FirstCluster;          // Solitamente Cluster 4
    UINT64 DataLength;            // Dimensione in Byte
} EXFAT_ENTRY_UPCASE_STRUCT;
#pragma pack(pop)
