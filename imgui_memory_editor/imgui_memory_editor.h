// Mini memory editor for Dear ImGui (to embed in your game/tools)
// Animated GIF: https://twitter.com/ocornut/status/894242704317530112
// Get latest version at http://www.github.com/ocornut/imgui_club
//
// You can adjust the keyboard repeat delay/rate in ImGuiIO.
// The code assume a mono-space font for simplicity! If you don't use the default font, use ImGui::PushFont()/PopFont() to switch to a mono-space font before caling this.
//
// Usage:
//   static MemoryEditor mem_edit_1;                                            // store your state somewhere
//   mem_edit_1.DrawWindow("Memory Editor", mem_block, mem_block_size, 0x0000); // create a window and draw memory editor (if you already have a window, use DrawContents())
//
// Usage:
//   static MemoryEditor mem_edit_2;
//   ImGui::Begin("MyWindow")
//   mem_edit_2.DrawContents(this, sizeof(*this), (size_t)this);
//   ImGui::End();
//
// Changelog:
// - v0.10: initial version
// - v0.11: always refresh active text input with the latest byte from source memory if it's not being edited.
// - v0.12: added OptMidRowsCount to allow extra spacing every XX rows.
// - v0.13: added optional ReadFn/WriteFn handlers to access memory via a function. various warning fixes for 64-bits.
// - v0.14: added GotoAddr member, added GotoAddrAndHighlight() and highlighting. fixed minor scrollbar glitch when resizing.
// - v0.15: added maximum window width. minor optimization.
// - v0.16: added OptGreyOutZeroes option. various sizing fixes when resizing using the "Rows" drag.
// - v0.17: added HighlightFn handler for optional non-contiguous highlighting.
// - v0.18: fixes for displaying 64-bits addresses, fixed mouse click gaps introduced in recent changes, cursor tracking scrolling fixes.
// - v0.19: fixed auto-focus of next byte leaving WantCaptureKeyboard=false for one frame. we now capture the keyboard during that transition.
// - v0.20: added options menu. added OptShowAscii checkbox. added optional HexII display. split Draw() in DrawWindow()/DrawContents(). fixing glyph width. refactoring/cleaning code.
// - v0.21: fixes for using DrawContents() in our own window. fixed HexII to actually be useful and not on the wrong side.
// - v0.22: clicking Ascii view select the byte in the Hex view. Ascii view highlight selection.
// - v0.23: fixed right-arrow triggering a byte write.
// - v0.24: changed DragInt("Rows" to use a %d data format (which is desirable since imgui 1.61).
// - v0.25: fixed wording: all occurrences of "Rows" renamed to "Columns".
// - v0.26: fixed clicking on hex region
// - v0.27: added viewer for common data types
//
// Todo/Bugs:
// - Arrows are being sent to the InputText() about to disappear which for LeftArrow makes the text cursor appear at position 1 for one frame.
// - Using InputText() is awkward and maybe overkill here, consider implementing something custom.

#pragma once
#include <stdio.h>  // sprintf, scanf

struct MemoryEditor
{
    typedef unsigned char u8;

    // Settings
    bool            Open;                                   // = true   // set to false when DrawWindow() was closed. ignore if not using DrawWindow
    bool            ReadOnly;                               // = false  // set to true to disable any editing
    int             Cols;                                   // = 16     //
    bool            OptShowAscii;                           // = true   //
    bool            OptShowHexII;                           // = false  //
    bool            OptGreyOutZeroes;                       // = true   //
    int             OptMidColsCount;                        // = 8      // set to 0 to disable extra spacing between every mid-cols
    int             OptAddrDigitsCount;                     // = 0      // number of addr digits to display (default calculated based on maximum displayed addr)
    ImU32           HighlightColor;                         //          // color of highlight
    u8              (*ReadFn)(const u8* data, size_t off);  // = NULL   // optional handler to read bytes
    void            (*WriteFn)(u8* data, size_t off, u8 d); // = NULL   // optional handler to write bytes
    bool            (*HighlightFn)(const u8* data, size_t off);//NULL   // optional handler to return Highlight property (to support non-contiguous highlighting)

    // State/Internals
    bool            ContentsWidthChanged;
    size_t          DataEditingAddr;
    bool            DataEditingTakeFocus;
    char            DataInputBuf[32];
    char            AddrInputBuf[32];
    size_t          GotoAddr;
    size_t          HighlightMin, HighlightMax;
    int             Endianess;
    int             IntWidth;
    int             IntFormat;

    MemoryEditor()
    {
        // Settings
        Open = true;
        ReadOnly = false;
        Cols = 16;
        OptShowAscii = true;
        OptShowHexII = false;
        OptGreyOutZeroes = true;
        OptMidColsCount = 8;
        OptAddrDigitsCount = 0;
        HighlightColor = IM_COL32(255, 255, 255, 40);
        ReadFn = NULL;
        WriteFn = NULL;
        HighlightFn = NULL;

        // State/Internals
        ContentsWidthChanged = false;
        DataEditingAddr = (size_t)-1;
        DataEditingTakeFocus = false;
        memset(DataInputBuf, 0, sizeof(DataInputBuf));
        memset(AddrInputBuf, 0, sizeof(AddrInputBuf));
        GotoAddr = (size_t)-1;
        HighlightMin = HighlightMax = (size_t)-1;
        Endianess = 0;
        IntWidth = 0;
        IntFormat = 2;
    }

    void GotoAddrAndHighlight(size_t addr_min, size_t addr_max)
    {
        GotoAddr = addr_min;
        HighlightMin = addr_min;
        HighlightMax = addr_max;
    }

    struct Sizes
    {
        int     AddrDigitsCount;
        float   LineHeight;
        float   GlyphWidth;
        float   HexCellWidth;
        float   SpacingBetweenMidCols;
        float   PosHexStart;
        float   PosHexEnd;
        float   PosAsciiStart;
        float   PosAsciiEnd;
        float   WindowWidth;
    };

    void CalcSizes(Sizes& s, size_t mem_size, size_t base_display_addr)
    {
        ImGuiStyle& style = ImGui::GetStyle();
        s.AddrDigitsCount = OptAddrDigitsCount;
        if (s.AddrDigitsCount == 0)
            for (size_t n = base_display_addr + mem_size - 1; n > 0; n >>= 4)
                s.AddrDigitsCount++;
        s.LineHeight = ImGui::GetTextLineHeight();
        s.GlyphWidth = ImGui::CalcTextSize("F").x + 1;                  // We assume the font is mono-space
        s.HexCellWidth = (float)(int)(s.GlyphWidth * 2.5f);             // "FF " we include trailing space in the width to easily catch clicks everywhere
        s.SpacingBetweenMidCols = (float)(int)(s.HexCellWidth * 0.25f); // Every OptMidColsCount columns we add a bit of extra spacing
        s.PosHexStart = (s.AddrDigitsCount + 2) * s.GlyphWidth;
        s.PosHexEnd = s.PosHexStart + (s.HexCellWidth * Cols);
        s.PosAsciiStart = s.PosAsciiEnd = s.PosHexEnd;
        if (OptShowAscii)
        {
            s.PosAsciiStart = s.PosHexEnd + s.GlyphWidth * 1;
            if (OptMidColsCount > 0)
                s.PosAsciiStart += ((Cols + OptMidColsCount - 1) / OptMidColsCount) * s.SpacingBetweenMidCols;
            s.PosAsciiEnd = s.PosAsciiStart + Cols * s.GlyphWidth;
        }
        s.WindowWidth = s.PosAsciiEnd + style.ScrollbarSize + style.WindowPadding.x * 2 + s.GlyphWidth;
    }

    int FormatSize(int format)
    {
        switch (format) {
        case 0: case 1: return 1;
        case 2: case 3: return 2;
        case 4: case 5: return 4;
        case 6: case 7: return 8;
        case 8: return 4;
        case 9: return 8;
        default: return 0;
        }
    }

    bool isBE()
    {
        uint16_t x = 1;
        char c[2];
        memcpy(c, &x, 2);
        return c[0];
    }

    static void *EndianessCopyBE(void *_dst, void *_src, size_t s, int isLE)
    {
        if (isLE) {
            uint8_t *dst = (uint8_t *)_dst;
            uint8_t *src = (uint8_t *)_src + s - 1;
            for (int i = 0, n = (int)s; i < n; ++i)
                memcpy(dst++, src--, 1);
            return _dst;
        } else {
            return memcpy(_dst, _src, s);
        }
    }

    static void *EndianessCopyLE(void *_dst, void *_src, size_t s, int isLE)
    {
        if (isLE) {
            return memcpy(_dst, _src, s);
        } else {
            uint8_t *dst = (uint8_t *)_dst;
            uint8_t *src = (uint8_t *)_src + s - 1;
            for (int i = 0, n = (int)s; i < n; ++i)
                memcpy(dst++, src--, 1);
            return _dst;
        }
    }

    void *EndianessCopy(void *dst, void *src, size_t size)
    {
        static void *(*fp)(void *, void *, size_t, int) = NULL;
        if (fp) return fp(dst, src, size, Endianess);
        if (isBE()) fp = EndianessCopyBE;
        else fp = EndianessCopyLE;
        return fp(dst, src, size, Endianess);
    }

    char const *GetBinary(uint8_t *buf, int width)
    {
        static char s[65];
        for (int j = 0, n = width / 8; j < n; ++j)
            for (int i = 0, m = 7; i < 8; ++i)
                s[(j*8)+m--] = ((buf[j] & (0x1 << i)) != 0) + 0x30;
        s[width+1] = 0;
        return s;
    }



#ifdef _MSC_VER
#define _PRISizeT   "IX"
#else
#define _PRISizeT   "zX"
#endif

    // Standalone Memory Editor window
    void DrawWindow(const char* title, u8* mem_data, size_t mem_size, size_t base_display_addr = 0x0000)
    {
        Sizes s;
        CalcSizes(s, mem_size, base_display_addr);
        ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(s.WindowWidth, FLT_MAX));

        Open = true;
        if (ImGui::Begin(title, &Open, ImGuiWindowFlags_NoScrollbar))
        {
            if (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) && ImGui::IsMouseClicked(1))
                ImGui::OpenPopup("context");
            DrawContents(mem_data, mem_size, base_display_addr);
            if (ContentsWidthChanged)
            {
                CalcSizes(s, mem_size, base_display_addr);
                ImGui::SetWindowSize(ImVec2(s.WindowWidth, ImGui::GetWindowSize().y));
            }
        }
        ImGui::End();
    }

    // Memory Editor contents only
    void DrawContents(u8* mem_data, size_t mem_size, size_t base_display_addr = 0x0000)
    {
        Sizes s;
        CalcSizes(s, mem_size, base_display_addr);
        ImGuiStyle& style = ImGui::GetStyle();

        // We begin into our scrolling region with the 'ImGuiWindowFlags_NoMove' in order to prevent click from moving the window.
        // This is used as a facility since our main click detection code doesn't assign an ActiveId so the click would normally be caught as a window-move.
        const float footer_height_to_reserve = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing() * 2.0f; // 1 separator, 2 input text
        ImGui::BeginChild("##scrolling", ImVec2(0, -footer_height_to_reserve), false, ImGuiWindowFlags_NoMove);
        ImDrawList* draw_list = ImGui::GetWindowDrawList();

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

        const int line_total_count = (int)((mem_size + Cols - 1) / Cols);
        ImGuiListClipper clipper(line_total_count, s.LineHeight);
        const size_t visible_start_addr = clipper.DisplayStart * Cols;
        const size_t visible_end_addr = clipper.DisplayEnd * Cols;

        bool data_next = false;

        if (ReadOnly || DataEditingAddr >= mem_size)
            DataEditingAddr = (size_t)-1;

        size_t data_editing_addr_backup = DataEditingAddr;
        size_t data_editing_addr_next = (size_t)-1;
        if (DataEditingAddr != (size_t)-1)
        {
            // Move cursor but only apply on next frame so scrolling with be synchronized (because currently we can't change the scrolling while the window is being rendered)
            if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_UpArrow)) && DataEditingAddr >= (size_t)Cols)          { data_editing_addr_next = DataEditingAddr - Cols; DataEditingTakeFocus = true; }
            else if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_DownArrow)) && DataEditingAddr < mem_size - Cols) { data_editing_addr_next = DataEditingAddr + Cols; DataEditingTakeFocus = true; }
            else if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_LeftArrow)) && DataEditingAddr > 0)               { data_editing_addr_next = DataEditingAddr - 1; DataEditingTakeFocus = true; }
            else if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_RightArrow)) && DataEditingAddr < mem_size - 1)   { data_editing_addr_next = DataEditingAddr + 1; DataEditingTakeFocus = true; }
        }
        if (data_editing_addr_next != (size_t)-1 && (data_editing_addr_next / Cols) != (data_editing_addr_backup / Cols))
        {
            // Track cursor movements
            const int scroll_offset = ((int)(data_editing_addr_next / Cols) - (int)(data_editing_addr_backup / Cols));
            const bool scroll_desired = (scroll_offset < 0 && data_editing_addr_next < visible_start_addr + Cols * 2) || (scroll_offset > 0 && data_editing_addr_next > visible_end_addr - Cols * 2);
            if (scroll_desired)
                ImGui::SetScrollY(ImGui::GetScrollY() + scroll_offset * s.LineHeight);
        }

        // Draw vertical separator
        ImVec2 window_pos = ImGui::GetWindowPos();
        if (OptShowAscii)
            draw_list->AddLine(ImVec2(window_pos.x + s.PosAsciiStart - s.GlyphWidth, window_pos.y), ImVec2(window_pos.x + s.PosAsciiStart - s.GlyphWidth, window_pos.y + 9999), ImGui::GetColorU32(ImGuiCol_Border));

        const ImU32 color_text = ImGui::GetColorU32(ImGuiCol_Text);
        const ImU32 color_disabled = OptGreyOutZeroes ? ImGui::GetColorU32(ImGuiCol_TextDisabled) : color_text;

        for (int line_i = clipper.DisplayStart; line_i < clipper.DisplayEnd; line_i++) // display only visible lines
        {
            size_t addr = (size_t)(line_i * Cols);
            ImGui::Text("%0*" _PRISizeT ": ", s.AddrDigitsCount, base_display_addr + addr);

            // Draw Hexadecimal
            for (int n = 0; n < Cols && addr < mem_size; n++, addr++)
            {
                float byte_pos_x = s.PosHexStart + s.HexCellWidth * n;
                if (OptMidColsCount > 0)
                    byte_pos_x += (n / OptMidColsCount) * s.SpacingBetweenMidCols;
                ImGui::SameLine(byte_pos_x);

                // Draw highlight
                if ((addr >= HighlightMin && addr < HighlightMax) || (HighlightFn && HighlightFn(mem_data, addr)))
                {
                    ImVec2 pos = ImGui::GetCursorScreenPos();
                    float highlight_width = s.GlyphWidth * 2;
                    bool is_next_byte_highlighted =  (addr + 1 < mem_size) && ((HighlightMax != (size_t)-1 && addr + 1 < HighlightMax) || (HighlightFn && HighlightFn(mem_data, addr + 1)));
                    if (is_next_byte_highlighted || (n + 1 == Cols))
                    {
                        highlight_width = s.HexCellWidth;
                        if (OptMidColsCount > 0 && n > 0 && (n + 1) < Cols && ((n + 1) % OptMidColsCount) == 0)
                            highlight_width += s.SpacingBetweenMidCols;
                    }
                    draw_list->AddRectFilled(pos, ImVec2(pos.x + highlight_width, pos.y + s.LineHeight), HighlightColor);
                }

                if (DataEditingAddr == addr)
                {
                    // Display text input on current byte
                    bool data_write = false;
                    ImGui::PushID((void*)addr);
                    if (DataEditingTakeFocus)
                    {
                        ImGui::SetKeyboardFocusHere();
                        ImGui::CaptureKeyboardFromApp(true);
                        sprintf(AddrInputBuf, "%0*" _PRISizeT, s.AddrDigitsCount, base_display_addr + addr);
                        sprintf(DataInputBuf, "%02X", ReadFn ? ReadFn(mem_data, addr) : mem_data[addr]);
                    }
                    ImGui::PushItemWidth(s.GlyphWidth * 2);
                    struct UserData
                    {
                        // FIXME: We should have a way to retrieve the text edit cursor position more easily in the API, this is rather tedious. This is such a ugly mess we may be better off not using InputText() at all here.
                        static int Callback(ImGuiTextEditCallbackData* data)
                        {
                            UserData* user_data = (UserData*)data->UserData;
                            if (!data->HasSelection())
                                user_data->CursorPos = data->CursorPos;
                            if (data->SelectionStart == 0 && data->SelectionEnd == data->BufTextLen)
                            {
                                // When not editing a byte, always rewrite its content (this is a bit tricky, since InputText technically "owns" the master copy of the buffer we edit it in there)
                                data->DeleteChars(0, data->BufTextLen);
                                data->InsertChars(0, user_data->CurrentBufOverwrite);
                                data->SelectionStart = 0;
                                data->SelectionEnd = data->CursorPos = 2;
                            }
                            return 0;
                        }
                        char   CurrentBufOverwrite[3];  // Input
                        int    CursorPos;               // Output
                    };
                    UserData user_data;
                    user_data.CursorPos = -1;
                    sprintf(user_data.CurrentBufOverwrite, "%02X", ReadFn ? ReadFn(mem_data, addr) : mem_data[addr]);
                    ImGuiInputTextFlags flags = ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_NoHorizontalScroll | ImGuiInputTextFlags_AlwaysInsertMode | ImGuiInputTextFlags_CallbackAlways;
                    if (ImGui::InputText("##data", DataInputBuf, 32, flags, UserData::Callback, &user_data))
                        data_write = data_next = true;
                    else if (!DataEditingTakeFocus && !ImGui::IsItemActive())
                        DataEditingAddr = data_editing_addr_next = (size_t)-1;
                    DataEditingTakeFocus = false;
                    ImGui::PopItemWidth();
                    if (user_data.CursorPos >= 2)
                        data_write = data_next = true;
                    if (data_editing_addr_next != (size_t)-1)
                        data_write = data_next = false;
                    int data_input_value;
                    if (data_write && sscanf(DataInputBuf, "%X", &data_input_value) == 1)
                    {
                        if (WriteFn)
                            WriteFn(mem_data, addr, (u8)data_input_value);
                        else
                            mem_data[addr] = (u8)data_input_value;
                    }
                    ImGui::PopID();
                }
                else
                {
                    // NB: The trailing space is not visible but ensure there's no gap that the mouse cannot click on.
                    u8 b = ReadFn ? ReadFn(mem_data, addr) : mem_data[addr];

                    if (OptShowHexII)
                    {
                        if ((b >= 32 && b < 128))
                            ImGui::Text(".%c ", b);
                        else if (b == 0xFF && OptGreyOutZeroes)
                            ImGui::TextDisabled("## ");
                        else if (b == 0x00)
                            ImGui::Text("   ");
                        else
                            ImGui::Text("%02X ", b);
                    }
                    else
                    {
                        if (b == 0 && OptGreyOutZeroes)
                            ImGui::TextDisabled("00 ");
                        else
                            ImGui::Text("%02X ", b);
                    }
                    if (!ReadOnly && ImGui::IsItemHovered() && ImGui::IsMouseClicked(0))
                    {
                        DataEditingTakeFocus = true;
                        data_editing_addr_next = addr;
                    }
                }
            }

            if (OptShowAscii)
            {
                // Draw ASCII values
                ImGui::SameLine(s.PosAsciiStart);
                ImVec2 pos = ImGui::GetCursorScreenPos();
                addr = line_i * Cols;
                ImGui::PushID(line_i);
                if (ImGui::InvisibleButton("ascii", ImVec2(s.PosAsciiEnd - s.PosAsciiStart, s.LineHeight)))
                {
                    DataEditingAddr = addr + (size_t)((ImGui::GetIO().MousePos.x - pos.x) / s.GlyphWidth);
                    DataEditingTakeFocus = true;
                }
                ImGui::PopID();
                for (int n = 0; n < Cols && addr < mem_size; n++, addr++)
                {
                    if (addr == DataEditingAddr)
                    {
                        draw_list->AddRectFilled(pos, ImVec2(pos.x + s.GlyphWidth, pos.y + s.LineHeight), ImGui::GetColorU32(ImGuiCol_FrameBg));
                        draw_list->AddRectFilled(pos, ImVec2(pos.x + s.GlyphWidth, pos.y + s.LineHeight), ImGui::GetColorU32(ImGuiCol_TextSelectedBg));
                    }
                    unsigned char c = ReadFn ? ReadFn(mem_data, addr) : mem_data[addr];
                    char display_c = (c < 32 || c >= 128) ? '.' : c;
                    draw_list->AddText(pos, (display_c == '.') ? color_disabled : color_text, &display_c, &display_c + 1);
                    pos.x += s.GlyphWidth;
                }
            }
        }
        clipper.End();
        ImGui::PopStyleVar(2);
        ImGui::EndChild();

        if (data_next && DataEditingAddr < mem_size)
        {
            DataEditingAddr = DataEditingAddr + 1;
            DataEditingTakeFocus = true;
        }
        else if (data_editing_addr_next != (size_t)-1)
        {
            DataEditingAddr = data_editing_addr_next;
        }

        ImGui::Separator();

        // Options menu
        if (ImGui::Button("Options"))
            ImGui::OpenPopup("context");
        if (ImGui::BeginPopup("context"))
        {
            ImGui::PushItemWidth(56);
            if (ImGui::DragInt("##cols", &Cols, 0.2f, 4, 32, "%d cols")) { ContentsWidthChanged = true; }
            ImGui::PopItemWidth();
            ImGui::Checkbox("Show HexII", &OptShowHexII);
            if (ImGui::Checkbox("Show Ascii", &OptShowAscii)) { ContentsWidthChanged = true; }
            ImGui::Checkbox("Grey out zeroes", &OptGreyOutZeroes);
            ImGui::EndPopup();
        }

        ImGui::SameLine();
        ImGui::Text("Range %0*" _PRISizeT "..%0*" _PRISizeT, s.AddrDigitsCount, base_display_addr, s.AddrDigitsCount, base_display_addr + mem_size - 1);
        ImGui::SameLine();
        ImGui::PushItemWidth((s.AddrDigitsCount + 1) * s.GlyphWidth + style.FramePadding.x * 2.0f);
        if (ImGui::InputText("##addr", AddrInputBuf, 32, ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue))
        {
            size_t goto_addr;
            if (sscanf(AddrInputBuf, "%" _PRISizeT, &goto_addr) == 1)
            {
                GotoAddr = goto_addr - base_display_addr;
                HighlightMin = HighlightMax = (size_t)-1;
            }
        }
        ImGui::PopItemWidth();

        if (GotoAddr != (size_t)-1)
        {
            if (GotoAddr < mem_size)
            {
                ImGui::BeginChild("##scrolling");
                ImGui::SetScrollFromPosY(ImGui::GetCursorStartPos().y + (GotoAddr / Cols) * ImGui::GetTextLineHeight());
                ImGui::EndChild();
                DataEditingAddr = GotoAddr;
                DataEditingTakeFocus = true;
            }
            GotoAddr = (size_t)-1;
        }


        ImGui::PushItemWidth((s.GlyphWidth * 6.0f) + style.FramePadding.x * 2.0f + style.ItemInnerSpacing.x);
        ImGui::Combo("##combo1", &Endianess, "LE\0BE\0\0");
        ImGui::PopItemWidth();

        ImGui::SameLine();
        ImGui::PushItemWidth((s.GlyphWidth * 10.0f) + style.FramePadding.x * 2.0f + style.ItemInnerSpacing.x);
        ImGui::Combo("##combo2", &IntWidth, "Int8\0UInt8\0Int16\0UInt16\0Int32\0UInt32\0Int64\0UInt64\0Float\0Double\0\0");
        ImGui::PopItemWidth();

        ImGui::SameLine();
        ImGui::PushItemWidth((s.GlyphWidth * 7.0f) + style.FramePadding.x * 2.0f + style.ItemInnerSpacing.x);
        ImGui::Combo("##combo3", &IntFormat, "Bin\0Oct\0Dec\0Hex\0\0");
        ImGui::PopItemWidth();

        ImGui::SameLine();
        if (data_editing_addr_backup != (size_t)-1) {
            uint8_t buf[8];
            int elem_size = FormatSize(IntWidth);
            size_t size = data_editing_addr_backup + elem_size > mem_size ? mem_size - data_editing_addr_backup : elem_size;
            if (ReadFn)
                for (int i = 0, n = (int)size; i < n; ++i)
                    buf[i] = ReadFn(mem_data, data_editing_addr_backup + i);
            else
                memcpy(buf, mem_data + data_editing_addr_backup, size);

            switch (IntWidth) {
            case 0: {
                int8_t int8 = 0;
                EndianessCopy(&int8, buf, size);
                switch (IntFormat) {
                case 0: ImGui::Text("%s", GetBinary(buf, 8)); break;
                case 1: ImGui::Text("%hho", int8); break;
                case 2: ImGui::Text("%hhd", int8); break;
                case 3: ImGui::Text("%hhx", int8); break;
                }
                break;
            }

            case 1: {
                uint8_t uint8 = 0;
                EndianessCopy(&uint8, buf, size);
                switch (IntFormat) {
                case 0: ImGui::Text("%s", GetBinary(buf, 8)); break;
                case 1: ImGui::Text("%hho", uint8); break;
                case 2: ImGui::Text("%hhu", uint8); break;
                case 3: ImGui::Text("%hhx", uint8); break;
                }
                break;
            }

            case 2: {
                int16_t int16 = 0;
                EndianessCopy(&int16, buf, size);
                switch (IntFormat) {
                case 0: ImGui::Text("%s", GetBinary(buf, 16)); break;
                case 1: ImGui::Text("%ho", int16); break;
                case 2: ImGui::Text("%hd", int16); break;
                case 3: ImGui::Text("%hx", int16); break;
                }
                break;
            }

            case 3: {
                uint16_t uint16 = 0;
                EndianessCopy(&uint16, buf, size);
                switch (IntFormat) {
                case 0: ImGui::Text("%s", GetBinary(buf, 16)); break;
                case 1: ImGui::Text("%ho", uint16); break;
                case 2: ImGui::Text("%hu", uint16); break;
                case 3: ImGui::Text("%hx", uint16); break;
                }
                break;
            }

            case 4: {
                int32_t int32 = 0;
                EndianessCopy(&int32, buf, size);
                switch (IntFormat) {
                case 0: ImGui::Text("%s", GetBinary(buf, 32)); break;
                case 1: ImGui::Text("%o", int32); break;
                case 2: ImGui::Text("%d", int32); break;
                case 3: ImGui::Text("%x", int32); break;
                }
                break;
            }

            case 5: {
                uint32_t uint32 = 0;
                EndianessCopy(&uint32, buf, size);
                switch (IntFormat) {
                case 0: ImGui::Text("%s", GetBinary(buf, 32)); break;
                case 1: ImGui::Text("%o", uint32); break;
                case 2: ImGui::Text("%u", uint32); break;
                case 3: ImGui::Text("%x", uint32); break;
                }
                break;
            }

            case 6: {
                int64_t int64 = 0;
                EndianessCopy(&int64, buf, size);
                switch (IntFormat) {
                case 0: ImGui::Text("%s", GetBinary(buf, 64)); break;
                case 1: ImGui::Text("%llo", (long long)int64); break;
                case 2: ImGui::Text("%lld", (long long)int64); break;
                case 3: ImGui::Text("%llx", (long long)int64); break;
                }
                break;
            }

            case 7: {
                uint64_t uint64 = 0;
                EndianessCopy(&uint64, buf, size);
                switch (IntFormat) {
                case 0: ImGui::Text("%s", GetBinary(buf, 64)); break;
                case 1: ImGui::Text("%llo", (long long)uint64); break;
                case 2: ImGui::Text("%llu", (long long)uint64); break;
                case 3: ImGui::Text("%llx", (long long)uint64); break;
                }
                break;
            }

            case 8: {
                float float32 = 0;
                EndianessCopy(&float32, buf, size);
                ImGui::Text("%a", float32);
                break;
            }

            case 9: {
                double float64 = 0;
                EndianessCopy(&float64, buf, size);
                ImGui::Text("%a", float64);
                break;
            }
            }
        } else {
            ImGui::Text("No selection");
        }

        // Notify the main window of our ideal child content size (FIXME: we are missing an API to get the contents size from the child)
        ImGui::SetCursorPosX(s.WindowWidth);
    }
};
#undef _PRISizeT
