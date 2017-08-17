# imgui_freetype

https://github.com/ocornut/imgui_club/tree/master/imgui_freetype

This is an attempt to replace stb_truetype (the default imgui's font rasterizer) with FreeType. Probably lots of bugs, not production ready.

1. Get latest FreeType binaries or build yourself.
2. Add imgui_freetype.h/cpp alongside your imgui sources.
3. Include imgui_freetype.h after imgui.h.
4. Call ImGuiFreeType::BuildFontAtlas() prior to calling GetTexDataXXX (and normal Build() won't be called):

```cpp
// See ImGuiFreeType::RasterizationFlags
unsigned int flags = ImGuiFreeType::DisableHinting;
ImGuiFreeType::BuildFontAtlas(io.Fonts, flags);
io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
```

Known issues:
1. Excessive font texture resolution.
2. FreeType's memory allocator is not overriden.

----

# imgui_memory_editor

https://github.com/ocornut/imgui_club/tree/master/imgui_memory_editor

Mini hexadecimal editor! Right-click for option menu. 
Features: Keyboard controls. Read-only mode. Optional Ascii display. Optional HexII display. Goto address. Highlight range/function. Read/Write handlers. 

```cpp
static MemoryEditor mem_edit_1;
mem_edit_1.DrawWindow("Memory Editor", data, data_size);
```

![memory editor](https://raw.githubusercontent.com/wiki/ocornut/imgui_club/images/memory_editor_v19.png)
