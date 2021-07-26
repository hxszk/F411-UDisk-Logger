/*
 * This file is part of Cleanflight and Betaflight.
 *
 * Cleanflight and Betaflight are free software. You can redistribute
 * this software and/or modify this software under the terms of the
 * GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * Cleanflight and Betaflight are distributed in the hope that they
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this software.
 *
 * If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "bf_utils.h"
#include "bf_flash.h"
#include "bf_flash_impl.h"
#include "bf_flash_w25q.h"


static flashDevice_t flashDevice;
static flashPartitionTable_t flashPartitionTable;
static int flashPartitions = 0;


bool flashDeviceInit(void)
{
    return w25q_Init(&flashDevice);
}

bool flashIsReady(void)
{
    return flashDevice.vTable->isReady(&flashDevice);
}

bool flashWaitForReady(void)
{
    return flashDevice.vTable->waitForReady(&flashDevice);
}

void flashEraseSector(uint32_t address)
{
    flashDevice.vTable->eraseSector(&flashDevice, address);
}

void flashEraseCompletely(void)
{
    flashDevice.vTable->eraseCompletely(&flashDevice);
}

void flashPageProgramBegin(uint32_t address)
{
    flashDevice.vTable->pageProgramBegin(&flashDevice, address);
}

void flashPageProgramContinue(const uint8_t *data, int length)
{
    flashDevice.vTable->pageProgramContinue(&flashDevice, data, length);
}

void flashPageProgramFinish(void)
{
    flashDevice.vTable->pageProgramFinish(&flashDevice);
}

void flashPageProgram(uint32_t address, const uint8_t *data, int length)
{
    flashDevice.vTable->pageProgram(&flashDevice, address, data, length);
}

int flashReadBytes(uint32_t address, uint8_t *buffer, int length)
{
    return flashDevice.vTable->readBytes(&flashDevice, address, buffer, length);
}

void flashFlush(void)
{
    if (flashDevice.vTable->flush) {
        flashDevice.vTable->flush(&flashDevice);
    }
}

static const flashGeometry_t noFlashGeometry = {
    .totalSize = 0,
};

const flashGeometry_t *flashGetGeometry(void)
{
    if (flashDevice.vTable && flashDevice.vTable->getGeometry) {
        return flashDevice.vTable->getGeometry(&flashDevice);
    }

    return &noFlashGeometry;
}

/*
 * Flash partitioning
 *
 * Partition table is not currently stored on the flash, in-memory only.
 *
 * Partitions are required so that Badblock management (inc spare blocks), FlashFS (Blackbox Logging), Configuration and Firmware can be kept separate and tracked.
 *
 * XXX FIXME
 * XXX Note that Flash FS must start at sector 0.
 * XXX There is existing blackbox/flash FS code the relies on this!!!
 * XXX This restriction can and will be fixed by creating a set of flash operation functions that take partition as an additional parameter.
 */

static void flashConfigurePartitions(void)
{

    const flashGeometry_t *flashGeometry = flashGetGeometry();
    if (flashGeometry->totalSize == 0) {
        return;
    }

    flashSector_t startSector = 0;
    flashSector_t endSector = flashGeometry->sectors - 1; // 0 based index

    const flashPartition_t *badBlockPartition = flashPartitionFindByType(FLASH_PARTITION_TYPE_BADBLOCK_MANAGEMENT);
    if (badBlockPartition) {
        endSector = badBlockPartition->startSector - 1;
    }

    flashPartitionSet(FLASH_PARTITION_TYPE_FLASHFS, startSector, endSector);
}

flashPartition_t *flashPartitionFindByType(flashPartitionType_e type)
{
    for (int index = 0; index < FLASH_MAX_PARTITIONS; index++) {
        flashPartition_t *candidate = &flashPartitionTable.partitions[index];
        if (candidate->type == type) {
            return candidate;
        }
    }

    return NULL;
}

const flashPartition_t *flashPartitionFindByIndex(uint8_t index)
{
    if (index >= flashPartitions) {
        return NULL;
    }

    return &flashPartitionTable.partitions[index];
}

void flashPartitionSet(uint8_t type, uint32_t startSector, uint32_t endSector)
{
    flashPartition_t *entry = flashPartitionFindByType(type);

    if (!entry) {
        if (flashPartitions == FLASH_MAX_PARTITIONS - 1) {
            return;
        }
        entry = &flashPartitionTable.partitions[flashPartitions++];
    }

    entry->type = type;
    entry->startSector = startSector;
    entry->endSector = endSector;
}

// Must be in sync with FLASH_PARTITION_TYPE
static const char *flashPartitionNames[] = {
    "UNKNOWN  ",
    "PARTITION",
    "FLASHFS  ",
    "BBMGMT   ",
    "FIRMWARE ",
    "CONFIG   ",
};

const char *flashPartitionGetTypeName(flashPartitionType_e type)
{
    if (type < ARRAYLEN(flashPartitionNames)) {
        return flashPartitionNames[type];
    }

    return NULL;
}

bool flashInit(void)
{
    memset(&flashPartitionTable, 0x00, sizeof(flashPartitionTable));
    flashPartitions = 0;

    bool haveFlash = flashDeviceInit();

    flashConfigurePartitions();

    return haveFlash;
}

int flashPartitionCount(void)
{
    return flashPartitions;
}

