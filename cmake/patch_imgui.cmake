# Writes a copy of ImGui's imgui_draw.cpp with the right-to-left text hook (cmake/imgui_rtl.inl)
# added to ImFont::RenderText. Usage: cmake -DIN=... -DOUT=... -DINL=... -P patch_imgui.cmake
file(READ "${IN}" src)
set(anchor "void ImFont::RenderText(ImDrawList* draw_list, float size, const ImVec2& pos, ImU32 col, const ImVec4& clip_rect, const char* text_begin, const char* text_end, float wrap_width, ImDrawTextFlags flags)\n{\n")
string(FIND "${src}" "${anchor}" at)
if(at EQUAL -1)
  message(FATAL_ERROR "patch_imgui.cmake: ImFont::RenderText not found in ${IN} (a different ImGui version?)")
endif()
string(REPLACE "${anchor}" "#include \"${INL}\"\n${anchor}    if (ImCimaRenderTextRtl(this, draw_list, size, pos, col, clip_rect, text_begin, text_end, wrap_width, flags))\n        return;\n" src "${src}")
file(WRITE "${OUT}.tmp" "${src}")
configure_file("${OUT}.tmp" "${OUT}" COPYONLY)  # rewritten only when it changed
file(REMOVE "${OUT}.tmp")
