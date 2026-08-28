#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "hittable.h"


using prim_handle = int;
inline constexpr prim_handle null_prim = -1;

class scene_edit;

class scene : public hittable
{
public:
  void set_stop_render(std::function<void()> stop)
  {
    _stop_render = std::move(stop);
  }

  scene_edit edit();

  uint64_t version() const
  {
    return _version.load(std::memory_order_acquire);
  }

  size_t size() const { return _live; }
  size_t slot_count() const { return _slots.size(); }
  size_t draw_count() const { return _draw.size(); }

  void commit() override
  {
    if (!_dirty.exchange(false, std::memory_order_acq_rel))
    {
      return;
    }

    _draw.clear();

    for (const record &r : _slots)
    {
      if (r.prim == nullptr) continue;

      r.prim->commit();
      if (r.visible)
      {
        _draw.push_back({r.prim.get(), r.prim_id});
      }
    }
  }

  bool hit(const ray &r, interval clipping_range, hit_info &info) const override
  {
    hit_info temp_info;
    bool did_hit = false;
    double closest = clipping_range.max;

    for (const entry &e : _draw)
    {
      if (!e.prim->hit(r, interval(clipping_range.min, closest), temp_info))
      {
        continue;
      }

      did_hit = true;
      closest = temp_info.t;
      temp_info.prim_id = e.prim_id;
      info = temp_info;

      temp_info.instance_id = -1;
      temp_info.element_id = -1;
    }

    return did_hit;
  }

private:
  friend class scene_edit;

  struct record
  {
    shared_ptr<hittable> prim;
    int32_t prim_id = -1;
    bool visible = true;
  };

  struct entry
  {
    const hittable *prim;
    int32_t prim_id;
  };

  std::vector<record> _slots;
  std::vector<prim_handle> _free;
  std::vector<entry> _draw;
  size_t _live = 0;

  std::function<void()> _stop_render;
  std::mutex _edit_lock;
  std::atomic<uint64_t> _version {0};
  std::atomic<bool> _dirty {false};
};


class scene_edit
{
public:
  scene_edit(const scene_edit &) = delete;
  scene_edit &operator=(const scene_edit &) = delete;
  scene_edit(scene_edit &&) = default;

  prim_handle insert(shared_ptr<hittable> prim)
  {
    prim_handle h;

    if (!_s->_free.empty())
    {
      h = _s->_free.back();
      _s->_free.pop_back();
    }
    else
    {
      h = prim_handle(_s->_slots.size());
      _s->_slots.emplace_back();
    }
    
    _s->_slots[h] = {std::move(prim), -1, true};
    _s->_live++;
    
    return h;
  }

  [[nodiscard]] bool set_prim(prim_handle h, shared_ptr<hittable> prim)
  {
    scene::record &r = _s->_slots[h];
    if (r.prim == nullptr || prim == nullptr)
    {
      return false;
    }

    r.prim = std::move(prim);
    return true;
  }

  void set_prim_id(prim_handle h, int32_t id)
  {
    _s->_slots[h].prim_id = id;
  }

  void set_visible(prim_handle h, bool visible)
  {
    _s->_slots[h].visible = visible;
  }

  void remove(prim_handle h)
  {
    if (h == null_prim || _s->_slots[h].prim == nullptr)
    {
      return;
    }

    _s->_slots[h] = {};
    _s->_free.push_back(h);
    _s->_live--;
  }

private:
  friend class scene;

  scene *_s;
  std::unique_lock<std::mutex> _lock;

  explicit scene_edit(scene &s) : _s(&s)
  {
    if (s._stop_render)
    {
      s._stop_render();
    }

    _lock = std::unique_lock<std::mutex>(s._edit_lock);
    s._version.fetch_add(1, std::memory_order_acq_rel);
    s._dirty.store(true, std::memory_order_release);
  }
};

inline scene_edit scene::edit()
{
  return scene_edit(*this);
}

