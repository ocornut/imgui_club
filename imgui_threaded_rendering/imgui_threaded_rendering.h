// Helper for staged/multi-threaded rendering v0.20, for Dear ImGui
// Get latest version at http://www.github.com/ocornut/imgui_club
// Licensed under The MIT License (MIT)

// Based on a discussion at https://github.com/ocornut/imgui/issues/1860#issuecomment-1927630727
// Since 1.92.0, textures also needs to be updated. See discussion at https://github.com/ocornut/imgui/issues/8597


/*

Index of this file:
// CHANGELOG
// USAGE
// ImDrawDataSnapshot - HEADERS
// ImDrawDataSnapshot - IMPLEMENTATION
// ImTextureQueue - HEADERS
// ImTextureQueue - IMPLEMENTATION

*/

//-----------------------------------------------------------------------------
// CHANGELOG
//-----------------------------------------------------------------------------

// - v0.20: (2026/07/22): added ImTextureQueue support.
// - v0.10: (2025/04/30): initial version. Not well tested.

//-----------------------------------------------------------------------------
// USAGE
//-----------------------------------------------------------------------------

/*

    // [Storage]
    ImDrawDataSnapshot  g_Snapshots[2];         // Keep snapshots persistent as we reuse buffers across frames.
    ImTextureQueue      g_TexQueue;
    std::mutex          g_TexQueueMutex;        // All calls to ImTextureQueue must be protected by a same mutex.

    // [Init]
    g_TexQueue.UpdateTexFunc = ImGui_ImplDX11_UpdateTexture;
    g_TexQueue.InFlightFrames = 3;

    // [Update thread] Acknowledge retired texture destroys.
    g_TexQueueMutex.lock();
    g_TexQueue.PreNewFrame();
    g_TexQueueMutex.unlock();
    ImGui::NewFrame();
    [...]
    ImGui::EndFrame();
    ImGui::Render();

    // [Update thread] Queue texture requests, then take/publish a snapshot of the ImDrawData
    // This ordering guarantees the render thread processes requests before rendering the snapshot referencing them.
    ImDrawData* draw_data = ImGui::GetDrawData();
    g_TexQueueMutex.lock();
    g_TexQueue.QueueRequests(draw_data);
    g_TexQueueMutex.unlock();
    ImDrawDataSnapshot* snapshot = &g_Snapshots[g_FrameIndex % 2];
    snapshot->SnapUsingSwap(draw_data, ImGui::GetTime());
    g_FrameIndex++;

    // [Render thread] After adopting the latest snapshot, before rendering it: process texture requests.
    // This also clears snapshot->DrawData.Textures so the backend render function won't process them.
    g_TexQueueMutex.lock();
    g_TexQueue.ProcessRequests(&snapshot->DrawData);
    g_TexQueueMutex.unlock();
    ImGui_ImplDX11_RenderDrawData(&snapshot->DrawData);

    // [Shutdown] After joining/stopping both threads
    g_TexQueue.Shutdown();
    for (ImDrawDataSnapshot& snapshot : g_Snapshots)
        snapshot.Clear(); // otherwise context will assert since 1.92.0
    ImGui_ImplDX11_Shutdown();
    ImGui::DestroyContext();

*/

// Multi-Viewport Support (Untested)
// - Generally tricky because of handling of ImGuiPlatformWindow.
//   - e.g. any interaction with viewports (e.g pos/size) are queued both ways and expected to process immediately.
// - ImTextureQueue: should work fine, as the texture queue is effectively associated to a context.
//   - Each function should be called only once.
//   - Each ImDrawData::Textures pointer MUST be nulled before passing to the backend Render function.
// - ImDrawDataSnapshot: app should package one snapshot per viewport.

#pragma once
#include "imgui_internal.h" // ImPool<>, ImHashData

//-----------------------------------------------------------------------------
// ImDrawDataSnapshot - HEADERS
//-----------------------------------------------------------------------------

struct ImDrawDataSnapshotEntry
{
    ImDrawList*     SrcCopy = NULL;     // Drawlist owned by main context
    ImDrawList*     OurCopy = NULL;     // Our copy
    double          LastUsedTime = 0.0;
};

struct ImDrawDataSnapshot
{
    // Members
    ImDrawData                      DrawData;
    ImPool<ImDrawDataSnapshotEntry> Cache;
    float                           MemoryCompactTimer = 20.0f; // Discard unused data after 20 seconds

    // Functions
    ~ImDrawDataSnapshot()           { Clear(); }
    void                            Clear();
    void                            SnapUsingSwap(ImDrawData* src, double current_time); // Efficient snapshot by swapping data, meaning "src" is unusable.
    //void                          SnapUsingCopy(ImDrawData* src, double current_time); // Deep-copy snapshot. Probably not needed.

    // Internals
    // FIXME: Could store an ID in ImDrawList to make this easier for user.
    ImGuiID                         GetDrawListID(ImDrawList* src_list) { return ImHashData(&src_list, sizeof(src_list)); }     // Hash pointer
    ImDrawDataSnapshotEntry*        GetOrAddEntry(ImDrawList* src_list) { return Cache.GetOrAddByKey(GetDrawListID(src_list)); }
};

//-----------------------------------------------------------------------------
// ImDrawDataSnapshot - IMPLEMENTATION
//-----------------------------------------------------------------------------

inline void ImDrawDataSnapshot::Clear()
{
    for (int n = 0; n < Cache.GetMapSize(); n++)
        if (ImDrawDataSnapshotEntry* entry = Cache.TryGetMapData(n))
            IM_DELETE(entry->OurCopy);
    Cache.Clear();
    DrawData.Clear();
}

inline void ImDrawDataSnapshot::SnapUsingSwap(ImDrawData* src, double current_time)
{
    ImDrawData* dst = &DrawData;
    IM_ASSERT(src != dst && src->Valid);

    // Copy all fields except CmdLists[]
    ImVector<ImDrawList*> backup_draw_list;
    backup_draw_list.swap(src->CmdLists);
    IM_ASSERT(src->CmdLists.Data == NULL);
    *dst = *src;
    backup_draw_list.swap(src->CmdLists);

    // Swap and mark as used
    for (ImDrawList* src_list : src->CmdLists)
    {
        ImDrawDataSnapshotEntry* entry = GetOrAddEntry(src_list);
        if (entry->OurCopy == NULL)
        {
            entry->SrcCopy = src_list;
            entry->OurCopy = IM_NEW(ImDrawList)(src_list->_Data);
        }
        IM_ASSERT(entry->SrcCopy == src_list);
        entry->SrcCopy->CmdBuffer.swap(entry->OurCopy->CmdBuffer); // Cheap swap
        entry->SrcCopy->IdxBuffer.swap(entry->OurCopy->IdxBuffer);
        entry->SrcCopy->VtxBuffer.swap(entry->OurCopy->VtxBuffer);
        entry->SrcCopy->CmdBuffer.reserve(entry->OurCopy->CmdBuffer.Capacity); // Preserve bigger size to avoid reallocs for two consecutive frames
        entry->SrcCopy->IdxBuffer.reserve(entry->OurCopy->IdxBuffer.Capacity);
        entry->SrcCopy->VtxBuffer.reserve(entry->OurCopy->VtxBuffer.Capacity);
        entry->LastUsedTime = current_time;
        dst->CmdLists.push_back(entry->OurCopy);
    }

    // Cleanup unused data
    const double gc_threshold = current_time - MemoryCompactTimer;
    for (int n = 0; n < Cache.GetMapSize(); n++)
        if (ImDrawDataSnapshotEntry* entry = Cache.TryGetMapData(n))
        {
            if (entry->LastUsedTime > gc_threshold)
                continue;
            IM_DELETE(entry->OurCopy);
            Cache.Remove(GetDrawListID(entry->SrcCopy), entry);
        }
};

//-----------------------------------------------------------------------------
// ImTextureQueue - HEADERS
//-----------------------------------------------------------------------------

// The update thread acts as a proxy backend: it acknowledges all texture requests immediately,
// so core lib effectively runs against a regular lock-step backend.
// Each texture gets a persistent record holding a staging mirror.
//
// Important: all calls needs to be protected by the same mutex. We don't provide the mutex for portability reasons.
// 
// (1) Update thread: ImTextureQueue::PreNewFrame()
//  - Call before ImGui::NewFrame()
//  - Acknowledges destroys retired by the render thread, so that core can destroy ImTextureData
//    during the NewFrame() call that follows.
//
// (2) Update thread: ImTextureQueue::QueueRequests()
//  - Call after ImGui::Render() and before publishing the ImDrawData snapshot.
//    Merges pending work into each texture's staging mirror, acknowledging immediately for core lib.
//  - WantCreate: initialize record + mirror, set tex->Status = OK immediately.
//  - WantUpdates: merge rects + union bounding box over the pending ones.
//  - WantDestroy: two steps:
//    - stamps our record but do not acknowledge yet. The mirror is not yet touched.
//    - a pending create/update is processed first, and the destroy executes once the mirror is idle.
//    - later, when the render thread reports the GPU destroy done, the next ImTextureQueue::PreNewFrame()
//      call can acknowledge, and the next ImGui::NewFrame() can destroy ImTextureData.
//    - makes ImDrawCmd::GetTexID() safe on the render thread: a texture referenced by any live snapshot is never destroyed.
//
// (3) Render thread: ImTextureQueue::ProcessRequests()
//  - Call after adopting the latest ImDrawData snapshot and before rendering it.
//    This ordering guarantees every texture referenced by a snapshot is created/updated before drawing.
//  - This calls the backend ImGui_ImplXXXX_UpdateTexture() provided in ImTextureQueue::UpdateTexFunc.
//  - Destroys use a consumer-side retirement clock, NOT tex->UnusedFrames (as update rate can be != render rate).
//
// Zero pixel copies: the mirror shares tex->Pixels with the live texture. This is safe because:
//  - Pixels storage never moves nor resizes during a texture lifetime (a resize allocates a new texture),
//  - Updates only ever write to virgin regions, so rects captured at queue time are immutable,
//  - We keep the ImTextureData alive until the render thread is done with it.
//
// Known limitations:
//  - Assumes a single Dear ImGui context per atlas, and that all textures go through the queue.
//    - ProcessRequests() clears draw_data->Textures so the backend render function won't process textures itself.
//    - Using multi-contexts currently require their own atlas.
//  - tex->TexID is written once by the render thread (on create) while the update thread may read it
//    in NewFrame() asserts/debug logs: formally a data race, benign in practice (single transition
//    from ImTextureID_Invalid, and behavior cannot be affected because tex->QueueUserData != NULL).
//  - tex->BackendUserData is not published to main thread (could be useful for debugging).
//  - If your app keeps submitting frames but does not render them: prefer still publishing/adopting
//    ImDrawData snapshots, and calling  ProcessRequests() on it in order for requests to not accumulate.
//    Or better: stop submitting Dear ImGui frames if you can't render them.
//  - Doesn't handle mid-run ImGui_ImplXXXX_InvalidateDeviceObjects() (device loss).

// Signature for e.g. ImGui_ImplDX11_UpdateTexture
typedef void (*ImGui_ImplUpdateTextureFunc)(ImTextureData* tex);
struct ImTextureQueueEntry;

// All calls should be guarded by caller's mutex.
struct ImTextureQueue
{
    // Members
    int                             InFlightFrames = 2;     // Number of extra in-flight frames.
    ImGui_ImplUpdateTextureFunc     UpdateTexFunc = nullptr;// Pointer to backend function (e.g. ImGui_ImplDX11_UpdateTexture)

    // Internal Members
    ImVector<ImTextureQueueEntry*>  Textures;               // [Internal] All per-texture records. List modified by update thread only (+ Shutdown); record/mirror fields are written by both threads under caller's mutex.
    int                             RenderFrame = 0;        // [Internal] Number of ProcessRequests() calls (~Presents)

    // Functions
    void    PreNewFrame();                                  // (Update thread) Call before NewFrame()
    void    QueueRequests(ImDrawData* draw_data);           // (Update thread) Call after Render(). Queue + acknowledge requests for textures referenced by this frame's draw data.
    void    ProcessRequests(ImDrawData* draw_data);         // (Render thread) Process staged requests. Call once per render frame, before rendering any viewports.
    void    Shutdown();                                     // Shutdown. Call after joining threads.
};

// Persistent per-texture state + staging mirror. Pointed to by ImTextureData::QueueUserData in live textures.
struct ImTextureQueueEntry
{
    ImTextureData*      TexLive = NULL;                     // Live texture owned by core.
    ImTextureData       TexCopy;                            // Staging mirror.
    int                 DestroyFrameCount = -1;             // ImDrawData::FrameCount of the frame which queued the destroy (-1 = none queued).
    int                 DestroyArmedAtRenderFrame = -1;     // Value of RenderFrame when the retirement countdown started.

    ~ImTextureQueueEntry() { TexCopy.Pixels = NULL; TexCopy.Updates.clear(); } // Pixels are owned by the live texture.
};

//-----------------------------------------------------------------------------
// ImTextureQueue - IMPLEMENTATION
//-----------------------------------------------------------------------------

// (Update thread)
inline void ImTextureQueue::PreNewFrame()
{
    for (int entry_n = 0; entry_n < Textures.Size; entry_n++)
    {
        ImTextureQueueEntry* entry = Textures[entry_n];
        if (entry->TexCopy.Status != ImTextureStatus_Destroyed)
            continue;

        // Render thread fully retired the texture: acknowledge so core can destroy the texture in NewFrame().
        ImTextureData* tex_live = entry->TexLive;
        IM_ASSERT(tex_live->Status == ImTextureStatus_WantDestroy);
        tex_live->WantDestroyNextFrame = true; // As UserTextures[] may SetStatus(ImTextureStatus_WantDestroy) directly.
        tex_live->SetStatus(ImTextureStatus_Destroyed);
        tex_live->SetTexID(ImTextureID_Invalid);
        tex_live->QueueUserData = NULL;
        Textures.erase(Textures.Data + entry_n--);
        IM_DELETE(entry);
    }
}

// (Update thread)
inline void ImTextureQueue::QueueRequests(ImDrawData* draw_data)
{
    IM_ASSERT(draw_data->Valid && draw_data->Textures != NULL);
    const int frame_count = draw_data->FrameCount;
    for (ImTextureData* tex_live : *draw_data->Textures)
    {
        if (tex_live->Status == ImTextureStatus_OK)
            continue;
        ImTextureQueueEntry* entry = (ImTextureQueueEntry*)tex_live->QueueUserData;
        if (tex_live->Status == ImTextureStatus_WantCreate)
        {
            // Initialize record + staging mirror.
            IM_ASSERT(entry == NULL && tex_live->TexID == ImTextureID_Invalid && tex_live->BackendUserData == NULL); // What the backend expects to see on a create request.
            entry = IM_NEW(ImTextureQueueEntry)();
            entry->TexLive = tex_live;
            entry->TexCopy = *tex_live; // Copy main struct. Pixels are pointing to original source.
            Textures.push_back(entry);
            tex_live->QueueUserData = entry; // Mark texture as referenced by our queue. This prevents core from deleting it when BackendUserData is not set.
            tex_live->Status = ImTextureStatus_OK;
        }
        else if (tex_live->Status == ImTextureStatus_WantUpdates)
        {
            IM_ASSERT(entry != NULL && "Texture was not created through this queue?");
            ImTextureData* tex_copy = &entry->TexCopy;
            if (tex_copy->Updates.Size == 0)
            {
                tex_copy->UpdateRect.x = tex_copy->UpdateRect.y = (unsigned short)~0;
                tex_copy->UpdateRect.w = tex_copy->UpdateRect.h = 0;
            }
            for (const ImTextureRect& r : tex_live->Updates)
                ImTextureDataQueueUpload(tex_copy, r.x, r.y, r.w, r.h);
            tex_copy->UseColors = tex_live->UseColors;
            tex_live->Status = ImTextureStatus_OK;
        }
        else if (tex_live->Status == ImTextureStatus_WantDestroy)
        {
            if (entry == NULL)
            {
                tex_live->Status = ImTextureStatus_Destroyed; // Never seen in WantCreate state. No snapshot can reference it, core can delete it fine.
                continue;
            }
            if (entry->DestroyFrameCount >= 0)
                continue; // Already tracked: either waiting for render thread, or retired (TexCopy.Status == Destroyed) waiting for PreNewFrame() to acknowledge.

            // Stamp the record, but do not acknowledge yet. The mirror is intentionally not touched here:
            // a pending create/update is processed first, and the destroy executes once the mirror is idle.
            // It stays correct for apps rendering every published snapshots. Avoids assuming the app only renders the latest snapshot!
            entry->DestroyFrameCount = frame_count;
        }
    }
}

// (Render thread)
inline void ImTextureQueue::ProcessRequests(ImDrawData* draw_data)
{
    IM_ASSERT(InFlightFrames >= 0);
    IM_ASSERT(UpdateTexFunc != NULL);

    const int adopted_frame_count = (draw_data != NULL) ? draw_data->FrameCount : INT_MAX; // NULL = shutdown drain: nothing will be rendered anymore.
    if (draw_data != NULL)
        draw_data->Textures = NULL; // We handle texture processing: make sure the backend's RenderDrawData() doesn't. Also this pointer aliases the update thread's live list, it must not be dereferenced on this thread.
    RenderFrame++;

    // Process each record's pending state.
    for (ImTextureQueueEntry* entry : Textures)
    {
        ImTextureData* tex_copy = &entry->TexCopy;

        // Process pending create/update. Call backend function on the mirror.
        if (tex_copy->Status == ImTextureStatus_WantCreate || tex_copy->Status == ImTextureStatus_WantUpdates)
        {
            ImTextureStatus status_request = tex_copy->Status;
            //IMGUI_DEBUG_PRINTF("Render [%05d]:\n", adopted_frame_count);
            UpdateTexFunc(tex_copy);
            IM_ASSERT(tex_copy->Status == ImTextureStatus_OK && "Backend must honor WantCreate/WantUpdates immediately!");
            if (status_request == ImTextureStatus_WantCreate)
                entry->TexLive->SetTexID(tex_copy->TexID); // Publish TexID: the only render-thread write to a live ImTextureData. ImDrawCmd::GetTexID() reads are done on this thread, always after this point.
            tex_copy->Updates.resize(0);
        }

        // Execute pending destroy once the mirror is idle, on the consumer-side retirement clock (see notes above).
        // (a Destroyed mirror doesn't re-enter here: it fails the == OK check until PreNewFrame() deletes the record)
        if (tex_copy->Status == ImTextureStatus_OK && entry->DestroyFrameCount >= 0)
        {
            if (adopted_frame_count < entry->DestroyFrameCount)
                continue; // A snapshot which may still reference this texture is or may become active.
            if (entry->DestroyArmedAtRenderFrame < 0)
                entry->DestroyArmedAtRenderFrame = RenderFrame;
            if (RenderFrame < entry->DestroyArmedAtRenderFrame + InFlightFrames)
                continue; // Wait for in-flight GPU frames to retire.

            tex_copy->Status = ImTextureStatus_WantDestroy;
            tex_copy->WantDestroyNextFrame = true;    // Make the SetStatus(_Destroyed) call in backend stick.
            tex_copy->UnusedFrames = 99;              // Deferring is our job: pass any backend-side UnusedFrames check.
            //IMGUI_DEBUG_PRINTF("Render [%05d]:\n", adopted_frame_count);
            UpdateTexFunc(tex_copy);
            IM_ASSERT(tex_copy->Status == ImTextureStatus_Destroyed && "Backend must honor WantDestroy immediately!");
        }
    }
}

inline void ImTextureQueue::Shutdown()
{
    InFlightFrames = 0;
    if (UpdateTexFunc != NULL)
        ProcessRequests(NULL);  // Process all pending work, executing deferred destroys immediately.
    PreNewFrame();              // Acknowledge retired destroys + delete their records.

    for (ImTextureQueueEntry* entry : Textures)
    {
        // Remaining textures are live and idle: hand the backend user data back to the live texture,
        // so that e.g. ImGui_ImplDX11_Shutdown() can destroy them through the regular single-threaded path.
        ImTextureData* tex_live = entry->TexLive;
        IM_ASSERT(entry->TexCopy.Status == ImTextureStatus_OK);
        tex_live->BackendUserData = entry->TexCopy.BackendUserData;
        tex_live->QueueUserData = NULL;
    }
    Textures.clear_delete();
}

//-----------------------------------------------------------------------------
