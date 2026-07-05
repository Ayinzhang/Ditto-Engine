#pragma once

#include "../3rdParty/ImGui/imgui.h"
#include "../Engine/Graphics/RHI/IRenderer.h"

namespace EditorRHI
{
    inline void RenderTargetImageUV(Ditto::IRenderer* renderer, ImVec2& uv0, ImVec2& uv1)
    {
        if (renderer && renderer->Backend() == Ditto::RendererBackend::DirectX12)
        {
            uv0 = ImVec2(0.0f, 0.0f);
            uv1 = ImVec2(1.0f, 1.0f);
            return;
        }

        uv0 = ImVec2(0.0f, 1.0f);
        uv1 = ImVec2(1.0f, 0.0f);
    }
}
