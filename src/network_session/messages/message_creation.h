/**
 *  @file       message_creation.h
 *
 *  @author     Tobias Anker <tobias.anker@kitsunemimi.moe>
 *
 *  @copyright  Apache License Version 2.0
 */

#ifndef MESSAGE_CREATION_H
#define MESSAGE_CREATION_H

#include <network_session/messages/message_definitions.h>
#include <network_session/session_handler.h>
#include <abstract_socket.h>

using Kitsune::Network::AbstractSocket;

namespace Kitsune
{
namespace Project
{
namespace Common
{

/**
 * @brief sendSessionInitStart
 * @param initialId
 * @param socket
 */
inline void
sendSessionInitStart(const uint32_t initialId,
                     Network::AbstractSocket* socket)
{
    // create message
    Session_InitStart_Message message;
    message.offeredSessionId = initialId;

    // update common-header
    message.commonHeader.sessionId = initialId;
    message.commonHeader.messageId = SessionHandler::m_sessionHandler->increaseMessageIdCounter();

    // send
    socket->sendMessage(&message, sizeof(Session_InitStart_Message));
}

/**
 * @brief sendSessionIdChange
 * @param oldId
 * @param newId
 * @param socket
 */
inline void
sendSessionIdChange(const uint32_t oldId,
                    const uint32_t newId,
                    Network::AbstractSocket* socket)
{
    // create message
    Session_IdChange_Message message;
    message.oldOfferedSessionId = oldId;
    message.newOfferedSessionId = newId;

    // update common-header
    message.commonHeader.sessionId = newId;
    message.commonHeader.messageId = SessionHandler::m_sessionHandler->increaseMessageIdCounter();

    // send
    socket->sendMessage(&message, sizeof(Session_InitStart_Message));
}

/**
 * @brief sendSessionIdConfirm
 * @param id
 * @param socket
 */
inline void
sendSessionIdConfirm(const uint32_t id,
                     Network::AbstractSocket* socket)
{
    Session_IdConfirm_Message message;
    message.confirmedSessionId = id;

    // update common-header
    message.commonHeader.sessionId = id;
    message.commonHeader.messageId = SessionHandler::m_sessionHandler->increaseMessageIdCounter();

    // send
    socket->sendMessage(&message, sizeof(Session_InitStart_Message));
}

/**
 * @brief sendSessionIniReply
 * @param id
 * @param socket
 */
inline void
sendSessionInitReply(const uint32_t id,
                    Network::AbstractSocket* socket)
{
    Session_InitReply_Message message;
    message.sessionId = id;

    // update common-header
    message.commonHeader.sessionId = id;
    message.commonHeader.messageId = SessionHandler::m_sessionHandler->increaseMessageIdCounter();

    // send
    socket->sendMessage(&message, sizeof(Session_InitStart_Message));
}

} // namespace Common
} // namespace Project
} // namespace Kitsune

#endif // MESSAGE_CREATION_H