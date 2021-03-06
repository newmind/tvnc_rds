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

#include "UpdateFilter.h"
#include "util/Log.h"
#include "util/CommonHeader.h"

static const int BLOCK_SIZE = 32;

UpdateFilter::UpdateFilter(ScreenGrabber *screenGrabber,
                           FrameBuffer *frameBuffer,
                           LocalMutex *frameBufferCriticalSection)
: m_screenGrabber(screenGrabber),
  m_frameBuffer(frameBuffer),
  m_fbMutex(frameBufferCriticalSection)
{
}

UpdateFilter::~UpdateFilter(void)
{
}

void UpdateFilter::filter(UpdateContainer *updateContainer)
{
  AutoLock al(m_fbMutex);

  FrameBuffer *screenFrameBuffer = m_screenGrabber->getScreenBuffer();

  if (!screenFrameBuffer->isEqualTo(m_frameBuffer)) {
    return;
  }

  Region toCheck = updateContainer->changedRegion;
  toCheck.add(&updateContainer->copiedRegion);
  toCheck.add(&updateContainer->videoRegion);

  std::vector<Rect> rects;
  std::vector<Rect>::iterator iRect;

  updateContainer->copiedRegion.getRectVector(&rects);
  Point *src = &updateContainer->copySrc;
  for (iRect = rects.begin(); iRect < rects.end(); iRect++) {
    m_frameBuffer->move(&(*iRect), src->x, src->y);
  }

  toCheck.getRectVector(&rects);

  Log::debug(_T("grabbing region, %d rectangles"), (int)rects.size());
  for (iRect = rects.begin(); iRect < rects.end(); iRect++) {
    if (!m_screenGrabber->grab(&(*iRect))) {
      return;
    }
  }
  Log::debug(_T("end of grabbing region"));

  updateContainer->changedRegion.clear();
  Rect *rect;
  for (iRect = rects.begin(); iRect < rects.end(); iRect++) {
    rect = &(*iRect);
    getChangedRegion(&updateContainer->changedRegion, rect);
  }

  updateContainer->changedRegion.getRectVector(&rects);
  for (iRect = rects.begin(); iRect < rects.end(); iRect++) {
    rect = &(*iRect);
    m_frameBuffer->copyFrom(rect, screenFrameBuffer, rect->left, rect->top);
  }
}

void UpdateFilter::getChangedRegion(Region *rgn, const Rect *rect)
{
  
  const UINT bytesPerPixel = m_frameBuffer->getBytesPerPixel();
  const int bytes_per_scanline = (rect->right - rect->left) * bytesPerPixel;

  const int bytesPerRow = m_frameBuffer->getBytesPerRow();
  const int offset = rect->top * bytesPerRow + rect->left * bytesPerPixel;
  unsigned char *o_ptr = (unsigned char *)m_frameBuffer->getBuffer() + offset;
  unsigned char *n_ptr = (unsigned char *)m_screenGrabber->getScreenBuffer()->getBuffer() + offset;

  Rect new_rect = rect;

  if ( rect->right - rect->left <= BLOCK_SIZE &&
       rect->bottom - rect->top <= BLOCK_SIZE ) {
      for (int y = rect->top; y < rect->bottom; y++) {
        if (memcmp(o_ptr, n_ptr, bytes_per_scanline) != 0) {
          new_rect.top = y;
          updateChangedSubRect(rgn, &new_rect);
          break;
        }
        o_ptr += bytesPerRow;
        n_ptr += bytesPerRow;
      }
      return;
  }

  new_rect.top = -1;
  for (int y = rect->top; y < rect->bottom; y++) {
    if (memcmp(o_ptr, n_ptr, bytes_per_scanline) != 0) {
      if (new_rect.top == -1) {
        new_rect.top = y;
      }
      int n = BLOCK_SIZE / 2 - 1;
      y += n;
      o_ptr += n * bytesPerRow;
      n_ptr += n * bytesPerRow;
    } else {
      if (new_rect.top != -1) {
        new_rect.bottom = y;
        updateChangedRect(rgn, &new_rect);
        new_rect.top = -1;
      }
    }
    o_ptr += bytesPerRow;
    n_ptr += bytesPerRow;
  }
  if (new_rect.top != -1) {
    new_rect.bottom = rect->bottom;
    updateChangedRect(rgn, &new_rect);
  }
}

void UpdateFilter::updateChangedRect(Region *rgn, const Rect *rect)
{
  if ( rect->right - rect->left <= BLOCK_SIZE &&
       rect->bottom - rect->top <= BLOCK_SIZE ) {
      updateChangedSubRect(rgn, rect);
      return;
  }

  const UINT bytesPerPixel = m_frameBuffer->getBytesPerPixel();

  Rect new_rect;
  int x, y, ay;

  const int bytesPerRow = m_frameBuffer->getBytesPerRow();
  const int offset = rect->top * bytesPerRow + rect->left * bytesPerPixel;
  unsigned char *o_topleft_ptr = (unsigned char *)m_frameBuffer->getBuffer() + offset;
  unsigned char *n_topleft_ptr = (unsigned char *)m_screenGrabber->getScreenBuffer()->getBuffer() + offset;

  for (y = rect->top; y < rect->bottom; y += BLOCK_SIZE)
  {
    unsigned char *o_row_ptr = o_topleft_ptr;
    unsigned char *n_row_ptr = n_topleft_ptr;

    const int blockbottom = min(y + BLOCK_SIZE, rect->bottom);
    new_rect.bottom = blockbottom;
    new_rect.left = -1;

    for (x = rect->left; x < rect->right; x += BLOCK_SIZE)
    {
      unsigned char *n_block_ptr = n_row_ptr;
      unsigned char *o_block_ptr = o_row_ptr;

      const UINT blockright = min(x + BLOCK_SIZE, rect->right);
      const UINT bytesPerBlockRow = (blockright-x) * bytesPerPixel;

      for (ay = y; ay < blockbottom; ay++) {
        if (memcmp(n_block_ptr, o_block_ptr, bytesPerBlockRow) != 0)
          break;
        n_block_ptr += bytesPerRow;
        o_block_ptr += bytesPerRow;
      }
      if (ay < blockbottom) {
        if (new_rect.left == -1) {
          new_rect.left = x;
          new_rect.top = ay;
        } else if (ay < new_rect.top) {
          new_rect.top = ay;
        }
      } else {
        if (new_rect.left != -1) {
          new_rect.right = x;
          updateChangedSubRect(rgn, &new_rect);
          new_rect.left = -1;
        }
      }

      o_row_ptr += bytesPerBlockRow;
      n_row_ptr += bytesPerBlockRow;
    }

    if (new_rect.left != -1) {
      new_rect.right = rect->right;
      updateChangedSubRect(rgn, &new_rect);
    }

    o_topleft_ptr += bytesPerRow * BLOCK_SIZE;
    n_topleft_ptr += bytesPerRow * BLOCK_SIZE;
  }
}

void UpdateFilter::updateChangedSubRect(Region *rgn, const Rect *rect)
{
  const UINT bytesPerPixel = m_frameBuffer->getBytesPerPixel();
  int bytes_in_row = (rect->right - rect->left) * bytesPerPixel;
  int y, i;

  const int bytesPerRow = m_frameBuffer->getBytesPerRow();
  int offset = (rect->bottom - 1) * bytesPerRow + rect->left * bytesPerPixel;
  unsigned char *o_ptr = (unsigned char *)m_frameBuffer->getBuffer() + offset;
  unsigned char *n_ptr = (unsigned char *)m_screenGrabber->getScreenBuffer()->getBuffer() + offset;
  Rect final_rect = rect;
  final_rect.bottom = rect->top + 1;
  for (y = rect->bottom - 1; y > rect->top; y--) {
    if (memcmp(o_ptr, n_ptr, bytes_in_row) != 0) {
      final_rect.bottom = y + 1;
      break;
    }
    n_ptr -= bytesPerRow;
    o_ptr -= bytesPerRow;
  }

  offset = final_rect.top * bytesPerRow + final_rect.left * bytesPerPixel;
  o_ptr = (unsigned char *)m_frameBuffer->getBuffer() + offset;
  n_ptr = (unsigned char *)m_screenGrabber->getScreenBuffer()->getBuffer() + offset;
  int left_delta = bytes_in_row - 1;
  int right_delta = 0;
  for (y = final_rect.top; y < final_rect.bottom; y++) {
    for (i = 0; i < bytes_in_row - 1; i++) {
      if (n_ptr[i] != o_ptr[i]) {
        if (i < left_delta)
          left_delta = i;
        break;
      }
    }
    for (i = bytes_in_row - 1; i > 0; i--) {
      if (n_ptr[i] != o_ptr[i]) {
        if (i > right_delta)
          right_delta = i;
        break;
      }
    }
    n_ptr += bytesPerRow;
    o_ptr += bytesPerRow;
  }
  final_rect.right = final_rect.left + right_delta / bytesPerPixel + 1;
  final_rect.left += left_delta / bytesPerPixel;

  rgn->addRect(&final_rect);
}
