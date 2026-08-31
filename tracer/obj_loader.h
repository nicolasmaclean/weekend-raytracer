#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "tiny_obj_loader.h"

#include "material.h"
#include "mesh.h"
#include "tracer.h"


struct obj_load_result
{
  std::vector<shared_ptr<mesh>> meshes;
  std::string warning;
  std::string error;           // non-empty means nothing was loaded
  size_t degenerate_faces = 0; // authored faces with fewer than 3 corners
};

// One `mesh` per OBJ shape (`o`/`g`), because one mesh has one material and one
// Rprim has one material binding. Materials themselves are the caller's
// business: .mtl is out of scope, see the plan's "What is explicitly NOT".
inline obj_load_result load_obj(const std::string &path, const shared_ptr<material> &mat)
{
  obj_load_result out;

  tinyobj::ObjReaderConfig cfg;
  // tinyobjloader's own triangulation renumbers faces in place and
  // hands back no map to the authored face. We'll just triangulate it ourselves.
  cfg.triangulate = false;
  cfg.vertex_color = false;

  // read obj file
  tinyobj::ObjReader reader;
  const bool ok = reader.ParseFromFile(path, cfg);
  out.warning = reader.Warning();
  out.error = reader.Error();
  if (!ok) return out;

  // convert parsed obj to our mesh objects
  const tinyobj::attrib_t &attrib = reader.GetAttrib();
  for (const tinyobj::shape_t &shape : reader.GetShapes())
  {
    auto m = make_shared<mesh>();
    m->mat = mat;

    // One vertex per distinct (position, normal) pair. Two faces that share a
    // position but not a normal must not share a vertex, or the crease between
    // them is smoothed away. Measured on the test icosphere: 3 840 corners
    // collapse to 642 vertices.
    std::map<std::pair<int, int>, int32_t> remap;
    bool have_normals = true;

    auto corner_vertex = [&](const tinyobj::index_t &idx)
    {
      const auto key = std::make_pair(idx.vertex_index, idx.normal_index);
      auto it = remap.find(key);
      if (it != remap.end()) return it->second;

      m->verts.emplace_back(attrib.vertices[3 * idx.vertex_index + 0],
                            attrib.vertices[3 * idx.vertex_index + 1],
                            attrib.vertices[3 * idx.vertex_index + 2]);

      if (idx.normal_index >= 0)
      {
        m->normals.emplace_back(attrib.normals[3 * idx.normal_index + 0],
                                attrib.normals[3 * idx.normal_index + 1],
                                attrib.normals[3 * idx.normal_index + 2]);
      }
      else
      {
        have_normals = false;
      }

      const auto id = int32_t(m->verts.size() - 1);
      remap.emplace(key, id);
      return id;
    };

    size_t corner = 0;
    for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++)
    {
      const unsigned n = shape.mesh.num_face_vertices[f];
      if (n < 3)
      {
        out.degenerate_faces++;
        corner += n;
        continue;
      }

      // Triangle fan from corner 0, and every triangle carries `f`. Same
      // topology HdMeshUtil produces, and the same limitation: correct for
      // convex faces, which is what an OBJ is supposed to contain.
      const int32_t v0 = corner_vertex(shape.mesh.indices[corner]);
      for (unsigned k = 1; k + 1 < n; k++)
      {
        const int32_t vk = corner_vertex(shape.mesh.indices[corner + k]);
        const int32_t vn = corner_vertex(shape.mesh.indices[corner + k + 1]);
        m->tris.push_back(v0);
        m->tris.push_back(vk);
        m->tris.push_back(vn);
        m->face.push_back(int32_t(f));
      }
      corner += n;
    }

    // A mesh is entirely smooth or entirely flat. A partially normalled OBJ
    // falls back to flat rather than leaving N shorter than P.
    if (!have_normals) m->normals.clear();

    if (!m->tris.empty()) out.meshes.push_back(std::move(m));
  }

  return out;
}

