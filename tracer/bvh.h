#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "tracer.h"
#include "aabb.h"
#include "hittable.h"
#include "interval.h"


// A binary BVH built with a binned surface-area heuristic.
//
// Traversal uses a callback at each leaf allowing this to service TLAS and BLAS.
//
// Two array orders exist, and confusing them is the one real hazard here:
//   build(boxes)  takes boxes in the CALLER's order and fills order() so that
//                 order()[i] is the caller index of the i'th primitive in BVH
//                 order. The caller permutes its payload to match.
//   refit(boxes)  takes boxes already in BVH order - that is, in the order the
//                 caller permuted its payload into. Topology is untouched.
class bvh
{
public:
  // SAH reads the ratio of these 2
  static constexpr double cost_traverse = 1.0;
  static constexpr double cost_intersect = 1.0;

  // Refit the tree untill cost exceds the original by this much
  static constexpr double rebuild_ratio = 1.3;

  // Stop subdividing at or below `leaf_size` and never emit a leaf larger 
  // than `max_leaf` even when the SAH asks for one.
  int leaf_size = 1;
  int max_leaf = 16;
  int bin_count = 12;

  struct node
  {
    aabb box;
    int32_t left_first = 0;  // index of the left child
    int32_t count = 0;       // 0 == interior node
    int32_t axis = 0;        
  };

  void build(const std::vector<aabb> &boxes)
  {
    _nodes.clear();
    _order.clear();
    _depth = 0;
    _built_cost = _cost = 0;

    const size_t n = boxes.size();
    if (n == 0)
    {
      return;
    }

    _refs.resize(n);
    for (size_t i = 0; i < n; i++)
    {
      _refs[i] = {boxes[i], boxes[i].centroid(), int32_t(i)};
    }

    _nodes.reserve(2 * n);
    _nodes.emplace_back();
    build_node(0, 0, int32_t(n), 1);

    _order.resize(n);
    for (size_t i = 0; i < n; i++)
    {
      _order[i] = _refs[i].index;
    }

    _refs.clear();
    _refs.shrink_to_fit();

    _built_cost = _cost = sah_cost();
  }

  // `boxes` must already be in BVH order.
  // returns cost so caller can decide refit vs rebuild
  double refit(const std::vector<aabb> &boxes)
  {
    double sum = 0;

    for (int32_t i = int32_t(_nodes.size()) - 1; i >= 0; i--)
    {
      node &nd = _nodes[i];
      if (nd.count > 0)
      {
        nd.box = aabb::empty();
        for (int32_t k = 0; k < nd.count; k++)
        {
          nd.box.expand(boxes[nd.left_first + k]);
        }
        sum += cost_intersect * nd.box.surface_area() * nd.count;
      }
      else
      {
        nd.box = merge(_nodes[nd.left_first].box, _nodes[nd.left_first + 1].box);
        sum += cost_traverse * nd.box.surface_area();
      }
    }

    const double root = _nodes.empty() ? 0 : _nodes[0].box.surface_area();
    _cost = root > 0 ? sum / root : 0;
    return _cost;
  }

  bool empty() const { return _nodes.empty(); }
  size_t node_count() const { return _nodes.size(); }
  int depth() const { return _depth; }
  const std::vector<int32_t> &order() const { return _order; }
  const std::vector<node> &nodes() const { return _nodes; }

  aabb bounds() const
  {
    return _nodes.empty() ? aabb::empty() : _nodes[0].box;
  }

  double built_cost() const { return _built_cost; }
  double cost() const { return _cost; }
  bool degraded() const { return _built_cost > 0 && _cost > rebuild_ratio * _built_cost; }

  // Surface area heuristic to compare bvh effectiveness, best statistic to use
  // other than a stopwatch and some testing
  double sah_cost() const
  {
    if (_nodes.empty()) return 0;

    const double root = _nodes[0].box.surface_area();
    if (root <= 0) return 0;

    double c = 0;
    for (const node &nd : _nodes)
    {
      const double a = nd.box.surface_area() / root;
      c += nd.count > 0 ? cost_intersect * a * nd.count : cost_traverse * a;
    }

    return c;
  }

  // leaf(first, count, clip, info) -> true if there is a hit closer than clip.max.
  // if leaf returns true, it also needs to set `info.t`
  template <class LeafFn>
  bool hit(const ray &r, interval clip, hit_info &info, LeafFn leaf) const
  {
    if (_nodes.empty()) return false;

    const slab_ray s(r);
    double closest = clip.max;
    bool did_hit = false;

    // One entry per level, and build_node() caps the depth below max_stack, so
    // this cannot overflow. See max_depth.
    int32_t stack[max_stack];
    double stack_t[max_stack];
    int sp = 0;
    int32_t cur = 0;

    if (!slab_hit(_nodes[0].box, s, clip.min, closest))
    {
      return false;
    }

    for (;;)
    {
      const node &nd = _nodes[cur];

      if (nd.count > 0)
      {
        if (leaf(nd.left_first, nd.count, interval(clip.min, closest), info))
        {
          did_hit = true;
          closest = info.t;
        }
      }
      else
      {
        // Use near child first because it's more likely to allow discarding 
        // 2nd box test
        int32_t a = nd.left_first;
        int32_t b = nd.left_first + 1;
        if (s.neg[nd.axis]) std::swap(a, b);

        double ta = 0, tb = 0;
        const bool ha = slab_enter(_nodes[a].box, s, clip.min, closest, ta);
        const bool hb = slab_enter(_nodes[b].box, s, clip.min, closest, tb);

        if (ha)
        {
          if (hb)
          {
            stack_t[sp] = tb;
            stack[sp++] = b;
          }
          cur = a;
          continue;
        }
        if (hb)
        {
          cur = b;
          continue;
        }
      }

      for (;;)
      {
        if (sp == 0) return did_hit;
        sp--;
        if (stack_t[sp] < closest)
        {
          cur = stack[sp];
          break;
        }
      }
    }
  }

private:
  static constexpr int max_stack = 64;
  static constexpr int max_bins = 64;

  // must be less than max_stack to make sure degenerate geo doesn't cause overflow
  static constexpr int max_depth = 60;

  struct prim_ref
  {
    aabb box;
    point3 centroid;
    int32_t index;
  };

  struct bin
  {
    aabb box = aabb::empty();
    int32_t count = 0;
  };

  std::vector<node> _nodes;
  std::vector<int32_t> _order;
  std::vector<prim_ref> _refs;
  int _depth = 0;
  double _built_cost = 0;
  double _cost = 0;

  static bool slab_enter(const aabb &b, const slab_ray &s, double tmin, double tmax, double &enter)
  {
    for (int a = 0; a < 3; a++)
    {
      double t0 = (b.lo[a] - s.o[a]) * s.inv[a];
      double t1 = (b.hi[a] - s.o[a]) * s.inv[a];
      if (s.neg[a]) std::swap(t0, t1);

      if (t0 > tmin) tmin = t0;
      if (t1 < tmax) tmax = t1;
      if (tmax < tmin) return false;   // strictly less-than: see slab_hit
    }

    enter = tmin;
    return true;
  }

  void make_leaf(int32_t index, int32_t first, int32_t count)
  {
    _nodes[index].left_first = first;
    _nodes[index].count = count;
  }

  // Split the range at its centroid median on `axis`.
  // Fallback for when SAH has nothing to say: coincident centroids, 
  // zero-area geometry, or a partition that came back empty on one side.
  void median_split(int32_t index, int32_t first, int32_t count, int axis, int depth)
  {
    const auto begin = _refs.begin() + first;
    const int32_t left_n = count / 2;
    std::nth_element(begin, begin + left_n, begin + count,
                     [&](const prim_ref &a, const prim_ref &b) {
                       return a.centroid[axis] < b.centroid[axis];
                     });
    split_children(index, first, count, left_n, axis, depth);
  }

  void split_children(int32_t index, int32_t first, int32_t count, int32_t left_n, int axis, int depth)
  {
    const int32_t left = int32_t(_nodes.size());
    _nodes.emplace_back();
    _nodes.emplace_back();

    _nodes[index].left_first = left;
    _nodes[index].count = 0;
    _nodes[index].axis = axis;

    // By index, never by reference: the emplace_back above can reallocate.
    build_node(left, first, left_n, depth + 1);
    build_node(left + 1, first + left_n, count - left_n, depth + 1);
  }

  void build_node(int32_t index, int32_t first, int32_t count, int depth)
  {
    if (depth > _depth) _depth = depth;

    aabb box = aabb::empty();
    aabb centroids = aabb::empty();
    for (int32_t i = first; i < first + count; i++)
    {
      box.expand(_refs[i].box);
      centroids.expand(_refs[i].centroid);
    }
    _nodes[index].box = box;

    if (count <= leaf_size || depth >= max_depth)
    {
      make_leaf(index, first, count);
      return;
    }

    // Widest axis of the CENTROID bounds, not of the box: the box can be wide
    // on an axis every centroid shares, and binning that axis puts every
    // primitive in one bin.
    const vec3 ext = centroids.extent();
    int axis = 0;
    if (ext[1] > ext[axis]) axis = 1;
    if (ext[2] > ext[axis]) axis = 2;

    // Zero surface area makes every SAH candidate cost zero, so the sweep picks
    // the first one it sees and the tree degenerates into a list - measured at
    // depth 98 over 100k coplanar boxes, which overruns the traversal stack.
    // Flat geometry is not exotic: a ground plane is two triangles like this.
    if (!(box.surface_area() > 0) || !(ext[axis] > 0))
    {
      if (count <= max_leaf)
      {
        make_leaf(index, first, count);
        return;
      }
      median_split(index, first, count, axis, depth);
      return;
    }

    // --- bin ---------------------------------------------------------------
    // Stack arrays, not vectors. build_node() runs once per node, so three heap
    // allocations here are three per node: measured at 487 prims, that alone
    // was two thirds of the build.
    bin bins[max_bins];
    for (int i = 0; i < bin_count; i++) bins[i] = bin();

    const double lo = centroids.lo[axis];
    const double scale = bin_count / ext[axis];

    for (int32_t i = first; i < first + count; i++)
    {
      int b = int((_refs[i].centroid[axis] - lo) * scale);
      b = std::min(std::max(b, 0), bin_count - 1);
      bins[b].box.expand(_refs[i].box);
      bins[b].count++;
    }

    // --- sweep -------------------------------------------------------------
    // left_area[i] describes bins [0, i]; the candidate between bin i and i+1
    // is evaluated against the suffix accumulated on the way back down. Two
    // linear passes, not O(bins^2).
    double left_area[max_bins];
    int32_t left_count[max_bins];
    {
      aabb acc = aabb::empty();
      int32_t n = 0;
      for (int i = 0; i < bin_count; i++)
      {
        acc.expand(bins[i].box);
        n += bins[i].count;
        left_area[i] = acc.surface_area();
        left_count[i] = n;
      }
    }

    double best_cost = infinity;
    int best_bin = -1;
    {
      aabb acc = aabb::empty();
      int32_t n = 0;
      for (int i = bin_count - 1; i > 0; i--)
      {
        acc.expand(bins[i].box);
        n += bins[i].count;
        if (left_count[i - 1] == 0 || n == 0) continue;

        const double c = left_area[i - 1] * left_count[i - 1] + acc.surface_area() * n;
        if (c < best_cost)
        {
          best_cost = c;
          best_bin = i - 1;     // the left side is bins [0, best_bin]
        }
      }
    }

    const double parent_area = box.surface_area();
    const double leaf_cost = cost_intersect * parent_area * count;
    const double split_cost = cost_traverse * parent_area + cost_intersect * best_cost;

    if (best_bin < 0 || (split_cost >= leaf_cost && count <= max_leaf))
    {
      make_leaf(index, first, count);
      return;
    }

    // --- partition ---------------------------------------------------------
    const auto begin = _refs.begin() + first;
    const auto end = begin + count;
    const auto mid = std::partition(begin, end, [&](const prim_ref &p) {
      int b = int((p.centroid[axis] - lo) * scale);
      b = std::min(std::max(b, 0), bin_count - 1);
      return b <= best_bin;
    });

    const int32_t left_n = int32_t(mid - begin);
    if (left_n == 0 || left_n == count)
    {
      // The bin the SAH chose and the bin std::partition computed disagreed,
      // which rounding can do at a bin edge. Falling through would recurse on
      // an empty child - infinite recursion, not a slow tree.
      median_split(index, first, count, axis, depth);
      return;
    }

    split_children(index, first, count, left_n, axis, depth);
  }
};
