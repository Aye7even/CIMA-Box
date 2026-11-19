// CIMA Box: right-to-left text (Arabic, Persian, Urdu, Hebrew) in ImGui.
// Inserted into imgui_draw.cpp at build time by cmake/patch_imgui.cmake, just before
// ImFont::RenderText. ImGui draws glyphs left to right in the order they are stored; when a text has
// right-to-left letters, each line (after word wrapping, which works on the text as written) is
// handed to the app to be shaped and reordered, then drawn as usual. Lines that read right to left
// line up on the right of the wrap width. The app sets the two functions (src/bidi.cpp); until it
// does, nothing changes.
bool (*GCimaTextHasRtl)(const char* b, const char* e) = NULL;
void (*GCimaTextVisual)(const char* b, const char* e, ImVector<char>* out, bool* rtl) = NULL;

static const int ImCimaDrawTextFlags_Visual = 1 << 29;  // a line that's already in drawing order

static bool ImCimaRenderTextRtl(ImFont* font, ImDrawList* draw_list, float size, const ImVec2& pos, ImU32 col, const ImVec4& clip_rect,
                                const char* text_begin, const char* text_end, float wrap_width, ImDrawTextFlags flags)
{
    if (!GCimaTextHasRtl || !GCimaTextVisual || (flags & ImCimaDrawTextFlags_Visual))
        return false;
    if (!text_end)
        text_end = text_begin + ImStrlen(text_begin);
    if (!GCimaTextHasRtl(text_begin, text_end))
        return false;
    const bool wrap = wrap_width > 0.0f;
    float y = pos.y;
    ImVector<char> line;
    const char* s = text_begin;
    while (s < text_end && y <= clip_rect.w)
    {
        const char* nl = (const char*)ImMemchr(s, '\n', (size_t)(text_end - s));
        const char* le = nl ? nl : text_end;
        const char* eol = wrap ? ImFontCalcWordWrapPositionEx(font, size, s, le, wrap_width, flags) : le;
        if (eol <= s && s < le)  // always move on by at least one character
        {
            unsigned int c;
            eol = s + ImTextCharFromUtf8(&c, s, le);
        }
        if (y + size >= clip_rect.y && eol > s)
        {
            bool rtl = false;
            GCimaTextVisual(s, eol, &line, &rtl);
            float x = pos.x;
            if (rtl && wrap)
            {
                const float w = font->CalcTextSizeA(size, FLT_MAX, 0.0f, line.Data, line.Data + line.Size).x;
                x += ImMax(0.0f, wrap_width - w);
            }
            if (line.Size > 0)
                font->RenderText(draw_list, size, ImVec2(x, y), col, clip_rect, line.Data, line.Data + line.Size, 0.0f,
                                 flags | ImCimaDrawTextFlags_Visual);
        }
        y += size;
        if (eol >= le)
            s = nl ? le + 1 : text_end;
        else
            for (s = eol; s < le && (*s == ' ' || *s == '\t'); s++) {}
    }
    return true;
}

