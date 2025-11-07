#include "clientlog.h"
#include <winioctl.h>

typedef struct _Drive {
    CHAR Root[4], VolumeName[MAX_PATH + 1], FilesystemName[MAX_PATH + 1];
    ULONGLONG TotalSize, FreeSize, BlockSize;
    DEVICE_TYPE Type;
    DWORD Partition;
    struct _Drive *Next;
} diskinfo_Drive;

typedef struct {
    diskinfo_Drive *Drives;
} diskinfo_Disk;

LPCSTR diskinfo_PrettyDeviceType(DWORD type) {
    switch (type) {
    case FILE_DEVICE_CD_ROM:
        return "CD-ROM";
    case FILE_DEVICE_DVD:
        return "DVD";
    case FILE_DEVICE_DISK:
        return "Disk";
    default:
        return "Unknown";
    }
}

LPCSTR diskinfo_PrettyPartitionStyle(DWORD style) {
    switch (style) {
    case PARTITION_STYLE_MBR:
        return "MBR";
    case PARTITION_STYLE_GPT:
        return "GPT";
    case PARTITION_STYLE_RAW:
        return "Raw";
    default:
        return "Unknown";
    }
}

LPCSTR diskinfo_PrettyMBRPartitionType(DWORD type) {
    switch (type) {
    case PARTITION_ENTRY_UNUSED:
        return "Unused";
    case PARTITION_EXTENDED:
        return "Extended";
    case PARTITION_FAT_12:
        return "FAT12";
    case PARTITION_FAT_16:
        return "FAT16";
    case PARTITION_FAT32:
        return "FAT32";
    case PARTITION_IFS:
        return "IFS";
    case PARTITION_LDM:
        return "LDM";
    case PARTITION_NTFT:
        return "NTFT";
    case VALID_NTFT:
        return "Valid NTFT";
    default:
        return "Unknown";
    }
}

LPCSTR diskinfo_PrettyGPTPartitionType(GUID type) {
    switch (type.Data1) {
    case 0x0:
        return "Unused";
    case 0xebd0a0a2:
        return "Basic";
    case 0xc12a7328:
        return "EFI System";
    case 0xe3c9e316:
        return "Microsoft Reserved";
    case 0x5808c8aa:
        return "LDM metadata";
    case 0xaf9b60a0:
        return "LDM data";
    case 0xde94bba4:
        return "Microsoft Recovery";
    default:
        return "Unknown";
    }
}

void diskinfo_AddVolumesToDisks(diskinfo_Disk *disks, DWORD numDisks, clog_Arena *a) {
    LOG_DEBUG("\tdiskinfo.c: Getting Logical Drives.");
    DWORD drivebits = GetLogicalDrives();
    LOG_DEBUG("\tdiskinfo.c: Logical drive bits '%lu'.", drivebits);
    WORD curr = 1;
    for (WORD i = 0; (i < 26) && drivebits; i++) {
        if (curr & drivebits) {
            LOG_DEBUG("\tdiskinfo.c: Start of drive '%c:\\'.", 'A' + i);
            drivebits = drivebits - curr;

            diskinfo_Drive *drive = clog_ArenaAlloc(a, diskinfo_Drive, 1);
            *drive = (diskinfo_Drive){0};
            sprintf(drive->Root, "%c:\\", 'A' + i);
            UINT dtype = GetDriveType(drive->Root);
            if (dtype != DRIVE_FIXED) {
                LOG_DEBUG("\tdiskinfo.c: Skipped drive '%c:\\'. Drive is not fixed storage, or has no volumes.", 'A' + i);
                continue;
            }

            CHAR driveRaw[7];
            sprintf(driveRaw, "\\\\.\\%c:", 'a' + i);

            LOG_DEBUG("\t\tdiskinfo.c: Opening drive file descriptor.");
            HANDLE hPhys = CreateFile(driveRaw, 0, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            STORAGE_DEVICE_NUMBER storageInfo;
            DWORD bytesReturned;
            LOG_DEBUG("\t\tdiskinfo.c: Reading device info from handle.");

            BOOL deviceWritable = DeviceIoControl(
                hPhys,
                IOCTL_DISK_IS_WRITABLE,
                NULL,
                0,
                NULL,
                0,
                &bytesReturned,
                NULL
            );

            if (!deviceWritable) {
                LOG_DEBUG("\t\tdiskinfo.c: Device not writeable. Skipping.");
                CloseHandle(hPhys);
                continue;
            }
            BOOL deviceNumberSuccess = DeviceIoControl(
                hPhys,
                IOCTL_STORAGE_GET_DEVICE_NUMBER,
                NULL,
                0,
                (LPVOID)&storageInfo,
                sizeof(storageInfo),
                &bytesReturned,
                NULL);
            CloseHandle(hPhys);

            if (deviceNumberSuccess) { // TODO handle error
                LOG_DEBUG("\t\tdiskinfo.c: Read device info from file descriptor. Adding drive.");
                diskinfo_Drive *drivestack = disks[storageInfo.DeviceNumber].Drives;
                diskinfo_Drive *drivestackPrev = NULL;
                while (drivestack != NULL) {
                    drivestackPrev = drivestack;
                    drivestack = drivestack->Next;
                }
                if (drivestackPrev == NULL) {
                    LOG_DEBUG("\t\t\tdiskinfo.c: Drive added in new stack.");
                    disks[storageInfo.DeviceNumber].Drives = drive;
                } else {
                    LOG_DEBUG("\t\t\tdiskinfo.c: Drive added in previous stack.");
                    drivestackPrev->Next = drive;
                }

                drive->Partition = storageInfo.PartitionNumber < 16 ? storageInfo.PartitionNumber : INFINITE;
                drive->Type = storageInfo.DeviceType;
            }

            LOG_DEBUG("\t\tdiskinfo.c: Reading volume info.");
            GetVolumeInformation(drive->Root, drive->VolumeName, MAX_PATH + 1, NULL, 0, NULL, drive->FilesystemName, MAX_PATH + 1);

            LOG_DEBUG("\t\tdiskinfo.c: Calculating volume size.");
            DWORD sectorsPerCluster, bytesPerSector, freeClusters, totalClusters;
            if (GetDiskFreeSpace(drive->Root, &sectorsPerCluster, &bytesPerSector, &freeClusters, &totalClusters)) {
                ULONGLONG bytesPerCluster = sectorsPerCluster * bytesPerSector;
                drive->TotalSize = totalClusters * bytesPerCluster;
                drive->FreeSize = freeClusters * bytesPerCluster;
                drive->BlockSize = bytesPerCluster;
            }

            LOG_DEBUG("\tdiskinfo.c: End of drive '%c:\\'.", 'A' + i);
        }

        curr *= 2;
    }
}

void clog_diskinfo(clog_Arena scratch) {
#define ArenaIndentAppend(arena, indentlevel, ...)             \
    do {                                                       \
        clog_ArenaAppend(arena, "\n%*s", indentlevel * 4, ""); \
        clog_ArenaAppend(arena, __VA_ARGS__);                  \
    } while (0)

    LOG_DEBUG("\tdiskinfo.c: Opening registry key HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Services\\disk\\Enum.");
    HKEY hKey;
    RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Services\\disk\\Enum", 0, KEY_READ, &hKey);
    DWORD numDisks, type, len = 64;
    RegGetValue(hKey, NULL, "Count", RRF_RT_REG_DWORD, &type, &numDisks, &len);
    RegCloseKey(hKey);
    LOG_DEBUG("\tdiskinfo.c: Registry key value: %lu.", numDisks);

    diskinfo_Disk disks[numDisks];
    ZeroMemory(disks, numDisks * sizeof(diskinfo_Disk));
    diskinfo_AddVolumesToDisks(disks, numDisks, &scratch);

    clog_ArenaAppend(&scratch, "[diskinfo]");
    for (DWORD i = 0; i < numDisks; i++) {
        LOG_DEBUG("\tdiskinfo.c: Start of physical drive '%lu'.", i);
        CHAR disk[64];
        sprintf(disk, "\\\\.\\PHYSICALDrive%lu", i);

        LOG_DEBUG("\t\tdiskinfo.c: Opening disk file descriptor.");
        HANDLE hPhys = CreateFile(disk, 0, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hPhys == INVALID_HANDLE_VALUE) {
            LOG_DEBUG("\t\tdiskinfo.c: Unable to open disk file descriptor.");
            continue;
        }
        clog_Defer(&scratch, hPhys, RETURN_INT, &CloseHandle);

        SIZE_T layoutBufsize = sizeof(DRIVE_LAYOUT_INFORMATION_EX) + 16 * sizeof(PARTITION_INFORMATION_EX);

        DRIVE_LAYOUT_INFORMATION_EX *driveInfo = clog_ArenaAlloc(&scratch, void, layoutBufsize);
        DWORD bytesReturned;

        LOG_DEBUG("\t\tdiskinfo.c: Reading partition layout info from handle.");
        BOOL succ = DeviceIoControl(
            hPhys,
            IOCTL_DISK_GET_DRIVE_LAYOUT_EX, // dwIoControlCode
            NULL,                           // lpInBuffer
            0,                              // nInBufferSize
            (LPVOID)driveInfo,              // output buffer
            layoutBufsize,                  // size of output buffer
            &bytesReturned,                 // number of bytes returned
            NULL);

        if (!succ || driveInfo->PartitionCount == 0) {
            LOG_DEBUG("\t\tdiskinfo.c: Unable to get partition layout info.");
        } else {
            ArenaIndentAppend(&scratch, 0, "%s", disk);
            LOG_DEBUG("\t\tdiskinfo.c: Found %lu partitions.", driveInfo->PartitionCount);
            ULONGLONG totalDiskLength = 0;
            for (WORD partitionIndex = 0; partitionIndex < driveInfo->PartitionCount; partitionIndex++) {
                PARTITION_INFORMATION_EX partition = driveInfo->PartitionEntry[partitionIndex];
                totalDiskLength += partition.PartitionLength.QuadPart;
            }

            CHAR totalDisk[16];
            ArenaIndentAppend(&scratch, 1, "Total disk size: %s", clog_utils_PrettyBytes(totalDiskLength, 0, totalDisk));
            ArenaIndentAppend(&scratch, 1, "Partitioning style; %s", diskinfo_PrettyPartitionStyle(driveInfo->PartitionStyle));
            ArenaIndentAppend(&scratch, 1, "Partition count: %lu", driveInfo->PartitionCount);

            diskinfo_Drive *drivestack = disks[i].Drives;
            while (drivestack != NULL) {
                if (drivestack->Partition == INFINITE && drivestack->Type == FILE_DEVICE_DISK) {
                    LOG_DEBUG("\t\tdiskinfo.c: Printing info for drive with no partition, '%s'.", drivestack->VolumeName);
                    ArenaIndentAppend(&scratch, 1, "%s Drive %s (no partition)", diskinfo_PrettyDeviceType(drivestack->Type), drivestack->Root);
                    if (drivestack->VolumeName[0] != '\0') ArenaIndentAppend(&scratch, 2, "Volume name:\t%s", drivestack->VolumeName);
                    if (drivestack->FilesystemName[0] != '\0') ArenaIndentAppend(&scratch, 2, "File system:\t%s", drivestack->FilesystemName);
                    CHAR bytesTmp[16];
                    if (drivestack->TotalSize) ArenaIndentAppend(&scratch, 2, "Total space: %9s", clog_utils_PrettyBytes(drivestack->TotalSize, 0, bytesTmp));
                    if (drivestack->FreeSize) ArenaIndentAppend(&scratch, 2, "Free space:  %9s", clog_utils_PrettyBytes(drivestack->FreeSize, 0, bytesTmp));
                    if (drivestack->BlockSize) ArenaIndentAppend(&scratch, 2, "Block size:  %9s", clog_utils_PrettyBytes(drivestack->BlockSize, 0, bytesTmp));
                }
                drivestack = drivestack->Next;
            }

            LOG_DEBUG("\t\tdiskinfo.c: Iterating through partitions.");
            CHAR bytesTmp[16];
            for (DWORD partitionIndex = 0; partitionIndex < driveInfo->PartitionCount; partitionIndex++) {
                PARTITION_INFORMATION_EX partition = driveInfo->PartitionEntry[partitionIndex];
                LOG_DEBUG("\t\tdiskinfo.c: Partition %lu.", partitionIndex);
                LOG_DEBUG("\t\t\tdiskinfo.c: Style %d.", partition.PartitionStyle);
                if (partition.PartitionStyle == PARTITION_STYLE_MBR && partition.Mbr.PartitionType != PARTITION_ENTRY_UNUSED) {
                    ArenaIndentAppend(&scratch, 1, "Partition %lu (%9s, type %s)", partitionIndex + 1, clog_utils_PrettyBytes(partition.PartitionLength.QuadPart, 0, bytesTmp), diskinfo_PrettyMBRPartitionType(partition.Mbr.PartitionType));
                } else if (partition.PartitionStyle == PARTITION_STYLE_GPT && partition.Gpt.PartitionType.Data1 != PARTITION_ENTRY_UNUSED) {
                    ArenaIndentAppend(&scratch, 1, "Partition %lu (%9s, type %s)", partitionIndex + 1, clog_utils_PrettyBytes(partition.PartitionLength.QuadPart, 0, bytesTmp), diskinfo_PrettyGPTPartitionType(partition.Gpt.PartitionType));
                }

                drivestack = disks[i].Drives;
                while (drivestack != NULL) {
                    if (drivestack->Partition == partitionIndex + 1) {
                        LOG_DEBUG("\t\t\tdiskinfo.c: Drive stack matched '%s'.", drivestack->VolumeName);
                        break;
                    }
                    drivestack = drivestack->Next;
                }
                if (drivestack != NULL && drivestack->Type == FILE_DEVICE_DISK) {
                    ArenaIndentAppend(&scratch, 2, "%s Drive %s", diskinfo_PrettyDeviceType(drivestack->Type), drivestack->Root);
                    ArenaIndentAppend(&scratch, 3, "Volume name: %s", drivestack->VolumeName);
                    ArenaIndentAppend(&scratch, 3, "File system: %s", drivestack->FilesystemName);
                    ArenaIndentAppend(&scratch, 3, "Total space: %9s", clog_utils_PrettyBytes(drivestack->TotalSize, 0, bytesTmp));
                    ArenaIndentAppend(&scratch, 3, "Free space:  %9s", clog_utils_PrettyBytes(drivestack->FreeSize, 0, bytesTmp));
                    ArenaIndentAppend(&scratch, 3, "Block size:  %9s", clog_utils_PrettyBytes(drivestack->BlockSize, 0, bytesTmp));
                }
                LOG_DEBUG("\t\tdiskinfo.c: End of partition %lu.", partitionIndex);
            }
        }

        clog_PopDefer(&scratch);
        LOG_DEBUG("\tdiskinfo.c: End of physical drive '%lu'.", i);
    }
#undef ArenaIndentAppend
}

#ifdef STANDALONE
int main(int argc, CHAR *argv[]) {
    clog_ArenaState *st = clog_ArenaMake(0x10000);
    clog_diskinfo(st->Memory);
    clog_PopDeferAll(&st->Memory);
    printf("%s", st->Start);
}
#endif