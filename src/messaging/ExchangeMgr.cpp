/*
 *
 *    Copyright (c) 2020-2021 Project CHIP Authors
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

/**
 *    @file
 *      This file implements the ExchangeManager class.
 *
 */

#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif

#ifndef __STDC_LIMIT_MACROS
#define __STDC_LIMIT_MACROS
#endif

#include <cstring>
#include <inttypes.h>
#include <stddef.h>

#include <crypto/RandUtils.h>
#include <lib/core/CHIPCore.h>
#include <lib/core/CHIPEncoding.h>
#include <lib/support/CHIPFaultInjection.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <messaging/ExchangeContext.h>
#include <messaging/ExchangeMgr.h>
#include <protocols/Protocols.h>

using namespace chip::Encoding;
using namespace chip::Inet;
using namespace chip::System;

namespace chip {
namespace Messaging {

/**
 *  Constructor for the ExchangeManager class.
 *  It sets the state to kState_NotInitialized.
 *
 *  @note
 *    The class must be initialized via ExchangeManager::Init()
 *    prior to use.
 *
 */
ExchangeManager::ExchangeManager() : mReliableMessageMgr(mContextPool)
{
    mState = State::kState_NotInitialized;
}

CHIP_ERROR ExchangeManager::Init(SessionManager * sessionManager)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    VerifyOrReturnError(mState == State::kState_NotInitialized, err = CHIP_ERROR_INCORRECT_STATE);

    mSessionManager = sessionManager;

    mNextExchangeId = chip::Crypto::GetRandU16();
    mNextKeyId      = 0;

    for (auto & handler : UMHandlerPool)
    {
        // Mark all handlers as unallocated.  This handles both initial
        // initialization and the case when the consumer shuts us down and
        // then re-initializes without removing registered handlers.
        handler.Reset();
    }

    sessionManager->SetMessageDelegate(this);

    mReliableMessageMgr.Init(sessionManager->SystemLayer());

    mState = State::kState_Initialized;

    return err;
}

CHIP_ERROR ExchangeManager::Shutdown()
{
    mReliableMessageMgr.Shutdown();

    mContextPool.ForEachActiveObject([](auto * ec) {
        // There should be no active object in the pool
        VerifyOrDie(false);
        return Loop::Continue;
    });

    if (mSessionManager != nullptr)
    {
        mSessionManager->SetMessageDelegate(nullptr);
        mSessionManager = nullptr;
    }

    mState = State::kState_NotInitialized;

    return CHIP_NO_ERROR;
}

ExchangeContext * ExchangeManager::NewContext(const SessionHandle & session, ExchangeDelegate * delegate)
{
    return mContextPool.CreateObject(this, mNextExchangeId++, session, true, delegate);
}

CHIP_ERROR ExchangeManager::RegisterUnsolicitedMessageHandlerForProtocol(Protocols::Id protocolId, ExchangeDelegate * delegate)
{
    return RegisterUMH(protocolId, kAnyMessageType, delegate);
}

CHIP_ERROR ExchangeManager::RegisterUnsolicitedMessageHandlerForType(Protocols::Id protocolId, uint8_t msgType,
                                                                     ExchangeDelegate * delegate)
{
    return RegisterUMH(protocolId, static_cast<int16_t>(msgType), delegate);
}

CHIP_ERROR ExchangeManager::UnregisterUnsolicitedMessageHandlerForProtocol(Protocols::Id protocolId)
{
    return UnregisterUMH(protocolId, kAnyMessageType);
}

CHIP_ERROR ExchangeManager::UnregisterUnsolicitedMessageHandlerForType(Protocols::Id protocolId, uint8_t msgType)
{
    return UnregisterUMH(protocolId, static_cast<int16_t>(msgType));
}

CHIP_ERROR ExchangeManager::RegisterUMH(Protocols::Id protocolId, int16_t msgType, ExchangeDelegate * delegate)
{
    UnsolicitedMessageHandler * selected = nullptr;

    for (auto & umh : UMHandlerPool)
    {
        if (!umh.IsInUse())
        {
            if (selected == nullptr)
                selected = &umh;
        }
        else if (umh.Matches(protocolId, msgType))
        {
            umh.Delegate = delegate;
            return CHIP_NO_ERROR;
        }
    }

    if (selected == nullptr)
        return CHIP_ERROR_TOO_MANY_UNSOLICITED_MESSAGE_HANDLERS;

    selected->Delegate    = delegate;
    selected->ProtocolId  = protocolId;
    selected->MessageType = msgType;

    SYSTEM_STATS_INCREMENT(chip::System::Stats::kExchangeMgr_NumUMHandlers);

    return CHIP_NO_ERROR;
}

CHIP_ERROR ExchangeManager::UnregisterUMH(Protocols::Id protocolId, int16_t msgType)
{
    for (auto & umh : UMHandlerPool)
    {
        if (umh.IsInUse() && umh.Matches(protocolId, msgType))
        {
            umh.Reset();
            SYSTEM_STATS_DECREMENT(chip::System::Stats::kExchangeMgr_NumUMHandlers);
            return CHIP_NO_ERROR;
        }
    }

    return CHIP_ERROR_NO_UNSOLICITED_MESSAGE_HANDLER;
}

void ExchangeManager::OnMessageReceived(const PacketHeader & packetHeader, const PayloadHeader & payloadHeader,
                                        const SessionHandle & session, const Transport::PeerAddress & source,
                                        DuplicateMessage isDuplicate, System::PacketBufferHandle && msgBuf)
{
    UnsolicitedMessageHandler * matchingUMH = nullptr;

    ChipLogProgress(ExchangeManager,
                    "Received message of type " ChipLogFormatMessageType " with protocolId " ChipLogFormatProtocolId
                    " and MessageCounter:" ChipLogFormatMessageCounter " on exchange " ChipLogFormatExchangeId,
                    payloadHeader.GetMessageType(), ChipLogValueProtocolId(payloadHeader.GetProtocolID()),
                    packetHeader.GetMessageCounter(), ChipLogValueExchangeIdFromReceivedHeader(payloadHeader));

    MessageFlags msgFlags;
    if (isDuplicate == DuplicateMessage::Yes)
    {
        msgFlags.Set(MessageFlagValues::kDuplicateMessage);
    }

    // Skip retrieval of exchange for group message since no exchange is stored
    // for group msg (optimization)
    if (!packetHeader.IsGroupSession())
    {
        // Search for an existing exchange that the message applies to. If a match is found...
        bool found = false;
        mContextPool.ForEachActiveObject([&](auto * ec) {
            if (ec->MatchExchange(session, packetHeader, payloadHeader))
            {
                // Found a matching exchange. Set flag for correct subsequent MRP
                // retransmission timeout selection.
                if (!ec->HasRcvdMsgFromPeer())
                {
                    ec->SetMsgRcvdFromPeer(true);
                }

                ChipLogDetail(ExchangeManager, "Found matching exchange: " ChipLogFormatExchange ", Delegate: %p",
                              ChipLogValueExchange(ec), ec->GetDelegate());

                // Matched ExchangeContext; send to message handler.
                ec->HandleMessage(packetHeader.GetMessageCounter(), payloadHeader, source, msgFlags, std::move(msgBuf));
                found = true;
                return Loop::Break;
            }
            return Loop::Continue;
        });

        if (found)
        {
            return;
        }
    }
    else
    {
        ChipLogProgress(ExchangeManager, "Received Groupcast Message with GroupId of %d",
                        packetHeader.GetDestinationGroupId().Value());
    }

    // If it's not a duplicate message, search for an unsolicited message handler if it is marked as being sent by an initiator.
    // Since we didn't find an existing exchange that matches the message, it must be an unsolicited message. However all
    // unsolicited messages must be marked as being from an initiator.
    if (!msgFlags.Has(MessageFlagValues::kDuplicateMessage) && payloadHeader.IsInitiator())
    {
        // Search for an unsolicited message handler that can handle the message. Prefer handlers that can explicitly
        // handle the message type over handlers that handle all messages for a profile.
        matchingUMH = nullptr;

        for (auto & umh : UMHandlerPool)
        {
            if (umh.IsInUse() && payloadHeader.HasProtocol(umh.ProtocolId))
            {
                if (umh.MessageType == payloadHeader.GetMessageType())
                {
                    matchingUMH = &umh;
                    break;
                }

                if (umh.MessageType == kAnyMessageType)
                    matchingUMH = &umh;
            }
        }
    }
    // Discard the message if it isn't marked as being sent by an initiator and the message does not need to send
    // an ack to the peer.
    else if (!payloadHeader.NeedsAck())
    {
        // Using same error message for all errors to reduce code size.
        ChipLogError(ExchangeManager, "OnMessageReceived failed, err = %s", ErrorStr(CHIP_ERROR_UNSOLICITED_MSG_NO_ORIGINATOR));
        return;
    }

    // If we found a handler or we need to send an ack, create an exchange to
    // handle the message.
    if (matchingUMH != nullptr || payloadHeader.NeedsAck())
    {
        ExchangeDelegate * delegate = matchingUMH ? matchingUMH->Delegate : nullptr;
        // If rcvd msg is from initiator then this exchange is created as not Initiator.
        // If rcvd msg is not from initiator then this exchange is created as Initiator.
        // Note that if matchingUMH is not null then rcvd msg if from initiator.
        // TODO: Figure out which channel to use for the received message
        ExchangeContext * ec =
            mContextPool.CreateObject(this, payloadHeader.GetExchangeID(), session, !payloadHeader.IsInitiator(), delegate);

        if (ec == nullptr)
        {
            // Using same error message for all errors to reduce code size.
            ChipLogError(ExchangeManager, "OnMessageReceived failed, err = %s", ErrorStr(CHIP_ERROR_NO_MEMORY));
            return;
        }

        ChipLogDetail(ExchangeManager, "Handling via exchange: " ChipLogFormatExchange ", Delegate: %p", ChipLogValueExchange(ec),
                      ec->GetDelegate());

        if (ec->IsEncryptionRequired() != packetHeader.IsEncrypted())
        {
            ChipLogError(ExchangeManager, "OnMessageReceived failed, err = %s", ErrorStr(CHIP_ERROR_INVALID_MESSAGE_TYPE));
            ec->Close();
            return;
        }

        CHIP_ERROR err = ec->HandleMessage(packetHeader.GetMessageCounter(), payloadHeader, source, msgFlags, std::move(msgBuf));
        if (err != CHIP_NO_ERROR)
        {
            // Using same error message for all errors to reduce code size.
            ChipLogError(ExchangeManager, "OnMessageReceived failed, err = %s", ErrorStr(err));
        }
    }
}

void ExchangeManager::CloseAllContextsForDelegate(const ExchangeDelegate * delegate)
{
    mContextPool.ForEachActiveObject([&](auto * ec) {
        if (ec->GetDelegate() == delegate)
        {
            // Make sure to null out the delegate before closing the context, so
            // we don't notify the delegate that the context is closing.  We
            // have to do this, because the delegate might be partially
            // destroyed by this point.
            ec->SetDelegate(nullptr);
            ec->Close();
        }
        return Loop::Continue;
    });
}

} // namespace Messaging
} // namespace chip
