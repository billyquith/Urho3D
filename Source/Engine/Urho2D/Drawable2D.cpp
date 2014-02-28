//
// Copyright (c) 2008-2014 the Urho3D project.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#include "Precompiled.h"
#include "Camera.h"
#include "Context.h"
#include "Drawable2D.h"
#include "Geometry.h"
#include "Material.h"
#include "Node.h"
#include "ResourceCache.h"
#include "Sprite2D.h"
#include "SpriteSheet2D.h"
#include "Technique.h"
#include "Texture2D.h"
#include "VertexBuffer.h"

#include "DebugNew.h"
#include "Log.h"

namespace Urho3D
{

extern const char* blendModeNames[];

Drawable2D::Drawable2D(Context* context) : Drawable(context, DRAWABLE_GEOMETRY),
    unitPerPixel_(1.0f),
    blendMode_(BLEND_REPLACE),
    zValue_(0.0f),
    vertexBuffer_(new VertexBuffer(context_)),
    geometryDirty_(true),
    materialDirty_(true)
{
    geometry_ = new Geometry(context);
    batches_.Resize(1);
    batches_[0].geometry_ = geometry_;
}

Drawable2D::~Drawable2D()
{
}

void Drawable2D::RegisterObject(Context* context)
{
    ACCESSOR_ATTRIBUTE(Drawable2D, VAR_FLOAT, "Unit Per Pixel", GetUnitPerPixel, SetUnitPerPixel, float, 1.0f, AM_DEFAULT);
    ACCESSOR_ATTRIBUTE(Drawable2D, VAR_RESOURCEREF, "Sprite", GetSpriteAttr, SetSpriteAttr, ResourceRef, ResourceRef(Sprite2D::GetTypeStatic()), AM_FILE);
    ACCESSOR_ATTRIBUTE(Drawable2D, VAR_RESOURCEREF, "Material", GetMaterialAttr, SetMaterialAttr, ResourceRef, ResourceRef(Material::GetTypeStatic()), AM_DEFAULT);
    ENUM_ACCESSOR_ATTRIBUTE(Drawable2D, "Blend Mode", GetBlendMode, SetBlendMode, BlendMode, blendModeNames, 0, AM_FILE);
    COPY_BASE_ATTRIBUTES(Drawable2D, Drawable);
}

void Drawable2D::ApplyAttributes()
{
    UpdateVertices();
    UpdateMaterial(true);
}

void Drawable2D::UpdateBatches(const FrameInfo& frame)
{
    if (materialDirty_)
        UpdateMaterial();

    const Matrix3x4& worldTransform = node_->GetWorldTransform();
    distance_ = frame.camera_->GetDistance(GetWorldBoundingBox().Center());

    batches_[0].distance_ = distance_;
    batches_[0].worldTransform_ = &worldTransform;
}

void Drawable2D::UpdateGeometry(const FrameInfo& frame)
{
    if (!geometryDirty_)
        return;

    if (verticesDirty_)
        UpdateVertices();

    if (vertices_.Size() > 0)
    {
        unsigned vertexCount = vertices_.Size() / 4 * 6;
        
        vertexBuffer_->SetSize(vertexCount, MASK_VERTEX2D);
        Vertex2D* dest = (Vertex2D*)vertexBuffer_->Lock(0, vertexCount, true);
        if (dest)
        {
            for (unsigned i = 0; i < vertices_.Size(); i += 4)
            {
                *(dest++) = vertices_[i + 0];
                *(dest++) = vertices_[i + 1];
                *(dest++) = vertices_[i + 2];

                *(dest++) = vertices_[i + 0];
                *(dest++) = vertices_[i + 2];
                *(dest++) = vertices_[i + 3];
            }

            vertexBuffer_->Unlock();
        }
        else
            LOGERROR("Failed to lock vertex buffer");
        
        if (geometry_->GetVertexBuffer(0) != vertexBuffer_)
            geometry_->SetVertexBuffer(0, vertexBuffer_, MASK_VERTEX2D);
        geometry_->SetDrawRange(TRIANGLE_LIST, 0, 0, 0, vertexCount);
    }
    else
    {
        if (geometry_->GetVertexBuffer(0) != vertexBuffer_)
            geometry_->SetVertexBuffer(0, vertexBuffer_, MASK_VERTEX2D);
        geometry_->SetDrawRange(TRIANGLE_LIST, 0, 0, 0, 0);
    }

    vertexBuffer_->ClearDataLost();
    geometryDirty_ = false;
}

UpdateGeometryType Drawable2D::GetUpdateGeometryType()
{
    if (geometryDirty_ || vertexBuffer_->IsDataLost())
        return UPDATE_MAIN_THREAD;
    else
        return UPDATE_NONE;
}

void Drawable2D::SetUnitPerPixel(float unitPerPixel)
{
    unitPerPixel_ = Max(1.0f, unitPerPixel);
    MarkVerticesDirty();
    MarkGeometryDirty();
}

void Drawable2D::SetSprite(Sprite2D* sprite)
{
    if (sprite == sprite_)
        return;

    sprite_ = sprite;
    MarkVerticesDirty();
    MarkGeometryDirty();
    MarkMaterialDirty();
}

void Drawable2D::SetMaterial(Material* material)
{
    if (material_ != material)
    {
        material_ = material;
        MarkMaterialDirty();
    }
}

void Drawable2D::SetBlendMode(BlendMode mode)
{
    if (blendMode_ != mode)
    {
        blendMode_ = mode;
        MarkMaterialDirty();
    }
}

void Drawable2D::SetZValue(float zValue)
{
    zValue_ = zValue;

    MarkVerticesDirty();
    MarkGeometryDirty();
}

Material* Drawable2D::GetMaterial() const
{
    return material_;
}

void Drawable2D::SetSpriteAttr(ResourceRef value)
{
    ResourceCache* cache = GetSubsystem<ResourceCache>();

    if (value.type_ == Sprite2D::GetTypeStatic())
    {
        SetSprite(cache->GetResource<Sprite2D>(value.name_));
        return;
    }

    if (value.type_ == SpriteSheet2D::GetTypeStatic())
    {
        // value.name_ include sprite speet name and sprite name.
        Vector<String> names = value.name_.Split('@');
        if (names.Size() != 2)
            return;

        const String& spriteSheetName = names[0];
        const String& spriteName = names[1];

        SpriteSheet2D* spriteSheet = cache->GetResource<SpriteSheet2D>(spriteSheetName);
        if (!spriteSheet)
            return;

        SetSprite(spriteSheet->GetSprite(spriteName));
    }
}

ResourceRef Drawable2D::GetSpriteAttr() const
{
    SpriteSheet2D* spriteSheet = 0;
    if (sprite_)
        spriteSheet = sprite_->GetSpriteSheet();

    if (!spriteSheet)
        return GetResourceRef(sprite_, Sprite2D::GetTypeStatic());

    // Combine sprite sheet name and sprite name as resource name.
    return ResourceRef(spriteSheet->GetType(), spriteSheet->GetName() + "@" + sprite_->GetName());
}

void Drawable2D::SetMaterialAttr(ResourceRef value)
{
    ResourceCache* cache = GetSubsystem<ResourceCache>();
    SetMaterial(cache->GetResource<Material>(value.name_));
}

ResourceRef Drawable2D::GetMaterialAttr() const
{
    return GetResourceRef(material_, Material::GetTypeStatic());
}

void Drawable2D::OnWorldBoundingBoxUpdate()
{
    if (verticesDirty_)
    {
        UpdateVertices();

        boundingBox_.Clear();
        for (unsigned i = 0; i < vertices_.Size(); ++i)
            boundingBox_.Merge(vertices_[i].position_);
    }

    worldBoundingBox_ = boundingBox_.Transformed(node_->GetWorldTransform());
}

void Drawable2D::UpdateMaterial(bool forceUpdate)
{
    if (!materialDirty_ && !forceUpdate)
        return;

    SharedPtr<Material> material;
    if (!material_)
    {
        material = new Material(context_);
        Technique* tech = new Technique(context_);
        Pass* pass = tech->CreatePass(PASS_ALPHA);
        pass->SetVertexShader("Basic");
        pass->SetVertexShaderDefines("DIFFMAP VERTEXCOLOR");

        pass->SetPixelShader("Basic");
        pass->SetPixelShaderDefines("DIFFMAP VERTEXCOLOR");
        
        pass->SetBlendMode(blendMode_);
        pass->SetDepthWrite(false);

        material->SetTechnique(0, tech);
        material->SetCullMode(CULL_NONE);
    }
    else
        material = material_->Clone();

    if (sprite_)
    {
        Texture2D* texture = sprite_->GetTexture();
        material->SetTexture(TU_DIFFUSE, texture);
    }

    batches_[0].material_ = material;
    materialDirty_ = false;
}

}