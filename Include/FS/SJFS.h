#pragma once
#include <types.h>

#define SJFS_MAGIC 0x534A4653
#define BLOCK_SIZE 4096

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

} __attribute__((packed)) SUPERBLOCK;

typedef struct {
    UINT32 ExtensionHash;
    UINT32 CategoryID;
} __attribute__((packed)) FILE_TYPE_MDATA;

typedef struct {
    UINT32 TableID;
    UINT32 Reserved;
    UINT64 StringOffset;        
} __attribute__((packed)) OPENWITH_MDATA;

typedef struct {
    UINT64          LastOpenedTime;
    FILE_TYPE_MDATA Type;
    OPENWITH_MDATA  OpenWith;
} __attribute__((packed)) FILE_MDATA;

typedef struct {
    UINT64 BandIndex;
    UINT32 TotalBlocks;
    UINT32 FreeBlocks;
    UINT64 BitmapOffsetBytes;
    UINT64 CentralDirHead;
} __attribute__((packed)) BAND_HEADER;

typedef struct {
    UINT64 StartLba;
    UINT32 BlockCount;
    UINT32 Reserved;
} __attribute__((packed)) SJFS_EXTENT;

typedef struct {
    UINT64      ExtensionOrNameHash;
    UINT64      InodeLba;
    FILE_MDATA  Metadata;
    CHAR8       FileName[64];
} __attribute__((packed)) SJFS_DIR_ENTRY;

typedef struct {
    UINT16          NodeType;
    UINT16          KeyCount;
    UINT32          Reserved;
    UINT64          ParentNodeLba;
    SJFS_DIR_ENTRY  Entries[36];
    UINT8           Padding[48];
} __attribute__((packed)) SJFS_BTREE_NODE;