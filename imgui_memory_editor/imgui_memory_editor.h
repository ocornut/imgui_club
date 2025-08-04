// Mini memory editor for Dear ImGui (to embed in your game/tools)
// Get latest version at http://www.github.com/ocornut/imgui_club
// Licensed under The MIT License (MIT)

// Right-click anywhere to access the Options menu!
// You can adjust the keyboard repeat delay/rate in ImGuiIO.
// The code assume a mono-space font for simplicity!
// If you don't use the default font, use ImGui::PushFont()/PopFont() to switch to a mono-space font before calling this.
//
// Usage:
//   // Create a window and draw memory editor inside it:
//   static MemoryEditor mem_edit_1;
//   static char data[0x10000];
//   size_t data_size = 0x10000;
//   mem_edit_1.DrawWindow("Memory Editor", data, data_size);
//
// Usage:
//   // If you already have a window, use DrawContents() instead:
//   static MemoryEditor mem_edit_2;
//   ImGui::Begin("MyWindow")
//   mem_edit_2.DrawContents(this, sizeof(*this), (size_t)this);
//   ImGui::End();
//
// Changelog:
// - v0.10: initial version
// - v0.23 (2017/08/17): added to github. fixed right-arrow triggering a byte write.
// - v0.24 (2018/06/02): changed DragInt("Rows" to use a %d data format (which is desirable since imgui 1.61).
// - v0.25 (2018/07/11): fixed wording: all occurrences of "Rows" renamed to "Columns".
// - v0.26 (2018/08/02): fixed clicking on hex region
// - v0.30 (2018/08/02): added data preview for common data types
// - v0.31 (2018/10/10): added OptUpperCaseHex option to select lower/upper casing display [@samhocevar]
// - v0.32 (2018/10/10): changed signatures to use void* instead of unsigned char*
// - v0.33 (2018/10/10): added OptShowOptions option to hide all the interactive option setting.
// - v0.34 (2019/05/07): binary preview now applies endianness setting [@nicolasnoble]
// - v0.35 (2020/01/29): using ImGuiDataType available since Dear ImGui 1.69.
// - v0.36 (2020/05/05): minor tweaks, minor refactor.
// - v0.40 (2020/10/04): fix misuse of ImGuiListClipper API, broke with Dear ImGui 1.79. made cursor position appears on left-side of edit box. option popup appears on mouse release. fix MSVC warnings where _CRT_SECURE_NO_WARNINGS wasn't working in recent versions.
// - v0.41 (2020/10/05): fix when using with keyboard/gamepad navigation enabled.
// - v0.42 (2020/10/14): fix for . character in ASCII view always being greyed out.
// - v0.43 (2021/03/12): added OptFooterExtraHeight to allow for custom drawing at the bottom of the editor [@leiradel]
// - v0.44 (2021/03/12): use ImGuiInputTextFlags_AlwaysOverwrite in 1.82 + fix hardcoded width.
// - v0.50 (2021/11/12): various fixes for recent dear imgui versions (fixed misuse of clipper, relying on SetKeyboardFocusHere() handling scrolling from 1.85). added default size.
// - v0.51 (2024/02/22): fix for layout change in 1.89 when using IMGUI_DISABLE_OBSOLETE_FUNCTIONS. (#34)
// - v0.52 (2024/03/08): removed unnecessary GetKeyIndex() calls, they are a no-op since 1.87.
// - v0.53 (2024/05/27): fixed right-click popup from not appearing when using DrawContents(). warning fixes. (#35)
// - v0.54 (2024/07/29): allow ReadOnly mode to still select and preview data. (#46) [@DeltaGW2])
// - v0.55 (2024/08/19): added BgColorFn to allow setting background colors independently from highlighted selection. (#27) [@StrikerX3]
//                       added MouseHoveredAddr public readable field. (#47, #27) [@StrikerX3]
//                       fixed a data preview crash with 1.91.0 WIP. fixed contiguous highlight color when using data preview.
//                       *BREAKING* added UserData field passed to all optional function handlers: ReadFn, WriteFn, HighlightFn, BgColorFn. (#50) [@silverweed]
// - v0.56 (2024/11/04): fixed MouseHovered, MouseHoveredAddr not being set when hovering a byte being edited. (#54)
// - v0.57 (2025/03/26): fixed warnings. using ImGui's ImSXX/ImUXX types instead of e.g. int32_t/uint32_t. (#56)
// - v0.58 (2025/03/31): fixed extraneous footer spacing (added in 0.51) breaking vertical auto-resize. (#53)
// - v0.59 (2025/04/08): fixed GotoAddrAndHighlight() not working if OptShowOptions is disabled.
//
// TODO:
// - This is generally old/crappy code, it should work but isn't very good.. to be rewritten some day.
// - PageUp/PageDown are not supported because we use _NoNav. This is a good test scenario for working out idioms of how to mix natural nav and our own...
// - Arrows are being sent to the InputText() about to disappear which for LeftArrow makes the text cursor appear at position 1 for one frame.
// - Using InputText() is awkward and maybe overkill here, consider implementing something custom.

#pragma once

#include <stdio.h>      // sprintf, scanf
#include <stdint.h>     // uint8_t, etc.
#include <ctype.h>      // isxdigit
#include <inttypes.h>   // PRIdPTR, SCNu64
#include <stdlib.h>     // malloc, free, abs
#include <string.h>     // memset, memcpy, strlen

#if defined(_MSC_VER) && !defined(snprintf)
#define ImSnprintf  _snprintf
#else
#define ImSnprintf  snprintf
#endif
#if defined(_MSC_VER) && !defined(__clang__)
#define _PRISizeT   "I"
#else
#define _PRISizeT   "z"
#endif

#if defined(_MSC_VER) || defined(_UCRT)
#pragma warning (push)
#pragma warning (disable: 4996) // warning C4996: 'sprintf': This function or variable may be unsafe.
#endif

#define IM_MIN(a, b) ((a) < (b) ? (a) : (b))
#define IM_MAX(a, b) ((a) >= (b) ? (a) : (b))

struct MemoryEditor
{
    enum DataFormat
    {
        DataFormat_Bin = 0,
        DataFormat_Dec = 1,
        DataFormat_Hex = 2,
        DataFormat_COUNT
    };

    // Settings
    bool            Open;                                       // = true   // set to false when DrawWindow() was closed. ignore if not using DrawWindow().
    bool            ReadOnly;                                   // = false  // disable any editing.
    int             Cols;                                       // = 16     // number of columns to display.
    bool            OptShowOptions;                             // = true   // display options button/context menu. when disabled, options will be locked unless you provide your own UI for them.
    bool            OptShowDataPreview;                         // = false  // display a footer previewing the decimal/binary/hex/float representation of the currently selected bytes.
    bool            OptShowHexII;                               // = false  // display values in HexII representation instead of regular hexadecimal: hide null/zero bytes, ascii values as ".X".
    bool            OptShowAscii;                               // = true   // display ASCII representation on the right side.
    bool            OptShowUtf8;                                // = false  // display UTF-8 representation on the right side.
    bool            OptGreyOutZeroes;                           // = true   // display null/zero bytes using the TextDisabled color.
    bool            OptUpperCaseHex;                            // = true   // display hexadecimal values as "FF" instead of "ff".
    int             OptMidColsCount;                            // = 8      // set to 0 to disable extra spacing between every mid-cols.
    int             OptAddrDigitsCount;                         // = 0      // number of addr digits to display (default calculated based on maximum displayed addr).
    float           OptFooterExtraHeight;                       // = 0      // space to reserve at the bottom of the widget to add custom widgets
    ImU32           HighlightColor;                             //          // background color of highlighted bytes.
    bool            OptAddrInputHex;                            // = true   // display address input in hexadecimal format
    bool            OptShowSearchPanel;                         // = false  // display search panel
    bool            OptSearchHex;                               // = true   // search in hex format
    bool            OptSearchText;                              // = true   // search in utf-8 format

    // Function handlers
    ImU8            (*ReadFn)(const ImU8* mem, size_t off, void* user_data);      // = 0      // optional handler to read bytes.
    void            (*WriteFn)(ImU8* mem, size_t off, ImU8 d, void* user_data);   // = 0      // optional handler to write bytes.
    bool            (*HighlightFn)(const ImU8* mem, size_t off, void* user_data); // = 0      // optional handler to return Highlight property (to support non-contiguous highlighting).
    ImU32           (*BgColorFn)(const ImU8* mem, size_t off, void* user_data);   // = 0      // optional handler to return custom background color of individual bytes.
    void*           UserData;                                                     // = NULL   // user data forwarded to the function handlers

    // Public read-only data
    bool            MouseHovered;                               // set when mouse is hovering a value.
    size_t          MouseHoveredAddr;                           // the address currently being hovered if MouseHovered is set.

    // [Internal State]
    bool            ContentsWidthChanged;
    size_t          DataPreviewAddr;
    size_t          DataEditingAddr;
    size_t          LastEditingAddr;
    bool            DataEditingTakeFocus;
    char            DataInputBuf[32];
    char            AddrInputBuf[32];
    char            SearchInputBuf[512];
    size_t          GotoAddr;
    size_t          HighlightMin, HighlightMax;
    int             PreviewEndianness;
    ImGuiDataType   PreviewDataType;
    bool            Selecting;
    size_t          SelectionAnchor;
    size_t          SelectionStart;
    size_t          SelectionEnd;
    bool            SelectionChanged;
    ImU32           SelectionColor;                             // background color of selected bytes.
    bool            SearchRequested;
    ImU8*           SearchPattern;                              // search pattern bytes
    size_t          SearchPatternSize;                          // size of search pattern array
    void*           MemData;
    size_t          MemSize;
    float           TargetScrollY;                              // Track current scroll position

    MemoryEditor()
    {
        // Settings
        Open = true;
        ReadOnly = false;
        Cols = 16;
        OptShowOptions = true;
        OptShowDataPreview = false;
        OptShowHexII = false;
        OptShowAscii = true;
        OptShowUtf8 = false;
        OptGreyOutZeroes = true;
        OptUpperCaseHex = true;
        OptMidColsCount = 8;
        OptAddrDigitsCount = 0;
        OptFooterExtraHeight = 0.0f;
        HighlightColor = IM_COL32(255, 255, 255, 50);
        OptAddrInputHex = true;
        OptShowSearchPanel = false;
        OptSearchHex = true;
        OptSearchText = false;
        ReadFn = nullptr;
        WriteFn = nullptr;
        HighlightFn = nullptr;
        BgColorFn = nullptr;
        UserData = nullptr;

        // State/Internals
        ContentsWidthChanged = false;
        DataPreviewAddr = DataEditingAddr = LastEditingAddr = (size_t)-1;
        DataEditingTakeFocus = false;
        memset(DataInputBuf, 0, sizeof(DataInputBuf));
        memset(AddrInputBuf, 0, sizeof(AddrInputBuf));
        memset(SearchInputBuf, 0, sizeof(SearchInputBuf));
        GotoAddr = (size_t)-1;
        MouseHovered = false;
        MouseHoveredAddr = 0;
        HighlightMin = HighlightMax = (size_t)-1;
        PreviewEndianness = 0;
        PreviewDataType = ImGuiDataType_S32;
        Selecting = false;
        SelectionAnchor = (size_t)-1;
        SelectionStart = SelectionEnd = (size_t)-1;
        SelectionChanged = false;
        SelectionColor = IM_COL32(100, 100, 255, 80);
        SearchRequested = false;
        SearchPattern = nullptr;
        SearchPatternSize = 0;
        TargetScrollY = 0.0f;
    }

    ~MemoryEditor()
    {
        if (SearchPattern != nullptr)
            free(SearchPattern);
    }

    static inline float Saturate(float f)
    {
        return (f < 0.0f) ? 0.0f : (f > 1.0f) ? 1.0f : f;
    }

    void GotoAddrAndHighlight(size_t addr_min, size_t addr_max)
    {
        GotoAddr = addr_min;
        HighlightMin = addr_min;
        HighlightMax = addr_max;
    }

    // Decode UTF-8 sequence at offset 'off' in 'data' of size 'size'
    // Returns number of bytes consumed (1-4), or 0 if invalid
    static int DecodeUTF8(const ImU8* data, size_t size, size_t off, ImU32* out_codepoint)
    {
        if (off >= size) return 0;

        ImU8 c = data[off];
        if (c < 0x80) // 1-byte sequence (ASCII)
        {
            *out_codepoint = c;
            return 1; // Allow \r (0x0D) and \n (0x0A)
        }
        if ((c & 0xE0) == 0xC0 && off + 1 < size) // 2-byte sequence
        {
            ImU32 codepoint = (c & 0x1F) << 6;
            if ((data[off + 1] & 0xC0) == 0x80)
            {
                codepoint |= (data[off + 1] & 0x3F);
                if (codepoint >= 0x80) // Valid non-ASCII codepoint
                {
                    *out_codepoint = codepoint;
                    return 2;
                }
            }
        }
        else if ((c & 0xF0) == 0xE0 && off + 2 < size) // 3-byte sequence
        {
            ImU32 codepoint = (c & 0x0F) << 12;
            if ((data[off + 1] & 0xC0) == 0x80 && (data[off + 2] & 0xC0) == 0x80)
            {
                codepoint |= (data[off + 1] & 0x3F) << 6;
                codepoint |= (data[off + 2] & 0x3F);
                if (codepoint >= 0x800)
                {
                    *out_codepoint = codepoint;
                    return 3;
                }
            }
        }
        else if ((c & 0xF8) == 0xF0 && off + 3 < size) // 4-byte sequence
        {
            ImU32 codepoint = (c & 0x07) << 18;
            if ((data[off + 1] & 0xC0) == 0x80 && (data[off + 2] & 0xC0) == 0x80 && (data[off + 3] & 0xC0) == 0x80)
            {
                codepoint |= (data[off + 1] & 0x3F) << 12;
                codepoint |= (data[off + 2] & 0x3F) << 6;
                codepoint |= (data[off + 3] & 0x3F);
                if (codepoint >= 0x10000 && codepoint <= 0x10FFFF)
                {
                    *out_codepoint = codepoint;
                    return 4;
                }
            }
        }
        return 0; // Invalid UTF-8 sequence
    }

    // Encode a Unicode codepoint into a UTF-8 string
    // out_buf must be at least 5 bytes (4 for UTF-8 + null terminator)
    // Returns number of bytes written to out_buf (1-4), or 0 if invalid
    static int EncodeUTF8(ImU32 codepoint, char* out_buf)
    {
        if (codepoint >= 0x110000)
            return 0;
        if (codepoint < 0x80)
        {
            out_buf[0] = (char)codepoint;
            out_buf[1] = 0;
            return 1;
        }
        if (codepoint < 0x800)
        {
            out_buf[0] = (char)(0xC0 | (codepoint >> 6));
            out_buf[1] = (char)(0x80 | (codepoint & 0x3F));
            out_buf[2] = 0;
            return 2;
        }
        if (codepoint < 0x10000)
        {
            out_buf[0] = (char)(0xE0 | (codepoint >> 12));
            out_buf[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
            out_buf[2] = (char)(0x80 | (codepoint & 0x3F));
            out_buf[3] = 0;
            return 3;
        }
        out_buf[0] = (char)(0xF0 | (codepoint >> 18));
        out_buf[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
        out_buf[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        out_buf[3] = (char)(0x80 | (codepoint & 0x3F));
        out_buf[4] = 0;
        return 4;
    }

    int GetCodePoint(size_t addr, ImU32& codepoint)
    {
        ImU8* mem_data = (ImU8*)MemData;
        ImU8 temp_buffer[4];
        size_t max_bytes = IM_MIN(4, MemSize - addr);
        for (size_t i = 0; i < max_bytes; ++i)
            temp_buffer[i] = ReadFn ? ReadFn(mem_data, addr + i, UserData) : mem_data[addr + i];
        int bytes = DecodeUTF8(temp_buffer, max_bytes, 0, &codepoint);
        return bytes;
    }

    void SetSelection(size_t start, size_t end)
    {
        if (start > end)
        {
            size_t temp = start - 1;
            start = end;
            end = temp;
        }
        // Adjust selection to align with UTF-8 sequence boundaries if OptShowUtf8 is enabled
        if (OptShowUtf8 && MemData && MemSize > 0)
        {
            ImU8* mem_data = (ImU8*)MemData;
            // Adjust start to the beginning of a UTF-8 sequence
            if (start < MemSize)
            {
                ImU32 codepoint = 0;
                ImU8 temp_buffer[4];
                size_t max_bytes = IM_MIN(4, MemSize - start);
                for (size_t i = 0; i < max_bytes; ++i)
                    temp_buffer[i] = ReadFn ? ReadFn(mem_data, start + i, UserData) : mem_data[start + i];
                int bytes = DecodeUTF8(temp_buffer, max_bytes, 0, &codepoint);
                if (bytes == 0 && (temp_buffer[0] & 0xC0) == 0x80 && start > 0)
                {
                    size_t temp_addr = start;
                    size_t max_steps = 4;
                    while (temp_addr > 0 && (temp_buffer[0] & 0xC0) == 0x80 && max_steps > 0)
                    {
                        temp_addr--;
                        max_steps--;
                        max_bytes = IM_MIN(4, MemSize - temp_addr);
                        for (size_t i = 0; i < max_bytes; ++i)
                            temp_buffer[i] = ReadFn ? ReadFn(mem_data, temp_addr + i, UserData) : mem_data[temp_addr + i];
                        bytes = DecodeUTF8(temp_buffer, max_bytes, 0, &codepoint);
                    }
                    if (bytes > 0 && temp_addr + bytes - 1 >= start && temp_addr < MemSize)
                        start = temp_addr;
                }
            }
            // Adjust end to the end of a UTF-8 sequence
            if (end < MemSize)
            {
                ImU32 codepoint = 0;
                ImU8 temp_buffer[4];
                size_t max_bytes = IM_MIN(4, MemSize - end);
                for (size_t i = 0; i < max_bytes; ++i)
                    temp_buffer[i] = ReadFn ? ReadFn(mem_data, end + i, UserData) : mem_data[end + i];
                int bytes = DecodeUTF8(temp_buffer, max_bytes, 0, &codepoint);
                if (bytes > 0 && end + bytes <= MemSize)
                    end = end + bytes - 1;
                else if (bytes == 0 && (temp_buffer[0] & 0xC0) == 0x80 && end > 0)
                {
                    size_t temp_addr = end;
                    size_t max_steps = 4;
                    while (temp_addr > 0 && (temp_buffer[0] & 0xC0) == 0x80 && max_steps > 0)
                    {
                        temp_addr--;
                        max_steps--;
                        max_bytes = IM_MIN(4, MemSize - temp_addr);
                        for (size_t i = 0; i < max_bytes; ++i)
                            temp_buffer[i] = ReadFn ? ReadFn(mem_data, temp_addr + i, UserData) : mem_data[temp_addr + i];
                        bytes = DecodeUTF8(temp_buffer, max_bytes, 0, &codepoint);
                    }
                    if (bytes > 0 && temp_addr + bytes - 1 >= end && temp_addr < MemSize)
                        end = temp_addr + bytes - 1;
                }
            }
        }
        SelectionStart = start;
        SelectionEnd = end;
        SelectionChanged = true;
    }

    void ClearSelection()
    {
        SelectionStart = SelectionEnd = (size_t)-1;
        SelectionChanged = true;
    }

    bool HasSelection() const { return SelectionStart != (size_t)-1 && SelectionEnd != (size_t)-1; }

    void CopySelectionAsHex(char** out) const
    {
        *out = nullptr;
        if (!HasSelection() || !MemData || MemSize == 0) return;

        size_t start = IM_MIN(SelectionStart, SelectionEnd);
        size_t end = IM_MIN(IM_MAX(SelectionStart, SelectionEnd), MemSize - 1);
        size_t len = (end - start + 1) * 3;

        *out = (char*)malloc(len);
        if (*out == nullptr) return;

        size_t pos = 0;
        for (size_t i = start; i <= end; ++i)
        {
            char buf[4];
            ImU8 byte = ReadFn ? ReadFn((const ImU8*)MemData, i, UserData) : ((const ImU8*)MemData)[i];
            ImSnprintf(buf, sizeof(buf), OptUpperCaseHex ? "%02X " : "%02x ", byte);
            memcpy(*out + pos, buf, strlen(buf));
            pos += strlen(buf);
        }
        if (pos > 0) (*out)[pos - 1] = '\0';
    }

    void CopySelectionAsDec(char** out) const
    {
        *out = nullptr;
        if (!HasSelection() || !MemData || MemSize == 0) return;

        size_t start = IM_MIN(SelectionStart, SelectionEnd);
        size_t end = IM_MIN(IM_MAX(SelectionStart, SelectionEnd), MemSize - 1);
        size_t len = (end - start + 1) * 4;

        *out = (char*)malloc(len);
        if (*out == nullptr) return;

        size_t pos = 0;
        for (size_t i = start; i <= end; ++i)
        {
            char buf[5];
            ImU8 byte = ReadFn ? ReadFn((const ImU8*)MemData, i, UserData) : ((const ImU8*)MemData)[i];
            ImSnprintf(buf, sizeof(buf), "%d ", byte);
            memcpy(*out + pos, buf, strlen(buf));
            pos += strlen(buf);
        }
        if (pos > 0) (*out)[pos - 1] = '\0';
    }

    void CopySelectionAsBin(char** out) const
    {
        *out = nullptr;
        if (!HasSelection() || !MemData || MemSize == 0) return;

        size_t start = IM_MIN(SelectionStart, SelectionEnd);
        size_t end = IM_MIN(IM_MAX(SelectionStart, SelectionEnd), MemSize - 1);
        size_t len = (end - start + 1) * 9;

        *out = (char*)malloc(len);
        if (*out == nullptr) return;

        size_t pos = 0;
        for (size_t i = start; i <= end; ++i)
        {
            ImU8 byte = ReadFn ? ReadFn((const ImU8*)MemData, i, UserData) : ((const ImU8*)MemData)[i];
            for (int j = 7; j >= 0; --j)
                (*out)[pos++] = (byte & (1 << j)) ? '1' : '0';
            (*out)[pos++] = ' ';
        }
        if (pos > 0) (*out)[pos - 1] = '\0';
    }

    void CopySelectionAsAscii(char** out) const
    {
        *out = nullptr;
        if (!HasSelection() || !MemData || MemSize == 0) return;

        if (OptShowUtf8)
        {
            // Delegate to CopySelectionAsUtf8 for UTF-8 mode
            CopySelectionAsUtf8(out);
            return;
        }

        size_t start = IM_MIN(SelectionStart, SelectionEnd);
        size_t end = IM_MIN(IM_MAX(SelectionStart, SelectionEnd), MemSize - 1);
        size_t len = (end - start + 1) * 4 + 1; // Max 4 bytes per char (conservative for ANSI)

        *out = (char*)malloc(len);
        if (*out == nullptr) return;

        size_t pos = 0;
        for (size_t i = start; i <= end && pos < len - 1; ++i)
        {
            ImU8 byte = ReadFn ? ReadFn((const ImU8*)MemData, i, UserData) : ((const ImU8*)MemData)[i];
            (*out)[pos++] = (byte >= 32 && byte < 128) ? (char)byte : '.';
        }
        (*out)[pos] = '\0';
    }

    void CopySelectionAsUtf8(char** out) const
    {
        *out = nullptr;
        if (!HasSelection() || !MemData || MemSize == 0) return;

        size_t start = IM_MIN(SelectionStart, SelectionEnd);
        size_t end = IM_MIN(IM_MAX(SelectionStart, SelectionEnd), MemSize - 1);
        size_t len = (end - start + 1) * 4 + 1; // Max 4 bytes per UTF-8 char

        *out = (char*)malloc(len);
        if (*out == nullptr) return;

        ImU8* temp_buffer = (ImU8*)malloc(end - start + 1);
        if (temp_buffer == nullptr)
        {
            free(*out);
            *out = nullptr;
            return;
        }
        for (size_t i = start; i <= end; ++i)
            temp_buffer[i - start] = ReadFn ? ReadFn((const ImU8*)MemData, i, UserData) : ((const ImU8*)MemData)[i];

        size_t pos = 0;
        for (size_t i = 0; i <= end - start && pos < len - 1;)
        {
            ImU32 codepoint = 0;
            int bytes = DecodeUTF8(temp_buffer, end - start + 1, i, &codepoint);
            if (bytes > 0)
            {
                char utf8_buf[5];
                int written = EncodeUTF8(codepoint, utf8_buf);
                if (written > 0)
                {
                    for (int j = 0; j < written && pos < len - 1; ++j)
                        (*out)[pos++] = utf8_buf[j];
                    i += bytes;
                }
                else
                {
                    // Write U+FFFD (�)
                    (*out)[pos++] = (char)0xEF;
                    (*out)[pos++] = (char)0xBF;
                    (*out)[pos++] = (char)0xBD;
                    i++;
                }
            }
            else
            {
                // Write U+FFFD (�)
                (*out)[pos++] = (char)0xEF;
                (*out)[pos++] = (char)0xBF;
                (*out)[pos++] = (char)0xBD;
                i++;
            }
        }
        (*out)[pos] = '\0';
        free(temp_buffer);
    }

    struct Sizes
    {
        int     AddrDigitsCount;            // Number of digits required to represent maximum address.
        float   LineHeight;                 // Height of each line (no spacing).
        float   GlyphWidth;                 // Glyph width (assume mono-space).
        float   HexCellWidth;               // Width of a hex edit cell ~2.5f * GlypHWidth.
        float   SpacingBetweenMidCols;      // Spacing between each columns section (OptMidColsCount).
        float   OffsetHexMinX;
        float   OffsetHexMaxX;
        float   OffsetAsciiMinX;
        float   OffsetAsciiMaxX;
        float   WindowWidth;                // Ideal window width.

        Sizes() { memset(this, 0, sizeof(*this)); }
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
        s.OffsetHexMinX = (s.AddrDigitsCount + 2) * s.GlyphWidth;
        s.OffsetHexMaxX = s.OffsetHexMinX + (s.HexCellWidth * Cols);
        s.OffsetAsciiMinX = s.OffsetAsciiMaxX = s.OffsetHexMaxX;
        if (OptShowAscii)
        {
            s.OffsetAsciiMinX = s.OffsetHexMaxX + s.GlyphWidth * 1;
            if (OptMidColsCount > 0)
                s.OffsetAsciiMinX += (float)((Cols + OptMidColsCount - 1) / OptMidColsCount) * s.SpacingBetweenMidCols;
            s.OffsetAsciiMaxX = s.OffsetAsciiMinX + Cols * s.GlyphWidth;
        }
        s.WindowWidth = s.OffsetAsciiMaxX + style.ScrollbarSize + style.WindowPadding.x * 2 + s.GlyphWidth;
    }

    // Standalone Memory Editor window
    void DrawWindow(const char* title, void* mem_data, size_t mem_size, size_t base_display_addr = 0x0000)
    {
        Sizes s;
        CalcSizes(s, mem_size, base_display_addr);
        ImGui::SetNextWindowSize(ImVec2(s.WindowWidth, s.WindowWidth * 0.60f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(s.WindowWidth, FLT_MAX));

        Open = true;
        if (ImGui::Begin(title, &Open, ImGuiWindowFlags_NoScrollbar))
        {
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
    void DrawContents(void* mem_data_void, size_t mem_size, size_t base_display_addr = 0x0000)
    {
        MemData = mem_data_void;
        MemSize = mem_size;

        if (Cols < 1)
            Cols = 1;

        ImU8* mem_data = (ImU8*)mem_data_void;
        Sizes s;
        CalcSizes(s, mem_size, base_display_addr);
        ImGuiStyle& style = ImGui::GetStyle();

        const ImVec2 contents_pos_start = ImGui::GetCursorScreenPos();

        // We begin into our scrolling region with the 'ImGuiWindowFlags_NoMove' in order to prevent click from moving the window.
        // This is used as a facility since our main click detection code doesn't assign an ActiveId so the click would normally be caught as a window-move.
        const float height_separator = style.ItemSpacing.y;
        float footer_height = OptFooterExtraHeight;
        if (OptShowOptions)
            footer_height += height_separator + ImGui::GetFrameHeightWithSpacing() * 1;
        if (OptShowDataPreview)
            footer_height += height_separator + ImGui::GetFrameHeightWithSpacing() * 1 + ImGui::GetTextLineHeightWithSpacing() * 3;
        if (HasSelection())
            footer_height += height_separator + ImGui::GetFrameHeightWithSpacing() * 1 + ImGui::GetTextLineHeightWithSpacing();
        if (OptShowSearchPanel)
            footer_height += height_separator + ImGui::GetFrameHeightWithSpacing() * 2;

        ImGui::BeginChild("##scrolling", ImVec2(-FLT_MIN, -footer_height), ImGuiChildFlags_None, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav);

        // Store current scroll position
        TargetScrollY = ImGui::GetScrollY();

        ImDrawList* draw_list = ImGui::GetWindowDrawList();

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

        // We are not really using the clipper API correctly here, because we rely on visible_start_addr/visible_end_addr for our scrolling function.
        const ImVec2 avail_size = ImGui::GetContentRegionAvail();
        const int line_total_count = (int)((mem_size + Cols - 1) / Cols);
        ImGuiListClipper clipper;
        clipper.Begin(line_total_count, s.LineHeight);

        bool data_next = false;

        if (DataEditingAddr >= mem_size)
            DataEditingAddr = (size_t)-1;
        if (DataPreviewAddr >= mem_size)
            DataPreviewAddr = (size_t)-1;

        size_t preview_data_type_size = OptShowDataPreview ? DataTypeGetSize(PreviewDataType) : 0;

        size_t data_editing_addr_next = (size_t)-1;
        if (DataEditingAddr != (size_t)-1)
        {
            const bool is_shift_down = ImGui::GetIO().KeyShift;
            const bool is_ctrl_down = ImGui::GetIO().KeyCtrl;
            bool scrolled = false;

            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
            {
                if (is_shift_down)
                {
                    // Initialize selection anchor if this is the first shift-press
                    if (SelectionAnchor == (size_t)-1)
                        SelectionAnchor = DataEditingAddr;

                    // Move selection end up one line
                    size_t new_addr = (DataEditingAddr >= (size_t)Cols) ? DataEditingAddr - Cols : 0;
                    SetSelection(SelectionAnchor, new_addr);
                    data_editing_addr_next = new_addr;
                }
                else
                {
                    // Regular up arrow - clear selection anchor
                    SelectionAnchor = (size_t)-1;
                    if ((ptrdiff_t)DataEditingAddr >= (ptrdiff_t)Cols)
                        data_editing_addr_next = DataEditingAddr - Cols;
                }
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
            {
                if (is_shift_down)
                {
                    if (SelectionAnchor == (size_t)-1)
                        SelectionAnchor = DataEditingAddr;

                    // Move selection end up one line
                    size_t new_addr = DataEditingAddr + Cols;
                    if (new_addr >= mem_size) new_addr = mem_size - 1;
                    SetSelection(SelectionAnchor, new_addr);
                    data_editing_addr_next = new_addr;
                }
                else
                {
                    SelectionAnchor = (size_t)-1;
                    if ((ptrdiff_t)DataEditingAddr < (ptrdiff_t)mem_size - Cols)
                        data_editing_addr_next = DataEditingAddr + Cols;
                }
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
            {
                if (is_shift_down)
                {
                    if (SelectionAnchor == (size_t)-1)
                        SelectionAnchor = DataEditingAddr;

                    size_t new_addr = (DataEditingAddr > 0) ? DataEditingAddr - 1 : 0;
                    SetSelection(SelectionAnchor, new_addr);
                    data_editing_addr_next = new_addr;
                }
                else
                {
                    SelectionAnchor = (size_t)-1;
                    if ((ptrdiff_t)DataEditingAddr > 0)
                        data_editing_addr_next = DataEditingAddr - 1;
                }
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow))
            {
                if (is_shift_down)
                {
                    if (SelectionAnchor == (size_t)-1)
                        SelectionAnchor = DataEditingAddr;

                    size_t new_addr = DataEditingAddr + 1;
                    if (new_addr >= mem_size) new_addr = mem_size - 1;
                    SetSelection(SelectionAnchor, new_addr);
                    data_editing_addr_next = new_addr;
                }
                else
                {
                    SelectionAnchor = (size_t)-1;
                    if ((ptrdiff_t)DataEditingAddr < (ptrdiff_t)mem_size - 1)
                        data_editing_addr_next = DataEditingAddr + 1;
                }
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_Home))
            {
                size_t line_start = (DataEditingAddr / Cols) * Cols;

                if (is_shift_down)
                {
                    if (SelectionAnchor == (size_t)-1)
                        SelectionAnchor = DataEditingAddr;

                    if (is_ctrl_down)
                    {
                        // Ctrl+Shift+Home: Select to start of data
                        SetSelection(SelectionAnchor, 0);
                        data_editing_addr_next = 0;
                    }
                    else
                    {
                        // Shift+Home: Select to start of line
                        SetSelection(SelectionAnchor, line_start);
                        data_editing_addr_next = line_start;
                    }
                }
                else
                {
                    SelectionAnchor = (size_t)-1;
                    if (is_ctrl_down)
                    {
                        // Ctrl+Home: Jump to start of data
                        data_editing_addr_next = 0;
                    }
                    else
                    {
                        // Home: Jump to start of line
                        data_editing_addr_next = line_start;
                    }
                }
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_End))
            {
                size_t line_end = ((DataEditingAddr / Cols) * Cols) + Cols - 1;
                if (line_end >= mem_size) line_end = mem_size - 1;

                if (is_shift_down)
                {
                    if (SelectionAnchor == (size_t)-1)
                        SelectionAnchor = DataEditingAddr;

                    if (is_ctrl_down)
                    {
                        // Ctrl+Shift+End: Select to end of data
                        SetSelection(SelectionAnchor, mem_size - 1);
                        data_editing_addr_next = mem_size - 1;
                    }
                    else
                    {
                        // Shift+End: Select to end of line
                        SetSelection(SelectionAnchor, line_end);
                        data_editing_addr_next = line_end;
                    }
                }
                else
                {
                    SelectionAnchor = (size_t)-1;
                    if (is_ctrl_down)
                    {
                        // Ctrl+End: Jump to end of data
                        data_editing_addr_next = mem_size - 1;
                    }
                    else
                    {
                        // End: Jump to end of line
                        data_editing_addr_next = line_end;
                    }
                }
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_PageUp))
            {
                int lines_per_page = (int)(ImGui::GetWindowHeight() / s.LineHeight);
                if (lines_per_page > 0)
                {
                    size_t new_addr;
                    if (DataEditingAddr < (size_t)lines_per_page * Cols)
                    {
                        // If less than a page from start, jump to address 0
                        new_addr = 0;
                        ImGui::SetScrollY(0.0f);
                    }
                    else
                    {
                        // Move up one page
                        new_addr = DataEditingAddr - (size_t)lines_per_page * Cols;
                        ImGui::SetScrollY(ImGui::GetScrollY() - lines_per_page * s.LineHeight);
                    }

                    if (is_shift_down)
                    {
                        // Shift+PageUp: move selection end up one page
                        if (SelectionAnchor == (size_t)-1)
                            SelectionAnchor = DataEditingAddr;
                        SetSelection(SelectionAnchor, new_addr);
                    }
                    else
                    {
                        SelectionAnchor = (size_t)-1;
                    }
                    data_editing_addr_next = new_addr;
                    scrolled = true;
                }
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_PageDown))
            {
                int lines_per_page = (int)(ImGui::GetWindowHeight() / s.LineHeight);
                if (lines_per_page > 0)
                {
                    // Move up one down
                    size_t new_addr = IM_MIN(mem_size - 1, DataEditingAddr + (size_t)lines_per_page * Cols);
                    ImGui::SetScrollY(ImGui::GetScrollY() + lines_per_page * s.LineHeight);

                    if (is_shift_down)
                    {
                        // Shift+PageDown: move selection end down one page
                        if (SelectionAnchor == (size_t)-1)
                            SelectionAnchor = DataEditingAddr;
                        SetSelection(SelectionAnchor, new_addr);
                    }
                    else
                    {
                        SelectionAnchor = (size_t)-1;
                    }
                    data_editing_addr_next = new_addr;
                    scrolled = true;
                }
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_A) && is_ctrl_down)
            {
                // Ctrl+A: Select all
                if (mem_size > 0)
                {
                    SelectionAnchor = 0;
                    SetSelection(0, mem_size - 1);
                }
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_F) && is_ctrl_down)
            {
                // Ctrl+F: Show search panel
                OptShowSearchPanel = true;
                // Copy selected text to search input if there is a selection
                if (HasSelection())
                {
                    char* selection_data = nullptr;
                    if (OptSearchHex)
                    {
                        CopySelectionAsHex(&selection_data);
                    }
                    else if (OptSearchText)
                    {
                        CopySelectionAsUtf8(&selection_data);
                    }
                    else // Decimal
                    {
                        CopySelectionAsDec(&selection_data);
                    }
                    if (selection_data != nullptr)
                    {
                        // Clear SearchInputBuf and copy the selection
                        memset(SearchInputBuf, 0, sizeof(SearchInputBuf));

                        // Determine the maximum length to copy (reserve space for null terminator)
                        size_t max_len = sizeof(SearchInputBuf) - 1;

                        // For UTF-8, ensure truncation happens at a valid character boundary
                        if (OptSearchText)
                        {
                            size_t valid_len = 0;
                            size_t pos = 0;
                            while (pos < strlen(selection_data) && valid_len < max_len)
                            {
                                ImU32 codepoint = 0;
                                int bytes = DecodeUTF8((const ImU8*)selection_data, strlen(selection_data), pos, &codepoint);
                                if (bytes == 0 || valid_len + bytes > max_len)
                                    break; // Stop if invalid or would exceed buffer
                                valid_len += bytes;
                                pos += bytes;
                            }
                            // Copy only the valid UTF-8 portion
                            ImSnprintf(SearchInputBuf, valid_len + 1, "%s", selection_data);
                        }
                        else if (!OptSearchHex && !OptSearchText) // Decimal
                        {
                            // For Decimal, truncate at the last space before max_len
                            size_t copy_len = max_len;
                            for (size_t i = max_len; i > 0; i--)
                            {
                                if (selection_data[i] == ' ')
                                {
                                    copy_len = i; // Truncate at the last space
                                    break;
                                }
                            }
                            ImSnprintf(SearchInputBuf, copy_len + 1, "%s", selection_data);
                        }
                        else // Hex
                        {
                            // For Hex, copy up to buffer size minus null terminator
                            ImSnprintf(SearchInputBuf, sizeof(SearchInputBuf) - 1, "%s", selection_data);
                        }
                        free(selection_data);
                    }
                }
            }
            if (data_editing_addr_next != (size_t)-1 && !scrolled)
            {
                // Calculate target line and scroll position
                int target_line = (int)(data_editing_addr_next / Cols);
                float target_scroll = target_line * s.LineHeight;

                // Smooth scroll to target position
                if (target_scroll < TargetScrollY)
                {
                    ImGui::SetScrollY(target_scroll);
                }
                else if (target_scroll > TargetScrollY + ImGui::GetWindowHeight() - s.LineHeight * 2)
                {
                    ImGui::SetScrollY(target_scroll - ImGui::GetWindowHeight() + s.LineHeight * 2);
                }
            }
        }

        // Draw vertical separator
        ImVec2 window_pos = ImGui::GetWindowPos();
        if (OptShowAscii)
            draw_list->AddLine(ImVec2(window_pos.x + s.OffsetAsciiMinX - s.GlyphWidth, window_pos.y), ImVec2(window_pos.x + s.OffsetAsciiMinX - s.GlyphWidth, window_pos.y + 9999), ImGui::GetColorU32(ImGuiCol_Border));

        const ImU32 color_text = ImGui::GetColorU32(ImGuiCol_Text);
        const ImU32 color_disabled = OptGreyOutZeroes ? ImGui::GetColorU32(ImGuiCol_TextDisabled) : color_text;

        const char* format_address = OptUpperCaseHex ? "%0*" _PRISizeT "X: " : "%0*" _PRISizeT "x: ";
        const char* format_byte = OptUpperCaseHex ? "%02X" : "%02x";
        const char* format_byte_space = OptUpperCaseHex ? "%02X " : "%02x ";

        MouseHovered = false;
        MouseHoveredAddr = 0;

        while (clipper.Step())
            for (int line_i = clipper.DisplayStart; line_i < clipper.DisplayEnd; line_i++) // display only visible lines
            {
                size_t addr = (size_t)line_i * Cols;
                ImGui::Text(format_address, s.AddrDigitsCount, base_display_addr + addr);

                // Draw Hexadecimal
                for (int n = 0; n < Cols && addr < mem_size; n++, addr++)
                {
                    float byte_pos_x = s.OffsetHexMinX + s.HexCellWidth * n;
                    if (OptMidColsCount > 0)
                        byte_pos_x += (float)(n / OptMidColsCount) * s.SpacingBetweenMidCols;
                    ImGui::SameLine(byte_pos_x);

                    // Check if mouse is hovering this byte
                    bool is_byte_hovered = false;
                    const ImVec2 pos = ImGui::GetCursorScreenPos();
                    if (ImGui::IsMouseHoveringRect(pos, ImVec2(pos.x + s.HexCellWidth, pos.y + s.LineHeight)))
                    {
                        is_byte_hovered = true;
                        MouseHovered = true;
                        MouseHoveredAddr = addr;
                    }

                    // Handle selection
                    if (is_byte_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup))
                    {
                        if (ImGui::GetIO().KeyShift && HasSelection() && SelectionAnchor != (size_t)-1)
                        {
                            // Extend existing selection using SelectionAnchor
                            if (SelectionAnchor != (size_t)-1)
                            {
                                SetSelection(SelectionAnchor, addr);
                                Selecting = false;
                            }
                        }
                        else
                        {
                            // Start new selection without Shift
                            SelectionAnchor = addr;
                            SetSelection(addr, addr);
                            Selecting = true;
                        }
                    }

                    // Mouse drag handling
                    if (is_byte_hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
                    {
                        SetSelection(SelectionAnchor, addr);
                        Selecting = true;
                    }
                    else if (Selecting && is_byte_hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                    {
                        DataEditingAddr = DataPreviewAddr = addr;
                        DataEditingTakeFocus = true;
                        Selecting = false;
                    }

                    // Draw highlight or custom background color
                    const bool is_highlight_from_user_range = (addr >= HighlightMin && addr < HighlightMax);
                    const bool is_highlight_from_user_func = (HighlightFn && HighlightFn(mem_data, addr, UserData));
                    const bool is_highlight_from_preview = (addr >= DataPreviewAddr && addr < DataPreviewAddr + preview_data_type_size);
                    const bool is_selected = HasSelection() && addr >= SelectionStart && addr <= SelectionEnd;

                    ImU32 bg_color = 0;
                    bool is_next_byte_highlighted = false;
                    if (is_highlight_from_user_range || is_highlight_from_user_func || is_highlight_from_preview || is_selected)
                    {
                        is_next_byte_highlighted = (addr + 1 < mem_size) && ((HighlightMax != (size_t)-1 && addr + 1 < HighlightMax) || (HighlightFn && HighlightFn(mem_data, addr + 1, UserData)) || (addr + 1 < DataPreviewAddr + preview_data_type_size) || (is_selected && addr + 1 <= SelectionEnd));
                        bg_color = is_selected ? SelectionColor : HighlightColor;
                    }
                    else if (BgColorFn != nullptr)
                    {
                        is_next_byte_highlighted = (addr + 1 < mem_size) && ((BgColorFn(mem_data, addr + 1, UserData) & IM_COL32_A_MASK) != 0);
                        bg_color = BgColorFn(mem_data, addr, UserData);
                    }
                    if (bg_color != 0)
                    {
                        float bg_width = s.GlyphWidth * 2;
                        if (is_next_byte_highlighted || (n + 1 == Cols))
                        {
                            bg_width = s.HexCellWidth;
                            if (OptMidColsCount > 0 && n > 0 && (n + 1) < Cols && ((n + 1) % OptMidColsCount) == 0)
                                bg_width += s.SpacingBetweenMidCols;
                        }
                        draw_list->AddRectFilled(pos, ImVec2(pos.x + bg_width, pos.y + s.LineHeight), bg_color);
                    }

                    if (DataEditingAddr == addr)
                    {
                        // Display text input on current byte
                        bool data_write = false;
                        ImGui::PushID((void*)addr);
                        if (DataEditingTakeFocus)
                        {
                            ImGui::SetKeyboardFocusHere(0);
                            ImSnprintf(DataInputBuf, 32, format_byte, ReadFn ? ReadFn(mem_data, addr, UserData) : mem_data[addr]);
                        }
                        struct InputTextUserData
                        {
                            // FIXME: We should have a way to retrieve the text edit cursor position more easily in the API, this is rather tedious. This is such a ugly mess we may be better off not using InputText() at all here.
                            static int Callback(ImGuiInputTextCallbackData* data)
                            {
                                InputTextUserData* user_data = (InputTextUserData*)data->UserData;
                                if (!data->HasSelection())
                                    user_data->CursorPos = data->CursorPos;
#if IMGUI_VERSION_NUM < 19102
                                if (data->Flags & ImGuiInputTextFlags_ReadOnly)
                                    return 0;
#endif
                                if (data->SelectionStart == 0 && data->SelectionEnd == data->BufTextLen)
                                {
                                    // When not editing a byte, always refresh its InputText content pulled from underlying memory data
                                    // (this is a bit tricky, since InputText technically "owns" the master copy of the buffer we edit it in there)
                                    data->DeleteChars(0, data->BufTextLen);
                                    data->InsertChars(0, user_data->CurrentBufOverwrite);
                                    data->SelectionStart = 0;
                                    data->SelectionEnd = 2;
                                    data->CursorPos = 0;
                                }
                                return 0;
                            }
                            char   CurrentBufOverwrite[3];  // Input
                            int    CursorPos;               // Output
                        };
                        InputTextUserData input_text_user_data;
                        input_text_user_data.CursorPos = -1;
                        ImSnprintf(input_text_user_data.CurrentBufOverwrite, 3, format_byte, ReadFn ? ReadFn(mem_data, addr, UserData) : mem_data[addr]);
                        ImGuiInputTextFlags flags = ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_NoHorizontalScroll | ImGuiInputTextFlags_CallbackAlways;
                        if (ReadOnly)
                            flags |= ImGuiInputTextFlags_ReadOnly;
                        flags |= ImGuiInputTextFlags_AlwaysOverwrite; // was ImGuiInputTextFlags_AlwaysInsertMode
                        ImGui::SetNextItemWidth(s.GlyphWidth * 2);
                        if (ImGui::InputText("##data", DataInputBuf, IM_ARRAYSIZE(DataInputBuf), flags, InputTextUserData::Callback, &input_text_user_data))
                            data_write = data_next = true;
                        else if (!DataEditingTakeFocus && !ImGui::IsItemActive())
                            DataEditingAddr = data_editing_addr_next = (size_t)-1;
                        DataEditingTakeFocus = false;
                        if (input_text_user_data.CursorPos >= 2)
                            data_write = data_next = true;
                        if (data_editing_addr_next != (size_t)-1)
                            data_write = data_next = false;
                        unsigned int data_input_value = 0;
                        if (!ReadOnly && data_write && sscanf(DataInputBuf, "%X", &data_input_value) == 1)
                        {
                            if (WriteFn)
                                WriteFn(mem_data, addr, (ImU8)data_input_value, UserData);
                            else
                                mem_data[addr] = (ImU8)data_input_value;
                        }
                        if (ImGui::IsItemHovered())
                        {
                            MouseHovered = true;
                            MouseHoveredAddr = addr;
                        }
                        ImGui::PopID();
                    }
                    else
                    {
                        // NB: The trailing space is not visible but ensure there's no gap that the mouse cannot click on.
                        ImU8 b = ReadFn ? ReadFn(mem_data, addr, UserData) : mem_data[addr];

                        if (OptShowHexII)
                        {
                            if ((b >= 32 && b < 128))
                                ImGui::Text(".%c ", b);
                            else if (b == 0xFF && OptGreyOutZeroes)
                                ImGui::TextDisabled("## ");
                            else if (b == 0x00)
                                ImGui::Text("   ");
                            else
                                ImGui::Text(format_byte_space, b);
                        }
                        else
                        {
                            if (b == 0 && OptGreyOutZeroes)
                                ImGui::TextDisabled("00 ");
                            else
                                ImGui::Text(format_byte_space, b);
                        }
                        if (ImGui::IsItemHovered())
                        {
                            MouseHovered = true;
                            MouseHoveredAddr = addr;
                            if (ImGui::IsMouseClicked(0))
                            {
                                DataEditingTakeFocus = true;
                                data_editing_addr_next = addr;
                            }
                        }
                    }
                }

                if (OptShowAscii)
                {
                    // Draw ASCII values
                    ImGui::SameLine(s.OffsetAsciiMinX);
                    ImVec2 pos = ImGui::GetCursorScreenPos();
                    addr = (size_t)line_i * Cols;

                    // Render continuous selection background
                    if (HasSelection())
                    {
                        size_t sel_start = IM_MAX(SelectionStart, (size_t)line_i * Cols);
                        size_t sel_end = IM_MIN(SelectionEnd, IM_MIN((size_t)line_i * Cols + Cols - 1, mem_size - 1));
                        if (sel_start <= sel_end && sel_end >= addr)
                        {
                            float sel_start_x = pos.x;
                            float sel_end_x = pos.x;
                            size_t temp_addr = addr;
                            float current_x = 0.0f;
                            // Calculate position of sel_start
                            while (temp_addr < sel_start && temp_addr < mem_size)
                            {
                                float char_width = s.GlyphWidth;
                                int bytes = 1;
                                if (OptShowUtf8)
                                {
                                    ImU32 codepoint = 0;
                                    bytes = GetCodePoint(temp_addr, codepoint);
                                    ImFont* font = ImGui::GetFont();
                                    ImFontBaked* baked = font->GetFontBaked(font->LegacySize);
                                    char_width = baked->GetCharAdvance((ImWchar)(codepoint >= 32 ? codepoint : '.'));
                                    if (!bytes)
                                        bytes = 1;
                                }
                                current_x += char_width;
                                temp_addr += bytes;
                            }
                            sel_start_x = pos.x + current_x;
                            while (temp_addr <= sel_end && temp_addr < mem_size)
                            {
                                float char_width = s.GlyphWidth;
                                int bytes = 1;
                                if (OptShowUtf8)
                                {
                                    ImU32 codepoint = 0;
                                    bytes = GetCodePoint(temp_addr, codepoint);
                                    ImFont* font = ImGui::GetFont();
                                    ImFontBaked* baked = font->GetFontBaked(font->LegacySize);
                                    char_width = baked->GetCharAdvance((ImWchar)(codepoint >= 32 ? codepoint : '.'));
                                    if (!bytes)
                                        bytes = 1;
                                }
                                sel_end_x = pos.x + current_x + char_width;
                                current_x += char_width;
                                temp_addr += bytes;
                            }
                            if (sel_start_x < sel_end_x)
                                draw_list->AddRectFilled(ImVec2(sel_start_x, pos.y), ImVec2(sel_end_x, pos.y + s.LineHeight), SelectionColor);
                        }
                    }

                    // Handle mouse interaction
                    const float mouse_off_x = ImGui::GetIO().MousePos.x - pos.x;
                    size_t mouse_addr = (size_t)-1;
                    if (mouse_off_x >= 0.0f && mouse_off_x < s.OffsetAsciiMaxX - s.OffsetAsciiMinX)
                    {
                        size_t line_end = IM_MIN((size_t)line_i * Cols + Cols - 1, mem_size - 1);
                        size_t temp_addr = (size_t)line_i * Cols;
                        float current_x = 0.0f;
                        size_t last_valid_addr = temp_addr;
                        float last_valid_x = current_x;
                        while (temp_addr <= line_end && temp_addr < mem_size)
                        {
                            float char_width = s.GlyphWidth;
                            int bytes = 1;
                            if (OptShowUtf8)
                            {
                                ImU32 codepoint = 0;
                                bytes = GetCodePoint(temp_addr, codepoint);
                                ImFont* font = ImGui::GetFont();
                                ImFontBaked* baked = font->GetFontBaked(font->LegacySize);
                                char_width = baked->GetCharAdvance((ImWchar)(codepoint >= 32 ? codepoint : '.'));
                                if (!bytes)
                                    bytes = 1;
                            }
                            if (current_x <= mouse_off_x && mouse_off_x < current_x + char_width)
                            {
                                mouse_addr = temp_addr;
                                break;
                            }
                            last_valid_addr = temp_addr;
                            last_valid_x = current_x;
                            current_x += char_width;
                            temp_addr += bytes;
                        }
                        if (mouse_addr == (size_t)-1 && last_valid_addr <= line_end && mouse_off_x >= last_valid_x)
                            mouse_addr = last_valid_addr;
                        if (mouse_addr != (size_t)-1 && OptShowUtf8 && mouse_addr < mem_size)
                        {
                            ImU32 codepoint = 0;
                            ImU8 temp_buffer[4];
                            size_t max_bytes = IM_MIN(4, mem_size - mouse_addr);
                            for (size_t i = 0; i < max_bytes; ++i)
                                temp_buffer[i] = ReadFn ? ReadFn(mem_data, mouse_addr + i, UserData) : mem_data[mouse_addr + i];
                            int bytes = DecodeUTF8(temp_buffer, max_bytes, 0, &codepoint);
                            if (bytes == 0 && (temp_buffer[0] & 0xC0) == 0x80 && mouse_addr > 0)
                            {
                                size_t temp_addr = mouse_addr;
                                size_t max_steps = 4;
                                while (temp_addr > 0 && (temp_buffer[0] & 0xC0) == 0x80 && max_steps > 0)
                                {
                                    temp_addr--;
                                    max_steps--;
                                    max_bytes = IM_MIN(4, mem_size - temp_addr);
                                    for (size_t i = 0; i < max_bytes; ++i)
                                        temp_buffer[i] = ReadFn ? ReadFn(mem_data, temp_addr + i, UserData) : mem_data[temp_addr + i];
                                    bytes = DecodeUTF8(temp_buffer, max_bytes, 0, &codepoint);
                                }
                                if (bytes > 0 && temp_addr + bytes - 1 >= mouse_addr && temp_addr < mem_size)
                                    mouse_addr = temp_addr;
                            }
                        }
                    }

                    ImGui::PushID(line_i);
                    ImGui::InvisibleButton("ascii", ImVec2(s.OffsetAsciiMaxX - s.OffsetAsciiMinX, s.LineHeight));
                    if (ImGui::IsItemHovered())
                    {
                        MouseHovered = true;
                        MouseHoveredAddr = mouse_addr;
                        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup))
                        {
                            if (ImGui::GetIO().KeyShift && HasSelection() && SelectionAnchor != (size_t)-1)
                            {
                                SetSelection(SelectionAnchor, mouse_addr);
                                Selecting = false;
                            }
                            else
                            {
                                SelectionAnchor = mouse_addr;
                                SetSelection(mouse_addr, mouse_addr);
                                Selecting = true;
                            }
                            DataEditingAddr = DataPreviewAddr = mouse_addr;
                            DataEditingTakeFocus = true;
                        }
                    }
                    ImGui::PopID();
                    int n = 0;
                    while (n < Cols && addr < mem_size)
                    {
                        float char_width = s.GlyphWidth;
                        ImU32 codepoint = 0;
                        int bytes = 1;
                        if (OptShowUtf8)
                        {
                            bytes = GetCodePoint(addr, codepoint);
                            ImFont* font = ImGui::GetFont();
                            ImFontBaked* baked = font->GetFontBaked(font->LegacySize);
                            char_width = baked->GetCharAdvance((ImWchar)(codepoint >= 32 ? codepoint : '.'));
                            if (!bytes)
                                bytes = 1;
                        }
                        bool is_byte_hovered = ImGui::IsMouseHoveringRect(pos, ImVec2(pos.x + char_width, pos.y + s.LineHeight));
                        if (is_byte_hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
                        {
                            SetSelection(SelectionAnchor, mouse_addr);
                            Selecting = true;
                        }
                        else if (Selecting && is_byte_hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                        {
                            DataEditingAddr = DataPreviewAddr = mouse_addr;
                            DataEditingTakeFocus = true;
                            Selecting = false;
                        }
                        if (addr == DataEditingAddr)
                        {
                            draw_list->AddRectFilled(pos, ImVec2(pos.x + char_width, pos.y + s.LineHeight), ImGui::GetColorU32(ImGuiCol_FrameBg));
                            draw_list->AddRectFilled(pos, ImVec2(pos.x + char_width, pos.y + s.LineHeight), ImGui::GetColorU32(ImGuiCol_TextSelectedBg));
                        }
                        else if (BgColorFn && (!HasSelection() || addr < SelectionStart || addr > SelectionEnd))
                        {
                            draw_list->AddRectFilled(pos, ImVec2(pos.x + char_width, pos.y + s.LineHeight), BgColorFn(mem_data, addr, UserData));
                        }
                        if (OptShowUtf8)
                        {
                            if (bytes > 0 && codepoint >= 32)
                            {
                                char utf8_buf[5];
                                int written = EncodeUTF8(codepoint, utf8_buf);
                                if (written > 0)
                                {
                                    draw_list->AddText(pos, color_text, utf8_buf, utf8_buf + written);
                                    pos.x += char_width;
                                }
                                else
                                {
                                    char dot_buf[] = { '.', 0 };
                                    draw_list->AddText(pos, color_disabled, dot_buf, dot_buf + 1);
                                    pos.x += char_width;
                                }
                                addr += bytes;
                                n += bytes;
                            }
                            else
                            {
                                char dot_buf[] = { '.', 0 };
                                draw_list->AddText(pos, color_disabled, dot_buf, dot_buf + 1);
                                pos.x += char_width;
                                addr++;
                                n++;
                            }
                        }
                        else
                        {
                            // ANSI mode
                            unsigned char c = ReadFn ? ReadFn(mem_data, addr, UserData) : mem_data[addr];
                            char display_c = (c < 32 || c >= 128) ? '.' : c;
                            draw_list->AddText(pos, (display_c == c) ? color_text : color_disabled, &display_c, &display_c + 1);
                            pos.x += char_width;
                            addr++;
                            n++;
                        }
                    }
                }
            }
        ImGui::PopStyleVar(2);

        // Handle scrolling when dragging outside the visible bytes
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && SelectionAnchor != (size_t)-1 && Selecting)
        {
            ImVec2 mouse_pos_screen = ImGui::GetMousePos();
            ImVec2 child_screen_pos = ImGui::GetWindowPos();
            ImVec2 mouse_pos_relative_to_child = ImVec2(mouse_pos_screen.x - child_screen_pos.x, mouse_pos_screen.y - child_screen_pos.y);

            float scroll_speed = 0.0f;
            float window_height = ImGui::GetWindowHeight();
            float fast_scroll_threshold = 70.0f;
            float base_speed_factor = 0.01f;
            float max_speed_factor = 0.5f;

            // Checking the upper bound of the visible area
            if (mouse_pos_relative_to_child.y < 0.0f)
            {
                float distance = 0.0f - mouse_pos_relative_to_child.y;
                float delta = Saturate(distance / fast_scroll_threshold);
                scroll_speed = -s.LineHeight * (base_speed_factor + delta * max_speed_factor);
            }
            // Checking the lower bound of the visible area
            else if (mouse_pos_relative_to_child.y > window_height)
            {
                float distance = mouse_pos_relative_to_child.y - window_height;
                float delta = Saturate(distance / fast_scroll_threshold);
                scroll_speed = s.LineHeight * (base_speed_factor + delta * max_speed_factor);
            }

            if (scroll_speed != 0.0f)
            {
                ImGui::SetScrollY(ImGui::GetScrollY() + scroll_speed);
            }
        }

        const float child_width = ImGui::GetWindowSize().x;
        ImGui::EndChild();

        // Notify the main window of our ideal child content size (FIXME: we are missing an API to get the contents size from the child)
        ImVec2 backup_pos = ImGui::GetCursorScreenPos();
        ImGui::SetCursorPosX(s.WindowWidth);
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
        ImGui::SetCursorScreenPos(backup_pos);

        if (data_next && DataEditingAddr + 1 < mem_size)
        {
            DataEditingAddr = DataPreviewAddr = DataEditingAddr + 1;
            DataEditingTakeFocus = true;
        }
        else if (data_editing_addr_next != (size_t)-1)
        {
            DataEditingAddr = DataPreviewAddr = data_editing_addr_next;
            LastEditingAddr = DataEditingAddr;
            DataEditingTakeFocus = true;
        }

        const bool lock_show_data_preview = OptShowDataPreview;
        if (OptShowOptions)
        {
            ImGui::Separator();
            DrawOptionsLine(s, mem_data, mem_size, base_display_addr);
        }

        if (lock_show_data_preview)
        {
            ImGui::Separator();
            DrawPreviewLine(s, mem_data, mem_size, base_display_addr);
        }

        if (GotoAddr != (size_t)-1)
        {
            if (GotoAddr < mem_size)
            {
                ImGui::BeginChild("##scrolling");
                ImGui::SetScrollY((GotoAddr / Cols) * ImGui::GetTextLineHeight() - avail_size.y * 0.5f);
                ImGui::EndChild();
                DataEditingAddr = DataPreviewAddr = GotoAddr;
                DataEditingTakeFocus = true;
            }
            GotoAddr = (size_t)-1;
            LastEditingAddr = GotoAddr; // Update LastEditingAddr
        }

        // Draw selection panel
        if (HasSelection())
        {
            ImGui::Separator();
            DrawSelectionLine(s, mem_data, mem_size, base_display_addr);
        }

        // Draw search panel
        if (OptShowSearchPanel)
        {
            ImGui::Separator();
            DrawSearchLine(s, mem_data, mem_size, base_display_addr);
        }

        const ImVec2 contents_pos_end(contents_pos_start.x + child_width, ImGui::GetCursorScreenPos().y);
        //ImGui::GetForegroundDrawList()->AddRect(contents_pos_start, contents_pos_end, IM_COL32(255, 0, 0, 255));
        if (OptShowOptions)
            if (ImGui::IsMouseHoveringRect(contents_pos_start, contents_pos_end))
                if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
                    ImGui::OpenPopup("OptionsPopup");

        // Copy selection to clipboard as hex when Ctrl+C is pressed, but only if no text input is active
        if (HasSelection() && ImGui::IsKeyPressed(ImGuiKey_C) && ImGui::GetIO().KeyCtrl && !ImGui::GetIO().WantTextInput)
        {
            char* hex = nullptr;
            CopySelectionAsHex(&hex);
            if (hex != nullptr)
            {
                ImGui::SetClipboardText(hex);
                free(hex);
            }
        }

        if (ImGui::BeginPopup("OptionsPopup"))
        {
            if (HasSelection())
            {
                if (ImGui::MenuItem("Copy as Hex", "Ctrl+C"))
                {
                    char* hex = nullptr;
                    CopySelectionAsHex(&hex);
                    if (hex != nullptr)
                    {
                        ImGui::SetClipboardText(hex);
                        free(hex);
                    }
                }
                if (ImGui::MenuItem("Copy as Dec"))
                {
                    char* dec = nullptr;
                    CopySelectionAsDec(&dec);
                    if (dec != nullptr)
                    {
                        ImGui::SetClipboardText(dec);
                        free(dec);
                    }
                }
                if (ImGui::MenuItem("Copy as Bin"))
                {
                    char* bin = nullptr;
                    CopySelectionAsBin(&bin);
                    if (bin != nullptr)
                    {
                        ImGui::SetClipboardText(bin);
                        free(bin);
                    }
                }
                if (ImGui::MenuItem("Copy as ASCII"))
                {
                    char* ascii = nullptr;
                    CopySelectionAsAscii(&ascii);
                    if (ascii != nullptr)
                    {
                        ImGui::SetClipboardText(ascii);
                        free(ascii);
                    }
                }
                if (ImGui::MenuItem("Copy as UTF-8"))
                {
                    char* utf8 = nullptr;
                    CopySelectionAsUtf8(&utf8);
                    if (utf8 != nullptr)
                    {
                        ImGui::SetClipboardText(utf8);
                        free(utf8);
                    }
                }
                ImGui::Separator();
            }
            ImGui::SetNextItemWidth(s.GlyphWidth * 7 + style.FramePadding.x * 2.0f);
            if (ImGui::DragInt("##cols", &Cols, 0.2f, 4, 32, "%d cols")) { ContentsWidthChanged = true; if (Cols < 1) Cols = 1; }
            ImGui::Checkbox("Show Data Preview", &OptShowDataPreview);
            ImGui::Checkbox("Show HexII", &OptShowHexII);
            if (ImGui::Checkbox("Show Ascii", &OptShowAscii)) { ContentsWidthChanged = true; }
            ImGui::Checkbox("Show UTF-8", &OptShowUtf8);
            ImGui::Checkbox("Grey out zeroes", &OptGreyOutZeroes);
            ImGui::Checkbox("Uppercase Hex", &OptUpperCaseHex);
            ImGui::Checkbox("Show Search Panel", &OptShowSearchPanel);

            ImGui::EndPopup();
        }
    }

    void DrawOptionsLine(const Sizes& s, void* mem_data, size_t mem_size, size_t base_display_addr)
    {
        IM_UNUSED(mem_data);
        ImGuiStyle& style = ImGui::GetStyle();

        // Options menu
        if (ImGui::Button("Options"))
            ImGui::OpenPopup("OptionsPopup");

        ImGui::SameLine();

        // Draw address input mode selection
        ImGui::PushID("addr_input_mode");
        bool format_changed = false;
        if (ImGui::RadioButton("Hex", OptAddrInputHex))
        {
            OptAddrInputHex = true;
            format_changed = true;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Dec", !OptAddrInputHex))
        {
            OptAddrInputHex = false;
            format_changed = true;
        }
        ImGui::PopID();
        ImGui::SameLine();

        // Define formats for address: with width for display, without for parsing
        const char* addr_format = OptAddrInputHex ? (OptUpperCaseHex ? "%" _PRISizeT "X" : "%" _PRISizeT "x") : "%" _PRISizeT "u";
        const char* addr_format_with_width = OptAddrInputHex ? (OptUpperCaseHex ? "%0*" _PRISizeT "X" : "%0*" _PRISizeT "x") : "%" _PRISizeT "u";

        // Build range format for displaying address range
        char format_range[32];
        ImSnprintf(format_range, IM_ARRAYSIZE(format_range), "| Range %s..%s | Go to:", addr_format_with_width, addr_format_with_width);

        // Display address range
        if (OptAddrInputHex)
        {
            ImGui::Text(format_range, s.AddrDigitsCount, base_display_addr, s.AddrDigitsCount, base_display_addr + mem_size - 1);
        }
        else
        {
            ImGui::Text(format_range, base_display_addr, base_display_addr + mem_size - 1);
        }
        ImGui::SameLine();

        // Update AddrInputBuf when format or DataEditingAddr changes
        if (format_changed || DataEditingAddr != (size_t)-1)
        {
            size_t addr_to_display = (size_t)-1;
            if (DataEditingAddr != (size_t)-1)
            {
                // Use DataEditingAddr and update LastEditingAddr
                LastEditingAddr = DataEditingAddr;
                addr_to_display = DataEditingAddr;
            }
            else if (LastEditingAddr != (size_t)-1)
            {
                // Use LastEditingAddr if no active editing
                addr_to_display = LastEditingAddr;
            }

            if (addr_to_display != (size_t)-1)
            {
                if (OptAddrInputHex)
                {
                    ImSnprintf(AddrInputBuf, IM_ARRAYSIZE(AddrInputBuf), addr_format_with_width,
                                s.AddrDigitsCount, base_display_addr + addr_to_display);
                }
                else
                {
                    ImSnprintf(AddrInputBuf, IM_ARRAYSIZE(AddrInputBuf), addr_format_with_width,
                                base_display_addr + addr_to_display);
                }
            }
            else
            {
                // Clear input if no valid address
                AddrInputBuf[0] = '\0';
            }
        }

        // Draw address input field
        ImGui::SetNextItemWidth((s.AddrDigitsCount + 1) * s.GlyphWidth + style.FramePadding.x * 2.0f);
        ImGuiInputTextFlags flags = OptAddrInputHex ? ImGuiInputTextFlags_CharsHexadecimal : ImGuiInputTextFlags_CharsDecimal;
        flags |= ImGuiInputTextFlags_EnterReturnsTrue;

        if (ImGui::InputText("##addr", AddrInputBuf, IM_ARRAYSIZE(AddrInputBuf), flags))
        {
            size_t goto_addr;
            if (sscanf(AddrInputBuf, addr_format, &goto_addr) == 1)
            {
                GotoAddr = goto_addr - base_display_addr;
                HighlightMin = HighlightMax = (size_t)-1;
                LastEditingAddr = GotoAddr; // Update LastEditingAddr
            }
        }

        //if (MouseHovered)
        //{
        //    ImGui::SameLine();
        //    ImGui::Text("Hovered: %p", MouseHoveredAddr);
        //}
    }

    void DrawSelectionLine(const Sizes& s, void* mem_data_void, size_t mem_size, size_t base_display_addr)
    {
        IM_UNUSED(mem_data_void);
        IM_UNUSED(mem_size);

        const char* format_hex = OptUpperCaseHex ? "%0*" _PRISizeT "X" : "%0*" _PRISizeT "x";
        const char* format_dec = "%" _PRISizeT "u";

        char start_buf[64];
        char end_buf[64];
        char range[132];

        if (OptAddrInputHex)
        {
            ImSnprintf(start_buf, sizeof(start_buf), format_hex, s.AddrDigitsCount, base_display_addr + SelectionStart);
            ImSnprintf(end_buf, sizeof(end_buf), format_hex, s.AddrDigitsCount, base_display_addr + SelectionEnd);
        }
        else
        {
            ImSnprintf(start_buf, sizeof(start_buf), format_dec, base_display_addr + SelectionStart);
            ImSnprintf(end_buf, sizeof(end_buf), format_dec, base_display_addr + SelectionEnd);
        }

        ImSnprintf(range, sizeof(range), "%s..%s", start_buf, end_buf);

        ImGui::Text("Selection: %s (%" PRIdPTR " bytes)", range,
                    (ptrdiff_t)(SelectionEnd - SelectionStart) >= 0 ?
                    (ptrdiff_t)(SelectionEnd - SelectionStart) + 1 :
                    -((ptrdiff_t)(SelectionEnd - SelectionStart)) + 1);
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy range"))
            ImGui::SetClipboardText(range);

        if (ImGui::Button("Copy Hex"))
        {
            char* hex = nullptr;
            CopySelectionAsHex(&hex);
            if (hex != nullptr)
            {
                ImGui::SetClipboardText(hex);
                free(hex);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Copy Dec"))
        {
            char* dec = nullptr;
            CopySelectionAsDec(&dec);
            if (dec != nullptr)
            {
                ImGui::SetClipboardText(dec);
                free(dec);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Copy Bin"))
        {
            char* bin = nullptr;
            CopySelectionAsBin(&bin);
            if (bin != nullptr)
            {
                ImGui::SetClipboardText(bin);
                free(bin);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Copy ASCII"))
        {
            char* ascii = nullptr;
            CopySelectionAsAscii(&ascii);
            if (ascii != nullptr)
            {
                ImGui::SetClipboardText(ascii);
                free(ascii);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Copy UTF-8"))
        {
            char* utf8 = nullptr;
            CopySelectionAsUtf8(&utf8);
            if (utf8 != nullptr)
            {
                ImGui::SetClipboardText(utf8);
                free(utf8);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear"))
        {
            ClearSelection();
        }
    }

    // Check if the current position is a non-breaking space (UTF-8: C2 A0)
    static bool is_nbsp(const char* p) { return (unsigned char)(p[0]) == 0xC2 && (unsigned char)(p[1]) == 0xA0; }

    // Skip regular spaces and non-breaking spaces (nbsp) in the string
    static const char* SkipWhitespace(const char* p)
    {
        while (*p)
        {
            if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
                p += 1;
            else if (is_nbsp(p)) p += 2;
            else break;
        }
        return p;
    }

    bool CheckPatternMatch(size_t addr, ImU8* mem_data, size_t mem_size, const ImU8* pattern, size_t pattern_size) const
    {
        if (addr + pattern_size > mem_size)
            return false; // Prevent out-of-bounds access

        // Byte-by-byte matching for Hex, Dec, or case-sensitive UTF-8
        size_t mem_pos = addr;
        size_t pat_pos = 0;
        while (pat_pos < pattern_size && mem_pos < mem_size)
        {
            if (pattern[pat_pos] == 0x0A) // Handle \n in pattern
            {
                ImU8 mem_byte = ReadFn ? ReadFn(mem_data, mem_pos, UserData) : mem_data[mem_pos];
                // Check for LF (0x0A) or CRLF (0x0D 0x0A)
                if (mem_pos < mem_size && mem_byte == 0x0A)
                {
                    mem_pos += 1;
                    pat_pos += 1;
                    continue;
                }
                else if (mem_pos + 1 < mem_size)
                {
                    ImU8 mem_byte_next = ReadFn ? ReadFn(mem_data, mem_pos + 1, UserData) : mem_data[mem_pos + 1];
                    if (mem_byte == 0x0D && mem_byte_next == 0x0A)
                    {
                        mem_pos += 2;
                        pat_pos += 1;
                        continue;
                    }
                }
                return false;
            }
            ImU8 mem_byte = ReadFn ? ReadFn(mem_data, mem_pos, UserData) : mem_data[mem_pos];
            if (mem_byte != pattern[pat_pos])
                return false;
            mem_pos += 1;
            pat_pos += 1;
        }
        return pat_pos == pattern_size && mem_pos <= mem_size;
    }

    void DrawSearchLine(const Sizes& s, void* mem_data_void, size_t mem_size, size_t base_display_addr)
    {
        IM_UNUSED(s);
        IM_UNUSED(base_display_addr);
        ImGuiStyle& style = ImGui::GetStyle();
        ImU8* mem_data = (ImU8*)mem_data_void;

        static bool use_data_preview_format = false;
        static bool search_backwards = false;
        static bool search_wrapped = false;
        static bool search_continuing = false; // Track if search is continuing
        static bool validation_failed = false; // Unified flag for validation errors
        static bool is_hex_error = false; // Track if error is hex-specific
        static bool is_text_error = false; // Track if error is text-specific
        static size_t current_search_pos = 0;
        static size_t match_count = 0; // Store total number of pattern matches
        static ImVector<size_t> match_positions; // Store positions of matches

        // Update current_search_pos and reset state if cursor moves
        if (DataEditingAddr != (size_t)-1 && current_search_pos != DataEditingAddr)
        {
            current_search_pos = DataEditingAddr;
            search_wrapped = search_continuing = false;
            match_count = 0; // Reset match count when cursor moves
            match_positions.clear();
        }

        // First line: search type and buttons
        ImGui::Text("Search:");
        ImGui::SameLine();
        ImGui::PushID("search_mode");
        if (ImGui::RadioButton("Hex", OptSearchHex))
        {
            OptSearchHex = true;
            OptSearchText = search_continuing = search_wrapped = validation_failed = false;
            match_count = 0;
            match_positions.clear();
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("UTF-8", OptSearchText))
        {
            OptSearchText = true;
            OptSearchHex = search_continuing = search_wrapped = validation_failed = false;
            match_count = 0;
            match_positions.clear();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Case sensitive");
        ImGui::SameLine();
        if (ImGui::RadioButton("Dec", !OptSearchHex && !OptSearchText))
        {
            OptSearchHex = OptSearchText = search_continuing = search_wrapped = validation_failed = false;
            match_count = 0;
            match_positions.clear();
        }
        ImGui::PopID();

        // Show format option for decimal mode
        if (!OptSearchHex && !OptSearchText && OptShowDataPreview)
        {
            ImGui::SameLine();
            ImGui::Checkbox("Use preview format", &use_data_preview_format);
        }

        // Search direction buttons
        ImGui::SameLine();
        if (ImGui::Button("Find Prev")) { SearchRequested = search_backwards = true; }
        ImGui::SameLine();
        if (ImGui::Button("Find Next")) { SearchRequested = true; search_backwards = false; }

        // Close button
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("X").x);
        if (ImGui::Button("X")) OptShowSearchPanel = false;

        // Second line: search input and indicators
        // Calculate available width for search input
        float input_width = ImGui::GetContentRegionAvail().x -
                            (validation_failed ? ImGui::CalcTextSize("Invalid decimal format.").x :
                            (ImGui::CalcTextSize("(0 matches)").x + ImGui::CalcTextSize("(wrapped)").x)) -
                            style.ItemSpacing.x * (validation_failed ? 1 : 2);

        // Search input field
        ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue;
        if (OptSearchText)
        {
            ImGui::InputTextMultiline("##search", SearchInputBuf, IM_ARRAYSIZE(SearchInputBuf),
                                        ImVec2(input_width, ImGui::GetTextLineHeightWithSpacing() + 2), flags);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Multiline text can be entered. Max length of pattern: %d bytes (not symbols).",
                                    IM_ARRAYSIZE(SearchInputBuf) - 1);
        }
        else
        {
            ImGui::SetNextItemWidth(input_width);
            ImGui::InputText("##search", SearchInputBuf, IM_ARRAYSIZE(SearchInputBuf), flags);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Max length of pattern: %d bytes (not symbols).", IM_ARRAYSIZE(SearchInputBuf) - 1);
        }

        // Display validation error or match count and wrap indicator
        ImGui::SameLine();
        if (validation_failed)
        {
            const char* err = is_hex_error ? "Invalid hex format." :
                              is_text_error ? "Invalid UTF-8 text." : "Invalid decimal format.";
            ImGui::TextColored(ImVec4(1,0,0,1), "%s", err);
        }
        else
        {
            ImGui::TextDisabled("(%" _PRISizeT "u matches)", match_count);
            if (search_wrapped) ImGui::SameLine(), ImGui::TextDisabled("(wrapped)");
        }

        if (!SearchRequested) return;

        // Reset search state
        SearchRequested = validation_failed = is_hex_error = is_text_error = false;
        match_count = 0;
        match_positions.clear();
        if (SearchPattern) { free(SearchPattern); SearchPattern = nullptr; SearchPatternSize = 0; }

        // Parse search pattern
        const char* p = SearchInputBuf;
        if (OptSearchText)
        {
            size_t byte_count = strlen(p);
            if (!byte_count) { validation_failed = is_text_error = true; }
            else
            {
                bool valid = true;
                while (*p && valid)
                {
                    ImU32 codepoint;
                    int consumed = DecodeUTF8((const ImU8*)p, strlen(p), 0, &codepoint);
                    if (!consumed) { valid = false; break; }
                    p += consumed;
                }
                if (valid)
                {
                    SearchPattern = (ImU8*)malloc(byte_count * sizeof(ImU8));
                    if (!SearchPattern) { validation_failed = is_text_error = true; }
                    else { memcpy(SearchPattern, SearchInputBuf, byte_count); SearchPatternSize = byte_count; }
                }
                else validation_failed = is_text_error = true;
            }
        }
        else if (OptSearchHex)
        {
            size_t capacity = 0;
            while (*p)
            {
                p = SkipWhitespace(p);
                if (!*p) break;
                if (!isxdigit(p[0]) || !isxdigit(p[1])) break;
                capacity++; p += 2;
            }
            if (!capacity) { validation_failed = is_hex_error = true; }
            else
            {
                SearchPattern = (ImU8*)malloc(capacity);
                if (!SearchPattern) { validation_failed = is_hex_error = true; }
                else
                {
                    p = SearchInputBuf;
                    size_t byte_count = 0;
                    while (*p && !validation_failed)
                    {
                        p = SkipWhitespace(p);
                        if (!*p) break;
                        unsigned int byte;
                        if (!isxdigit(p[0]) || !isxdigit(p[1]) || sscanf(p, "%02X", &byte) != 1)
                        {
                            validation_failed = is_hex_error = true;
                            break;
                        }
                        SearchPattern[byte_count++] = (ImU8)byte; p += 2;
                    }
                    SearchPatternSize = validation_failed ? 0 : capacity;
                }
            }
        }
        else // Decimal
        {
            if (use_data_preview_format && OptShowDataPreview)
            {
                char* cleanInput = (char*)malloc(strlen(p) + 1);
                if (!cleanInput) { validation_failed = true; }
                else
                {
                    size_t pos = 0;
                    while (*p) { p = SkipWhitespace(p); if (!*p) break; cleanInput[pos++] = *p++; }
                    cleanInput[pos] = '\0';
                    bool valid = pos > 0 && strspn(cleanInput, "0123456789") == pos;
                    uint64_t value = 0;
                    if (valid && sscanf(cleanInput, "%" SCNu64, &value) == 1)
                    {
                        size_t capacity = DataTypeGetSize(PreviewDataType);
                        SearchPattern = (ImU8*)malloc(capacity);
                        if (!SearchPattern) { validation_failed = true; }
                        else
                        {
                            SearchPatternSize = capacity;
                            for (size_t i = 0; i < capacity; i++)
                            {
                                size_t shift = (PreviewEndianness == 1) ? (capacity - 1 - i) * 8 : i * 8;
                                SearchPattern[i] = (ImU8)((value >> shift) & 0xFF);
                            }
                        }
                    }
                    else { validation_failed = true; }
                    free(cleanInput);
                }
            }
            else
            {
                size_t capacity = 0;
                while (*p)
                {
                    p = SkipWhitespace(p);
                    if (!*p) break;
                    const char* start = p;
                    while (*p && *p != ' ' && !is_nbsp(p)) p++;
                    if (p > start) capacity++;
                }
                if (!capacity) { validation_failed = true; }
                else
                {
                    SearchPattern = (ImU8*)malloc(capacity);
                    if (!SearchPattern) { validation_failed = true; }
                    else
                    {
                        p = SearchInputBuf;
                        size_t i = 0;
                        while (*p && i < capacity)
                        {
                            p = SkipWhitespace(p);
                            const char* start = p;
                            while (*p && *p != ' ' && !is_nbsp(p)) p++;
                            if (p == start) { validation_failed = true; break; }
                            char temp_buf[16];
                            size_t len = IM_MIN((size_t)(p - start), sizeof(temp_buf) - 1);
                            strncpy(temp_buf, start, len); temp_buf[len] = '\0';
                            unsigned int byte;
                            if (sscanf(temp_buf, "%u", &byte) != 1 || byte > 255) { validation_failed = true; break; }
                            SearchPattern[i++] = (ImU8)byte;
                        }
                        SearchPatternSize = validation_failed ? 0 : i;
                    }
                }
            }
        }

        if (validation_failed && SearchPattern) { free(SearchPattern); SearchPattern = nullptr; SearchPatternSize = 0; }
        if (!SearchPatternSize) return;

        // Search for matches
        bool found = false;
        search_wrapped = false;
        match_count = 0;
        match_positions.clear();
        for (size_t addr = 0; addr <= mem_size - SearchPatternSize; addr++)
        {
            if (CheckPatternMatch(addr, mem_data, mem_size, SearchPattern, SearchPatternSize))
            {
                match_positions.push_back(addr);
                match_count++;
            }
        }

        // Find next/previous match
        if (match_count)
        {
            size_t start_addr = search_backwards ? (current_search_pos >= SearchPatternSize ? current_search_pos - SearchPatternSize : 0) :
                                                  (search_continuing ? current_search_pos + 1 : current_search_pos);
            for (int i = search_backwards ? match_positions.size() - 1 : 0;
                 search_backwards ? i >= 0 : i < match_positions.size();
                 search_backwards ? i-- : i++)
            {
                if ((search_backwards && match_positions[i] <= start_addr) || (!search_backwards && match_positions[i] >= start_addr))
                {
                    current_search_pos = match_positions[i];
                    found = true;
                    break;
                }
            }
            // Wrap-around search
            if (!found && (search_backwards ? current_search_pos < mem_size : current_search_pos > 0))
            {
                for (int i = search_backwards ? match_positions.size() - 1 : 0;
                     search_backwards ? i >= 0 : i < match_positions.size();
                     search_backwards ? i-- : i++)
                {
                    if ((search_backwards && match_positions[i] > start_addr) || (!search_backwards && match_positions[i] < start_addr))
                    {
                        current_search_pos = match_positions[i];
                        found = search_wrapped = true;
                        break;
                    }
                }
            }
            if (found)
            {
                SetSelection(current_search_pos, current_search_pos + SearchPatternSize - 1);
                GotoAddr = current_search_pos;
                search_continuing = true;
            }
        }
    }

    void DrawPreviewLine(const Sizes& s, void* mem_data_void, size_t mem_size, size_t base_display_addr)
    {
        IM_UNUSED(base_display_addr);
        ImU8* mem_data = (ImU8*)mem_data_void;
        ImGuiStyle& style = ImGui::GetStyle();
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Preview as:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth((s.GlyphWidth * 10.0f) + style.FramePadding.x * 2.0f + style.ItemInnerSpacing.x);

        static const ImGuiDataType supported_data_types[] = { ImGuiDataType_S8, ImGuiDataType_U8, ImGuiDataType_S16, ImGuiDataType_U16, ImGuiDataType_S32, ImGuiDataType_U32, ImGuiDataType_S64, ImGuiDataType_U64, ImGuiDataType_Float, ImGuiDataType_Double };
        if (ImGui::BeginCombo("##combo_type", DataTypeGetDesc(PreviewDataType), ImGuiComboFlags_HeightLargest))
        {
            for (int n = 0; n < IM_ARRAYSIZE(supported_data_types); n++)
            {
                ImGuiDataType data_type = supported_data_types[n];
                if (ImGui::Selectable(DataTypeGetDesc(data_type), PreviewDataType == data_type))
                    PreviewDataType = data_type;
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth((s.GlyphWidth * 6.0f) + style.FramePadding.x * 2.0f + style.ItemInnerSpacing.x);
        ImGui::Combo("##combo_endianness", &PreviewEndianness, "LE\0BE\0\0");

        char buf[128] = "";
        float x = s.GlyphWidth * 6.0f;
        bool has_value = DataPreviewAddr != (size_t)-1;
        if (has_value)
            DrawPreviewData(DataPreviewAddr, mem_data, mem_size, PreviewDataType, DataFormat_Dec, buf, (size_t)IM_ARRAYSIZE(buf));
        ImGui::Text("Dec"); ImGui::SameLine(x); ImGui::TextUnformatted(has_value ? buf : "N/A");
        if (has_value)
        {
            ImGui::SameLine();
            if (ImGui::SmallButton("Copy##CopyDec"))
                ImGui::SetClipboardText(buf);
        }
        if (has_value)
            DrawPreviewData(DataPreviewAddr, mem_data, mem_size, PreviewDataType, DataFormat_Hex, buf, (size_t)IM_ARRAYSIZE(buf));
        ImGui::Text("Hex"); ImGui::SameLine(x); ImGui::TextUnformatted(has_value ? buf : "N/A");
        if (has_value)
        {
            ImGui::SameLine();
            if (ImGui::SmallButton("Copy##CopyHex"))
                ImGui::SetClipboardText(buf);
        }
        if (has_value)
            DrawPreviewData(DataPreviewAddr, mem_data, mem_size, PreviewDataType, DataFormat_Bin, buf, (size_t)IM_ARRAYSIZE(buf));
        buf[IM_ARRAYSIZE(buf) - 1] = 0;
        ImGui::Text("Bin"); ImGui::SameLine(x); ImGui::TextUnformatted(has_value ? buf : "N/A");
        if (has_value)
        {
            ImGui::SameLine();
            if (ImGui::SmallButton("Copy##CopyBin"))
                ImGui::SetClipboardText(buf);
        }
    }

    // Utilities for Data Preview (since we don't access imgui_internal.h)
    // FIXME: This technically depends on ImGuiDataType order.
    const char* DataTypeGetDesc(ImGuiDataType data_type) const
    {
        const char* descs[] = { "Int8", "Uint8", "Int16", "Uint16", "Int32", "Uint32", "Int64", "Uint64", "Float", "Double" };
        IM_ASSERT(data_type >= 0 && data_type < IM_ARRAYSIZE(descs));
        return descs[data_type];
    }

    size_t DataTypeGetSize(ImGuiDataType data_type) const
    {
        const size_t sizes[] = { 1, 1, 2, 2, 4, 4, 8, 8, sizeof(float), sizeof(double) };
        IM_ASSERT(data_type >= 0 && data_type < IM_ARRAYSIZE(sizes));
        return sizes[data_type];
    }

    const char* DataFormatGetDesc(DataFormat data_format) const
    {
        const char* descs[] = { "Bin", "Dec", "Hex" };
        IM_ASSERT(data_format >= 0 && data_format < DataFormat_COUNT);
        return descs[data_format];
    }

    bool IsBigEndian() const
    {
        ImU16 x = 1;
        char c[2];
        memcpy(c, &x, 2);
        return c[0] != 0;
    }

    static void* EndiannessCopyBigEndian(void* _dst, void* _src, size_t s, int is_little_endian)
    {
        if (is_little_endian)
        {
            ImU8* dst = (ImU8*)_dst;
            ImU8* src = (ImU8*)_src + s - 1;
            for (int i = 0, n = (int)s; i < n; ++i)
                memcpy(dst++, src--, 1);
            return _dst;
        }
        else
        {
            return memcpy(_dst, _src, s);
        }
    }

    static void* EndiannessCopyLittleEndian(void* _dst, void* _src, size_t s, int is_little_endian)
    {
        if (is_little_endian)
        {
            return memcpy(_dst, _src, s);
        }
        else
        {
            ImU8* dst = (ImU8*)_dst;
            ImU8* src = (ImU8*)_src + s - 1;
            for (int i = 0, n = (int)s; i < n; ++i)
                memcpy(dst++, src--, 1);
            return _dst;
        }
    }

    void* EndiannessCopy(void* dst, void* src, size_t size) const
    {
        static void* (*fp)(void*, void*, size_t, int) = nullptr;
        if (fp == nullptr)
            fp = IsBigEndian() ? EndiannessCopyBigEndian : EndiannessCopyLittleEndian;
        return fp(dst, src, size, PreviewEndianness);
    }

    const char* FormatBinary(const ImU8* buf, int width) const
    {
        IM_ASSERT(width <= 64);
        size_t out_n = 0;
        static char out_buf[64 + 8 + 1];
        int n = width / 8;
        for (int j = n - 1; j >= 0; --j)
        {
            for (int i = 0; i < 8; ++i)
                out_buf[out_n++] = (buf[j] & (1 << (7 - i))) ? '1' : '0';
            out_buf[out_n++] = ' ';
        }
        IM_ASSERT(out_n < IM_ARRAYSIZE(out_buf));
        out_buf[out_n] = 0;
        return out_buf;
    }

    // [Internal]
    void DrawPreviewData(size_t addr, const ImU8* mem_data, size_t mem_size, ImGuiDataType data_type, DataFormat data_format, char* out_buf, size_t out_buf_size) const
    {
        ImU8 buf[8];
        size_t elem_size = DataTypeGetSize(data_type);
        size_t size = addr + elem_size > mem_size ? mem_size - addr : elem_size;
        if (ReadFn)
            for (int i = 0, n = (int)size; i < n; ++i)
                buf[i] = ReadFn(mem_data, addr + i, UserData);
        else
            memcpy(buf, mem_data + addr, size);

        if (data_format == DataFormat_Bin)
        {
            ImU8 binbuf[8];
            EndiannessCopy(binbuf, buf, size);
            ImSnprintf(out_buf, out_buf_size, "%s", FormatBinary(binbuf, (int)size * 8));
            return;
        }

        out_buf[0] = 0;
        switch (data_type)
        {
        case ImGuiDataType_S8:
        {
            ImS8 data = 0;
            EndiannessCopy(&data, buf, size);
            if (data_format == DataFormat_Dec) { ImSnprintf(out_buf, out_buf_size, "%hhd", data); return; }
            if (data_format == DataFormat_Hex) { ImSnprintf(out_buf, out_buf_size, "0x%02x", data & 0xFF); return; }
            break;
        }
        case ImGuiDataType_U8:
        {
            ImU8 data = 0;
            EndiannessCopy(&data, buf, size);
            if (data_format == DataFormat_Dec) { ImSnprintf(out_buf, out_buf_size, "%hhu", data); return; }
            if (data_format == DataFormat_Hex) { ImSnprintf(out_buf, out_buf_size, "0x%02x", data & 0XFF); return; }
            break;
        }
        case ImGuiDataType_S16:
        {
            ImS16 data = 0;
            EndiannessCopy(&data, buf, size);
            if (data_format == DataFormat_Dec) { ImSnprintf(out_buf, out_buf_size, "%hd", data); return; }
            if (data_format == DataFormat_Hex) { ImSnprintf(out_buf, out_buf_size, "0x%04x", data & 0xFFFF); return; }
            break;
        }
        case ImGuiDataType_U16:
        {
            ImU16 data = 0;
            EndiannessCopy(&data, buf, size);
            if (data_format == DataFormat_Dec) { ImSnprintf(out_buf, out_buf_size, "%hu", data); return; }
            if (data_format == DataFormat_Hex) { ImSnprintf(out_buf, out_buf_size, "0x%04x", data & 0xFFFF); return; }
            break;
        }
        case ImGuiDataType_S32:
        {
            ImS32 data = 0;
            EndiannessCopy(&data, buf, size);
            if (data_format == DataFormat_Dec) { ImSnprintf(out_buf, out_buf_size, "%d", data); return; }
            if (data_format == DataFormat_Hex) { ImSnprintf(out_buf, out_buf_size, "0x%08x", data); return; }
            break;
        }
        case ImGuiDataType_U32:
        {
            ImU32 data = 0;
            EndiannessCopy(&data, buf, size);
            if (data_format == DataFormat_Dec) { ImSnprintf(out_buf, out_buf_size, "%u", data); return; }
            if (data_format == DataFormat_Hex) { ImSnprintf(out_buf, out_buf_size, "0x%08x", data); return; }
            break;
        }
        case ImGuiDataType_S64:
        {
            ImS64 data = 0;
            EndiannessCopy(&data, buf, size);
            if (data_format == DataFormat_Dec) { ImSnprintf(out_buf, out_buf_size, "%lld", (long long)data); return; }
            if (data_format == DataFormat_Hex) { ImSnprintf(out_buf, out_buf_size, "0x%016llx", (long long)data); return; }
            break;
        }
        case ImGuiDataType_U64:
        {
            ImU64 data = 0;
            EndiannessCopy(&data, buf, size);
            if (data_format == DataFormat_Dec) { ImSnprintf(out_buf, out_buf_size, "%llu", (long long)data); return; }
            if (data_format == DataFormat_Hex) { ImSnprintf(out_buf, out_buf_size, "0x%016llx", (long long)data); return; }
            break;
        }
        case ImGuiDataType_Float:
        {
            float data = 0.0f;
            EndiannessCopy(&data, buf, size);
            if (data_format == DataFormat_Dec) { ImSnprintf(out_buf, out_buf_size, "%f", data); return; }
            if (data_format == DataFormat_Hex) { ImSnprintf(out_buf, out_buf_size, "%a", data); return; }
            break;
        }
        case ImGuiDataType_Double:
        {
            double data = 0.0;
            EndiannessCopy(&data, buf, size);
            if (data_format == DataFormat_Dec) { ImSnprintf(out_buf, out_buf_size, "%f", data); return; }
            if (data_format == DataFormat_Hex) { ImSnprintf(out_buf, out_buf_size, "%a", data); return; }
            break;
        }
        default:
        case ImGuiDataType_COUNT:
            break;
        } // Switch
        IM_ASSERT(0); // Shouldn't reach
    }
};

#undef IM_MAX
#undef IM_MIN
#undef _PRISizeT
#undef ImSnprintf

#ifdef _MSC_VER
#pragma warning (pop)
#endif
