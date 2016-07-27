//
//  SteamClient.cpp
//  steamworks-wrapper/src/steamworks-wrapper
//
//  Created by Clement Brisset on 6/8/16.
//  Copyright 2016 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "SteamClient.h"

#include <atomic>

#include <QtCore/QDebug>

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverloaded-virtual"
#endif

#include <steam/steam_api.h>

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif


static const Ticket INVALID_TICKET = Ticket();

class SteamTicketRequests {
public:
    SteamTicketRequests();
    ~SteamTicketRequests();

    HAuthTicket startRequest(TicketRequestCallback callback);
    void stopRequest(HAuthTicket authTicket);
    void stopAll();

    STEAM_CALLBACK(SteamTicketRequests, onGetAuthSessionTicketResponse,
                   GetAuthSessionTicketResponse_t, _getAuthSessionTicketResponse);

    STEAM_CALLBACK(SteamTicketRequests, onGameRichPresenceJoinRequested,
                   GameRichPresenceJoinRequested_t, _gameRichPresenceJoinRequestedResponse);

private:
    struct PendingTicket {
        HAuthTicket authTicket;
        Ticket ticket;
        TicketRequestCallback callback;
    };

    std::vector<PendingTicket> _pendingTickets;
};

SteamTicketRequests::SteamTicketRequests() :
    _getAuthSessionTicketResponse(this, &SteamTicketRequests::onGetAuthSessionTicketResponse),
    _gameRichPresenceJoinRequestedResponse(this, &SteamTicketRequests::onGameRichPresenceJoinRequested)
{
}

SteamTicketRequests::~SteamTicketRequests() {
    stopAll();
}

HAuthTicket SteamTicketRequests::startRequest(TicketRequestCallback callback) {
    static const uint32 MAX_TICKET_SIZE { 1024 };
    uint32 ticketSize { 0 };
    char ticket[MAX_TICKET_SIZE];

    auto authTicket = SteamUser()->GetAuthSessionTicket(ticket, MAX_TICKET_SIZE, &ticketSize);
    qDebug() << "Got Steam auth session ticket:" << authTicket;

    if (authTicket == k_HAuthTicketInvalid) {
        qWarning() << "Auth session ticket is invalid.";
        callback(INVALID_TICKET);
    } else {
        PendingTicket pendingTicket{ authTicket, QByteArray(ticket, ticketSize).toHex(), callback };
        _pendingTickets.push_back(pendingTicket);
    }

    return authTicket;
}

void SteamTicketRequests::stopRequest(HAuthTicket authTicket) {
    auto it = std::find_if(_pendingTickets.begin(), _pendingTickets.end(), [&authTicket](const PendingTicket& pendingTicket) {
        return pendingTicket.authTicket == authTicket;
    });

    if (it != _pendingTickets.end()) {
        SteamUser()->CancelAuthTicket(it->authTicket);
        it->callback(INVALID_TICKET);
        _pendingTickets.erase(it);
    }
}

void SteamTicketRequests::stopAll() {
    auto steamUser = SteamUser();
    if (steamUser) {
        for (const auto& pendingTicket : _pendingTickets) {
            steamUser->CancelAuthTicket(pendingTicket.authTicket);
            pendingTicket.callback(INVALID_TICKET);
        }
    }
    _pendingTickets.clear();
}

void SteamTicketRequests::onGetAuthSessionTicketResponse(GetAuthSessionTicketResponse_t* pCallback) {
    auto authTicket = pCallback->m_hAuthTicket;

    auto it = std::find_if(_pendingTickets.begin(), _pendingTickets.end(), [&authTicket](const PendingTicket& pendingTicket) {
        return pendingTicket.authTicket == authTicket;
    });


    if (it != _pendingTickets.end()) {

        if (pCallback->m_eResult == k_EResultOK) {
            qDebug() << "Got steam callback, auth session ticket is valid. Send it." << authTicket;
            it->callback(it->ticket);
        } else {
            qWarning() << "Steam auth session ticket callback encountered an error:" << pCallback->m_eResult;
            it->callback(INVALID_TICKET);
        }

        _pendingTickets.erase(it);
    } else {
        qWarning() << "Could not find steam auth session ticket in list of pending tickets:" << authTicket;
    }
}

#include <QString>
#include <QCoreApplication>
#include <QtGui/QEvent.h>
#include <QMimeData>
#include <QUrl>
const QString PREFIX = "--url \"";
const QString SUFFIX = "\"";


void SteamTicketRequests::onGameRichPresenceJoinRequested(GameRichPresenceJoinRequested_t* pCallback) {
    auto url = QString::fromLocal8Bit(pCallback->m_rgchConnect);

    if (url.startsWith(PREFIX) && url.endsWith(SUFFIX)) {
        url.remove(0, PREFIX.size());
        url.remove(-SUFFIX.size(), SUFFIX.size());
    }

    qDebug() << "Joining:" << url;
    auto mimeData = new QMimeData();
    mimeData->setUrls(QList<QUrl>() << QUrl(url));
    auto event = new QDropEvent(QPointF(0,0), Qt::MoveAction, mimeData, Qt::LeftButton, Qt::NoModifier);

    QCoreApplication::postEvent(qApp, event);
}



static std::atomic_bool initialized { false };
static SteamTicketRequests steamTicketRequests;


bool SteamClient::isRunning() {
    if (!initialized) {
        init();
    }
    return initialized;
}

bool SteamClient::init() {
    if (SteamAPI_IsSteamRunning() && !initialized) {
        initialized = SteamAPI_Init();

        if (initialized) {
            SteamFriends()->SetRichPresence("status", "Localhost");
            SteamFriends()->SetRichPresence("connect", "--url \"hifi://10.0.0.185:40117/10,10,10\"");
        }
    }
    return initialized;
}

void SteamClient::shutdown() {
    if (initialized) {
        SteamAPI_Shutdown();
    }

    steamTicketRequests.stopAll();
}

void SteamClient::runCallbacks() {
    if (!initialized) {
        return;
    }

    auto steamPipe = SteamAPI_GetHSteamPipe();
    if (!steamPipe) {
        qDebug() << "Could not get SteamPipe";
        return;
    }

    Steam_RunCallbacks(steamPipe, false);
}

void SteamClient::requestTicket(TicketRequestCallback callback) {
    if (!initialized) {
        if (SteamAPI_IsSteamRunning()) {
            init();
        } else {
            qWarning() << "Steam is not running";
            callback(INVALID_TICKET);
            return;
        }
    }

    if (!initialized) {
        qDebug() << "Steam not initialized";
        return;
    }

    steamTicketRequests.startRequest(callback);
}


