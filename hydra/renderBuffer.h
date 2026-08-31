#pragma once

#include "pxr/imaging/hd/renderBuffer.h"
// hd/renderBuffer.h names GfVec3i in Allocate()'s signature but does not include
// it, and TF_WARN comes from tf/diagnostic.h. Without both, this header only
// compiles when something upstream happened to include them first.
#include "pxr/base/gf/vec3i.h"
#include "pxr/base/tf/diagnostic.h"

// tracer
#include "tracer/render_buffer.h"

PXR_NAMESPACE_OPEN_SCOPE

// tracer's buffer_format is HdFormat with the same integer values
// (render_buffer.h:14 vs hd/types.h:408). Verified for the range we allocate.
static_assert(int(buffer_format::unorm8)       == int(HdFormatUNorm8), "");
static_assert(int(buffer_format::float32_vec4) == int(HdFormatFloat32Vec4), "");
static_assert(int(buffer_format::int32_vec4)   == int(HdFormatInt32Vec4), "");

class HdWeekendRenderBuffer final : public HdRenderBuffer
{
public:
    HdWeekendRenderBuffer(SdfPath const& id) : HdRenderBuffer(id) {}

    bool Allocate(GfVec3i const& dims, HdFormat format, bool multiSampled) override
    {
        if (dims[2] != 1) // §8.1: depth==1 only
        {                       
            TF_WARN("Only 2D render buffers are supported");
            return false;
        }
        return _buf.allocate(
            dims[0], dims[1], 
            static_cast<buffer_format>(int(format)), multiSampled
        );
    }

    unsigned int GetWidth()  const override { return _buf.width();  }
    unsigned int GetHeight() const override { return _buf.height(); }
    unsigned int GetDepth()  const override { return 1; }
    HdFormat GetFormat()     const override { return static_cast<HdFormat>(int(_buf.format())); }
    bool IsMultiSampled()    const override { return _buf.is_multisampled(); }

    void* Map()            override { return _buf.map(); }
    void  Unmap()          override { _buf.unmap(); }
    bool  IsMapped() const override { return _buf.is_mapped(); }
    void  Resolve()        override { _buf.resolve(); }
    bool  IsConverged() const override { return _buf.is_converged(); }
    void  SetConverged(bool c)      { _buf.set_converged(c); }

    // The renderer writes through this, not through HdRenderBuffer.
    render_buffer& Buffer() { return _buf; }

protected:
    void _Deallocate() override { _buf.deallocate(); }

private:
    render_buffer _buf;
};

PXR_NAMESPACE_CLOSE_SCOPE

