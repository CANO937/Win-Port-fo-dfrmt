#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
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

BOOLEAN DismountAndLockVolume(HANDLE hDisk) {
    DWORD bytesReturned;
    
    DeviceIoControl(hDisk, IOCTL_DISK_UPDATE_PROPERTIES, NULL, 0, NULL, 0, &bytesReturned, NULL);
    DeviceIoControl(hDisk, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &bytesReturned, NULL);

    if (!DeviceIoControl(hDisk, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &bytesReturned, NULL)) {
        DeviceIoControl(hDisk, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &bytesReturned, NULL);
    }

    return TRUE;
}
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

BOOLEAN CreateGptPartition(HANDLE hDisk, UINT64* outStartLba, UINT64* outSectorCount) {
    UINT32 sectorSize = 0;
    UINT64 totalSectors = 0;
    UINT64 diskSizeBytes = 0;

    if (!GetDiskGeometryInfo(hDisk, &sectorSize, &totalSectors, &diskSizeBytes)) {
        printf("Errore: Impossibile leggere la geometria del disco.\n");
        return FALSE;
    }

    printf("Informazioni Disco:\n");
    printf(" - Dimensione settore: %u Byte\n", sectorSize);
    printf(" - Settori totali     : %llu\n", totalSectors);

    UINT64 alignmentSectors = (1024 * 1024) / sectorSize;
    UINT64 startLba = alignmentSectors; 
    UINT64 endLba = totalSectors - 35;

    if (startLba >= endLba) {
        printf("Errore: Disco troppo piccolo per ospitare una partizione GPT.\n");
        return FALSE;
    }

    *outStartLba = startLba;
    *outSectorCount = (endLba - startLba + 1);


    ProtectiveMbr pMBR;
    memset(&pMBR, 0, sizeof(ProtectiveMbr));
    pMBR.partitions[0].boot_indicator = 0x00;
    pMBR.partitions[0].starting_chs[1] = 0x01;
    pMBR.partitions[0].os_type = 0xEE;
    pMBR.partitions[0].ending_chs[0] = 0xFF;
    pMBR.partitions[0].ending_chs[1] = 0xFF;
    pMBR.partitions[0].ending_chs[2] = 0xFF;
    pMBR.partitions[0].starting_lba = 1;
    UINT64 mbrSectors = totalSectors - 1;
    pMBR.partitions[0].size_in_lba = (mbrSectors > 0xFFFFFFFF) ? 0xFFFFFFFF : (UINT32)mbrSectors;
    pMBR.boot_signature = 0xAA55;

    if (!WriteProtectiveMBR(hDisk, &pMBR)) return FALSE;

    GPT_ENTRY entries[128];
    memset(entries, 0, sizeof(entries));
    static const UINT8 GUID_BASIC_DATA[16] = { 0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44, 0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7 };
    memcpy(entries[0].PartitionTypeGUID, GUID_BASIC_DATA, 16);
    CoCreateGuid((GUID*)entries[0].UniquePartitionGUID);
    entries[0].StartingLBA = startLba;
    entries[0].EndingLBA = endLba;
    entries[0].Attributes = 0;
    const wchar_t* partName = L"Basic Data";
    wcsncpy((wchar_t*)entries[0].PartitionName, partName, 36);

    UINT32 entriesCRC32 = CalculateCRC32(entries, sizeof(entries));

    if (!WriteGptEntries(hDisk, 2, entries)) return FALSE;
    UINT64 backupEntriesLba = totalSectors - 33;
    if (!WriteGptEntries(hDisk, backupEntriesLba, entries)) return FALSE;

    GPT_HEADER primaryHeader;
    memset(&primaryHeader, 0, sizeof(GPT_HEADER));
    memcpy(primaryHeader.Signature, "EFI PART", 8);
    primaryHeader.Revision = 0x00010000;
    primaryHeader.HeaderSize = sizeof(GPT_HEADER);
    primaryHeader.HeaderCRC32 = 0;
    primaryHeader.Reserved = 0;
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

INT32 main(int argc, char *argv[]) {
    CHAR8 *disk = NULL;
    CHAR8 *fs = NULL;
    CHAR8 *part = NULL;
    CHAR8 *vName = NULL;
    CHAR16 volumeName[11];

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) disk = argv[++i];
        else if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) fs = argv[++i];
        else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) part = argv[++i];
        else if (strcmp(argv[i], "-v") == 0 && i + 1 < argc) {
            char *src = argv[++i];
            int j;
            
            for (j = 0; src[j] != '\0' && j < 10; j++) {
                volumeName[j] = (CHAR16)src[j];
            }
            volumeName[j] = L'\0';
            
            vName = (CHAR8 *)src;
        }

    }


    if (!disk || !fs || !part || !vName) {
        printf("Uso: dfrmt -d <disco> -f <exfat|sjfs> -t <gpt|mbr> -v <nome volume>\n");
        return 1;
    }

    char drivePath[64];
    UINT16 diskNumber = atoi(disk);
    snprintf(drivePath, sizeof(drivePath), "\\\\.\\PhysicalDrive%d", diskNumber);

    if (strcmp(fs, "exfat") != 0 && strcmp(fs, "sjfs") != 0 && strcmp(fs, "texfat") != 0) {
        printf("Errore: Filesystem non supportato.\n");
        return 1;
    }

    if (strcmp(part, "gpt") != 0 && strcmp(part, "mbr") != 0) {
        printf("Errore: Tipo partizione non supportato\n");
        return 1;
    }

    printf("ATTENZIONE: Tutti i dati sul disco %s verranno cancellati!\n", disk);
    printf("Procedere? [Y/N]: ");
    char answ[4];
    if (scanf("%3s", answ) != 1 || (strcmp(answ, "Y") != 0 && strcmp(answ, "y") != 0)) {
        printf("Operazione annullata.\n");
        return 0;
    }

    HANDLE hDisk = OpenDiskRaw(drivePath);
    if (hDisk == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        printf("Impossibile aprire il disco %s (richiesti privilegi di Amministratore).\n", disk);
        if (error == ERROR_ACCESS_DENIED) {
            printf("Assicurati di eseguire il programma come AMMINISTRATORE.\n");
        }
        return 1;
    }

    UINT32 sectorSize = 0;
    UINT64 totalSectors = 0;
    UINT64 diskSizeBytes = 0;
    if (!GetDiskGeometryInfo(hDisk, &sectorSize, &totalSectors, &diskSizeBytes)) {
        printf("Errore durante la lettura della geometria del disco.\n");
        CloseHandle(hDisk);
        return 1;
    }

    printf("Informazioni Disco:\n");
    printf(" - Dimensione settore: %u Byte\n", sectorSize);
    printf(" - Settori totali     : %llu\n", totalSectors);

    UINT64 partStartLba = 2048; // Allineamento 1 MB
    UINT64 partEndLba = totalSectors - 35;
    if (partStartLba >= partEndLba) {
        printf("Errore: Disco troppo piccolo per ospitare una partizione GPT.\n");
        CloseHandle(hDisk);
        return 1;
    }
    UINT64 partSectorCount = (partEndLba - partStartLba + 1);

    printf("[1/3] Pulizia tabella partizioni e sblocco settori...\n");
    // NOTA: Assicurati che SECTOR_SIZE sia definito, o usa la variabile dinamica sectorSize
    BYTE zeroSector[512] = { 0 }; 
    for (UINT64 i = 0; i < 34; i++) {
        WriteSectors(hDisk, i, 1, zeroSector);
    }
    DWORD dummy;
    DeviceIoControl(hDisk, IOCTL_DISK_UPDATE_PROPERTIES, NULL, 0, NULL, 0, &dummy, NULL);

    printf("[2/3] Formattazione File System (%s)...\n", fs);
    if (strcmp(fs, "exfat") == 0) {
        if (!FormatExfat(hDisk, partStartLba, partSectorCount, volumeName)) {
            printf("Errore durante la formattazione exFAT.\n");
            CloseHandle(hDisk);
            return 1;
        }
    } else if (strcmp(fs, "sjfs") == 0) {
        // format_sjfs(hDisk, partStartLba, partSectorCount);
    }

    printf("[3/3] Scrittura MBR Protettivo e Tabella GPT...\n");
    UINT64 dummyStart = 0, dummyCount = 0;
    if (!CreateGptPartition(hDisk, &dummyStart, &dummyCount)) {
        printf("Errore durante la creazione della tabella GPT.\n");
        CloseHandle(hDisk);
        return 1;
    }

    DeviceIoControl(hDisk, IOCTL_DISK_UPDATE_PROPERTIES, NULL, 0, NULL, 0, &dummy, NULL);

    printf("\nOperazione completata con successo!\n");
    CloseHandle(hDisk);
    return 0;
}
