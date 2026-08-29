#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#include <winioctl.h>
#include <objbase.h>
#include <io.h>
#endif

#include <types.h>
#include <gpt.h>
#include <FS/exFAT.h>
#include <mbr.h>

#define SECTOR_SIZE 512
#include <winioctl.h>

UINT32 CalculateCRC32(const void* data, size_t size) {
    UINT32 crc = 0xFFFFFFFF;
    const UINT8* p = (const UINT8*)data;
    while (size--) {
        crc ^= *p++;
        for (int i = 0; i < 8; i++) {
            crc = (crc >> 1) ^ (0xEDB88320 & (-(INT32)(crc & 1)));
        }
    }
    return ~crc;
}
UINT32 CalculateExfatBootChecksum(const UINT8* sectorBuffer, size_t sectorCount, UINT32 currentChecksum) {
    size_t totalBytes = sectorCount * SECTOR_SIZE;
    for (size_t index = 0; index < totalBytes; index++) {
        if (index == 106 || index == 107 || index == 112) {
            continue;
        }
        currentChecksum = ((currentChecksum & 1) ? 0x80000000 : 0) + (currentChecksum >> 1) + (UINT32)sectorBuffer[index];
    }
    return currentChecksum;
}
BOOLEAN WriteSectors(HANDLE hDisk, UINT64 lba, UINT32 sectorCount, const void* buffer) {
    DWORD bytesWritten = 0;
    LARGE_INTEGER offset;

    offset.QuadPart = lba * SECTOR_SIZE;

    if (!SetFilePointerEx(hDisk, offset, NULL, FILE_BEGIN)) {
        printf("Errore SetFilePointerEx all'LBA %llu. Errore Win32: %lu\n", lba, GetLastError());
        return FALSE;
    }

    DWORD bytesToWrite = sectorCount * SECTOR_SIZE;
    if (!WriteFile(hDisk, buffer, bytesToWrite, &bytesWritten, NULL) || bytesWritten != bytesToWrite) {
        printf("Errore WriteFile all'LBA %llu. Errore Win32: %lu\n", lba, GetLastError());
        return FALSE;
    }

    return TRUE;
}
BOOLEAN GetDiskGeometryInfo(HANDLE hDisk, UINT32* outSectorSize, UINT64* outTotalSectors, UINT64* outDiskSizeBytes) {
    DISK_GEOMETRY_EX diskGeometry;
    DWORD bytesReturned = 0;

    BOOL result = DeviceIoControl(hDisk, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, NULL, 0, &diskGeometry, sizeof(diskGeometry), &bytesReturned, NULL);

    if (!result) {
        DWORD error = GetLastError();
        printf("Errore DeviceIoControl (IOCTL_DISK_GET_DRIVE_GEOMETRY_EX): %lu\n", error);
        return FALSE;
    }

    UINT32 sectorSize = diskGeometry.Geometry.BytesPerSector;
    UINT64 diskSizeBytes = (UINT64)diskGeometry.DiskSize.QuadPart;
    UINT64 totalSectors = diskSizeBytes / sectorSize;

    if (outSectorSize) *outSectorSize = sectorSize;
    if (outDiskSizeBytes) *outDiskSizeBytes = diskSizeBytes;
    if (outTotalSectors) *outTotalSectors = totalSectors;

    return TRUE;
}
BOOLEAN WriteGptHeader(HANDLE hDisk, UINT64 lba, const GPT_HEADER* gptHeader) {
    BYTE sectorBuffer[SECTOR_SIZE] = { 0 };

    memcpy(sectorBuffer, gptHeader, sizeof(GPT_HEADER));

    return WriteSectors(hDisk, lba, 1, sectorBuffer);
}
BOOLEAN WriteProtectiveMBR(HANDLE hDisk, const ProtectiveMbr* pMBR) {
    BYTE sectorBuffer[SECTOR_SIZE] = { 0 };

    memcpy(sectorBuffer, pMBR, sizeof(ProtectiveMbr));

    return WriteSectors(hDisk, 0, 1, sectorBuffer);
}
BOOLEAN WriteGptEntries(HANDLE hDisk, UINT64 startLba, const GPT_ENTRY entries[128]) {
    UINT32 totalBytes = 128 * sizeof(GPT_ENTRY); // 16384 byte
    UINT32 sectorCount = totalBytes / SECTOR_SIZE; // 32 settori

    return WriteSectors(hDisk, startLba, sectorCount, entries);
}
HANDLE OpenDiskRaw(const CHAR8* diskPath) {
    HANDLE hDisk = INVALID_HANDLE_VALUE;

    printf("Tentativo di apertura di %s in modalità RAW...\n", diskPath);
    hDisk = CreateFileA(
        diskPath,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH,
        NULL
    );

    if (hDisk != INVALID_HANDLE_VALUE) {
        DWORD bytesReturned;
        DeviceIoControl(hDisk, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &bytesReturned, NULL);
        DeviceIoControl(hDisk, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &bytesReturned, NULL);
    }

    return hDisk;
}
UINT32 CalculateUpcaseChecksum(const UINT8* data, size_t size) {
    UINT32 checksum = 0;
    for (size_t i = 0; i < size; i++) {
        checksum = ((checksum & 1) ? 0x80000000 : 0) + (checksum >> 1) + (UINT32)data[i];
    }
    return checksum;
}
UINT64 ParseSizeToSectors(const char* inputStr, UINT32 sectorSize, UINT64 maxAvailableSectors) {
    if (_stricmp(inputStr, "max") == 0 || strcmp(inputStr, "0") == 0) {
        return maxAvailableSectors;
    }

    double val = 0;
    char unit[16] = { 0 };

    if (sscanf(inputStr, "%lf%15s", &val, unit) < 1) {
        return 0;
    }

    UINT64 bytes = 0;
    if (_stricmp(unit, "GB") == 0 || _stricmp(unit, "G") == 0) {
        bytes = (UINT64)(val * 1024 * 1024 * 1024);
    } else if (_stricmp(unit, "MB") == 0 || _stricmp(unit, "M") == 0) {
        bytes = (UINT64)(val * 1024 * 1024);
    } else if (_stricmp(unit, "KB") == 0 || _stricmp(unit, "K") == 0) {
        bytes = (UINT64)(val * 1024);
    } else if (_stricmp(unit, "B") == 0 || strlen(unit) == 0) {
        bytes = (UINT64)val;
    } else {
        printf("Unità non riconosciuta: %s. Usa B, KB, MB, GB o MAX.\n", unit);
        return 0;
    }

    UINT64 sectors = bytes / sectorSize;
    if (sectors > maxAvailableSectors) {
        sectors = maxAvailableSectors;
    }
    return sectors;
}
BOOLEAN ReadSectors(HANDLE hDisk, UINT64 lba, UINT32 sectorCount, void* buffer) {
    DWORD bytesRead = 0;
    LARGE_INTEGER offset;
    offset.QuadPart = lba * SECTOR_SIZE;
    if (!SetFilePointerEx(hDisk, offset, NULL, FILE_BEGIN)) return FALSE;
    DWORD bytesToRead = sectorCount * SECTOR_SIZE;
    return ReadFile(hDisk, buffer, bytesToRead, &bytesRead, NULL) && (bytesRead == bytesToRead);
}
BOOLEAN CreateEmptyGpt(HANDLE hDisk, UINT64 totalSectors) {
    ProtectiveMbr pMBR;
    memset(&pMBR, 0, sizeof(ProtectiveMbr));
    pMBR.partitions[0].boot_indicator = 0x00;
    pMBR.partitions[0].starting_chs[1] = 0x01;
    pMBR.partitions[0].os_type = 0xEE; // GPT Protective
    pMBR.partitions[0].ending_chs[0] = 0xFF;
    pMBR.partitions[0].ending_chs[1] = 0xFF;
    pMBR.partitions[0].ending_chs[2] = 0xFF;
    pMBR.partitions[0].starting_lba = 1;
    UINT64 mbrSectors = totalSectors - 1;
    pMBR.partitions[0].size_in_lba = (mbrSectors > 0xFFFFFFFF) ? 0xFFFFFFFF : (UINT32)mbrSectors;
    pMBR.boot_signature = 0xAA55;

    if (!WriteProtectiveMBR(hDisk, &pMBR)) return FALSE;

    GPT_ENTRY emptyEntries[128];
    memset(emptyEntries, 0, sizeof(emptyEntries));

    UINT32 entriesCRC32 = CalculateCRC32(emptyEntries, sizeof(emptyEntries));
    if (!WriteGptEntries(hDisk, 2, emptyEntries)) return FALSE;

    UINT64 backupEntriesLba = totalSectors - 33;
    if (!WriteGptEntries(hDisk, backupEntriesLba, emptyEntries)) return FALSE;

    GPT_HEADER primaryHeader;
    memset(&primaryHeader, 0, sizeof(GPT_HEADER));
    memcpy(primaryHeader.Signature, "EFI PART", 8);
    primaryHeader.Revision = 0x00010000;
    primaryHeader.HeaderSize = sizeof(GPT_HEADER);
    primaryHeader.CurrentLBA = 1;
    primaryHeader.BackupLBA = totalSectors - 1;
    primaryHeader.FirstUsableLBA = 34;
    primaryHeader.LastUsableLBA = totalSectors - 35;
    CoCreateGuid((GUID*)primaryHeader.DiskGUID);
    primaryHeader.PartitionEntryLBA = 2;
    primaryHeader.NumberOfPartitionEntries = 128;
    primaryHeader.SizeOfPartitionEntry = sizeof(GPT_ENTRY);
    primaryHeader.PartitionEntryArrayCRC32 = entriesCRC32;
    primaryHeader.HeaderCRC32 = CalculateCRC32(&primaryHeader, sizeof(GPT_HEADER));

    if (!WriteGptHeader(hDisk, 1, &primaryHeader)) return FALSE;

    GPT_HEADER backupHeader = primaryHeader;
    backupHeader.CurrentLBA = totalSectors - 1;
    backupHeader.BackupLBA = 1;
    backupHeader.PartitionEntryLBA = backupEntriesLba;
    backupHeader.HeaderCRC32 = 0;
    backupHeader.HeaderCRC32 = CalculateCRC32(&backupHeader, sizeof(GPT_HEADER));

    if (!WriteGptHeader(hDisk, totalSectors - 1, &backupHeader)) return FALSE;

    return TRUE;
}

BOOLEAN FormatExfat(HANDLE hDisk, UINT64 startLba, UINT64 sectorCount, CHAR16 volumeName[11]) {
    printf("[exFAT] Avvio formattazione a LBA %llu (%llu settori)...\n", startLba, sectorCount);

    BYTE zeroBuffer[512] = { 0 };
    for (UINT64 i = 0; i < 256; i++) {
        WriteSectors(hDisk, startLba + i, 1, zeroBuffer);
    }

    UINT32 sectorsPerCluster = 64;
    UINT32 clusterSize = sectorsPerCluster * SECTOR_SIZE;

    UINT32 fatOffsetSectors = 128;
    UINT64 dataSectors = sectorCount - fatOffsetSectors;
    UINT32 totalClusters = (UINT32)(dataSectors / sectorsPerCluster);
    UINT32 fatLengthSectors = ((totalClusters * 4) + SECTOR_SIZE - 1) / SECTOR_SIZE;
    UINT32 clusterHeapOffsetSectors = fatOffsetSectors + fatLengthSectors;

    clusterHeapOffsetSectors = (clusterHeapOffsetSectors + 2047) & ~2047;
    totalClusters = (UINT32)((sectorCount - clusterHeapOffsetSectors) / sectorsPerCluster);

    BYTE bootRegion[12 * SECTOR_SIZE];
    memset(bootRegion, 0, sizeof(bootRegion));

    EXFAT_VBR* vbr = (EXFAT_VBR*)bootRegion;
    vbr->JumpBoot[0] = 0xEB;
    vbr->JumpBoot[1] = 0x76;
    vbr->JumpBoot[2] = 0x90;
    memcpy(vbr->FileSystemName, "EXFAT   ", 8);
    vbr->PartitionOffset = startLba;
    vbr->VolumeLength = sectorCount;
    vbr->FatOffset = fatOffsetSectors;
    vbr->FatLength = fatLengthSectors;
    vbr->ClusterHeapOffset = clusterHeapOffsetSectors;
    vbr->ClusterCount = totalClusters;
    vbr->RootDirectoryCluster = 2;
    vbr->VolumeSerialNumber = 0x12345678;
    vbr->FileSystemRevision = 0x0100;
    vbr->VolumeFlags = 0x0000;
    vbr->BytesPerSectorShift = 9;
    vbr->SectorsPerClusterShift = 6;
    vbr->NumberOfFats = 1;
    vbr->DriveSelect = 0x80;
    vbr->PercentInUse = 0x00;

    for (int s = 0; s <= 8; s++) {
        bootRegion[s * SECTOR_SIZE + 510] = 0x55;
        bootRegion[s * SECTOR_SIZE + 511] = 0xAA;
    }

    UINT32 checksum = CalculateExfatBootChecksum(bootRegion, 11, 0);
    UINT32* checksumSector = (UINT32*)(bootRegion + 11 * SECTOR_SIZE);
    for (size_t i = 0; i < SECTOR_SIZE / sizeof(UINT32); i++) {
        checksumSector[i] = checksum;
    }

    if (!WriteSectors(hDisk, startLba, 12, bootRegion)) return FALSE;
    if (!WriteSectors(hDisk, startLba + 12, 12, bootRegion)) return FALSE;

    size_t upcaseSize = 65536 * sizeof(UINT16); // 131.072 Byte
    UINT32 upcaseSectors = (UINT32)(upcaseSize / SECTOR_SIZE); // 256 Settori
    BYTE* upcaseBuffer = (BYTE*)calloc(1, upcaseSize);
    if (!upcaseBuffer) return FALSE;

    UINT16* upcase16 = (UINT16*)upcaseBuffer;
    for (int i = 0; i < 65536; i++) {
        if (i >= 'a' && i <= 'z') {
            upcase16[i] = (UINT16)(i - 32);
        } else {
            upcase16[i] = (UINT16)i;
        }
    }
    UINT32 upcaseChecksum = CalculateUpcaseChecksum(upcaseBuffer, upcaseSize);

    UINT64 cluster4Lba = startLba + clusterHeapOffsetSectors + (UINT64)(4 - 2) * sectorsPerCluster;
    if (!WriteSectors(hDisk, cluster4Lba, upcaseSectors, upcaseBuffer)) {
        free(upcaseBuffer);
        return FALSE;
    }
    free(upcaseBuffer);

    UINT32* fatBuffer = (UINT32*)calloc(fatLengthSectors, SECTOR_SIZE);
    if (!fatBuffer) return FALSE;

    fatBuffer[0] = 0xFFFFFFF8;
    fatBuffer[1] = 0xFFFFFFFF;
    fatBuffer[2] = EXFAT_FAT_EOF;
    fatBuffer[3] = EXFAT_FAT_EOF;
    fatBuffer[4] = 5;
    fatBuffer[5] = 6; 
    fatBuffer[6] = 7;
    fatBuffer[7] = EXFAT_FAT_EOF;

    if (!WriteSectors(hDisk, startLba + fatOffsetSectors, fatLengthSectors, fatBuffer)) {
        free(fatBuffer);
        return FALSE;
    }
    free(fatBuffer);

    BYTE* rootBuffer = (BYTE*)calloc(1, clusterSize);
    if (!rootBuffer) return FALSE;

    EXFAT_ENTRY_LABEL_STRUCT* label = (EXFAT_ENTRY_LABEL_STRUCT*)(rootBuffer + 0);
    label->EntryType = EXFAT_ENTRY_LABEL;
    label->CharacterCount = 5;
    memcpy(label->VolumeLabel, volumeName, 5 * sizeof(wchar_t));

    EXFAT_ENTRY_BITMAP_STRUCT* bitmap = (EXFAT_ENTRY_BITMAP_STRUCT*)(rootBuffer + 32);
    bitmap->EntryType = EXFAT_ENTRY_BITMAP;
    bitmap->BitmapFlags = 0;
    bitmap->FirstCluster = 3;
    bitmap->DataLength = (totalClusters + 7) / 8;

    EXFAT_ENTRY_UPCASE_STRUCT* upcase = (EXFAT_ENTRY_UPCASE_STRUCT*)(rootBuffer + 64);
    upcase->EntryType = EXFAT_ENTRY_UPCASE;
    upcase->TableChecksum = upcaseChecksum;
    upcase->FirstCluster = 4;
    upcase->DataLength = upcaseSize;

    UINT64 cluster2Lba = startLba + clusterHeapOffsetSectors + (UINT64)(2 - 2) * sectorsPerCluster;
    if (!WriteSectors(hDisk, cluster2Lba, sectorsPerCluster, rootBuffer)) {
        free(rootBuffer);
        return FALSE;
    }
    free(rootBuffer);

    BYTE* bitmapBuffer = (BYTE*)calloc(1, clusterSize);
    if (!bitmapBuffer) return FALSE;
    bitmapBuffer[0] = 0x3F;

    UINT64 cluster3Lba = startLba + clusterHeapOffsetSectors + (UINT64)(3 - 2) * sectorsPerCluster;
    if (!WriteSectors(hDisk, cluster3Lba, sectorsPerCluster, bitmapBuffer)) {
        free(bitmapBuffer);
        return FALSE;
    }
    free(bitmapBuffer);

    printf("[exFAT] Scrittura metadati completata con successo.\n");
    return TRUE;
}

BOOLEAN AddPartitionToGpt(HANDLE hDisk, const CHAR8* fsType, const char* sizeStr, const wchar_t* partName) {
    UINT32 sectorSize = 0;
    UINT64 totalSectors = 0;
    UINT64 diskSizeBytes = 0;
    if (!GetDiskGeometryInfo(hDisk, &sectorSize, &totalSectors, &diskSizeBytes)) {
        printf("Errore lettura geometria disco.\n");
        return FALSE;
    }

    BYTE headerBuf[SECTOR_SIZE];
    if (!ReadSectors(hDisk, 1, 1, headerBuf)) {
        printf("Errore lettura Header GPT.\n");
        return FALSE;
    }
    GPT_HEADER* header = (GPT_HEADER*)headerBuf;
    if (memcmp(header->Signature, "EFI PART", 8) != 0) {
        printf("Errore: Il disco non contiene una tabella GPT valida. Inizializzalo prima con 'frmtdisk'.\n");
        return FALSE;
    }

    GPT_ENTRY entries[128];
    if (!ReadSectors(hDisk, 2, 32, entries)) {
        printf("Errore lettura voci GPT.\n");
        return FALSE;
    }

    UINT64 highestLbaUsed = 2047;
    int freeSlotIndex = -1;

    static const UINT8 ZERO_GUID[16] = { 0 };

    for (int i = 0; i < 128; i++) {
        if (memcmp(entries[i].PartitionTypeGUID, ZERO_GUID, 16) != 0) {
            if (entries[i].EndingLBA > highestLbaUsed) {
                highestLbaUsed = entries[i].EndingLBA;
            }
        } else if (freeSlotIndex == -1) {
            freeSlotIndex = i;
        }
    }

    if (freeSlotIndex == -1) {
        printf("Errore: Raggiunto il limite massimo di 128 partizioni GPT.\n");
        return FALSE;
    }

    UINT64 alignmentSectors = (1024 * 1024) / sectorSize;
    UINT64 newStartLba = (highestLbaUsed + 1 + alignmentSectors - 1) & ~(alignmentSectors - 1);
    UINT64 lastUsableLba = header->LastUsableLBA;

    if (newStartLba >= lastUsableLba) {
        printf("Errore: Nessun spazio libero rimasto sul disco.\n");
        return FALSE;
    }

    UINT64 maxAvailableSectors = lastUsableLba - newStartLba + 1;
    UINT64 requestedSectors = ParseSizeToSectors(sizeStr, sectorSize, maxAvailableSectors);

    if (requestedSectors == 0) {
        printf("Dimensione partizione non valida.\n");
        return FALSE;
    }

    UINT64 newEndLba = newStartLba + requestedSectors - 1;
    printf("[GPT] Nuova partizione #%d: LBA %llu -> %llu (%llu MB)\n", 
           freeSlotIndex + 1, newStartLba, newEndLba, (requestedSectors * sectorSize) / (1024 * 1024));

    printf("[1/2] Formattazione File System (%s)...\n", fsType);
    if (strcmp(fsType, "exfat") == 0) {
        if (!FormatExfat(hDisk, newStartLba, requestedSectors, (CHAR16*)partName)) {
            printf("Errore durante la formattazione exFAT della nuova partizione.\n");
            return FALSE;
        }
    } else {
        printf("File system '%s' non ancora implementato.\n", fsType);
        return FALSE;
    }

    static const UINT8 GUID_BASIC_DATA[16] = {
        0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44, 
        0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7
    };
    memcpy(entries[freeSlotIndex].PartitionTypeGUID, GUID_BASIC_DATA, 16);
    CoCreateGuid((GUID*)entries[freeSlotIndex].UniquePartitionGUID);
    entries[freeSlotIndex].StartingLBA = newStartLba;
    entries[freeSlotIndex].EndingLBA = newEndLba;
    entries[freeSlotIndex].Attributes = 0;
    wcsncpy((wchar_t*)entries[freeSlotIndex].PartitionName, partName, 36);

    UINT32 entriesCRC32 = CalculateCRC32(entries, sizeof(entries));

    header->PartitionEntryArrayCRC32 = entriesCRC32;
    header->HeaderCRC32 = 0;
    header->HeaderCRC32 = CalculateCRC32(header, sizeof(GPT_HEADER));

    printf("[2/2] Aggiornamento Tabella GPT...\n");
    WriteGptEntries(hDisk, 2, entries);
    WriteGptHeader(hDisk, 1, header);

    UINT64 backupEntriesLba = totalSectors - 33;
    WriteGptEntries(hDisk, backupEntriesLba, entries);

    GPT_HEADER backupHeader = *header;
    backupHeader.CurrentLBA = totalSectors - 1;
    backupHeader.BackupLBA = 1;
    backupHeader.PartitionEntryLBA = backupEntriesLba;
    backupHeader.HeaderCRC32 = 0;
    backupHeader.HeaderCRC32 = CalculateCRC32(&backupHeader, sizeof(GPT_HEADER));
    WriteGptHeader(hDisk, totalSectors - 1, &backupHeader);

    DWORD dummy;
    DeviceIoControl(hDisk, IOCTL_DISK_UPDATE_PROPERTIES, NULL, 0, NULL, 0, &dummy, NULL);

    return TRUE;
}

INT32 main() {
    char cmd[64];
    char disk[64];
    char fs[64];
    char part[64];
    char drivePath[64];
    char noOfBytesPerPartition[64];
    char driveSelected = 0;

    while (1) {
        printf("\ndfrmt >>> ");
        if (scanf("%63s", cmd) != 1) continue;

        if (strcmp(cmd, "setdisk") == 0) {
            printf("dfrmt >>> [Disk Number] ");
            if (scanf("%63s", disk) == 1) {
                UINT16 diskNumber = atoi(disk);
                snprintf(drivePath, sizeof(drivePath), "\\\\.\\PhysicalDrive%d", diskNumber);
                driveSelected = 1;
                printf("Disco impostato: %s\n", drivePath);
            }
        }
        else if (strcmp(cmd, "frmtdisk") == 0) {    
            if (!driveSelected) {
                printf("Errore: Seleziona prima un disco con 'setdisk'.\n");
                continue;
            }
            
            printf("dfrmt >>> [Partition Scheme (gpt/mbr)] ");
            scanf("%63s", part); 
            if (strcmp(part, "gpt") != 0 && strcmp(part, "mbr") != 0) {
                printf("Errore: Tipo partizione non supportato\n");
                continue;
            }
            
            printf("ATTENZIONE: Tutti i dati e le partizioni sul disco %s verranno cancellati!\n", drivePath);
            printf("Procedere? [Y/N]: ");
            char answ[4];
            if (scanf("%3s", answ) != 1 || (strcmp(answ, "Y") != 0 && strcmp(answ, "y") != 0)) {
                printf("Operazione annullata.\n");
                continue;
            }

            HANDLE hDisk = OpenDiskRaw(drivePath);
            if (hDisk == INVALID_HANDLE_VALUE) {
                printf("Impossibile aprire il disco %s (richiesti privilegi di Amministratore).\n", drivePath);
                continue;
            }

            UINT32 sectorSize = 0;
            UINT64 totalSectors = 0;
            UINT64 diskSizeBytes = 0;
            if (!GetDiskGeometryInfo(hDisk, &sectorSize, &totalSectors, &diskSizeBytes)) {
                printf("Errore durante la lettura della geometria del disco.\n");
                CloseHandle(hDisk);
                continue;
            }

            printf("[1/3] Pulizia metadati (GPT Primaria e Backup GPT)...\n");
            BYTE zeroSector[512] = { 0 }; 

            for (UINT64 i = 0; i < 34; i++) {
                WriteSectors(hDisk, i, 1, zeroSector);
            }

            UINT64 backupStartLba = totalSectors - 33;
            for (UINT64 i = backupStartLba; i < totalSectors; i++) {
                WriteSectors(hDisk, i, 1, zeroSector);
            }

            DWORD dummy;
            DeviceIoControl(hDisk, IOCTL_DISK_UPDATE_PROPERTIES, NULL, 0, NULL, 0, &dummy, NULL);

            if (strcmp(part, "gpt") == 0) {
                printf("[2/3] Inizializzazione Tabella GPT vuota...\n");
                if (!CreateEmptyGpt(hDisk, totalSectors)) {
                    printf("Errore durante la creazione della GPT.\n");
                    CloseHandle(hDisk);
                    continue;
                }
            } else if (strcmp(part, "mbr") == 0) {
                printf("[2/3] unsupported\n");
                return 0;
            }

            printf("[3/3] Aggiornamento proprietà del disco nel OS...\n");
            DeviceIoControl(hDisk, IOCTL_DISK_UPDATE_PROPERTIES, NULL, 0, NULL, 0, &dummy, NULL);
            
            printf("\nDisco inizializzato con successo! Ora puoi usare 'addpart' per aggiungere partizioni.\n");
            CloseHandle(hDisk);
        }
        else if (strcmp(cmd, "addpart") == 0) { 
            if (!driveSelected) {
                printf("Errore: Seleziona prima un disco con 'setdisk'.\n");
                continue;
            }

            printf("dfrmt >>> [Filesystem] ");
            scanf("%63s", fs);
            if (strcmp(fs, "exfat") != 0 && strcmp(fs, "sjfs") != 0) {
                printf("Errore: Filesystem non supportato.\n");
                continue;
            }

            printf("dfrmt >>> [Partition Size (es. 500MB, 2GB, MAX)] ");
            scanf("%63s", noOfBytesPerPartition);

            char labelBuf[64];
            printf("dfrmt >>> [Partition Label] ");
            scanf("%63s", labelBuf);

            wchar_t wLabel[36] = { 0 };
            mbstowcs(wLabel, labelBuf, 35);

            HANDLE hDisk = OpenDiskRaw(drivePath);
            if (hDisk == INVALID_HANDLE_VALUE) {
                printf("Impossibile aprire il disco %s (richiesti privilegi di Amministratore).\n", drivePath);
                continue;
            }

            if (AddPartitionToGpt(hDisk, fs, noOfBytesPerPartition, wLabel)) {
                printf("\nPartizione aggiunta e formattata con successo!\n");
            } else {
                printf("\nErrore durante l'aggiunta della partizione.\n");
            }

            CloseHandle(hDisk);
        }
        else if (strcmp(cmd, "exit") == 0) {
            return 0;
        }
        else {
            printf("Comandi disponibili: setdisk, frmtdisk, addpart, exit\n");
        }
    }

    return 0;
}