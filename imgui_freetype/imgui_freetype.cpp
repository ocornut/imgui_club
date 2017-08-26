// Wrapper to use Freetype (instead of stb_truetype) for Dear ImGui
// Get latest version at http://www.github.com/ocornut/imgui_club
// Original code by @Vuhdo (Aleksei Skriabin)

// Changelog:
// - v0.50: imported from https://github.com/Vuhdo/imgui_freetype, updated for latest changes in ImFontAtlas, minor tweaks.
// - v0.51: cleanup, optimizations, support for ImFontConfig::RasterizerFlags

#include "imgui_freetype.h"
#include <stdint.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_SYNTHESIS_H
#include "imgui_internal.h"

#ifdef _MSC_VER
#pragma warning (disable: 4505) // unreferenced local function has been removed (stb stuff)
#endif

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-function"          // warning: 'xxxx' defined but not used
#endif

namespace 
{
    // Glyph metrics:
    // --------------
    //
    //                       xmin                     xmax
    //                        |                         |
    //                        |<-------- width -------->|
    //                        |                         |
    //              |         +-------------------------+----------------- ymax
    //              |         |    ggggggggg   ggggg    |     ^        ^
    //              |         |   g:::::::::ggg::::g    |     |        |
    //              |         |  g:::::::::::::::::g    |     |        |
    //              |         | g::::::ggggg::::::gg    |     |        |
    //              |         | g:::::g     g:::::g     |     |        |
    //    offsetX  -|-------->| g:::::g     g:::::g     |  offsetY     |
    //              |         | g:::::g     g:::::g     |     |        |
    //              |         | g::::::g    g:::::g     |     |        |
    //              |         | g:::::::ggggg:::::g     |     |        |
    //              |         |  g::::::::::::::::g     |     |      height
    //              |         |   gg::::::::::::::g     |     |        |
    //  baseline ---*---------|---- gggggggg::::::g-----*--------      |
    //            / |         |             g:::::g     |              |
    //     origin   |         | gggggg      g:::::g     |              |
    //              |         | g:::::gg   gg:::::g     |              |
    //              |         |  g::::::ggg:::::::g     |              |
    //              |         |   gg:::::::::::::g      |              |
    //              |         |     ggg::::::ggg        |              |
    //              |         |         gggggg          |              v
    //              |         +-------------------------+----------------- ymin
    //              |                                   |
    //              |------------- advanceX ----------->|

    /// A structure that describe a glyph.
    struct GlyphInfo 
    {
        float Width;		// Glyph's width in pixels.
        float Height;		// Glyph's height in pixels.
        float OffsetX;		// The distance from the origin ("pen position") to the left of the glyph.
        float OffsetY;		// The distance from the origin to the top of the glyph. This is usually a value < 0.
        float AdvanceX;		// The distance from the origin to the origin of the next glyph. This is usually a value > 0.
    };

    // Rasterized glyph image (8-bit alpha coverage).
    struct GlyphBitmap 
    {
        static const uint32_t MaxWidth = 256;
        static const uint32_t MaxHeight = 256;
        uint8_t grayscale[MaxWidth * MaxHeight];
        uint32_t width, height, pitch;
    };

    // Font parameters and metrics.
    struct FontInfo 
    {
        uint32_t    PixelHeight;        // Size this font was generated with.
        float       Ascender;           // The pixel extents above the baseline in pixels (typically positive).
        float       Descender;          // The extents below the baseline in pixels (typically negative).
        float       LineSpacing;        // The baseline-to-baseline distance. Note that it usually is larger than the sum of the ascender and descender taken as absolute values. There is also no guarantee that no glyphs extend above or below subsequent baselines when using this distance. Think of it as a value the designer of the font finds appropriate.
        float       LineGap;            // The spacing in pixels between one row's descent and the next row's ascent.
        float       MaxAdvanceWidth;    // This field gives the maximum horizontal cursor advance for all glyphs in the font.
        uint32_t    GlyphsCount;        // The number of glyphs available in the font face.
        const char* FamilyName;
        const char* StyleName;
    };

    // FreeType glyph rasterizer.
    // NB: No ctor/dtor, explicitly call Init()/Shutdown()
    struct FreeTypeFont
    {
        bool        Init(const ImFontConfig& cfg);      // Initialize from an external data buffer. Doesn't copy data, and you must ensure it stays valid up to this object lifetime.
        void        Shutdown();
        void        SetPixelHeight(int pixel_height);   // Change font pixel size. All following calls to RasterizeGlyph() will use this size
        bool        RasterizeGlyph(uint32_t codepoint, GlyphInfo& glyph_info, GlyphBitmap& glyph_bitmap, unsigned int font_flags, FT_Int32 freetype_flags); // Generate glyph image.

        // [Internals]
        FontInfo    m_info;                             // Font descriptor of the current font.
        FT_Library  m_library;
        FT_Face     m_face;
    };

    // From SDL_ttf: Handy routines for converting from fixed point
    #define FT_CEIL(X)  (((X + 63) & -64) / 64)

    bool FreeTypeFont::Init(const ImFontConfig& cfg)
    {
        // FIXME: substitute allocator
        FT_Error error = FT_Init_FreeType(&m_library);
        if (error != 0)
            return false;
        error = FT_New_Memory_Face(m_library, (uint8_t*)cfg.FontData, (uint32_t)cfg.FontDataSize, (uint32_t)cfg.FontNo, &m_face);
        if (error != 0)
            return false;
        error = FT_Select_Charmap(m_face, FT_ENCODING_UNICODE);
        if (error != 0)
            return false;
        memset(&m_info, 0, sizeof(m_info));
        SetPixelHeight((uint32_t)cfg.SizePixels);

        // Fill up the font info
        //FT_Size_Metrics metrics = m_face->size->metrics;
        m_info.PixelHeight = (uint32_t)cfg.SizePixels;
        m_info.GlyphsCount = m_face->num_glyphs;
        m_info.FamilyName = m_face->family_name;
        m_info.StyleName = m_face->style_name;
        return true;
    }

    void FreeTypeFont::Shutdown()
    {
        if (m_face) 
        {
            FT_Done_Face(m_face);
            m_face = NULL;
            FT_Done_FreeType(m_library);
            m_library = NULL;
        }
    }

    void FreeTypeFont::SetPixelHeight(int pixel_height) 
    {
        // I'm not sure how to deal with font sizes properly.
        // As far as I understand, currently ImGui assumes that the 'pixel_height' is a maximum height of an any given glyph,
        // i.e. it's the sum of font's ascender and descender. Seems strange to me.
        FT_Size_RequestRec req;
        req.type = FT_SIZE_REQUEST_TYPE_REAL_DIM;
        req.width = 0;
        req.height = (uint32_t)pixel_height * 64;
        req.horiResolution = 0;
        req.vertResolution = 0;
        FT_Request_Size(m_face, &req);

        // update font info
        FT_Size_Metrics metrics = m_face->size->metrics;
        m_info.PixelHeight = (uint32_t)pixel_height;
        m_info.Ascender = (float)FT_CEIL(metrics.ascender);
        m_info.Descender = (float)FT_CEIL(metrics.descender);
        m_info.LineSpacing = (float)FT_CEIL(metrics.height);
        m_info.LineGap = (float)FT_CEIL(metrics.height - metrics.ascender + metrics.descender);
        m_info.MaxAdvanceWidth = (float)FT_CEIL(metrics.max_advance);
    }

    bool FreeTypeFont::RasterizeGlyph(uint32_t codepoint, GlyphInfo& glyph_info, GlyphBitmap& glyph_bitmap, unsigned int font_flags, FT_Int32 load_flags)
    {
        uint32_t glyph_index = FT_Get_Char_Index(m_face, codepoint);
        FT_Error error = FT_Load_Glyph(m_face, glyph_index, load_flags);
        if (error)
            return false;

        // Need an outline for this to work
        FT_GlyphSlot slot = m_face->glyph;
        IM_ASSERT(slot->format == FT_GLYPH_FORMAT_OUTLINE);

        if (font_flags & ImGuiFreeType::Bold)
            FT_GlyphSlot_Embolden(slot);
        if (font_flags & ImGuiFreeType::Oblique)
            FT_GlyphSlot_Oblique(slot);

        // Retrieve the glyph
        FT_Glyph glyph_desc;
        error = FT_Get_Glyph(slot, &glyph_desc);
        if (error != 0)
            return false;

        // Rasterize
        error = FT_Glyph_To_Bitmap(&glyph_desc, FT_RENDER_MODE_NORMAL, 0, 1);
        if (error != 0)
            return false;

        FT_BitmapGlyph ft_bitmap = (FT_BitmapGlyph)glyph_desc;
        glyph_info.AdvanceX = (float)FT_CEIL(slot->advance.x);
        glyph_info.OffsetX = (float)ft_bitmap->left;
        glyph_info.OffsetY = -(float)ft_bitmap->top;
        glyph_info.Width = (float)ft_bitmap->bitmap.width;
        glyph_info.Height = (float)ft_bitmap->bitmap.rows;

        glyph_bitmap.width = ft_bitmap->bitmap.width;
        glyph_bitmap.height = ft_bitmap->bitmap.rows;
        glyph_bitmap.pitch = (uint32_t)ft_bitmap->bitmap.pitch;

        IM_ASSERT(glyph_bitmap.pitch <= GlyphBitmap::MaxWidth);
        if (glyph_bitmap.width > 0)
            memcpy(glyph_bitmap.grayscale, ft_bitmap->bitmap.buffer, glyph_bitmap.pitch * glyph_bitmap.height);

        // Cleanup
        FT_Done_Glyph(glyph_desc);

        return true;
    }
}

#define STBRP_ASSERT(x)    IM_ASSERT(x)
#define STBRP_STATIC
#define STB_RECT_PACK_IMPLEMENTATION
#include "stb_rect_pack.h"

bool ImGuiFreeType::BuildFontAtlas(ImFontAtlas* atlas, unsigned int extra_flags)
{
    IM_ASSERT(atlas->ConfigData.Size > 0);
    IM_ASSERT(atlas->TexGlyphPadding == 1); // Not supported

    ImFontAtlasBuildRegisterDefaultCustomRects(atlas);

    atlas->TexID = NULL;
    atlas->TexWidth = atlas->TexHeight = 0;
    atlas->TexUvWhitePixel = ImVec2(0, 0);
    atlas->ClearTexData();

    ImVector<FreeTypeFont> fonts;
    fonts.resize(atlas->ConfigData.Size);

    ImVec2 max_glyph_size(1.0f, 1.0f);

    // Count glyphs/ranges, initialize font
    int total_glyphs_count = 0;
    int total_ranges_count = 0;
    for (int input_i = 0; input_i < atlas->ConfigData.Size; input_i++) 
    {
        ImFontConfig& cfg = atlas->ConfigData[input_i];
        FreeTypeFont& font_face = fonts[input_i];
        IM_ASSERT(cfg.DstFont && (!cfg.DstFont->IsLoaded() || cfg.DstFont->ContainerAtlas == atlas));

        if (!font_face.Init(cfg))
            return false;

        max_glyph_size.x = ImMax(max_glyph_size.x, font_face.m_info.MaxAdvanceWidth);
        max_glyph_size.y = ImMax(max_glyph_size.y, font_face.m_info.Ascender - font_face.m_info.Descender);

        if (!cfg.GlyphRanges)
            cfg.GlyphRanges = atlas->GetGlyphRangesDefault();
        for (const ImWchar* in_range = cfg.GlyphRanges; in_range[0] && in_range[ 1 ]; in_range += 2, total_ranges_count++) 
            total_glyphs_count += (in_range[1] - in_range[0]) + 1;
    }

    // We need a width for the skyline algorithm. Using a dumb heuristic here to decide of width. User can override TexDesiredWidth and TexGlyphPadding if they wish.
    // Width doesn't really matter much, but some API/GPU have texture size limitations and increasing width can decrease height.
    atlas->TexWidth = (atlas->TexDesiredWidth > 0) ? atlas->TexDesiredWidth : (total_glyphs_count > 4000) ? 4096 : (total_glyphs_count > 2000) ? 2048 : (total_glyphs_count > 1000) ? 1024 : 512;

    // We don't do the original first pass to determine texture height, but just rough estimate.
    // Looks ugly inaccurate and excessive, but AFAIK with FreeType we actually need to render glyphs to get exact sizes.
    // Alternatively, we could just render all glyphs into a big shadow buffer, get their sizes, do the rectangle packing and just copy back from the 
    // shadow buffer to the texture buffer. Will give us an accurate texture height, but eat a lot of temp memory. Probably no one will notice.)
    const int total_rects = total_glyphs_count + atlas->CustomRects.size();
    float min_rects_per_row = ceilf((atlas->TexWidth / (max_glyph_size.x + 1.0f)));
    float min_rects_per_column = ceilf(total_rects / min_rects_per_row);
    atlas->TexHeight = (int)(min_rects_per_column * (max_glyph_size.y + 1.0f));

    // Create texture
    atlas->TexHeight = ImUpperPowerOfTwo(atlas->TexHeight);
    atlas->TexPixelsAlpha8 = (unsigned char*)ImGui::MemAlloc(atlas->TexWidth * atlas->TexHeight);
    memset(atlas->TexPixelsAlpha8, 0, atlas->TexWidth * atlas->TexHeight);

    // Start packing
    ImVector<stbrp_node> pack_nodes;
    pack_nodes.resize(total_rects);
    stbrp_context context;
    stbrp_init_target(&context, atlas->TexWidth, atlas->TexHeight, pack_nodes.Data, total_rects);

    // Pack our extra data rectangles first, so it will be on the upper-left corner of our texture (UV will have small values).
    ImFontAtlasBuildPackCustomRects(atlas, &context);

    // Render characters, setup ImFont and glyphs for runtime
    for (int input_i = 0; input_i < atlas->ConfigData.Size; input_i++)
    {
        ImFontConfig& cfg = atlas->ConfigData[input_i];
        FreeTypeFont& font_face = fonts[input_i];
        unsigned font_flags = cfg.RasterizerFlags | extra_flags;
        ImFont* dst_font = cfg.DstFont;

        FT_Int32 freetype_flags = FT_LOAD_NO_BITMAP;
        if (font_flags & ImGuiFreeType::NoHinting)      freetype_flags |= FT_LOAD_NO_HINTING;
        if (font_flags & ImGuiFreeType::NoAutoHint)     freetype_flags |= FT_LOAD_NO_AUTOHINT;
        if (font_flags & ImGuiFreeType::ForceAutoHint)  freetype_flags |= FT_LOAD_FORCE_AUTOHINT;
        if (font_flags & ImGuiFreeType::LightHinting)   
            freetype_flags |= FT_LOAD_TARGET_LIGHT;
        else if (font_flags & ImGuiFreeType::MonoHinting)   
            freetype_flags |= FT_LOAD_TARGET_MONO;
        else                                                
            freetype_flags |= FT_LOAD_TARGET_NORMAL;

        float ascent = font_face.m_info.Ascender;
        float descent = font_face.m_info.Descender;
        ImFontAtlasBuildSetupFont(atlas, dst_font, &cfg, ascent, descent);
        float off_x = cfg.GlyphOffset.x;
        float off_y = cfg.GlyphOffset.y + (float)(int)(dst_font->Ascent + 0.5f);

        dst_font->FallbackGlyph = NULL; // Always clear fallback so FindGlyph can return NULL. It will be set again in BuildLookupTable()
        for (const ImWchar* in_range = cfg.GlyphRanges; in_range[0] && in_range[1]; in_range += 2) 
        {
            for (uint32_t codepoint = in_range[0]; codepoint <= in_range[1]; ++codepoint) 
            {
                if (cfg.MergeMode && dst_font->FindGlyph((unsigned short)codepoint))
                    continue;

                GlyphInfo glyph_info;
                GlyphBitmap glyph_bitmap;
                font_face.RasterizeGlyph(codepoint, glyph_info, glyph_bitmap, font_flags, freetype_flags);

                // Copy rasterized pixels to main texture
                stbrp_rect rect;
                rect.w = (uint16_t)glyph_bitmap.width + 1;		// account for texture filtering
                rect.h = (uint16_t)glyph_bitmap.height + 1;
                stbrp_pack_rects(&context, &rect, 1);
                const uint8_t* src = glyph_bitmap.grayscale;
                uint8_t* dst = atlas->TexPixelsAlpha8 + rect.y * atlas->TexWidth + rect.x;
                for (uint32_t yy = 0; yy < glyph_bitmap.height; ++yy)
                {
                    memcpy(dst, src, glyph_bitmap.width);
                    src += glyph_bitmap.pitch;
                    dst += atlas->TexWidth;
                }

                dst_font->Glyphs.resize(dst_font->Glyphs.Size + 1);
                ImFont::Glyph& glyph = dst_font->Glyphs.back();
                glyph.Codepoint = (ImWchar)codepoint;
                glyph.X0 = glyph_info.OffsetX + off_x;
                glyph.Y0 = glyph_info.OffsetY + off_y;
                glyph.X1 = glyph.X0 + glyph_info.Width;
                glyph.Y1 = glyph.Y0 + glyph_info.Height;
                glyph.U0 = rect.x / (float)atlas->TexWidth;
                glyph.V0 = rect.y / (float)atlas->TexHeight;
                glyph.U1 = (rect.x + glyph_info.Width) / (float)atlas->TexWidth;
                glyph.V1 = (rect.y + glyph_info.Height) / (float)atlas->TexHeight;
                glyph.XAdvance = (glyph_info.AdvanceX + cfg.GlyphExtraSpacing.x);  // Bake spacing into XAdvance

                if (cfg.PixelSnapH)
                    glyph.XAdvance = (float)(int)(glyph.XAdvance + 0.5f);
                dst_font->MetricsTotalSurface += (int)((glyph.U1 - glyph.U0) * atlas->TexWidth + 1.99f) * (int)((glyph.V1 - glyph.V0) * atlas->TexHeight + 1.99f); // +1 to account for average padding, +0.99 to round
            }
        }
        cfg.DstFont->BuildLookupTable();
    }

    // Cleanup
    for (int n = 0; n < fonts.Size; n++)
        fonts[n].Shutdown();

    // Render into our custom data block
    ImFontAtlasBuildRenderDefaultTexData(atlas);

    return true;
}
