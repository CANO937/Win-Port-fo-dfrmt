#pragma once
#include <types.h>

#define SJFS_MAGIC 0x534A4653
#define BLOCK_SIZE 4096

#define MOUNTED   0x01
#define UMMOUNTED 0x02
#define ALLOK     0x03

#pragma pack(push, 1)
typedef struct {
    UINT32 Magic;
    UINT32 Flags;
    UINT8  Status;
    UINT8  BandsFlag;
    UINT8  Reserved0[6];
    
    UINT64 HashID;
    UINT64 Wtime;
    UINT64 Mtime;
    
    UINT32 BlockSize;
    UINT32 Reserved1;
    
    UINT64 TotalBlocks;
    UINT64 JournalStart;
    UINT64 JournalBlocks;
    UINT64 BandSizeInBytes;
    UINT64 RootExtentLba;
    UINT64 DiskStartTableOffset;
    UINT64 DiskStartTableSize;
    UINT8  Reserved[3992];
} SUPERBLOCK;

typedef struct {
    UINT32 ExtensionHash;
    UINT32 CategoryID;
} FILE_TYPE_MDATA;

typedef struct {
    UINT32 TableID;
    UINT32 StringOffset; 
} OPENWITH_MDATA;

typedef struct {
    UINT64          LastOpenedTime;
    FILE_TYPE_MDATA Type;
    OPENWITH_MDATA  OpenWith;
} FILE_MDATA;

typedef struct {
    UINT64 BandIndex;
    UINT32 TotalBlocks;
    UINT32 FreeBlocks;
    UINT64 BitmapOffsetBytes;
    UINT64 CentralDirHead;
} BAND_HEADER;

typedef struct {
    UINT64 StartLba;
    UINT32 BlockCount;
    UINT32 Reserved;
} SJFS_EXTENT;

typedef struct {
    UINT64      ExtensionOrNameHash;
    UINT64      InodeLba;
    FILE_MDATA  Metadata;
    CHAR8       FileName[64];
} SJFS_DIR_ENTRY;

typedef struct {
    UINT16          NodeType;
    UINT16          KeyCount;
    UINT32          Reserved;
    UINT64          ParentNodeLba;
    SJFS_DIR_ENTRY  Entries[36];
    UINT8           Padding[336]; // Garantisce dimensione esatta di 4096 Byte
} SJFS_BTREE_NODE;
#pragma pack(pop)
