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

#include <cstdio>

#include "geometry_trisoup.h"

#include "PCCPointSetProcessing.h"
#include "geometry.h"
#include "geometry_octree.h"

namespace pcc {

//============================================================================

// The number of fractional bits used in trisoup triangle voxelisation
const int kTrisoupFpBits = 8;

// The value 1 in fixed-point representation
const int kTrisoupFpOne = 1 << (kTrisoupFpBits);

//============================================================================

bool
operator<(const TrisoupSegment& s1, const TrisoupSegment& s2)
{
  // assert all quantities are at most 21 bits
  uint64_t s1startpos = (uint64_t(s1.startpos[0]) << 42)
    | (uint64_t(s1.startpos[1]) << 21) | s1.startpos[2];

  uint64_t s2startpos = (uint64_t(s2.startpos[0]) << 42)
    | (uint64_t(s2.startpos[1]) << 21) | s2.startpos[2];

  if (s1startpos < s2startpos)
    return true;

  if (s1startpos != s2startpos)
    return false;

  uint64_t s1endpos = (uint64_t(s1.endpos[0]) << 42)
    | (uint64_t(s1.endpos[1]) << 21) | s1.endpos[2];

  uint64_t s2endpos = (uint64_t(s2.endpos[0]) << 42)
    | (uint64_t(s2.endpos[1]) << 21) | s2.endpos[2];

  if (s1endpos < s2endpos)
    return true;

  if (s1endpos == s2endpos)
    if (s1.index < s2.index)  // stable sort
      return true;

  return false;
}

//============================================================================

void
decodeGeometryTrisoup(
  const GeometryParameterSet& gps,
  const GeometryBrickHeader& gbh,
  PCCPointSet3& pointCloud,
  EntropyDecoder* arithmeticDecoder)
{
  // trisoup uses octree coding until reaching the triangulation level.
  pcc::ringbuf<PCCOctree3Node> nodes;
  decodeGeometryOctree(gps, gbh, pointCloud, arithmeticDecoder, &nodes);

  int blockWidth = 1 << gps.trisoup_node_size_log2;

  uint32_t symbolCount;
  AdaptiveBitModel ctxTemp;
  StaticBitModel ctxBypass;

  // Decode segind from bitstream.
  symbolCount = arithmeticDecoder->decodeExpGolomb(0, ctxBypass, ctxTemp);

  AdaptiveMAryModel multiSymbolSegindModel0(256);
  std::vector<bool> segind;
  for (uint32_t i = 0; i < symbolCount; i++) {
    const uint8_t c = arithmeticDecoder->decode(multiSymbolSegindModel0);
    for (int b = 7; b >= 0; b--) {
      segind.push_back(!!(c & (1 << b)));
    }
  }

  // Decode vertices from bitstream.
  symbolCount = arithmeticDecoder->decodeExpGolomb(0, ctxBypass, ctxTemp);
  AdaptiveMAryModel multiSymbolVerticesModel0(blockWidth);
  std::vector<uint8_t> vertices;
  for (uint32_t i = 0; i < symbolCount; i++) {
    const uint8_t c = arithmeticDecoder->decode(multiSymbolVerticesModel0);
    vertices.push_back(c);
  }

  // Compute refinedVertices.
  int32_t maxval = (1 << gbh.geom_max_node_size_log2) - 1;
  decodeTrisoupCommon(nodes, segind, vertices, pointCloud, blockWidth, maxval);
}

//============================================================================

template<typename T>
Vec3<T>
crossProduct(const Vec3<T> a, const Vec3<T> b)
{
  Vec3<T> ret;
  ret[0] = a[1] * b[2] - a[2] * b[1];
  ret[1] = a[2] * b[0] - a[0] * b[2];
  ret[2] = a[0] * b[1] - a[1] * b[0];
  return ret;
}

//---------------------------------------------------------------------------

Vec3<int32_t>
truncate(const Vec3<int64_t> in, const int32_t offset)
{
  Vec3<int32_t> out;
  out[0] = (int32_t)in[0] + offset;
  out[1] = (int32_t)in[1] + offset;
  out[2] = (int32_t)in[2] + offset;
  if (out[0] < 0)
    out[0] = 0;
  if (out[1] < 0)
    out[1] = 0;
  if (out[2] < 0)
    out[2] = 0;

  return out;
}

//---------------------------------------------------------------------------
// An integer approximation of atan2().
// x, y, and the returned result are fixed-point integers with
// kTrisoupFpBits fractional bits

int32_t
trisoupAtan2(int32_t x, int32_t y)
{
  assert(x != 0 && y != 0);
  if (y == 0) {
    if (x < 0)
      return 804;  // PI * (1<< kTrisoupFpBits)
    else
      return 0;
  } else if (x == 0) {
    if (y > 0)
      return 402;  // (PI/2)   * (1<< kTrisoupFpBits)
    else
      return 1206;  // (PI*3/2) * (1<< kTrisoupFpBits)
  } else {
    int idx = 0;
    int z = abs((y << 8) / x);  //rad is calc in (x>0 && y>0) domain
    if (z <= 256) {             //1<<kTrisoupFpBits
      idx = z / 12;             //0.05<<kTrisoupFpBits
    } else {
      idx = z > 40 ? 40 : z;
    }

    static const int kAtanLut[41] = {
      0,   12,  25,  38,  50,  62,  74,  86,  97,  108, 118, 128, 138, 147,
      156, 164, 172, 180, 187, 194, 201, 283, 319, 339, 351, 359, 365, 370,
      373, 376, 378, 380, 382, 383, 385, 386, 387, 387, 388, 389, 389};
    int atan = kAtanLut[idx];

    //offset
    if (x < 0 && y > 0)
      atan += 402;  // + PI/2
    else if (x < 0 && y < 0)
      atan += 804;  // + PI
    else if (x > 0 && y < 0)
      atan += 1206;  // + PI*3/2
    return atan;
  }
}

//---------------------------------------------------------------------------

bool
boundaryinsidecheck(const Vec3<int32_t> a, const int bbsize)
{
  if (a[0] < 0 || a[0] > bbsize)
    return false;
  if (a[1] < 0 || a[1] > bbsize)
    return false;
  if (a[2] < 0 || a[2] > bbsize)
    return false;
  return true;
}

//---------------------------------------------------------------------------

bool
rayIntersectsTriangle(
  const Vec3<int64_t> rayOrigin,
  const Vec3<int64_t> rayVector,
  const Vec3<int64_t> TriangleVertex0,
  const Vec3<int64_t> TriangleVertex1,
  const Vec3<int64_t> TriangleVertex2,
  Vec3<int64_t>& outIntersectionPoint)
{
  Vec3<int64_t> edge1 = TriangleVertex1 - TriangleVertex0;
  Vec3<int64_t> edge2 = TriangleVertex2 - TriangleVertex0;
  Vec3<int64_t> s = rayOrigin - TriangleVertex0;
  Vec3<int64_t> h = crossProduct(rayVector, edge2);

  int64_t a = (edge1 * h) >> kTrisoupFpBits;
  if (a == 0)
    return false;

  int64_t u = (s * h) / a;
  if (u < 0 || u > kTrisoupFpOne)
    return false;

  Vec3<int64_t> q = crossProduct(s, edge1);
  int64_t v = (rayVector * q) / a;
  if (v < 0 || (u + v) > kTrisoupFpOne)
    return false;

  int64_t t = (edge2 * q) / a;
  if (t > 0) {
    // ray intersection
    Vec3<int64_t> IntersectionPoint = (rayVector * t) >> kTrisoupFpBits;
    outIntersectionPoint = rayOrigin + IntersectionPoint;
    return true;
  } else {
    // There is a line intersection but not a ray intersection
    return false;
  }
}

//---------------------------------------------------------------------------
// Trisoup geometry decoding, at both encoder and decoder.
// Compute from leaves, segment indicators, and vertices
// a set of triangles, refine the triangles, and output their vertices.
//
// @param leaves  list of blocks containing the surface
// @param segind, indicators for edges of blocks if they intersect the surface
// @param vertices, locations of intersections

void
decodeTrisoupCommon(
  const ringbuf<PCCOctree3Node>& leaves,
  const std::vector<bool>& segind,
  const std::vector<uint8_t>& vertices,
  PCCPointSet3& pointCloud,
  int defaultBlockWidth,
  int poistionClipValue)
{
  // Put all leaves' sgements into a list.
  std::vector<TrisoupSegment> segments;
  for (int i = 0; i < leaves.size(); i++) {
    auto leaf = leaves[i];

    // Width of block.
    const uint32_t blockWidth =
      defaultBlockWidth;  // in future, may override with leaf blockWidth

    // Eight corners of block.
    const Vec3<uint32_t> pos000({0, 0, 0});
    const Vec3<uint32_t> posW00({blockWidth, 0, 0});
    const Vec3<uint32_t> pos0W0({0, blockWidth, 0});
    const Vec3<uint32_t> posWW0({blockWidth, blockWidth, 0});
    const Vec3<uint32_t> pos00W({0, 0, blockWidth});
    const Vec3<uint32_t> posW0W({blockWidth, 0, blockWidth});
    const Vec3<uint32_t> pos0WW({0, blockWidth, blockWidth});
    const Vec3<uint32_t> posWWW({blockWidth, blockWidth, blockWidth});

    // x: left to right; y: bottom to top; z: far to near
    segments.push_back(  // far bottom edge
      {leaf.pos + pos000, leaf.pos + posW00, 12 * i + 0, -1, -1});
    segments.push_back(  // far left edge
      {leaf.pos + pos000, leaf.pos + pos0W0, 12 * i + 1, -1, -1});
    segments.push_back(  // far top edge
      {leaf.pos + pos0W0, leaf.pos + posWW0, 12 * i + 2, -1, -1});
    segments.push_back(  // far right edge
      {leaf.pos + posW00, leaf.pos + posWW0, 12 * i + 3, -1, -1});
    segments.push_back(  // bottom left edge
      {leaf.pos + pos000, leaf.pos + pos00W, 12 * i + 4, -1, -1});
    segments.push_back(  // top left edge
      {leaf.pos + pos0W0, leaf.pos + pos0WW, 12 * i + 5, -1, -1});
    segments.push_back(  // top right edge
      {leaf.pos + posWW0, leaf.pos + posWWW, 12 * i + 6, -1, -1});
    segments.push_back(  // bottom right edge
      {leaf.pos + posW00, leaf.pos + posW0W, 12 * i + 7, -1, -1});
    segments.push_back(  // near bottom edge
      {leaf.pos + pos00W, leaf.pos + posW0W, 12 * i + 8, -1, -1});
    segments.push_back(  // near left edge
      {leaf.pos + pos00W, leaf.pos + pos0WW, 12 * i + 9, -1, -1});
    segments.push_back(  // near top edge
      {leaf.pos + pos0WW, leaf.pos + posWWW, 12 * i + 10, -1, -1});
    segments.push_back(  // near right edge
      {leaf.pos + posW0W, leaf.pos + posWWW, 12 * i + 11, -1, -1});
  }

  // Copy list of segments to another list to be sorted.
  std::vector<TrisoupSegment> sortedSegments;
  for (int i = 0; i < segments.size(); i++)
    sortedSegments.push_back(segments[i]);

  // Sort the list and find unique segments.
  std::sort(sortedSegments.begin(), sortedSegments.end());
  std::vector<TrisoupSegment> uniqueSegments;
  uniqueSegments.push_back(sortedSegments[0]);
  segments[sortedSegments[0].index].uniqueIndex = 0;
  for (int i = 1; i < sortedSegments.size(); i++) {
    if (
      uniqueSegments.back().startpos != sortedSegments[i].startpos
      || uniqueSegments.back().endpos != sortedSegments[i].endpos) {
      // sortedSegment[i] is different from uniqueSegments.back().
      // Start a new uniqueSegment.
      uniqueSegments.push_back(sortedSegments[i]);
    }
    segments[sortedSegments[i].index].uniqueIndex = uniqueSegments.size() - 1;
  }

  // Get vertex for each unique segment that intersects the surface.
  int vertexCount = 0;
  for (int i = 0; i < uniqueSegments.size(); i++) {
    if (segind[i]) {  // intersects the surface
      uniqueSegments[i].vertex = vertices[vertexCount++];
    } else {  // does not intersect the surface
      uniqueSegments[i].vertex = -1;
    }
  }

  // Copy vertices back to original (non-unique, non-sorted) segments.
  for (int i = 0; i < segments.size(); i++) {
    segments[i].vertex = uniqueSegments[segments[i].uniqueIndex].vertex;
  }

  // Create list of refined vertices, one leaf at a time.
  std::vector<Vec3<int32_t>> refinedVertices;
  for (int i = 0; i < leaves.size(); i++) {
    uint32_t blockWidth = 0;

    // Representation for a vertex in preparation for sorting.
    struct Vertex {
      Vec3<int32_t> pos;  // position of vertex
      int32_t theta;      // angle of vertex when projected along dominant axis
      int32_t tiebreaker;  // coordinate of vertex along dominant axis
      bool operator()(Vertex v1, Vertex v2)
      {
        if (v1.theta > v2.theta)
          return true;  // sort in decreasing order of theta
        if (v1.theta == v2.theta && v1.tiebreaker < v2.tiebreaker)
          return true;
        return false;
      }
    } vertex;

    // Find up to 12 vertices for this leaf.
    std::vector<Vertex> leafVertices;
    for (int j = 0; j < 12; j++) {
      TrisoupSegment& segment = segments[i * 12 + j];
      if (segment.vertex < 0)
        continue;  // skip segments that do not intersect the surface

      // Get distance along edge of vertex.
      // Vertex code is the index of the voxel along the edge of the block
      // of surface intersection./ Put decoded vertex at center of voxel,
      // unless voxel is first or last along the edge, in which case put the
      // decoded vertex at the start or endpoint of the segment.
      Vec3<uint32_t> direction = segment.endpos - segment.startpos;
      blockWidth =
        std::max(direction[0], std::max(direction[1], direction[2]));
      int32_t distance;
      if (segment.vertex == 0)
        distance = 0;
      else if (segment.vertex == blockWidth - 1)
        distance = blockWidth << kTrisoupFpBits;
      else  // 0 < segment.vertex < blockWidth-1
        distance = (int32_t)(
          (segment.vertex << kTrisoupFpBits) + (1 << (kTrisoupFpBits - 1)));

      // Get 3D position of point of intersection.
      Vec3<int32_t> point(
        segment.startpos[0] << kTrisoupFpBits,
        segment.startpos[1] << kTrisoupFpBits,
        segment.startpos[2] << kTrisoupFpBits);
      if (direction[0] > 0)
        point[0] += distance;
      else if (direction[1] > 0)
        point[1] += distance;
      else  // direction[2] > 0
        point[2] += distance;

      // Add vertex to list of points.
      leafVertices.push_back({point, 0, 0});
    }

    // Skip leaves that have fewer than 3 vertices.
    if (leafVertices.size() < 3)
      continue;

    // Compute mean of leaf vertices.
    Vec3<int32_t> blockCentroid = 0;
    for (int j = 0; j < leafVertices.size(); j++) {
      blockCentroid += leafVertices[j].pos;
    }
    blockCentroid /= (int32_t)leafVertices.size();

    // Compute variance of each component of leaf vertices.
    Vec3<int32_t> SS = 0;
    for (int j = 0; j < leafVertices.size(); j++) {
      Vec3<int32_t> S = leafVertices[j].pos - blockCentroid;
      SS += {(S[0] * S[0]) >> kTrisoupFpBits, (S[1] * S[1]) >> kTrisoupFpBits,
             (S[2] * S[2]) >> kTrisoupFpBits};
    }

    // Dominant axis is the coordinate minimizing the variance.
    int32_t minSS = SS[0];
    int32_t dominantAxis = 0;
    for (int32_t j = 1; j < 3; j++) {
      if (minSS > SS[j]) {
        minSS = SS[j];
        dominantAxis = j;
      }
    }

    // Project vertices along dominant axis (i.e., into YZ, XZ, or XY plane).
    // Sort projected vertices by decreasing angle in [-pi,+pi] around center
    // of block (i.e., clockwise from 9:00) breaking ties in angle by
    // increasing distance along the dominant axis.
    Vec3<uint32_t> bc = leaves[i].pos + (blockWidth / 2);
    Vec3<int32_t> blockCenter = {(int32_t)(bc[0] << kTrisoupFpBits),
                                 (int32_t)(bc[1] << kTrisoupFpBits),
                                 (int32_t)(bc[2] << kTrisoupFpBits)};
    for (int j = 0; j < leafVertices.size(); j++) {
      Vec3<int32_t> S = leafVertices[j].pos - blockCenter;
      switch (dominantAxis) {
      case 0:  // dominant axis is X so project into YZ plane
        leafVertices[j].theta = (int32_t)trisoupAtan2(S[2], S[1]);
        leafVertices[j].tiebreaker = S[0];
        break;
      case 1:  // dominant axis is Y so project into XZ plane
        leafVertices[j].theta = (int32_t)trisoupAtan2(S[2], S[0]);
        leafVertices[j].tiebreaker = S[1];
        break;
      case 2:  // dominant axis is Z so project into XY plane
        leafVertices[j].theta = (int32_t)trisoupAtan2(S[1], S[0]);
        leafVertices[j].tiebreaker = S[2];
        break;
      }
    }
    std::sort(leafVertices.begin(), leafVertices.end(), vertex);

    // Table of triangles that make up an n-gon.
    const int polyTriangles[][3] = {
      {0, 1, 2},                                                  // n = 3
      {0, 1, 2},   {2, 3, 0},                                     // n = 4
      {0, 1, 2},   {2, 3, 4}, {4, 0, 2},                          // n = 5
      {0, 1, 2},   {2, 3, 4}, {4, 5, 0},  {0, 2, 4},              // n = 6
      {0, 1, 2},   {2, 3, 4}, {4, 5, 6},  {6, 0, 2},  {2, 4, 6},  // n = 7
      {0, 1, 2},   {2, 3, 4}, {4, 5, 6},  {6, 7, 0},  {0, 2, 4},
      {4, 6, 0},  // n = 8
      {0, 1, 2},   {2, 3, 4}, {4, 5, 6},  {6, 7, 8},  {8, 0, 2},
      {2, 4, 6},   {6, 8, 2},  // n = 9
      {0, 1, 2},   {2, 3, 4}, {4, 5, 6},  {6, 7, 8},  {8, 9, 0},
      {0, 2, 4},   {4, 6, 8}, {8, 0, 4},  // n = 10
      {0, 1, 2},   {2, 3, 4}, {4, 5, 6},  {6, 7, 8},  {8, 9, 10},
      {10, 0, 2},  {2, 4, 6}, {6, 8, 10}, {10, 2, 6},  // n = 11
      {0, 1, 2},   {2, 3, 4}, {4, 5, 6},  {6, 7, 8},  {8, 9, 10},
      {10, 11, 0}, {0, 2, 4}, {4, 6, 8},  {8, 10, 0}, {0, 4, 8}  // n = 12
    };

    // Divide vertices into triangles according to table
    // and upsample each triangle by an upsamplingFactor.
    int triCount = (int)leafVertices.size() - 2;
    int triStart = (triCount - 1) * triCount / 2;
    for (int triIndex = 0; triIndex < triCount; triIndex++) {
      int j0 = polyTriangles[triStart + triIndex][0];
      int j1 = polyTriangles[triStart + triIndex][1];
      int j2 = polyTriangles[triStart + triIndex][2];
      Vec3<int64_t> v0 = {(int64_t)leafVertices[j0].pos[0],
                          (int64_t)leafVertices[j0].pos[1],
                          (int64_t)leafVertices[j0].pos[2]};
      Vec3<int64_t> v1 = {(int64_t)leafVertices[j1].pos[0],
                          (int64_t)leafVertices[j1].pos[1],
                          (int64_t)leafVertices[j1].pos[2]};
      Vec3<int64_t> v2 = {(int64_t)leafVertices[j2].pos[0],
                          (int64_t)leafVertices[j2].pos[1],
                          (int64_t)leafVertices[j2].pos[2]};

      for (int k = 0; k < 3; k++) {
        Vec3<int32_t> foundvoxel = truncate(
          (k == 0 ? v0 : (k == 1 ? v1 : v2)), -(1 << (kTrisoupFpBits - 1)));
        if (boundaryinsidecheck(
              foundvoxel >> kTrisoupFpBits, poistionClipValue)) {
          refinedVertices.push_back(foundvoxel >> kTrisoupFpBits);
        }
      }

      int g1, g2;
      const int g1pos[3] = {1, 0, 0};
      const int g2pos[3] = {2, 2, 1};
      for (int direction = 0; direction < 3; direction++) {
        Vec3<int64_t> rayVector, rayOrigin, intersection;
        const int startposG1 =
          std::min(
            std::min(v0[g1pos[direction]], v1[g1pos[direction]]),
            v2[g1pos[direction]])
          >> kTrisoupFpBits;
        const int startposG2 =
          std::min(
            std::min(v0[g2pos[direction]], v1[g2pos[direction]]),
            v2[g2pos[direction]])
          >> kTrisoupFpBits;
        const int endposG1 =
          std::max(
            std::max(v0[g1pos[direction]], v1[g1pos[direction]]),
            v2[g1pos[direction]])
          >> kTrisoupFpBits;
        const int endposG2 =
          std::max(
            std::max(v0[g2pos[direction]], v1[g2pos[direction]]),
            v2[g2pos[direction]])
          >> kTrisoupFpBits;
        for (g1 = startposG1; g1 <= endposG1; g1++) {
          for (g2 = startposG2; g2 <= endposG2; g2++) {
            std::vector<Vec3<int64_t>> intersectionVertices;
            for (int sign = -1; sign <= 1; sign += 2) {
              const int rayStart = (sign > 0 ? -1 : poistionClipValue + 1);
              if (direction == 0) {
                rayOrigin = Vec3<int64_t>(rayStart, g1, g2);
                rayVector = Vec3<int64_t>(sign, 0, 0);
              } else if (direction == 1) {
                rayOrigin = Vec3<int64_t>(g1, rayStart, g2);
                rayVector = Vec3<int64_t>(0, sign, 0);
              } else if (direction == 2) {
                rayOrigin = Vec3<int64_t>(g1, g2, rayStart);
                rayVector = Vec3<int64_t>(0, 0, sign);
              }
              if (rayIntersectsTriangle(
                    rayOrigin << kTrisoupFpBits, rayVector << kTrisoupFpBits,
                    v0, v1, v2, intersection)) {
                Vec3<int32_t> foundvoxel =
                  truncate(intersection, -(1 << (kTrisoupFpBits - 1)));
                if (boundaryinsidecheck(
                      foundvoxel >> kTrisoupFpBits, poistionClipValue)) {
                  refinedVertices.push_back(foundvoxel >> kTrisoupFpBits);
                }
              }
            }
          }
        }
      }
    }
  }

  std::sort(refinedVertices.begin(), refinedVertices.end());
  refinedVertices.erase(
    std::unique(refinedVertices.begin(), refinedVertices.end()),
    refinedVertices.end());

  // Move list of points to pointCloud.
  pointCloud.resize(refinedVertices.size());
  for (int i = 0; i < refinedVertices.size(); i++) {
    pointCloud[i] =
      PCCPoint3D{(double)refinedVertices[i][0], (double)refinedVertices[i][1],
                 (double)refinedVertices[i][2]};
  }
}

//============================================================================

}  // namespace pcc
