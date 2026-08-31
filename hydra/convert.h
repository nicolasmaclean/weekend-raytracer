#pragma once

#include <cstring>

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/tf/token.h>
#include <pxr/imaging/hd/tokens.h>

#include "tracer/mat4.h"
#include "tracer/render_buffer.h"

using namespace pxr;


inline mat4 ToMat4(const GfMatrix4d &m)
{
  mat4 out;
  std::memcpy(&out.m[0][0], m.GetArray(), 16 * sizeof(double));
  return out;
}

inline bool ToAov(TfToken const &name, aov *out)
{
  if (name == HdAovTokens->color)
    *out = aov::color;
  else if (name == HdAovTokens->depth)
    *out = aov::depth;
  else if (name == HdAovTokens->cameraDepth)
    *out = aov::camera_depth;
  else if (name == HdAovTokens->normal)
    *out = aov::normal;
  else if (name == HdAovTokens->Neye)
    *out = aov::n_eye;
  else if (name == HdAovTokens->primId)
    *out = aov::prim_id;
  else if (name == HdAovTokens->instanceId)
    *out = aov::instance_id;
  else if (name == HdAovTokens->elementId)
    *out = aov::element_id;
  else
    return false;
  return true;
}

