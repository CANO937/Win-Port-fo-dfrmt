#pragma once
#include "types.h" 

#pragma pack(push, 1)
typedef struct {
    UINT8  Signature[8];
    UINT32 Revision;
    UINT32 HeaderSize;
    UINT32 HeaderCRC32;
    UINT32 Reserved;
    UINT64 CurrentLBA;
    UINT64 BackupLBA;
    UINT64 FirstUsableLBA;
    UINT64 LastUsableLBA;
    UINT8  DiskGUID[16];
    UINT64 PartitionEntryLBA;
    UINT32 NumberOfPartitionEntries;
    UINT32 SizeOfPartitionEntry;
    UINT32 PartitionEntryArrayCRC32;
} GPT_HEADER;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
    UINT8  PartitionTypeGUID[16];
    UINT8  UniquePartitionGUID[16];
    UINT64 StartingLBA;
    UINT64 EndingLBA;
    UINT64 Attributes;
    UINT16 PartitionName[36];
} GPT_ENTRY;
#pragma pack(pop)

