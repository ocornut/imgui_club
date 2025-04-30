# Index

Officially maintained small extensions for Dear ImGui:
- [Memory Editor](#imgui_memory_editor)
- [Multi-Context Compositor](#imgui_multicontext_compositor)
- [Threaded Rendering](#imgui_threaded_rendering)

<BR>You also can find many useful third-party snippets here: https://github.com/ocornut/imgui/wiki/Useful-Extensions

# imgui_memory_editor

https://github.com/ocornut/imgui_club/tree/main/imgui_memory_editor

Mini hexadecimal editor! Right-click for option menu.
Features: Keyboard controls. Read-only mode. Optional Ascii display. Optional HexII display. Goto address. Highlight range/function. Read/Write handlers.

**Usage**
```cpp
static MemoryEditor mem_edit;
mem_edit.DrawWindow("Memory Editor", data, data_size);
```
![memory editor](https://raw.githubusercontent.com/wiki/ocornut/imgui_club/images/memory_editor_v19.gif)

![memory editor](https://raw.githubusercontent.com/wiki/ocornut/imgui_club/images/memory_editor_v32.png)

# imgui_multicontext_compositor

https://github.com/ocornut/imgui_club/tree/main/imgui_multicontext_compositor

When using and displaying multiple contexts simultaneously (e.g. Update vs Rendering domains):
- Manage z-order of contexts
- Manage input routing
- Allow drag and drop between contexts

![multi_context_compositor.gif](https://github.com/user-attachments/assets/220a9469-db15-419a-8f29-3e0bf7025c84)

# imgui_threaded_rendering

https://github.com/ocornut/imgui_club/tree/main/imgui_threaded_rendering

Helper to take a snapshot of ImDrawData in order to render it later.
