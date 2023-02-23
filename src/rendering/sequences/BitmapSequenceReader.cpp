/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making libpag available.
//
//  Copyright (C) 2021 THL A29 Limited, a Tencent company. All rights reserved.
//
//  Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file
//  except in compliance with the License. You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "BitmapSequenceReader.h"
#include "tgfx/core/ImageCodec.h"
#include "tgfx/core/PixelBuffer.h"

namespace pag {
BitmapSequenceReader::BitmapSequenceReader(std::shared_ptr<File> file, BitmapSequence* sequence)
    : file(std::move(file)), sequence(sequence) {
  // Force allocating a raster PixelBuffer if staticContent is false, otherwise the asynchronous
  // decoding will fail due to the memory sharing mechanism.
  auto staticContent = sequence->composition->staticContent();
  pixelBuffer = tgfx::PixelBuffer::Make(sequence->width, sequence->height, false, staticContent);
  tgfx::Pixmap(pixelBuffer).eraseAll();
}

std::shared_ptr<tgfx::ImageBuffer> BitmapSequenceReader::onMakeBuffer(Frame targetFrame) {
  // a locker is required here because decodeFrame() could be called from multiple threads.
  std::lock_guard<std::mutex> autoLock(locker);
  if (lastDecodeFrame == targetFrame) {
    return pixelBuffer;
  }
  if (pixelBuffer == nullptr) {
    return nullptr;
  }
  auto startFrame = findStartFrame(targetFrame);
  lastDecodeFrame = -1;
  auto& bitmapFrames = static_cast<BitmapSequence*>(sequence)->frames;
  tgfx::Pixmap bitmap(pixelBuffer);
  for (Frame frame = startFrame; frame <= targetFrame; frame++) {
    auto bitmapFrame = bitmapFrames[frame];
    auto firstRead = true;
    for (auto bitmapRect : bitmapFrame->bitmaps) {
      auto imageBytes = tgfx::Data::MakeWithoutCopy(bitmapRect->fileBytes->data(),
                                                    bitmapRect->fileBytes->length());
      auto codec = tgfx::ImageCodec::MakeFrom(imageBytes);
      // The returned image could be nullptr if the frame is an empty frame.
      if (codec != nullptr) {
        if (firstRead && bitmapFrame->isKeyframe &&
            !(codec->width() == bitmap.width() && codec->height() == bitmap.height())) {
          // clear the whole screen if the size of the key frame is smaller than the screen.
          bitmap.eraseAll();
        }
        auto offset = bitmap.rowBytes() * bitmapRect->y + bitmapRect->x * 4;
        auto result = codec->readPixels(
            bitmap.info(), reinterpret_cast<uint8_t*>(bitmap.writablePixels()) + offset);
        if (!result) {
          return nullptr;
        }
        firstRead = false;
      }
    }
  }
  lastDecodeFrame = targetFrame;
  return pixelBuffer;
}

void BitmapSequenceReader::onReportPerformance(Performance* performance, int64_t decodingTime) {
  performance->imageDecodingTime += decodingTime;
}

Frame BitmapSequenceReader::findStartFrame(Frame targetFrame) {
  Frame startFrame = 0;
  auto& bitmapFrames = static_cast<BitmapSequence*>(sequence)->frames;
  for (Frame frame = targetFrame; frame >= 0; frame--) {
    if (frame == lastDecodeFrame + 1 || bitmapFrames[static_cast<size_t>(frame)]->isKeyframe) {
      startFrame = frame;
      break;
    }
  }
  return startFrame;
}
}  // namespace pag
