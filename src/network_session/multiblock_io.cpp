/**
 * @file       multiblock_io.h
 *
 * @author     Tobias Anker <tobias.anker@kitsunemimi.moe>
 *
 * @copyright  Apache License Version 2.0
 *
 *      Copyright 2019 Tobias Anker
 *
 *      Licensed under the Apache License, Version 2.0 (the "License");
 *      you may not use this file except in compliance with the License.
 *      You may obtain a copy of the License at
 *
 *          http://www.apache.org/licenses/LICENSE-2.0
 *
 *      Unless required by applicable law or agreed to in writing, software
 *      distributed under the License is distributed on an "AS IS" BASIS,
 *      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *      See the License for the specific language governing permissions and
 *      limitations under the License.
 */

#include "multiblock_io.h"

#include <libKitsunemimiProjectNetwork/network_session/session.h>
#include <libKitsunemimiPersistence/logger/logger.h>
#include <network_session/messages_processing/multiblock_data_processing.h>

namespace Kitsunemimi
{
namespace Project
{

MultiblockIO::MultiblockIO(Session* session)
    : Kitsunemimi::Common::Thread()
{
    m_session = session;
}

/**
 * @brief initialize multiblock-message by data-buffer for a new multiblock and bring statemachine
 *        into required state
 *
 * @param size total size of the payload of the message (no header)
 *
 * @return false, if session is already in send/receive of a multiblock-message
 */
uint64_t
MultiblockIO::createBacklogBuffer(const void* data,
                                  const uint64_t size)
{
    const uint32_t numberOfBlocks = static_cast<uint32_t>(size / 4096) + 1;

    // set or create id
    const uint64_t newMultiblockId = getRandValue();

    // init new multiblock-message
    MultiblockMessage newMultiblockMessage;
    newMultiblockMessage.multiBlockBuffer = new Kitsunemimi::Common::DataBuffer(numberOfBlocks);
    newMultiblockMessage.messageSize = size;
    newMultiblockMessage.multiblockId = newMultiblockId;

    Kitsunemimi::Common::addDataToBuffer(newMultiblockMessage.multiBlockBuffer,
                                         data,
                                         size);

    // TODO: check if its really possible and if the memory can not be allocated, return 0

    while(m_backlog_lock.test_and_set(std::memory_order_acquire))  // acquire lock
                 ; // spin
    m_backlog.push_back(newMultiblockMessage);
    m_backlog_lock.clear(std::memory_order_release);

    send_Data_Multi_Init(m_session, newMultiblockId, size);

    return newMultiblockId;
}

/**
 * @brief MultiblockIO::createIncomingBuffer
 * @param multiblockId
 * @param size
 * @return
 */
bool
MultiblockIO::createIncomingBuffer(const uint64_t multiblockId,
                                   const uint64_t size)
{
    const uint32_t numberOfBlocks = static_cast<uint32_t>(size / 4096) + 1;

    // init new multiblock-message
    MultiblockMessage newMultiblockMessage;
    newMultiblockMessage.multiBlockBuffer = new Kitsunemimi::Common::DataBuffer(numberOfBlocks);
    newMultiblockMessage.messageSize = size;
    newMultiblockMessage.multiblockId = multiblockId;

    // TODO: check if its really possible and if the memory can not be allocated, return false

    while(m_incoming_lock.test_and_set(std::memory_order_acquire))  // acquire lock
                 ; // spin
    m_incoming.insert(std::pair<uint64_t, MultiblockMessage>(multiblockId, newMultiblockMessage));
    m_incoming_lock.clear(std::memory_order_release);

    return true;
}

/**
 * @brief append data to the data-buffer for the multiblock-message
 *
 * @param multiblockId
 * @param data pointer to the data
 * @param size number of bytes
 *
 * @return false, if session is not in the multiblock-transfer-state
 */
bool
MultiblockIO::writeDataIntoBuffer(const uint64_t multiblockId,
                                  const void* data,
                                  const uint64_t size)
{
    bool result = false;
    while(m_incoming_lock.test_and_set(std::memory_order_acquire))  // acquire lock
                 ; // spin

    std::map<uint64_t, MultiblockMessage>::iterator it;
    it = m_incoming.find(multiblockId);

    if(it != m_incoming.end())
    {
        result = Kitsunemimi::Common::addDataToBuffer(it->second.multiBlockBuffer,
                                                      data,
                                                      size);
    }

    m_incoming_lock.clear(std::memory_order_release);

    return result;
}

/**
 * @brief MultiblockIO::makeMultiblockReady
 * @param multiblockId
 * @return
 */
bool
MultiblockIO::makeMultiblockReady(const uint64_t multiblockId)
{
    bool found = false;

    while(m_backlog_lock.test_and_set(std::memory_order_acquire))  // acquire lock
                 ; // spin

    std::deque<MultiblockMessage>::iterator it;
    for(it = m_backlog.begin();
        it != m_backlog.end();
        it++)
    {
        if(it->multiblockId == multiblockId)
        {
            it->isReady = true;
            found = true;
        }
    }

    m_backlog_lock.clear(std::memory_order_release);

    if(found) {
        continueThread();
    }

    return found;
}

/**
 * @brief MultiblockIO::getIncomingBuffer
 * @param multiblockId
 * @param eraseFromMap
 * @return
 */
MultiblockIO::MultiblockMessage
MultiblockIO::getIncomingBuffer(const uint64_t multiblockId,
                                const bool eraseFromMap)
{
    MultiblockMessage tempBuffer;

    while(m_incoming_lock.test_and_set(std::memory_order_acquire))  // acquire lock
                 ; // spin

    std::map<uint64_t, MultiblockMessage>::iterator it;
    it = m_incoming.find(multiblockId);

    if(it != m_incoming.end())
    {
        tempBuffer = it->second;
        if(eraseFromMap) {
            m_incoming.erase(it);
        }
    }

    m_incoming_lock.clear(std::memory_order_release);

    return tempBuffer;
}

/**
 * @brief MultiblockIO::sendOutgoingData
 * @param messageBuffer
 * @return
 */
bool
MultiblockIO::sendOutgoingData(const MultiblockMessage& messageBuffer)
{
    // counter values
    uint64_t totalSize = messageBuffer.messageSize;
    uint64_t currentMessageSize = 0;
    uint32_t partCounter = 0;

    // static values
    const uint32_t totalPartNumber = static_cast<uint32_t>(totalSize / 1000) + 1;
    const uint8_t* dataPointer = messageBuffer.multiBlockBuffer->getBlock(0);

    while(totalSize != 0)
    {
        // get message-size base on the rest
        currentMessageSize = 1000;
        if(totalSize < 1000) {
            currentMessageSize = totalSize;
        }
        totalSize -= currentMessageSize;

        // send single packet
        send_Data_Multi_Static(m_session,
                               messageBuffer.multiblockId,
                               totalPartNumber,
                               partCounter,
                               dataPointer + (1000 * partCounter),
                               currentMessageSize);

        partCounter++;
    }

    // finish multi-block
    send_Data_Multi_Finish(m_session, messageBuffer.multiblockId);

    return true;
}

/**
 * @brief MultiblockIO::abortMultiblockDataTransfer
 * @param multiblockId
 * @return
 */
bool
MultiblockIO::abortMultiblockDataTransfer(const uint64_t multiblockId)
{

}

/**
 * @brief last step of a mutliblock data-transfer by cleaning the buffer. Can also initialize the
 *        abort-process for a multiblock-datatransfer
 *
 * @param multiblockId
 * @param initAbort true to initialize an abort-process

 * @return true, if statechange was successful, else false
 */
bool
MultiblockIO::finishMultiblockDataTransfer()
{
    return false;
}

/**
 * @brief Session::getRandValue
 *
 * @return
 */
uint64_t
MultiblockIO::getRandValue()
{
    uint64_t newId = 0;

    // 0 is the undefined value and should never be allowed
    while(newId == 0)
    {
        newId = (static_cast<uint64_t>(rand()) << 32) | static_cast<uint64_t>(rand());
    }

    return newId;
}

void
MultiblockIO::run()
{
    while(m_abort == false)
    {
        MultiblockMessage tempBuffer;

        while(m_backlog_lock.test_and_set(std::memory_order_acquire))  // acquire lock
                     ; // spin

        if(m_backlog.empty() == false)
        {
            tempBuffer = m_backlog.front();
            m_backlog.pop_front();
            m_backlog_lock.clear(std::memory_order_release);
        }
        else
        {
            m_backlog_lock.clear(std::memory_order_release);
            blockThread();
        }

        if(tempBuffer.multiBlockBuffer != nullptr) {
            sendOutgoingData(tempBuffer);
        }
    }
}

} // namespace Project
} // namespace Kitsunemimi
