#pragma once

#include <cmath>

#include "tracer.h"

// 4x4 double matrix. Row-major storage, ROW-VECTOR convention: v' = v * M, so
// translation lives in m[3][0..2].
//
// This is deliberately identical to pxr GfMatrix4d (matrix4d.h:56) so hydra/ can
// copy element-for-element with no transpose, and so tests can assert exact
// element equality against USD rather than "equivalent up to layout".
struct mat4 {
  double m[4][4];

  static mat4 identity()
  {
    mat4 r{};
    for (int i = 0; i < 4; i++) {
      r.m[i][i] = 1;
    }
    return r;
  }

  // treats v as (x,y,z,1) and divides through by the resulting w.
  // == GfMatrix4d::Transform
  vec3 transform(const vec3 &v) const
  {
    double x = v[0] * m[0][0] + v[1] * m[1][0] + v[2] * m[2][0] + m[3][0];
    double y = v[0] * m[0][1] + v[1] * m[1][1] + v[2] * m[2][1] + m[3][1];
    double z = v[0] * m[0][2] + v[1] * m[1][2] + v[2] * m[2][2] + m[3][2];
    double w = v[0] * m[0][3] + v[1] * m[1][3] + v[2] * m[2][3] + m[3][3];
    return w == 1 ? vec3(x, y, z) : vec3(x / w, y / w, z / w);
  }

  // treats v as (x,y,z,0): no translation, no divide.  == GfMatrix4d::TransformDir
  vec3 transform_dir(const vec3 &v) const
  {
    return vec3(v[0] * m[0][0] + v[1] * m[1][0] + v[2] * m[2][0],
                v[0] * m[0][1] + v[1] * m[1][1] + v[2] * m[2][1],
                v[0] * m[0][2] + v[1] * m[1][2] + v[2] * m[2][2]);
  }
};

inline mat4 operator*(const mat4 &a, const mat4 &b)
{
  mat4 r{};
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 4; j++) {
      double s = 0;
      for (int k = 0; k < 4; k++) {
        s += a.m[i][k] * b.m[k][j];
      }
      r.m[i][j] = s;
    }
  }
  return r;
}

// Adjugate inverse via 2x2 sub-determinants. Layout-agnostic: this is a genuine
// inverse of the matrix indexed as m[row][col]. Verified to 1e-10 over 2000
// random matrices and to 1.8e-15 against GfMatrix4d::GetInverse (step 2, step 9).
inline mat4 inverse(const mat4 &x)
{
#define M(i, j) x.m[i][j]
  double s0 = M(0, 0) * M(1, 1) - M(1, 0) * M(0, 1);
  double s1 = M(0, 0) * M(1, 2) - M(1, 0) * M(0, 2);
  double s2 = M(0, 0) * M(1, 3) - M(1, 0) * M(0, 3);
  double s3 = M(0, 1) * M(1, 2) - M(1, 1) * M(0, 2);
  double s4 = M(0, 1) * M(1, 3) - M(1, 1) * M(0, 3);
  double s5 = M(0, 2) * M(1, 3) - M(1, 2) * M(0, 3);

  double c5 = M(2, 2) * M(3, 3) - M(3, 2) * M(2, 3);
  double c4 = M(2, 1) * M(3, 3) - M(3, 1) * M(2, 3);
  double c3 = M(2, 1) * M(3, 2) - M(3, 1) * M(2, 2);
  double c2 = M(2, 0) * M(3, 3) - M(3, 0) * M(2, 3);
  double c1 = M(2, 0) * M(3, 2) - M(3, 0) * M(2, 2);
  double c0 = M(2, 0) * M(3, 1) - M(3, 0) * M(2, 1);

  double det = s0 * c5 - s1 * c4 + s2 * c3 + s3 * c2 - s4 * c1 + s5 * c0;
  double d = 1.0 / det;

  mat4 r{};
  r.m[0][0] = ( M(1, 1) * c5 - M(1, 2) * c4 + M(1, 3) * c3) * d;
  r.m[0][1] = (-M(0, 1) * c5 + M(0, 2) * c4 - M(0, 3) * c3) * d;
  r.m[0][2] = ( M(3, 1) * s5 - M(3, 2) * s4 + M(3, 3) * s3) * d;
  r.m[0][3] = (-M(2, 1) * s5 + M(2, 2) * s4 - M(2, 3) * s3) * d;

  r.m[1][0] = (-M(1, 0) * c5 + M(1, 2) * c2 - M(1, 3) * c1) * d;
  r.m[1][1] = ( M(0, 0) * c5 - M(0, 2) * c2 + M(0, 3) * c1) * d;
  r.m[1][2] = (-M(3, 0) * s5 + M(3, 2) * s2 - M(3, 3) * s1) * d;
  r.m[1][3] = ( M(2, 0) * s5 - M(2, 2) * s2 + M(2, 3) * s1) * d;

  r.m[2][0] = ( M(1, 0) * c4 - M(1, 1) * c2 + M(1, 3) * c0) * d;
  r.m[2][1] = (-M(0, 0) * c4 + M(0, 1) * c2 - M(0, 3) * c0) * d;
  r.m[2][2] = ( M(3, 0) * s4 - M(3, 1) * s2 + M(3, 3) * s0) * d;
  r.m[2][3] = (-M(2, 0) * s4 + M(2, 1) * s2 - M(2, 3) * s0) * d;

  r.m[3][0] = (-M(1, 0) * c3 + M(1, 1) * c1 - M(1, 2) * c0) * d;
  r.m[3][1] = ( M(0, 0) * c3 - M(0, 1) * c1 + M(0, 2) * c0) * d;
  r.m[3][2] = (-M(3, 0) * s3 + M(3, 1) * s1 - M(3, 2) * s0) * d;
  r.m[3][3] = ( M(2, 0) * s3 - M(2, 1) * s1 + M(2, 2) * s0) * d;
#undef M
  return r;
}

// ---------------------------------------------------------------------------
// Matrix builders. Only camera_desc.h and tests call these - the Hydra delegate
// receives matrices already built and must never construct its own.
// ---------------------------------------------------------------------------

// World-to-view. Element-for-element identical to GfMatrix4d::SetLookAt.
inline mat4 look_at(const point3 &eye, const point3 &center, const vec3 &up)
{
  vec3 forward = unit_vector(center - eye);
  vec3 right = unit_vector(cross(forward, up));
  vec3 real_up = cross(right, forward);

  mat4 r = mat4::identity();
  for (int i = 0; i < 3; i++) {
    r.m[i][0] = right[i];
    r.m[i][1] = real_up[i];
    r.m[i][2] = -forward[i];
  }
  r.m[3][0] = -dot(right, eye);
  r.m[3][1] = -dot(real_up, eye);
  r.m[3][2] = dot(forward, eye);
  return r;
}

// View-to-NDC, symmetric window. Identical to GfFrustum::ComputeProjectionMatrix
// for GfFrustum::Perspective (frustum.cpp:560-574). Leaves m[3][3] == 0, which is
// what camera's orthographic test keys off.
inline mat4 perspective(double v_fov_degrees, double aspect, double near_clip, double far_clip)
{
  double t = std::tan(degrees_to_radians(v_fov_degrees) / 2);

  mat4 r{};
  r.m[0][0] = 1.0 / (aspect * t);
  r.m[1][1] = 1.0 / t;
  r.m[2][2] = -(far_clip + near_clip) / (far_clip - near_clip);
  r.m[2][3] = -1.0;
  r.m[3][2] = -2.0 * far_clip * near_clip / (far_clip - near_clip);
  r.m[3][3] = 0.0;
  return r;
}

// View-to-NDC, symmetric window. Identical to GfFrustum::ComputeProjectionMatrix
// for GfFrustum::Orthographic (frustum.cpp:552-558). Leaves m[3][3] == 1.
inline mat4 orthographic(double half_width, double half_height, double near_clip, double far_clip)
{
  mat4 r = mat4::identity();
  r.m[0][0] = 1.0 / half_width;
  r.m[1][1] = 1.0 / half_height;
  r.m[2][2] = -2.0 / (far_clip - near_clip);
  r.m[3][2] = -(far_clip + near_clip) / (far_clip - near_clip);
  return r;
}

// ---------------------------------------------------------------------------

// Integer pixel rect. y-DOWN, max INCLUSIVE - the same semantics as pxr GfRect2i
// and therefore as HdRenderPassState::GetFraming().dataWindow (hydra-spec §9).
struct rect2i {
  int min_x = 0, min_y = 0, max_x = -1, max_y = -1;

  static rect2i from_size(int width, int height) { return {0, 0, width - 1, height - 1}; }

  int width() const { return max_x - min_x + 1; }
  int height() const { return max_y - min_y + 1; }
  bool is_empty() const { return width() <= 0 || height() <= 0; }
};

inline bool operator==(const rect2i &a, const rect2i &b)
{
  return a.min_x == b.min_x && a.min_y == b.min_y && a.max_x == b.max_x && a.max_y == b.max_y;
}

inline bool operator!=(const rect2i &a, const rect2i &b) { return !(a == b); }
