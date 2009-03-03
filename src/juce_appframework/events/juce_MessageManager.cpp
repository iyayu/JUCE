/*
  ==============================================================================

   This file is part of the JUCE library - "Jules' Utility Class Extensions"
   Copyright 2004-7 by Raw Material Software ltd.

  ------------------------------------------------------------------------------

   JUCE can be redistributed and/or modified under the terms of the
   GNU General Public License, as published by the Free Software Foundation;
   either version 2 of the License, or (at your option) any later version.

   JUCE is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with JUCE; if not, visit www.gnu.org/licenses or write to the
   Free Software Foundation, Inc., 59 Temple Place, Suite 330,
   Boston, MA 02111-1307 USA

  ------------------------------------------------------------------------------

   If you'd like to release a closed-source product which uses JUCE, commercial
   licenses are also available: visit www.rawmaterialsoftware.com/juce for
   more information.

  ==============================================================================
*/

#include "../../juce_core/basics/juce_StandardHeader.h"

BEGIN_JUCE_NAMESPACE

#include "juce_MessageManager.h"
#include "juce_ActionListenerList.h"
#include "../application/juce_Application.h"
#include "../gui/components/juce_Component.h"
#include "../../juce_core/threads/juce_Thread.h"
#include "../../juce_core/basics/juce_Time.h"


//==============================================================================
// platform-specific functions..
bool juce_dispatchNextMessageOnSystemQueue (bool returnIfNoPendingMessages);
bool juce_postMessageToSystemQueue (void* message);


//==============================================================================
MessageManager* MessageManager::instance = 0;

static const int quitMessageId = 0xfffff321;

MessageManager::MessageManager() throw()
  : broadcastListeners (0),
    quitMessagePosted (false),
    quitMessageReceived (false)
{
    currentLockingThreadId = 0;
    messageThreadId = Thread::getCurrentThreadId();
}

MessageManager::~MessageManager() throw()
{
    deleteAndZero (broadcastListeners);

    doPlatformSpecificShutdown();

    jassert (instance == this);
    instance = 0;  // do this last in case this instance is still needed by doPlatformSpecificShutdown()
}

MessageManager* MessageManager::getInstance() throw()
{
    if (instance == 0)
    {
        instance = new MessageManager();
        doPlatformSpecificInitialisation();
    }

    return instance;
}

void MessageManager::postMessageToQueue (Message* const message)
{
    if (quitMessagePosted || ! juce_postMessageToSystemQueue (message))
        delete message;
}

//==============================================================================
// not for public use..
void MessageManager::deliverMessage (void* message)
{
    const MessageManagerLock lock;

    Message* const m = (Message*) message;
    MessageListener* const recipient = m->messageRecipient;

    if (messageListeners.contains (recipient))
    {
        JUCE_TRY
        {
            recipient->handleMessage (*m);
        }
        JUCE_CATCH_EXCEPTION
    }
    else if (recipient == 0 && m->intParameter1 == quitMessageId)
    {
        quitMessageReceived = true;
    }

    delete m;
}

//==============================================================================
#if ! JUCE_MAC
void MessageManager::runDispatchLoop()
{
    jassert (isThisTheMessageThread()); // must only be called by the message thread

    runDispatchLoopUntil (-1);
}

void MessageManager::stopDispatchLoop()
{
    Message* const m = new Message (quitMessageId, 0, 0, 0);
    m->messageRecipient = 0;
    postMessageToQueue (m);

    quitMessagePosted = true;
}

bool MessageManager::runDispatchLoopUntil (int millisecondsToRunFor)
{
    jassert (isThisTheMessageThread()); // must only be called by the message thread

    const int64 endTime = Time::currentTimeMillis() + millisecondsToRunFor;

    while ((millisecondsToRunFor < 0 || endTime > Time::currentTimeMillis())
            && ! quitMessageReceived)
    {
        JUCE_TRY
        {
            juce_dispatchNextMessageOnSystemQueue (millisecondsToRunFor >= 0);
        }
        JUCE_CATCH_EXCEPTION
    }

    return ! quitMessageReceived;
}

#endif

//==============================================================================
void MessageManager::deliverBroadcastMessage (const String& value)
{
    if (broadcastListeners != 0)
        broadcastListeners->sendActionMessage (value);
}

void MessageManager::registerBroadcastListener (ActionListener* const listener) throw()
{
    if (broadcastListeners == 0)
        broadcastListeners = new ActionListenerList();

    broadcastListeners->addActionListener (listener);
}

void MessageManager::deregisterBroadcastListener (ActionListener* const listener) throw()
{
    if (broadcastListeners != 0)
        broadcastListeners->removeActionListener (listener);
}

//==============================================================================
bool MessageManager::isThisTheMessageThread() const throw()
{
    return Thread::getCurrentThreadId() == messageThreadId;
}

void MessageManager::setCurrentMessageThread (const Thread::ThreadID threadId) throw()
{
    messageThreadId = threadId;
}

bool MessageManager::currentThreadHasLockedMessageManager() const throw()
{
    return Thread::getCurrentThreadId() == currentLockingThreadId;
}

//==============================================================================
MessageManagerLock::MessageManagerLock() throw()
    : lastLockingThreadId (0),
      locked (false)
{
    if (MessageManager::instance != 0)
    {
        MessageManager::instance->messageDispatchLock.enter();
        lastLockingThreadId = MessageManager::instance->currentLockingThreadId;
        MessageManager::instance->currentLockingThreadId = Thread::getCurrentThreadId();
        locked = true;
    }
}

MessageManagerLock::MessageManagerLock (Thread* const thread) throw()
    : locked (false)
{
    jassert (thread != 0);  // This will only work if you give it a valid thread!

    if (MessageManager::instance != 0)
    {
        for (;;)
        {
            if (MessageManager::instance->messageDispatchLock.tryEnter())
            {
                locked = true;
                lastLockingThreadId = MessageManager::instance->currentLockingThreadId;
                MessageManager::instance->currentLockingThreadId = Thread::getCurrentThreadId();
                break;
            }

            if (thread != 0 && thread->threadShouldExit())
                break;

            Thread::sleep (1);
        }
    }
}

MessageManagerLock::~MessageManagerLock() throw()
{
    if (locked && MessageManager::instance != 0)
    {
        MessageManager::instance->currentLockingThreadId = lastLockingThreadId;
        MessageManager::instance->messageDispatchLock.exit();
    }
}


END_JUCE_NAMESPACE
