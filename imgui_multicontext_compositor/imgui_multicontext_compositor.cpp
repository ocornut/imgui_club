// Multi-Context Compositor v0.11, for Dear ImGui
// Get latest version at http://www.github.com/ocornut/imgui_club
// Licensed under The MIT License (MIT)

// Read header files for changelog, API usage and details.

#include "imgui_multicontext_compositor.h"
#include "imgui_internal.h" // ImGuiContext

//-----------------------------------------------------------------------------
// ImGuiMultiContextCompositor Implementation
//-----------------------------------------------------------------------------

void ImGuiMultiContextCompositor_AddContext(ImGuiMultiContextCompositor* mcc, ImGuiContext* ctx)
{
    IM_ASSERT(mcc->Contexts.contains(ctx) == false);
    mcc->Contexts.push_back(ctx);
    mcc->ContextsFrontToBack.push_back(ctx);
}

void ImGuiMultiContextCompositor_RemoveContext(ImGuiMultiContextCompositor* mcc, ImGuiContext* ctx)
{
    mcc->Contexts.find_erase(ctx);
    mcc->ContextsFrontToBack.find_erase(ctx);
}

static void ImGuiMultiContextCompositor_BringContextToFront(ImGuiMultiContextCompositor* mcc, ImGuiContext* ctx, ImGuiContext* ctx_to_keep_inputs_for)
{
    mcc->ContextsFrontToBack.find_erase(ctx);
    mcc->ContextsFrontToBack.push_front(ctx);

    for (ImGuiContext* other_ctx : mcc->ContextsFrontToBack)
        if (other_ctx != ctx && other_ctx != ctx_to_keep_inputs_for)
            other_ctx->IO.ClearInputKeys();
}

static bool ImGuiMultiContextCompositor_DragDropGetPayloadFromSourceContext(ImGuiMultiContextCompositor* mcc)
{
    ImGuiContext* src_ctx = mcc->CtxDragDropSrc;
    ImGuiPayload* dst_payload = &mcc->DragDropPayload;

    if (!src_ctx->DragDropActive)
        return false;
    if (src_ctx->DragDropSourceFlags & ImGuiDragDropFlags_PayloadNoCrossContext)
        return false;
    ImGuiPayload* src_payload = &src_ctx->DragDropPayload;
    *dst_payload = *src_payload;
    dst_payload->Data = ImGui::MemAlloc(src_payload->DataSize);
    memcpy(dst_payload->Data, src_payload->Data, src_payload->DataSize);
    return true;
}

static void MultiContext_DragDropSetPayloadToDestContext(ImGuiMultiContextCompositor* mcc, ImGuiContext* dst_ctx)
{
    IM_ASSERT(dst_ctx == ImGui::GetCurrentContext());
    ImGuiPayload* src_payload = &mcc->DragDropPayload;
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceExtern | ImGuiDragDropFlags_SourceNoPreviewTooltip))
    {
        ImGui::SetDragDropPayload(src_payload->DataType, src_payload->Data, src_payload->DataSize);
        ImGui::EndDragDropSource();
    }
}

static void MultiContext_DragDropFreePayload(ImGuiPayload* src_payload)
{
    ImGui::MemFree(src_payload->Data);
    src_payload->Data = NULL;
}

void ImGuiMultiContextCompositor_PreNewFrameUpdateAll(ImGuiMultiContextCompositor* mcc)
{
    // Clear transient data
    mcc->CtxMouseFirst = NULL;
    mcc->CtxMouseExclusive = NULL;
    mcc->CtxMouseShape = NULL;
    mcc->CtxKeyboardExclusive = NULL;
    mcc->CtxDragDropSrc = NULL;
    mcc->CtxDragDropDst = NULL;
    mcc->DragDropPayload.Clear();

    // Sync point (before NewFrame calls)
    // PASS 1:
    // - Find out who will receive mouse position (one or multiple contexts)
    // - FInd out who will change mouse cursor (one context)
    // - Find out who has an active drag and drop
    for (ImGuiContext* ctx : mcc->ContextsFrontToBack)
    {
#ifdef IMGUI_HAS_DOCK
        // When hovering a secondary viewport, only enable mouse for the context owning it
        // We specifically use 'ctx->IO.MouseHoveredViewport' (current, submitted by backend) and not 'ctx->MouseLastHoveredViewport' (last valid one)
        if (mcc->CtxMouseExclusive == NULL && ctx->IO.MouseHoveredViewport != 0)
        {
            ImGuiViewport* hovered_viewport = NULL;
            for (ImGuiViewport* viewport : ctx->PlatformIO.Viewports)
                if (viewport->ID == ctx->IO.MouseHoveredViewport)
                    hovered_viewport = viewport;
            if (hovered_viewport != NULL && (hovered_viewport->Flags & ImGuiViewportFlags_CanHostOtherWindows) == 0)
                mcc->CtxMouseExclusive = ctx;
        }

        // When a secondary viewport is focused, only enable keyboard for the context owning it.
        if (mcc->CtxKeyboardExclusive == NULL && ctx->NavWindow != NULL)
            if (ImGuiViewport* viewport = ctx->NavWindow->Viewport)
                if ((viewport->Flags & ImGuiViewportFlags_IsFocused) && (viewport->Flags & ImGuiViewportFlags_CanHostOtherWindows) == 0)
                    mcc->CtxKeyboardExclusive = ctx;
#endif

        // When hovering a main/shared viewport,
        // - feed mouse front-to-back until reaching context that has io.WantCaptureMouse.
        // - track second context to pass drag and drop payload
        if (ctx->IO.WantCaptureMouse && mcc->CtxMouseFirst == NULL)
            mcc->CtxMouseFirst = ctx;
        if (ctx->HoveredWindowBeforeClear != NULL && mcc->CtxDragDropDst == NULL)
            mcc->CtxDragDropDst = ctx;

        // Who owns mouse shape?
        if (mcc->CtxMouseShape == NULL && ctx->MouseCursor != ImGuiMouseCursor_Arrow)
            mcc->CtxMouseShape = ctx;

        // Who owns drag and drop source?
        if (ctx->DragDropActive == true && (ctx->DragDropSourceFlags & ImGuiDragDropFlags_SourceExtern) == 0 && mcc->CtxDragDropSrc == NULL)
            mcc->CtxDragDropSrc = ctx;
        else if (ctx->DragDropActive == false && mcc->CtxDragDropSrc == ctx)
            mcc->CtxDragDropSrc = NULL;
    }

    // If no secondary viewport are focused, we'll keep keyboard to top-most context
    if (mcc->CtxKeyboardExclusive == NULL)
        mcc->CtxKeyboardExclusive = mcc->ContextsFrontToBack.front();

    // Deep copy payload for replication
    if (mcc->CtxDragDropSrc)
        ImGuiMultiContextCompositor_DragDropGetPayloadFromSourceContext(mcc);
    if (mcc->CtxDragDropDst && mcc->DragDropPayload.Data == NULL)
        mcc->CtxDragDropDst = NULL;

    // Bring drag target context to front when using DragDropHold press
    // FIXME-MULTICONTEXT: Works but change of order means source tooltip not visible anymore...
    // - Solution 1 ? if user code always submitted drag and drop tooltip derived from payload data
    //   instead of submitting at drag source location, this wouldn't be a problem at the front
    //   most context could always display the tooltip. But it's a constraint.
    // - Solution 2 ? would be a more elaborate composited rendering, where top layer (tooltip)
    //   of one ImDrawData would be moved to another ImDrawData.
    // - Solution 3 ? somehow find a way to enforce tooltip always on own viewport, always on top?
    // Ultimately this is not so important, it's already quite a fun luxury to have cross context DND.
#if 0
    if (mcc->CtxDragDropDst && mcc->CtxDragDropDst != mcc->ContextsFrontToBack.front())
        if (mcc->CtxDragDropDst->DragDropHoldJustPressedId != 0)
            ImGuiMultiContextCompositor_BringContextToFront(mcc, mcc->CtxDragDropDst, mcc->ContextsFrontToBack.front());
#endif

    // PASS 2:
    // - Enable/disable mouse interactions on selected contexts.
    // - Enable/disable mouse cursor change so only 1 context can do it.
    // - Bring a context to front whenever clicked any of its windows.
    bool is_above_ctx_with_mouse_first = true;
    for (ImGuiContext* ctx : mcc->ContextsFrontToBack)
    {
        ImGuiIO& io = ctx->IO;
        const bool ctx_is_front = (ctx == mcc->ContextsFrontToBack.front());

        // Focused secondary viewport or top-most context in shared viewport gets keyboard
        if (mcc->CtxKeyboardExclusive == ctx)
            io.ConfigFlags &= ~ImGuiConfigFlags_NoKeyboard; // Allow keyboard interactions
        else
            io.ConfigFlags |= ImGuiConfigFlags_NoKeyboard; // Disable keyboard interactions

        // Top-most context with MouseCursor shape request gets it
        if (mcc->CtxMouseShape == NULL || mcc->CtxMouseShape == ctx)
            io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange; // Allow mouse cursor changes
        else
            io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange; // Disable mouse cursor changes

        if (mcc->CtxMouseExclusive != NULL)
        {
            // Single context gets mouse interactions
            if (mcc->CtxMouseExclusive == ctx)
                io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse; // Allow mouse interactions
            else
                io.ConfigFlags |= ImGuiConfigFlags_NoMouse; // Disable mouse interactions
        }
        else
        {
            // Top-most io.WantCaptureMouse context & anything above it gets mouse interactions
            if (is_above_ctx_with_mouse_first || mcc->CtxDragDropDst == ctx)
                io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse; // Allow mouse interactions
            else
                io.ConfigFlags |= ImGuiConfigFlags_NoMouse; // Disable mouse interactions
        }

        // Bring to front on click
        if ((mcc->CtxMouseExclusive == ctx || mcc->CtxMouseFirst == ctx) && !ctx_is_front)
        {
            bool any_mouse_clicked = false; // conceptually a ~ImGui::IsAnyMouseClicked(), not worth adding to API.
            for (bool clicked : io.MouseClicked)
                any_mouse_clicked |= clicked;
            if (any_mouse_clicked)
                ImGuiMultiContextCompositor_BringContextToFront(mcc, ctx, NULL);
        }

        if (mcc->CtxMouseFirst == ctx)
            is_above_ctx_with_mouse_first = false;
    }
}

// This could technically be registered as a hook, but it would make things too magical.
void ImGuiMultiContextCompositor_PostNewFrameUpdateOne(ImGuiMultiContextCompositor* mcc, ImGuiContext* ctx)
{
    // Propagate drag and drop
    // (against all odds since we are only READING from 'mcc' and writing to our target
    // context this should be parallel/threading friendly)
    if (mcc->CtxDragDropDst == ctx && mcc->CtxDragDropDst != mcc->CtxDragDropSrc)
        MultiContext_DragDropSetPayloadToDestContext(mcc, ctx);
}

void ImGuiMultiContextCompositor_PostEndFrameUpdateAll(ImGuiMultiContextCompositor* mcc)
{
    // Clear drag and drop payload
    if (mcc->DragDropPayload.Data != NULL)
        MultiContext_DragDropFreePayload(&mcc->DragDropPayload);
}

void ImGuiMultiContextCompositor_ShowDebugWindow(ImGuiMultiContextCompositor* mcc)
{
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->Pos);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1.0f, 1.0f, 1.0f, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
    ImGui::Begin("Multi-Context Compositor Overlay", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoInputs);
    ImGui::SeparatorText("Multi-Context Compositor");
    ImGui::Text("Front: %s", mcc->ContextsFrontToBack.front()->ContextName);
    ImGui::Text("MousePos first: %s", mcc->CtxMouseFirst ? mcc->CtxMouseFirst->ContextName : "");
    ImGui::Text("MousePos excl.: %s", mcc->CtxMouseExclusive ? mcc->CtxMouseExclusive->ContextName : "");
    ImGui::Text("Keyboard excl.: %s", mcc->CtxKeyboardExclusive ? mcc->CtxKeyboardExclusive->ContextName : "");
    ImGui::Text("DragDrop src: %s", mcc->CtxDragDropSrc ? mcc->CtxDragDropSrc->ContextName : "");
    ImGui::Text("DragDrop dst: %s", mcc->CtxDragDropDst ? mcc->CtxDragDropDst->ContextName : "");
    ImGui::End();
    ImGui::PopStyleColor(2);
}
