/* The copyright in this software is being made available under the BSD
 * Licence, included below.  This software may be subject to other third
 * party and contributor rights, including patent rights, and no such
 * rights are granted under this licence.
 *
 * Copyright (c) 2017-2018, ISO/IEC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 *
 * * Neither the name of the ISO/IEC nor the names of its contributors
 *   may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef TMC3_h
#define TMC3_h

#define _CRT_SECURE_NO_WARNINGS

#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>

#include "PCCPointSet.h"
#include "PCCTMC3Decoder.h"
#include "PCCTMC3Encoder.h"

#include "TMC3Config.h"

#include "pcc_chrono.h"

enum ColorTransform { COLOR_TRANSFORM_NONE = 0, COLOR_TRANSFORM_RGB_TO_YCBCR = 1 };

enum CodecMode {
  CODEC_MODE_ENCODE = 0,
  CODEC_MODE_DECODE = 1,
  CODEC_MODE_ENCODE_LOSSLESS_GEOMETRY = 2,
  CODEC_MODE_DECODE_LOSSLESS_GEOMETRY = 3,
  CODEC_MODE_ENCODE_TRISOUP_GEOMETRY = 4,
  CODEC_MODE_DECODE_TRISOUP_GEOMETRY = 5,
};

struct Parameters {
  std::string uncompressedDataPath;
  std::string compressedStreamPath;
  std::string reconstructedDataPath;
  ColorTransform colorTransform;
  CodecMode mode;
  bool roundOutputPositions;
  pcc::PCCTMC3Encoder3Parameters encodeParameters;
  pcc::DecoderParameters decodeParameters;
};

typedef pcc::chrono::Stopwatch<pcc::chrono::utime_inc_children_clock> Stopwatch;

bool ParseParameters(int argc, char *argv[], Parameters &params);
int Compress(const Parameters &params, Stopwatch&);
int Decompress(const Parameters &params, Stopwatch&);

#endif /* TMC3_h */
