// Copyright (C) 2008, 2009, 2010 GlavSoft LLC.
// All rights reserved.
//
//-------------------------------------------------------------------------
// This file is part of the TightVNC software.  Please visit our Web site:
//
//                       http://www.tightvnc.com/
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//-------------------------------------------------------------------------
//

#include "ReconnectingChannel.h"
#include "util/Log.h"
#include "util/DateTime.h"
#include "desktop-ipc/ReconnectException.h"

ReconnectingChannel::ReconnectingChannel(unsigned int timeOut)
: m_timeOut(timeOut),
  m_isClosed(false),
  m_channel(0),
  m_oldChannel(0),
  m_chanWasChanged(false)
{
}

ReconnectingChannel::~ReconnectingChannel()
{
  if (m_channel != 0) {
    delete m_channel;
  }
  if (m_oldChannel != 0) {
    delete m_oldChannel;
  }
}

void ReconnectingChannel::close()
{
  AutoLock al(&m_chanMut);
  m_isClosed = true;
  if (m_channel != 0) {
    m_channel->close();
  }
  if (m_oldChannel != 0) {
    m_oldChannel->close();
  }
  m_timer.notify();
}

void ReconnectingChannel::replaceChannel(Channel *newChannel)
{
  AutoLock al(&m_chanMut);
  if (m_channel != 0) {
    m_chanWasChanged = true; 
  }
  if (m_oldChannel != 0) {
    delete m_channel; 
  }
  m_oldChannel = m_channel;
  m_channel = newChannel; 
  m_timer.notify();
}

Channel *ReconnectingChannel::getChannel(const TCHAR *funName)
{
  if (m_isClosed) {
    StringStorage errMess;
    errMess.format(_T("The %s() function has failed:")
                   _T(" connection has already been closed."),
                   funName);
    throw IOException(errMess.getString());
  }
  Channel *channel;
  {
    AutoLock al(&m_chanMut);
    if (m_oldChannel != 0) {
      delete m_oldChannel;
      m_oldChannel = 0;
    }
    if (m_chanWasChanged) {
      m_chanWasChanged = false;
      StringStorage errMess;
      errMess.format(_T("Transport was reconnected outside from")
                     _T(" the %s() function. The %s()")
                     _T(" function at this time will be aborted."),
                     funName,
                     funName);
      throw ReconnectException(errMess.getString());
    }
    channel = m_channel;
  }
  return channel;
}

size_t ReconnectingChannel::write(const void *buffer, size_t len)
{
  Channel *channel = getChannel(_T("write"));

  try {
    if (channel == 0) {
      throw Exception(_T("write() function stopped because transport")
                      _T(" has not been initialized yet."));
    }
    return channel->write(buffer, len);
  } catch (Exception &e) {
    Log::error(e.getMessage());
    waitForReconnect(_T("write"), channel);
  }

  return 0; 
}

size_t ReconnectingChannel::read(void *buffer, size_t len)
{
  Channel *channel = getChannel(_T("read"));

  try {
    if (channel == 0) {
      throw Exception(_T("read() function stopped because transport")
                      _T(" has not been initialized yet."));
    }
    return channel->read(buffer, len);
  } catch (Exception &e) {
    Log::error(e.getMessage());
    waitForReconnect(_T("read"), channel);
  }

  return 0; 
}

void ReconnectingChannel::waitForReconnect(const TCHAR *funName,
                                         Channel *channel)
{
  DateTime startTime = DateTime::now();
  bool success = false;
  while (!success) {
    unsigned int timeForWait = max((int)m_timeOut - 
                                   (int)(DateTime::now() -
                                         startTime).getTime(),
                                   0);
    if (timeForWait == 0 || m_isClosed) { 
      StringStorage errMess;
      errMess.format(_T("The ReconnectingChannel::%s() function")
                     _T(" failed."), funName);
      throw IOException(errMess.getString());
    }
    m_timer.waitForEvent(timeForWait);
    AutoLock al(&m_chanMut);
    if (m_channel != channel) {
      m_chanWasChanged = false; 
      success = true;
    }
  }
  Log::info(_T("ReconnectingChannel was successfully reconnected."));
  if (channel != 0) { 
    StringStorage errMess;
    errMess.format(_T("Transport was reconnected in the")
                   _T(" %s() function. The %s()")
                   _T(" function() at this time will be aborted"),
                   funName, funName);
    throw ReconnectException(errMess.getString());
  }
}
