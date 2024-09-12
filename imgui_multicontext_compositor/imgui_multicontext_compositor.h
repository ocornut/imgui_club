// Multi-Context Compositor v0.11, for Dear ImGui
// Get latest version at http://www.github.com/ocornut/imgui_club
// Licensed under The MIT License (MIT)

// When using and displaying multiple contexts simultaneously:
// - Manage z-order of contexts.
// - Manage input routing.
// - Allow drag and drop between contexts.
//
// Tips for using multiple contexts simultaneously:
// - Give each of them unique title bar colors.
// - Make sure they each have their individual .ini file.

// CHANGELOG:
// - v0.10: (2024/07/16): initial version. Requires dear imgui 1.90.9+.
// - v0.11: (2024/08/01): fixed an issue clicking between two secondary viewport of different contexts.
//                        fixed an issue routing keyboard to secondary viewports. [tom bui]

// TODO:
// - Ctrl+Tab could be multi-context aware
// - Focusing a context optionally shouldn't unfocus windows in other context

// USAGE:
/*
    // Store persistent state somewhere
    static ImGuiMultiContextCompositor g_mcc_instance;
    ...
    // Add your contexts
    ImGuiMultiContextCompositor_AddContext(mcc, ctx1); // Add update context
    ImGuiMultiContextCompositor_AddContext(mcc, ctx2); // Add rendering context
    ...
    // New Frame
    ImGuiMultiContextCompositor_PreNewFrameUpdateAll(mcc);
    ImGui::SetCurrentContext(ctx1);
    ImGui::NewFrame();
    ImGuiMultiContextCompositor_PostNewFrameUpdateOne(mcc);
    ...
    ImGui::SetCurrentContext(ctx2);
    ImGui::NewFrame();
    ImGuiMultiContextCompositor_PostNewFrameUpdateOne(mcc);
    ...
    // End of frame
    ImGui::Render()/ImGui::EndFrame();
    ImGuiMultiContextCompositor_PostEndFrameUpdateAll(mcc);
*/

#pragma once

#include "imgui.h"

struct ImGuiMultiContextCompositor
{
    // List of context + sorted front to back
    ImVector<ImGuiContext*> Contexts;
    ImVector<ImGuiContext*> ContextsFrontToBack;

    // [Internal]
    ImGuiContext*   CtxMouseFirst = NULL;       // When hovering a main/shared viewport, first context with io.WantCaptureMouse
    ImGuiContext*   CtxMouseExclusive = NULL;   // When hovering a secondary viewport
    ImGuiContext*   CtxMouseShape = NULL;       // Context owning mouse cursor shape
    ImGuiContext*   CtxKeyboardExclusive = NULL;// When focusing a secondary viewport
    ImGuiContext*   CtxDragDropSrc = NULL;      // Source context for drag and drop
    ImGuiContext*   CtxDragDropDst = NULL;      // When hovering a main/shared viewport, second context with io.WantCaptureMouse for Drag Drop target
    ImGuiPayload    DragDropPayload;            // Deep copy of drag and drop payload.
};

//-----------------------------------------------------------------------------
// ImGuiMultiContextCompositor Interface
//-----------------------------------------------------------------------------

// Add/remove context.
void ImGuiMultiContextCompositor_AddContext(ImGuiMultiContextCompositor* mcc, ImGuiContext* ctx);
void ImGuiMultiContextCompositor_RemoveContext(ImGuiMultiContextCompositor* mcc, ImGuiContext* ctx);

// Call at a shared sync point before calling NewFrame() on any context.
void ImGuiMultiContextCompositor_PreNewFrameUpdateAll(ImGuiMultiContextCompositor* mcc);

// Call after caling NewFrame() on a given context.
void ImGuiMultiContextCompositor_PostNewFrameUpdateOne(ImGuiMultiContextCompositor* mcc, ImGuiContext* ctx);

// Call at a shared sync point after calling EndFrame() on all contexts.
void ImGuiMultiContextCompositor_PostEndFrameUpdateAll(ImGuiMultiContextCompositor* mcc);

// Debug display
void ImGuiMultiContextCompositor_ShowDebugWindow(ImGuiMultiContextCompositor* mcc);
