// -*-Mode: C++;-*- // technically C99

//*BeginLicense**************************************************************
//
//---------------------------------------------------------------------------
// TAZeR (github.com/pnnl/tazer/)
//---------------------------------------------------------------------------
//
// Copyright ((c)) 2019, Battelle Memorial Institute
//
// 1. Battelle Memorial Institute (hereinafter Battelle) hereby grants
//    permission to any person or entity lawfully obtaining a copy of
//    this software and associated documentation files (hereinafter "the
//    Software") to redistribute and use the Software in source and
//    binary forms, with or without modification.  Such person or entity
//    may use, copy, modify, merge, publish, distribute, sublicense,
//    and/or sell copies of the Software, and may permit others to do
//    so, subject to the following conditions:
//    
//    * Redistributions of source code must retain the above copyright
//      notice, this list of conditions and the following disclaimers.
//
//    * Redistributions in binary form must reproduce the above
//      copyright notice, this list of conditions and the following
//      disclaimer in the documentation and/or other materials provided
//      with the distribution.
//
//    * Other than as used herein, neither the name Battelle Memorial
//      Institute or Battelle may be used in any form whatsoever without
//      the express written consent of Battelle.
//
// 2. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
//    CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
//    INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
//    MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
//    DISCLAIMED. IN NO EVENT SHALL BATTELLE OR CONTRIBUTORS BE LIABLE
//    FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
//    CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
//    OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
//    BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
//    LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
//    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
//    USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
//    DAMAGE.
//
// ***
//
// This material was prepared as an account of work sponsored by an
// agency of the United States Government.  Neither the United States
// Government nor the United States Department of Energy, nor Battelle,
// nor any of their employees, nor any jurisdiction or organization that
// has cooperated in the development of these materials, makes any
// warranty, express or implied, or assumes any legal liability or
// responsibility for the accuracy, completeness, or usefulness or any
// information, apparatus, product, software, or process disclosed, or
// represents that its use would not infringe privately owned rights.
//
// Reference herein to any specific commercial product, process, or
// service by trade name, trademark, manufacturer, or otherwise does not
// necessarily constitute or imply its endorsement, recommendation, or
// favoring by the United States Government or any agency thereof, or
// Battelle Memorial Institute. The views and opinions of authors
// expressed herein do not necessarily state or reflect those of the
// United States Government or any agency thereof.
//
//                PACIFIC NORTHWEST NATIONAL LABORATORY
//                             operated by
//                               BATTELLE
//                               for the
//                  UNITED STATES DEPARTMENT OF ENERGY
//                   under Contract DE-AC05-76RL01830
// 
//*EndLicense****************************************************************

#include "ScalableMetaData.h"
#include "ScalableCache.h"

// #define DPRINTF(...) fprintf(stderr, __VA_ARGS__); fflush(stderr)
#define DPRINTF(...)

uint8_t * ScalableMetaData::getBlockData(uint64_t blockIndex, uint64_t fileOffset, bool &reserve, bool track) {
    bool toReserve = false;
    auto blockEntry = &blocks[blockIndex];
    
    //JS: This is for the first call from readBlock, which sets the read lock until blockset
    if(track) {
        auto timeStamp = trackAccess(blockIndex, fileOffset);
        auto old = blockEntry->blkLock.readerLock();
        toReserve = (old == 0);
        blockEntry->timeStamp.store(timeStamp);
        DPRINTF("ScalableMetaData::getBlockData blockIndex: %lu reserve: %d\n", blockIndex, old);
    }

    //JS: Look to see if the data is avail, notice this is after a reader lock
    uint8_t * ret = blockEntry->data.load();
    if(ret)
        toReserve = false;
    reserve = toReserve;
    return ret;
}

void ScalableMetaData::setBlock(uint64_t blockIndex, uint8_t * data) {
    auto blockEntry = &blocks[blockIndex];

    //JS: Inc our total number of blocks
    numBlocks.fetch_add(1);
    blockEntry->data.store(data);

    //JS: Dec our usage. It was inc'ed in getBlock
    auto temp = blockEntry->blkLock.readerUnlock();
    DPRINTF("ScalableMetaData::setBlock unlock: %lu\n", temp);

    metaLock.writerLock();
    currentBlocks.push_back(blockEntry);
    metaLock.writerUnlock();
}

void ScalableMetaData::decBlockUsage(uint64_t blockIndex) {
    auto temp = blocks[blockIndex].blkLock.readerUnlock();
    DPRINTF("ScalableMetaData::decBlockUsage unlock: %lu\n", temp);
}

bool ScalableMetaData::backOutOfReservation(uint64_t blockIndex) {
    auto ret = blocks[blockIndex].blkLock.cowardlyUpdgradeWriterLock();
    if(ret)
        blocks[blockIndex].blkLock.writerUnlock();
    return ret;
}

uint64_t ScalableMetaData::trackAccess(uint64_t blockIndex, uint64_t readIndex) {
    metaLock.writerLock();

    uint64_t timeStamp = (uint64_t)Timer::getTimestamp();
    std::array<uint64_t, 3> access = {readIndex, blockIndex, timeStamp};
    window.push_front(access);
    
    if(window.size() == 6)
        window.pop_back();
    metaLock.writerUnlock();
    
    return timeStamp;
}

//JS TODO: add to trackBlock with -1 for blockIndex and priority
bool ScalableMetaData::checkPattern(Cache * cache, uint32_t fileIndex) {
    bool ret = true;
    metaLock.readerLock();

    auto newPattern = pattern;
    auto oldPattern = pattern;
    if (window.size() > 2) {
        newPattern = BLOCKSTREAMING;
        for (unsigned int i = 1; i < window.size(); i++) {
            uint64_t stride = window[i-1][1] - window[i][1];
            DPRINTF("%d:%lu - %d:%lu %lu\n", i, window[i-1][1], i-1, window[i][1], stride);
            if (stride < 0 || stride > 1) {
                newPattern = UNKNOWN;
            }
        }
    }
    DPRINTF("oldPattern: %s newPattern: %s\n", patternName[oldPattern], patternName[newPattern]);
    //JS: For now the only case that limits is linear with more than one block
    if(newPattern == BLOCKSTREAMING && numBlocks.load())
        ret = false;

    metaLock.readerUnlock();

    if(oldPattern != newPattern) {
        DPRINTF("[JS] Updating cache pattern %s\n", patternName[newPattern]);
        ((ScalableCache*)cache)->trackPattern(fileIndex, patternName[newPattern]);
        metaLock.writerLock();
        pattern = newPattern;
        metaLock.writerUnlock();
    }
    return ret;
}

uint64_t ScalableMetaData::getLastTimeStamp() {
    metaLock.readerLock();
    auto ret = window.front()[2];
    metaLock.readerUnlock();
    return ret;
}

uint8_t * ScalableMetaData::oldestBlock(uint64_t &blockIndex) {
    metaLock.writerLock();

    if(currentBlocks.size() > 1) {
    DPRINTF("ScalableMetaData::oldestBlock CurrentBlock.size(): %d\n", currentBlocks.size());
        sort(currentBlocks.begin(), currentBlocks.end(), 
            [](BlockEntry* lhs, BlockEntry* rhs) {
                return lhs->timeStamp.load() < rhs->timeStamp.load();
            });
    }
    // std::cerr << meta->currentBlocks.front()->timeStamp.load() << " " << meta->currentBlocks.back()->timeStamp.load() << std::endl;
    
    uint8_t * ret = NULL;
    for(auto it = currentBlocks.begin(); it != currentBlocks.end(); it++) {
        //JS: Check the block is not being requested
        if ((*it)->blkLock.cowardlyTryWriterLock()) {
            //JS: For tracking...
            blockIndex = (*it)->blockIndex;
            DPRINTF("block: %lu timestamp: %lu\n", blockIndex, (*it)->timeStamp.load());
            //JS: Take its memory!!!
            ret = (*it)->data;
            (*it)->data.store(NULL);
            (*it)->blkLock.writerUnlock();
            numBlocks.fetch_sub(1);
            //JS: And remove its existence...
            currentBlocks.erase(it);
            break;
        }
    }

    metaLock.writerUnlock();
    return ret;
}