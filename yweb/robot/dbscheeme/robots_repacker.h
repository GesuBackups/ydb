#pragma once

#include "baserecords.h"
#include <library/cpp/robots_txt/robotstxtcfg.h>

inline void RepackRobotsOldToNew(
    const char* oldRobotsPacked,
    const ui32 oldRobotsPackedSize,
    char* newRobotsPacked,
    ui32& newRobotsPackedSize,
    const ui32 botId=robotstxtcfg::id_defbot) {

    newRobotsPackedSize = 0;

    *((ui32*)newRobotsPacked) = 1; // num of bots
    newRobotsPacked += sizeof(ui32);
    newRobotsPackedSize += sizeof(ui32);

    *((ui32*)newRobotsPacked) = botId; // bot id
    newRobotsPacked += sizeof(ui32);
    newRobotsPackedSize += sizeof(ui32);

    if (oldRobotsPackedSize != 0) {
        memcpy(newRobotsPacked, oldRobotsPacked, oldRobotsPackedSize); // bot data
        newRobotsPackedSize += oldRobotsPackedSize;
    }
}

inline void RepackRobotsNewToOld(
    const ui32 wantedBotId,
    const char* newRobotsPacked,
    const ui32 newRobotsPackedSize,
    char* oldRobotsPacked,
    ui32& oldRobotsPackedSize) {

    Y_UNUSED(newRobotsPackedSize);

    oldRobotsPackedSize = 0;

    ui32 numOfEncodedBots = *((ui32*)newRobotsPacked);
    newRobotsPacked += sizeof(ui32);

    for (ui32 botIndex = 0; botIndex < numOfEncodedBots; ++botIndex) {

        ui32 botId = *((ui32*)newRobotsPacked);
        newRobotsPacked += sizeof(ui32);
        ui32 botDataSize = *((ui32*)newRobotsPacked);
        if (botId == wantedBotId && botDataSize > sizeof(ui32)) {
            memcpy(oldRobotsPacked, newRobotsPacked, botDataSize);
            oldRobotsPackedSize = botDataSize;
            return;
        }
        if (botId == robotstxtcfg::id_anybot) {
            memcpy(oldRobotsPacked, newRobotsPacked, botDataSize);
            oldRobotsPackedSize = botDataSize;
        }
        newRobotsPacked += botDataSize;

    }

}

inline void RepackRobots(
    const robots_old_t& oldRobots,
    multirobots_t& newRobots) {

    // repack from old format to new format
    if (oldRobots.Size != 0)
        RepackRobotsOldToNew(oldRobots.Data, oldRobots.Size, newRobots.Data, newRobots.Size);
    else
        newRobots.Size = 0;
}

inline void RepackRobots(
    const ui32 wantedBotId,
    const multirobots_t& newRobots,
    robots_old_t& oldRobots) {

    // repack from new format to old format, save data only for wantedBotId
    if (!newRobots.IsEmpty())
        RepackRobotsNewToOld(wantedBotId, newRobots.Data, newRobots.Size, oldRobots.Data, oldRobots.Size);
    else
        oldRobots.Size = 0;
}

inline void RepackHostRobots(
    const host_robots_old_t& oldHostRobots,
    host_multirobots_t& newHostRobots) {

    // repack from old format to new format
    char* packedPtr = newHostRobots.Data;

    size_t namelen = strlen(oldHostRobots.Data) + 1;
    strncpy(packedPtr, oldHostRobots.Data, namelen);
    packedPtr += namelen;

    if (oldHostRobots.Size != 0)
        RepackRobotsOldToNew(oldHostRobots.Data + namelen, oldHostRobots.Size, packedPtr, newHostRobots.Size);
    else
        newHostRobots.Size = 0;
}

inline void RepackHostRobots(
    const ui32 wantedBotId,
    const host_multirobots_t& newHostRobots,
    host_robots_old_t& oldHostRobots) {

    // repack from old format to new format
    char* packedPtr = oldHostRobots.Data;

    size_t namelen = strlen(newHostRobots.Data) + 1;
    strncpy(packedPtr, newHostRobots.Data, namelen);
    packedPtr += namelen;

    if (!newHostRobots.IsEmpty())
        RepackRobotsNewToOld(wantedBotId, newHostRobots.Data + namelen, newHostRobots.Size, packedPtr, oldHostRobots.Size);
    else
        oldHostRobots.Size = 0;

}
