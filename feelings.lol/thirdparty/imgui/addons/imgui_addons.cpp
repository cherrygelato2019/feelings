#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "imgui_addons.h"
#include "../imgui.h"
#include "../imgui_internal.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <map>
#include <string>
#include <cstring>
#include <iostream>

using namespace ImGui;
using std::string;

ImVec4 ImAdd::HexToColorVec4(unsigned int hex_color, float alpha)
{
    ImVec4 color;

    color.x = ((hex_color >> 16) & 0xFF) / 255.0f;
    color.y = ((hex_color >> 8) & 0xFF) / 255.0f;
    color.z = (hex_color & 0xFF) / 255.0f;
    color.w = alpha;

    return color;
}

void ImAdd::Text(ImVec4 color, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    PushStyleColor(ImGuiCol_Text, color);
    TextV(fmt, args);
    PopStyleColor();
    va_end(args);
}

void ImAdd::Text(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    TextV(fmt, args);
    va_end(args);
}

void ImAdd::TextUnformatted(const char* text)
{
    ImGui::TextUnformatted(text ? text : "");
}

void ImAdd::SeparatorText(const char* label)
{
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return;

    const ImGuiStyle& style = GetStyle();
    const ImVec2 label_size = CalcTextSize(label ? label : "", NULL, true);
    const float line_thickness = 1.0f;
    const float sep_height = ImMax(label_size.y + style.FramePadding.y * 0.5f, style.ItemSpacing.y * 2.0f);

    const ImVec2 pos = window->DC.CursorPos;
    const float width = GetContentRegionAvail().x;
    const ImRect bb(pos, pos + ImVec2(width, sep_height));
    ItemSize(bb);
    if (!ItemAdd(bb, 0))
        return;

    ImDrawList* draw = window->DrawList;
    const ImU32 line_col = GetColorU32(ImGuiCol_Separator);
    const ImU32 text_col = GetColorU32(ImGuiCol_Text);
    const float mid_y = bb.Min.y + sep_height * 0.5f;
    const float text_x = bb.Min.x + style.FramePadding.x;
    const float gap = 6.0f;

    if (label && label[0])
    {
        draw->AddText(ImVec2(text_x, mid_y - label_size.y * 0.5f), text_col, label);
        const float line_start = text_x + label_size.x + gap;
        if (line_start < bb.Max.x)
            draw->AddLine(ImVec2(line_start, mid_y), ImVec2(bb.Max.x, mid_y), line_col, line_thickness);
    }
    else
    {
        draw->AddLine(ImVec2(bb.Min.x, mid_y), ImVec2(bb.Max.x, mid_y), line_col, line_thickness);
    }
}

void ImAdd::TextV(const char* fmt, va_list args)
{
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;

    const char* text, * text_end;
    ImFormatStringToTempBufferV(&text, &text_end, fmt, args);

    // Accept null ranges
    if (text == text_end)
        text = text_end = "";

    // Calculate length
    const char* text_begin = text;
    if (text_end == NULL)
        text_end = text + strlen(text); // FIXME-OPT

    const ImVec2 pos = window->DC.CursorPos;
    const ImVec2 text_size = CalcTextSize(text, NULL, true);

    ImRect bb(pos, pos + text_size);
    ItemSize(text_size, 0.0f);
    if (!ItemAdd(bb, 0))
        return;

    // Render (we don't hide text after ## in this end-user function)
    const ImU32 outline = IM_COL32( 0, 0, 0, 255 );
    PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(outline));
    {
        RenderTextWrapped(bb.Min + ImVec2(-1, -1), text_begin, text_end, 0.0f);
        RenderTextWrapped(bb.Min + ImVec2(1, -1), text_begin, text_end, 0.0f);
        RenderTextWrapped(bb.Min + ImVec2(1, 1), text_begin, text_end, 0.0f);
        RenderTextWrapped(bb.Min + ImVec2(-1, 1), text_begin, text_end, 0.0f);
    }
    PopStyleColor();
    RenderTextWrapped(bb.Min, text_begin, text_end, 0.0f);
}
bool ImAdd::Button(const char* label, const ImVec2& size_arg, ImGuiButtonFlags flags)
{
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);
    const ImVec2 label_size = CalcTextSize(label, NULL, true);

    ImVec2 pos = window->DC.CursorPos;
    if ((flags & ImGuiButtonFlags_AlignTextBaseLine) && style.FramePadding.y < window->DC.CurrLineTextBaseOffset)
        pos.y += window->DC.CurrLineTextBaseOffset - style.FramePadding.y;

    // Modify padding to be larger and the height to be shorter
    ImVec2 size = CalcItemSize(size_arg, label_size.x + style.FramePadding.x * 4.0f, label_size.y + style.FramePadding.y * 1.5f); // Increased padding horizontally and reduced height

    const ImRect bb(pos, pos + size);
    ItemSize(size, style.FramePadding.y);
    if (!ItemAdd(bb, id))
        return false;

    bool hovered, held;
    bool pressed = ButtonBehavior(bb, id, &hovered, &held, flags);

    // Colors
    ImVec4 colFrame = GetStyleColorVec4((held && hovered) ? ImGuiCol_ButtonActive : hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button);

    // Animations
    struct stColors_State {
        ImVec4 Frame;
    };

    static std::map<ImGuiID, stColors_State> anim;
    auto it_anim = anim.find(id);

    if (it_anim == anim.end())
    {
        anim.insert({ id, stColors_State() });
        it_anim = anim.find(id);

        it_anim->second.Frame = colFrame;
    }

    it_anim->second.Frame = ImLerp(it_anim->second.Frame, colFrame, 1.0f / IMADD_ANIMATIONS_SPEED * ImGui::GetIO().DeltaTime);

    // Render
    RenderNavHighlight(bb, id);
    RenderFrame(bb.Min, bb.Max, GetColorU32(it_anim->second.Frame), true, style.FrameRounding);
    window->DrawList->AddRectFilledMultiColor(bb.Min + ImVec2(style.FrameBorderSize, style.FrameBorderSize), bb.Max - ImVec2(style.FrameBorderSize, style.FrameBorderSize), GetColorU32(ImGuiCol_BorderShadow, 0), GetColorU32(ImGuiCol_BorderShadow, 0), GetColorU32(ImGuiCol_BorderShadow), GetColorU32(ImGuiCol_BorderShadow));

    window->DrawList->AddText(pos + size / 2 - label_size / 2 + ImVec2(1, 1), GetColorU32(ImGuiCol_TitleBg), label);
    window->DrawList->AddText(pos + size / 2 - label_size / 2 + ImVec2(-1, -1), GetColorU32(ImGuiCol_TitleBg), label);
    window->DrawList->AddText(pos + size / 2 - label_size / 2 + ImVec2(-1, 1), GetColorU32(ImGuiCol_TitleBg), label);
    window->DrawList->AddText(pos + size / 2 - label_size / 2 + ImVec2(1, -1), GetColorU32(ImGuiCol_TitleBg), label);
    window->DrawList->AddText(pos + size / 2 - label_size / 2, GetColorU32(ImGuiCol_Text), label);

    return pressed;
}

static void ShortKeyName(int k, bool listening, char* buf, int buf_n)
{
    if (listening)
    {
        strcpy_s(buf, buf_n, "...");
        return;
    }
    if (k <= 0)
    {
        strcpy_s(buf, buf_n, "-");
        return;
    }
    if (k >= 0 && k < (int)IM_ARRAYSIZE(szKeyNames))
    {
        const char* s = szKeyNames[k];
        if (s && strncmp(s, "KEY ", 4) == 0)
            s += 4;
        if (s && _stricmp(s, "INSERT") == 0) { strcpy_s(buf, buf_n, "INS"); return; }
        if (s && _stricmp(s, "DELETE") == 0) { strcpy_s(buf, buf_n, "DEL"); return; }
        if (s && _stricmp(s, "ESCAPE") == 0) { strcpy_s(buf, buf_n, "ESC"); return; }
        if (s && _stricmp(s, "LBUTTON") == 0) { strcpy_s(buf, buf_n, "M1"); return; }
        if (s && _stricmp(s, "RBUTTON") == 0) { strcpy_s(buf, buf_n, "M2"); return; }
        if (s && _stricmp(s, "MBUTTON") == 0) { strcpy_s(buf, buf_n, "M3"); return; }
        if (s && _stricmp(s, "XBUTTON1") == 0) { strcpy_s(buf, buf_n, "M4"); return; }
        if (s && _stricmp(s, "XBUTTON2") == 0) { strcpy_s(buf, buf_n, "M5"); return; }
        if (s && s[0] && _stricmp(s, "Unknown") != 0)
        {
            strncpy_s(buf, buf_n, s, _TRUNCATE);
            if ((int)strlen(buf) > 4)
                buf[4] = 0;
            return;
        }
    }
    strcpy_s(buf, buf_n, "-");
}

static float KeyBindChipWidth()
{
    return ImMax(32.0f, IM_ROUND(GImGui->FontSize * 2.8f));
}

static bool BindKeyCapture(ImGuiID id, int* k)
{
    ImGuiContext& g = *GImGui;
    if (g.ActiveId != id)
        return false;

    SetActiveIdUsingAllKeyboardKeys();

    static std::map<ImGuiID, bool> wait_release;
    if (g.ActiveIdIsJustActivated)
        wait_release[id] = true;

    bool& wait = wait_release[id];
    bool any_down = false;
    int picked = 0;

    for (int vk = 1; vk <= 0xA5; vk++)
    {
        if ((GetAsyncKeyState(vk) & 0x8000) == 0)
            continue;
        any_down = true;
        const bool mouse = (vk >= VK_LBUTTON && vk <= VK_XBUTTON2);
        if (wait && mouse)
            continue;
        if (vk == VK_ESCAPE)
        {
            picked = -1;
            break;
        }
        if (picked == 0)
            picked = vk;
    }

    if (!any_down)
        wait = false;

    if (picked == -1)
    {
        *k = 0;
        ClearActiveID();
        wait = false;
        return true;
    }
    if (picked > 0 && (!wait || picked > VK_XBUTTON2))
    {
        *k = picked;
        ClearActiveID();
        wait = false;
        return true;
    }
    return false;
}

static bool DrawHotkeysPopup(int* mode)
{
    const ImVec4 accent = ImGui::GetStyleColorVec4(ImGuiCol_Separator);
    const ImVec4 idle = ImVec4(150.0f / 255.0f, 150.0f / 255.0f, 150.0f / 255.0f, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0, 0, 0, 0.80f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::GetStyleColorVec4(ImGuiCol_SliderGrab));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 4.0f));

    bool changed = false;
    if (ImGui::BeginPopup("##hotkeys", ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize))
    {
        const char* modes[] = { "Hold", "Toggle", "Always" };
        const int cur = mode ? *mode : ImAdd::KeybindMode_Toggle;
        for (int i = 0; i < 3; ++i)
        {
            const bool selected = (cur == i);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const ImVec2 sz(ImMax(ImGui::GetContentRegionAvail().x, 72.0f), ImGui::GetFontSize() + 4.0f);
            ImGui::InvisibleButton(modes[i], sz);
            ImVec4 col = idle;
            if (ImGui::IsItemHovered())
                col = ImLerp(idle, accent, 0.55f);
            if (selected)
                col = accent;
            ImGui::GetWindowDrawList()->AddText(p, ImGui::GetColorU32(col), modes[i]);
            if (ImGui::IsItemClicked() && mode)
            {
                *mode = i;
                changed = true;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }

    ImGui::PopStyleVar(5);
    ImGui::PopStyleColor(2);
    return changed;
}

static bool DrawKeyBindChip(const char* str_id, int* k, int* mode = nullptr)
{
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(str_id);
    const float h = IM_ROUND(g.FontSize);
    const float w = KeyBindChipWidth();
    const ImVec2 pos = window->DC.CursorPos;
    const ImRect bb(pos, pos + ImVec2(w, h));

    ItemSize(bb, style.FramePadding.y);
    if (!ItemAdd(bb, id))
        return false;

    const bool hovered = ItemHoverable(bb, id, 0);
    if (hovered)
        g.MouseCursor = ImGuiMouseCursor_Hand;

    if (hovered && g.IO.MouseClicked[0])
    {
        SetActiveID(id, window);
        SetActiveIdUsingAllKeyboardKeys();
        FocusWindow(window);
    }
    else if (g.ActiveId == id && g.IO.MouseClicked[0] && !hovered)
        ClearActiveID();

    if (mode && hovered && g.IO.MouseClicked[1])
    {
        if (g.ActiveId == id)
            ClearActiveID();
        OpenPopup("##hotkeys");
    }

    const bool changed = BindKeyCapture(id, k);
    const bool listening = (g.ActiveId == id);
    DrawHotkeysPopup(mode);

    char buf[16];
    ShortKeyName(*k, listening, buf, IM_ARRAYSIZE(buf));

    ImU32 bg = GetColorU32(listening ? ImGuiCol_FrameBgActive : hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg);
    window->DrawList->AddRectFilled(bb.Min, bb.Max, bg, style.FrameRounding);
    window->DrawList->AddRect(bb.Min, bb.Max, GetColorU32(listening ? ImGuiCol_Separator : ImGuiCol_Border), style.FrameRounding);
    const ImVec2 ts = CalcTextSize(buf);
    const ImVec2 tp = ImVec2(bb.Min.x + (bb.GetWidth() - ts.x) * 0.5f, bb.Min.y + (bb.GetHeight() - ts.y) * 0.5f - 1.0f);
    window->DrawList->AddText(tp, GetColorU32(listening ? ImGuiCol_Separator : ImGuiCol_Text), buf);
    return changed;
}

static ImGuiID g_cp_id = 0;
static int g_cp_frame = 0;
static int g_cp_seen_frame = -1;
static float* g_cp_col = nullptr;
static ImRect g_cp_anchor;
static std::map<ImGuiID, ImVec4> g_cp_hsv;

static void DrawCheckerboard(ImDrawList* dl, const ImRect& r, float step)
{
    if (!(r.Min.x < r.Max.x) || !(r.Min.y < r.Max.y))
        return;
    if (step < 2.0f)
        step = 4.0f;
    dl->PushClipRect(r.Min, r.Max, true);
    dl->AddRectFilled(r.Min, r.Max, IM_COL32(28, 28, 28, 255));
    const ImU32 tile = IM_COL32(52, 52, 52, 255);
    const int max_tiles = 64;
    int yi = 0;
    for (float y = r.Min.y; y < r.Max.y && yi < max_tiles; y += step, yi++)
    {
        int xi = 0;
        for (float x = r.Min.x; x < r.Max.x && xi < max_tiles; x += step, xi++)
        {
            if (((xi + yi) & 1) == 0)
                continue;
            dl->AddRectFilled(ImVec2(x, y), ImVec2(ImMin(x + step, r.Max.x), ImMin(y + step, r.Max.y)), tile);
        }
    }
    dl->PopClipRect();
}

static ImU32 F4U32(float r, float g, float b, float a = 1.0f)
{
    return ColorConvertFloat4ToU32(ImVec4(ImSaturate(r), ImSaturate(g), ImSaturate(b), ImSaturate(a)));
}

static void DrawGrabSquare(ImDrawList* dl, const ImVec2& c, float sz)
{
    dl->AddRectFilled(c - ImVec2(sz + 1.0f, sz + 1.0f), c + ImVec2(sz + 1.0f, sz + 1.0f), IM_COL32(0, 0, 0, 220));
    dl->AddRectFilled(c - ImVec2(sz, sz), c + ImVec2(sz, sz), IM_COL32(255, 255, 255, 255));
}

static bool DrawColorPickerPanel(ImGuiID id, float col[4], const ImRect& anchor)
{
    ImGuiContext& g = *GImGui;
    ImGuiStyle& style = g.Style;

    const float sv = 168.0f;
    const float bar = 18.0f;
    const float gap = 6.0f;
    const float pad = 10.0f;
    const float width = pad * 2.0f + sv + gap + bar;
    const float height = pad * 2.0f + sv + gap + bar;

    ImVec2 pos(anchor.Max.x + 8.0f, anchor.Min.y);
    const ImVec2 ds = g.IO.DisplaySize;
    if (pos.x + width > ds.x - 8.0f)
        pos.x = anchor.Min.x - width - 8.0f;
    if (pos.x < 8.0f)
        pos.x = 8.0f;
    if (pos.y + height > ds.y - 8.0f)
        pos.y = ImMax(8.0f, ds.y - height - 8.0f);

    SetNextWindowPos(pos, ImGuiCond_Always);
    SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
    PushStyleColor(ImGuiCol_WindowBg, style.Colors[ImGuiCol_ChildBg]);
    PushStyleColor(ImGuiCol_Border, style.Colors[ImGuiCol_SliderGrab]);
    PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);
    PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, pad));
    PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);

    char name[32];
    ImFormatString(name, IM_ARRAYSIZE(name), "##color_picker_%08X", id);
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav;
    if (!Begin(name, nullptr, flags))
    {
        End();
        PopStyleVar(5);
        PopStyleColor(2);
        return false;
    }

    ImDrawList* dl = GetWindowDrawList();
    const ImVec2 origin = GetCursorScreenPos();
    const ImU32 border_col = GetColorU32(ImGuiCol_Border);

    ImVec4& hv = g_cp_hsv[id];
    float& H = hv.x;
    float& S = hv.y;
    float& V = hv.z;
    H = ImSaturate(H);
    S = ImSaturate(S);
    V = ImSaturate(V);
    col[3] = ImSaturate(col[3]);
    bool changed = false;

    ImRect svr(origin, origin + ImVec2(sv, sv));
    InvisibleButton("##sv", ImVec2(sv, sv));
    if (IsItemActive() && sv > 0.0f)
    {
        S = ImSaturate((g.IO.MousePos.x - svr.Min.x) / sv);
        V = 1.0f - ImSaturate((g.IO.MousePos.y - svr.Min.y) / sv);
        changed = true;
        g.MouseCursor = ImGuiMouseCursor_Hand;
    }
    else if (IsItemHovered())
        g.MouseCursor = ImGuiMouseCursor_Hand;

    float hr, hg, hb;
    ColorConvertHSVtoRGB(H, 1.0f, 1.0f, hr, hg, hb);
    dl->PushClipRect(svr.Min, svr.Max, true);
    dl->AddRectFilledMultiColor(svr.Min, svr.Max, IM_COL32(255, 255, 255, 255), F4U32(hr, hg, hb), F4U32(hr, hg, hb), IM_COL32(255, 255, 255, 255));
    dl->AddRectFilledMultiColor(svr.Min, svr.Max, 0, 0, IM_COL32(0, 0, 0, 255), IM_COL32(0, 0, 0, 255));
    dl->PopClipRect();
    dl->AddRect(svr.Min, svr.Max, border_col);
    DrawGrabSquare(dl, ImVec2(svr.Min.x + S * sv, svr.Min.y + (1.0f - V) * sv), 3.5f);

    SameLine(0.0f, gap);
    const ImVec2 hue_pos = GetCursorScreenPos();
    ImRect huer(hue_pos, hue_pos + ImVec2(bar, sv));
    InvisibleButton("##hue", ImVec2(bar, sv));
    if (IsItemActive() && sv > 0.0f)
    {
        H = ImSaturate((g.IO.MousePos.y - huer.Min.y) / sv);
        changed = true;
        g.MouseCursor = ImGuiMouseCursor_Hand;
    }
    else if (IsItemHovered())
        g.MouseCursor = ImGuiMouseCursor_Hand;

    const int segs = 24;
    dl->PushClipRect(huer.Min, huer.Max, true);
    for (int i = 0; i < segs; i++)
    {
        const float h0 = (float)i / (float)segs;
        const float h1 = (float)(i + 1) / (float)segs;
        float r0, g0, b0, r1, g1, b1;
        ColorConvertHSVtoRGB(h0, 1.0f, 1.0f, r0, g0, b0);
        ColorConvertHSVtoRGB(h1, 1.0f, 1.0f, r1, g1, b1);
        dl->AddRectFilledMultiColor(
            ImVec2(huer.Min.x, huer.Min.y + h0 * sv),
            ImVec2(huer.Max.x, huer.Min.y + h1 * sv),
            F4U32(r0, g0, b0), F4U32(r0, g0, b0), F4U32(r1, g1, b1), F4U32(r1, g1, b1));
    }
    dl->PopClipRect();
    dl->AddRect(huer.Min, huer.Max, border_col);
    const float hy = huer.Min.y + H * sv;
    dl->AddRectFilled(ImVec2(huer.Min.x - 1.0f, hy - 1.5f), ImVec2(huer.Max.x + 1.0f, hy + 1.5f), IM_COL32(0, 0, 0, 220));
    dl->AddRectFilled(ImVec2(huer.Min.x, hy - 1.0f), ImVec2(huer.Max.x, hy + 1.0f), IM_COL32(255, 255, 255, 255));

    SetCursorScreenPos(ImVec2(origin.x, origin.y + sv + gap));
    ImRect ar(GetCursorScreenPos(), GetCursorScreenPos() + ImVec2(sv, bar));
    InvisibleButton("##alpha", ImVec2(sv, bar));
    if (IsItemActive() && ar.GetWidth() > 0.0f)
    {
        col[3] = ImSaturate((g.IO.MousePos.x - ar.Min.x) / ar.GetWidth());
        changed = true;
        g.MouseCursor = ImGuiMouseCursor_Hand;
    }
    else if (IsItemHovered())
        g.MouseCursor = ImGuiMouseCursor_Hand;

    float cr, cg, cb;
    ColorConvertHSVtoRGB(H, S, V, cr, cg, cb);
    DrawCheckerboard(dl, ar, 4.0f);
    dl->PushClipRect(ar.Min, ar.Max, true);
    dl->AddRectFilledMultiColor(ar.Min, ar.Max, F4U32(cr, cg, cb, 0.0f), F4U32(cr, cg, cb, 1.0f), F4U32(cr, cg, cb, 1.0f), F4U32(cr, cg, cb, 0.0f));
    dl->PopClipRect();
    dl->AddRect(ar.Min, ar.Max, border_col);
    const float ax = ar.Min.x + col[3] * ar.GetWidth();
    dl->AddRectFilled(ImVec2(ax - 1.5f, ar.Min.y - 1.0f), ImVec2(ax + 1.5f, ar.Max.y + 1.0f), IM_COL32(0, 0, 0, 220));
    dl->AddRectFilled(ImVec2(ax - 1.0f, ar.Min.y), ImVec2(ax + 1.0f, ar.Max.y), IM_COL32(255, 255, 255, 255));

    SameLine(0.0f, gap);
    ImRect pr(GetCursorScreenPos(), GetCursorScreenPos() + ImVec2(bar, bar));
    Dummy(ImVec2(bar, bar));
    DrawCheckerboard(dl, pr, 4.0f);
    dl->AddRectFilled(pr.Min, pr.Max, F4U32(cr, cg, cb, col[3]));
    dl->AddRect(pr.Min, pr.Max, border_col);

    if (changed)
        ColorConvertHSVtoRGB(H, S, V, col[0], col[1], col[2]);

    const bool over_picker = IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    if (g.IO.MouseClicked[0] && !over_picker && g.FrameCount > g_cp_frame && g.ActiveId == 0)
        g_cp_id = 0;
    if (IsKeyPressed(ImGuiKey_Escape))
        g_cp_id = 0;

    End();
    PopStyleVar(5);
    PopStyleColor(2);
    return changed;
}

bool ImAdd::ColorPicker(const char* str_id, float col[4], const ImVec2& size_arg)
{
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems || col == nullptr)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(str_id);
    const float h = size_arg.y > 0.0f ? size_arg.y : ImMax(16.0f, IM_ROUND(g.FontSize));
    const float w = size_arg.x > 0.0f ? size_arg.x : h;
    const ImVec2 pos = window->DC.CursorPos;
    const ImRect bb(pos, pos + ImVec2(w, h));

    ItemSize(bb, style.FramePadding.y);
    if (!ItemAdd(bb, id))
        return false;

    bool hovered, held;
    const bool pressed = ButtonBehavior(bb, id, &hovered, &held);
    if (hovered)
        g.MouseCursor = ImGuiMouseCursor_Hand;
    if (pressed)
    {
        if (g_cp_id == id)
        {
            g_cp_id = 0;
            g_cp_col = nullptr;
        }
        else
        {
            g_cp_id = id;
            g_cp_frame = g.FrameCount;
            g_cp_col = col;
            g_cp_anchor = bb;
            ImVec4 hv;
            ColorConvertRGBtoHSV(ImSaturate(col[0]), ImSaturate(col[1]), ImSaturate(col[2]), hv.x, hv.y, hv.z);
            g_cp_hsv[id] = hv;
        }
    }

    if (g_cp_id == id)
    {
        g_cp_col = col;
        g_cp_anchor = bb;
        g_cp_seen_frame = g.FrameCount;
    }

    DrawCheckerboard(window->DrawList, bb, 4.0f);
    window->DrawList->AddRectFilled(bb.Min, bb.Max, F4U32(col[0], col[1], col[2], col[3]));
    const bool lit = hovered || g_cp_id == id;
    window->DrawList->AddRect(bb.Min, bb.Max, GetColorU32(lit ? ImGuiCol_Separator : ImGuiCol_Border));
    return pressed;
}

void ImAdd::RenderColorPickers()
{
    ImGuiContext& g = *GImGui;
    if (g_cp_id == 0 || g_cp_col == nullptr)
        return;
    if (g_cp_seen_frame != g.FrameCount)
    {
        g_cp_id = 0;
        g_cp_col = nullptr;
        return;
    }
    DrawColorPickerPanel(g_cp_id, g_cp_col, g_cp_anchor);
}

bool ImAdd::CheckBox(const char* label, bool* v, int* keybind, float* color, float* color2, int* keybind_mode, float* color3, float* color4)
{
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems) return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);
    const ImVec2 label_size = CalcTextSize(label, NULL, true);

    const float box_size = IM_ROUND(g.FontSize);
    const float gap = 6.0f;
    const float bind_w = keybind ? KeyBindChipWidth() : 0.0f;
    const float col_w = (color || color2 || color3 || color4) ? ImMax(16.0f, box_size) : 0.0f;
    const int n_col = (color ? 1 : 0) + (color2 ? 1 : 0) + (color3 ? 1 : 0) + (color4 ? 1 : 0);
    const float colors_w = n_col * col_w + (n_col > 1 ? gap * (n_col - 1) : 0.0f);
    const float acc_w = colors_w + ((colors_w > 0.0f && bind_w > 0.0f) ? gap : 0.0f) + bind_w;

    const ImVec2 pos = window->DC.CursorPos;
    float right_edge = ImMin(window->InnerClipRect.Max.x, window->WorkRect.Max.x) - 6.0f;
    if (right_edge < pos.x + box_size + acc_w + 16.0f)
        right_edge = pos.x + ImMax(GetContentRegionAvail().x - 6.0f, box_size + acc_w + 24.0f);
    const float left_w = ImMax(box_size + 8.0f, right_edge - pos.x - acc_w - (acc_w > 0.0f ? gap : 0.0f));

    const ImRect box_bb(pos, pos + ImVec2(box_size, box_size));
    const ImRect left_bb(pos, pos + ImVec2(left_w, box_size));

    BeginGroup();
    ItemSize(left_bb, style.FramePadding.y);
    if (!ItemAdd(left_bb, id, &box_bb))
    {
        EndGroup();
        return false;
    }

    bool hovered, held;
    bool pressed = ButtonBehavior(left_bb, id, &hovered, &held);
    if (pressed)
    {
        *v = !(*v);
        MarkItemEdited(id);
    }

    const int a = (int)(style.Alpha * 255);
    struct CheckAnim { float hover; float on; };
    static std::map<ImGuiID, CheckAnim> check_anim;
    CheckAnim& ca = check_anim[id];
    const float anim_t = ImSaturate(ImGui::GetIO().DeltaTime / IMADD_ANIMATIONS_SPEED);
    ca.hover = ImLerp(ca.hover, hovered ? 1.0f : 0.0f, anim_t);
    ca.on = ImLerp(ca.on, *v ? 1.0f : 0.0f, anim_t);

    const int ah = (int)(ca.hover * 15.0f);
    ImU32 col_tl = IM_COL32(76 + ah, 76 + ah, 76 + ah, a);
    ImU32 col_bl = IM_COL32(51 + ah, 51 + ah, 51 + ah, a);
    window->DrawList->AddRectFilledMultiColor(box_bb.Min, box_bb.Max, col_tl, col_tl, col_bl, col_bl);
    if (ca.on > 0.01f)
        window->DrawList->AddRectFilled(box_bb.Min, box_bb.Max, GetColorU32(ImGuiCol_Separator, style.Alpha * ca.on));
    window->DrawList->AddRect(box_bb.Min, box_bb.Max, GetColorU32(ca.hover > 0.5f ? ImGuiCol_SliderGrab : ImGuiCol_Border));

    ImVec2 label_pos(pos.x + box_size + 6.0f, pos.y + (box_size - label_size.y) * 0.5f);
    ImU32 label_col = GetColorU32(ImGuiCol_Text, style.Alpha * (0.55f + 0.45f * ca.on));
    if (label_size.x > 0.0f)
        window->DrawList->AddText(label_pos, label_col, label);

    PushID(label);
    float acc_x = right_edge - acc_w;
    if (color)
    {
        SameLine(0.0f, 0.0f);
        SetCursorScreenPos(ImVec2(acc_x, pos.y));
        ColorPicker("##col", color, ImVec2(col_w, box_size));
        acc_x += col_w + gap;
    }
    if (color2)
    {
        SameLine(0.0f, 0.0f);
        SetCursorScreenPos(ImVec2(acc_x, pos.y));
        ColorPicker("##col2", color2, ImVec2(col_w, box_size));
        acc_x += col_w + gap;
    }
    if (color3)
    {
        SameLine(0.0f, 0.0f);
        SetCursorScreenPos(ImVec2(acc_x, pos.y));
        ColorPicker("##col3", color3, ImVec2(col_w, box_size));
        acc_x += col_w + gap;
    }
    if (color4)
    {
        SameLine(0.0f, 0.0f);
        SetCursorScreenPos(ImVec2(acc_x, pos.y));
        ColorPicker("##col4", color4, ImVec2(col_w, box_size));
        acc_x += col_w + gap;
    }
    if (keybind)
    {
        SameLine(0.0f, 0.0f);
        SetCursorScreenPos(ImVec2(right_edge - bind_w, pos.y));
        DrawKeyBindChip("##bind", keybind, keybind_mode);
    }
    PopID();
    EndGroup();

    return pressed;
}


bool ImAdd::SliderScalar(const char* label, ImGuiDataType data_type, void* p_data, const void* p_min, const void* p_max, float width, const char* format)
{
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiID id = window->GetID(label);
    if (format == NULL)
        format = DataTypeGetInfo(data_type)->PrintFmt;

    const ImVec2 label_size = CalcTextSize(label, NULL, true);
    const float row_w = CalcItemSize(ImVec2(width, 0), CalcItemWidth(), 0).x;
    const float gap = 8.0f;
    const float label_col = (label_size.x > 0.0f) ? label_size.x + gap : 0.0f;
    const float track_h = ImMax(14.0f, g.FontSize + 3.0f);
    const float row_h = ImMax(track_h, label_size.y);

    const ImVec2 pos = window->DC.CursorPos;
    const ImRect total_bb(pos, pos + ImVec2(row_w, row_h));
    const ImRect frame_bb(
        ImVec2(pos.x + label_col, pos.y + (row_h - track_h) * 0.5f),
        ImVec2(pos.x + row_w, pos.y + (row_h + track_h) * 0.5f));

    ItemSize(total_bb, 0.0f);
    if (!ItemAdd(total_bb, id, &frame_bb))
        return false;

    const bool hovered = ItemHoverable(frame_bb, id, g.LastItemData.ItemFlags);
    const bool clicked = hovered && IsMouseClicked(0, ImGuiInputFlags_None, id);
    const bool make_active = clicked || (g.NavActivateId == id);
    if (make_active && clicked)
        SetKeyOwner(ImGuiKey_MouseLeft, id);
    if (make_active)
    {
        SetActiveID(id, window);
        SetFocusID(id, window);
        FocusWindow(window);
        g.ActiveIdUsingNavDirMask |= (1 << ImGuiDir_Left) | (1 << ImGuiDir_Right);
    }

    ImRect grab_bb;
    const bool value_changed = SliderBehavior(frame_bb, id, data_type, p_data, p_min, p_max, format, 0, &grab_bb);
    if (value_changed)
        MarkItemEdited(id);

    float vmin = 0.0f, vmax = 1.0f, vcur = 0.0f;
    if (data_type == ImGuiDataType_S32)
    {
        vmin = (float)(*(const int*)p_min);
        vmax = (float)(*(const int*)p_max);
        vcur = (float)(*(const int*)p_data);
    }
    else
    {
        vmin = *(const float*)p_min;
        vmax = *(const float*)p_max;
        vcur = *(const float*)p_data;
    }
    const float target_t = (vmax != vmin) ? ImSaturate((vcur - vmin) / (vmax - vmin)) : 0.0f;

    const bool active = (g.ActiveId == id);
    ImVec4 colTrack = GetStyleColorVec4(ImGuiCol_FrameBg);
    colTrack.x *= 0.55f; colTrack.y *= 0.55f; colTrack.z *= 0.55f;
    ImVec4 colFill = GetStyleColorVec4(ImGuiCol_Separator);
    if (active)
    {
        colFill.x = ImMin(1.0f, colFill.x * 1.12f);
        colFill.y = ImMin(1.0f, colFill.y * 1.12f);
        colFill.z = ImMin(1.0f, colFill.z * 1.12f);
    }

    struct SliderAnim { ImVec4 Track; ImVec4 Fill; float t; float hover; bool init; };
    static std::map<ImGuiID, SliderAnim> slider_anim;
    SliderAnim& sa = slider_anim[id];
    if (!sa.init)
    {
        sa.Track = colTrack;
        sa.Fill = colFill;
        sa.t = target_t;
        sa.hover = (hovered || active) ? 1.0f : 0.0f;
        sa.init = true;
    }

    const float dt = g.IO.DeltaTime;
    const float col_t = ImSaturate(dt / IMADD_ANIMATIONS_SPEED);
    const float fill_t = ImSaturate(dt / 0.08f);
    sa.Track = ImLerp(sa.Track, colTrack, col_t);
    sa.Fill = ImLerp(sa.Fill, colFill, col_t);
    sa.t = ImLerp(sa.t, target_t, fill_t);
    sa.hover = ImLerp(sa.hover, (hovered || active) ? 1.0f : 0.0f, col_t);

    if (label_size.x > 0.0f)
    {
        const ImVec2 lp(pos.x, pos.y + (row_h - label_size.y) * 0.5f);
        window->DrawList->AddText(lp, GetColorU32(ImGuiCol_Text), label);
    }

    RenderNavHighlight(frame_bb, id);
    window->DrawList->AddRectFilled(frame_bb.Min, frame_bb.Max, GetColorU32(sa.Track), 0.0f);

    ImVec4 fill_draw = sa.Fill;
    fill_draw.x = ImMin(1.0f, fill_draw.x + 0.08f * sa.hover);
    fill_draw.y = ImMin(1.0f, fill_draw.y + 0.08f * sa.hover);
    fill_draw.z = ImMin(1.0f, fill_draw.z + 0.08f * sa.hover);
    if (sa.t > 0.001f)
    {
        const float fill_x = ImLerp(frame_bb.Min.x, frame_bb.Max.x, sa.t);
        window->DrawList->PushClipRect(frame_bb.Min, ImVec2(fill_x, frame_bb.Max.y), true);
        window->DrawList->AddRectFilled(frame_bb.Min, frame_bb.Max, GetColorU32(fill_draw), 0.0f);
        window->DrawList->PopClipRect();
    }

    char value_buf[64];
    DataTypeFormatString(value_buf, IM_ARRAYSIZE(value_buf), data_type, p_data, format);
    const ImVec2 value_size = CalcTextSize(value_buf);
    const ImVec2 text_pos(
        frame_bb.Min.x + (frame_bb.GetWidth() - value_size.x) * 0.5f,
        frame_bb.Min.y + (track_h - value_size.y) * 0.5f);
    window->DrawList->AddText(text_pos, GetColorU32(ImGuiCol_Text), value_buf);

    return value_changed;
}

bool ImAdd::SliderFloat(const char* label, float* v, float v_min, float v_max, float width, const char* format)
{
    return SliderScalar(label, ImGuiDataType_Float, v, &v_min, &v_max, width, format);
}

bool ImAdd::SliderInt(const char* label, int* v, int v_min, int v_max, float width, const char* format)
{
    return SliderScalar(label, ImGuiDataType_S32, v, &v_min, &v_max, width, format);
}

bool ImAdd::Selectable(const char* label, bool selected, const ImVec2& size_arg)
{
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);
    const ImVec2 label_size = CalcTextSize(label, NULL, true);

    ImVec2 pos = window->DC.CursorPos;
    ImVec2 size = CalcItemSize(size_arg, label_size.x + style.FramePadding.x * 2.0f, label_size.y + style.FramePadding.y * 2.0f);

    const ImRect bb(pos, pos + size);
    ItemSize(size, style.FramePadding.y);
    if (!ItemAdd(bb, id))
        return false;

    bool hovered, held;
    bool pressed = ButtonBehavior(bb, id, &hovered, &held);
    float borderSize = style.FrameBorderSize;

    // Colors
    ImVec4 colFrameMain = GetStyleColorVec4((hovered && !selected) ? held ? ImGuiCol_HeaderActive : ImGuiCol_HeaderHovered : ImGuiCol_Header);
    ImVec4 colFrameNull = colFrameMain; colFrameNull.w = 0.0f;
    ImVec4 colFrame = ((!hovered && !selected) ? colFrameNull : colFrameMain);

    ImVec4 colBorderMain = GetStyleColorVec4(ImGuiCol_Border);
    ImVec4 colBorderNull = colBorderMain; colBorderNull.w = 0.0f;
    ImVec4 colBorder = (selected ? colBorderMain : colBorderNull);

    // Animations
    struct stColors_State {
        ImVec4 Frame;
        ImVec4 Border;
    };

    static std::map<ImGuiID, stColors_State> anim;
    auto it_anim = anim.find(id);

    if (it_anim == anim.end())
    {
        anim.insert({ id, stColors_State() });
        it_anim = anim.find(id);

        it_anim->second.Frame = colFrame;
        it_anim->second.Border = colBorder;
    }

    it_anim->second.Frame = ImLerp(it_anim->second.Frame, colFrame, 1.0f / IMADD_ANIMATIONS_SPEED * ImGui::GetIO().DeltaTime);
    it_anim->second.Border = ImLerp(it_anim->second.Border, colBorder, 1.0f / IMADD_ANIMATIONS_SPEED * ImGui::GetIO().DeltaTime);

    // Render
    RenderNavHighlight(bb, id);

    window->DrawList->AddRectFilled(bb.Min, bb.Max, GetColorU32(it_anim->second.Frame), style.FrameRounding);
    if (selected) {
        window->DrawList->AddRectFilledMultiColor(bb.Min, bb.Max, GetColorU32(ImGuiCol_BorderShadow, 0), GetColorU32(ImGuiCol_BorderShadow, 0), GetColorU32(ImGuiCol_BorderShadow), GetColorU32(ImGuiCol_BorderShadow));
    }

    if (borderSize > 0)
        window->DrawList->AddRect(bb.Min, bb.Max, GetColorU32(it_anim->second.Border), style.FrameRounding, 0, borderSize);

    PushStyleColor(ImGuiCol_Text, style.Colors[ImGuiCol_TitleBg]);
    {
        RenderText(pos + (bb.Max - bb.Min) / 2 - label_size / 2 + ImVec2(1, 1), label);
        RenderText(pos + (bb.Max - bb.Min) / 2 - label_size / 2 + ImVec2(-1, -1), label);
        RenderText(pos + (bb.Max - bb.Min) / 2 - label_size / 2 + ImVec2(-1, 1), label);
        RenderText(pos + (bb.Max - bb.Min) / 2 - label_size / 2 + ImVec2(1, -1), label);
    }
    PopStyleColor();
    RenderText(pos + (bb.Max - bb.Min) / 2 - label_size / 2, label);

    return pressed;
}

static bool BeginComboField(const char* label, const char* preview, float width)
{
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);
    const ImVec2 label_size = CalcTextSize(label, NULL, true);
    if (!preview)
        preview = "";

    const float row_w = CalcItemSize(ImVec2(width, 0), CalcItemWidth(), 0).x;
    const float gap = 8.0f;
    const float label_col = (label_size.x > 0.0f) ? label_size.x + gap : 0.0f;
    const float track_h = ImMax(14.0f, g.FontSize + 3.0f);
    const float row_h = ImMax(track_h, label_size.y);
    const float arrow_size = track_h;

    const ImVec2 pos = window->DC.CursorPos;
    const ImRect total_bb(pos, pos + ImVec2(row_w, row_h));
    const ImRect frame_bb(
        ImVec2(pos.x + label_col, pos.y + (row_h - track_h) * 0.5f),
        ImVec2(pos.x + row_w, pos.y + (row_h + track_h) * 0.5f));

    ItemSize(total_bb, 0.0f);
    if (!ItemAdd(total_bb, id, &frame_bb))
        return false;

    bool hovered, held;
    const bool pressed = ButtonBehavior(frame_bb, id, &hovered, &held);
    const ImGuiID popup_id = ImHashStr("##ComboPopup", 0, id);
    bool popup_open = IsPopupOpen(popup_id, ImGuiPopupFlags_None);
    if (pressed && !popup_open)
    {
        OpenPopupEx(popup_id, ImGuiPopupFlags_None);
        popup_open = true;
    }

    ImVec4 colFrame = GetStyleColorVec4((hovered || popup_open) ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg);
    ImVec4 colText = GetStyleColorVec4((popup_open || hovered) ? ImGuiCol_Text : ImGuiCol_TextDisabled);

    struct ComboAnim { ImVec4 Frame; ImVec4 Text; };
    static std::map<ImGuiID, ComboAnim> anim;
    ComboAnim& ca = anim[id];
    const float at = ImSaturate(g.IO.DeltaTime / 0.12f);
    ca.Frame = ImLerp(ca.Frame.w <= 0.0f ? colFrame : ca.Frame, colFrame, at);
    ca.Text = ImLerp(ca.Text.w <= 0.0f ? colText : ca.Text, colText, at);

    if (label_size.x > 0.0f)
        window->DrawList->AddText(ImVec2(pos.x, pos.y + (row_h - label_size.y) * 0.5f), GetColorU32(ImGuiCol_Text), label);

    RenderNavHighlight(frame_bb, id);
    window->DrawList->AddRectFilled(frame_bb.Min, frame_bb.Max, GetColorU32(ca.Frame), 0.0f);
    RenderArrow(window->DrawList,
        ImVec2(frame_bb.Max.x - arrow_size + style.FramePadding.y, frame_bb.Min.y + style.FramePadding.y),
        GetColorU32(ca.Text), popup_open ? ImGuiDir_Up : ImGuiDir_Down, 0.7f);

    const ImVec2 tsz = CalcTextSize(preview);
    RenderTextClipped(
        frame_bb.Min + ImVec2(style.FramePadding.x, (track_h - tsz.y) * 0.5f),
        ImVec2(frame_bb.Max.x - arrow_size, frame_bb.Max.y),
        preview, NULL, &tsz, ImVec2(0.0f, 0.0f));

    if (!popup_open)
        return false;

    SetNextWindowPos(ImVec2(frame_bb.Min.x, frame_bb.Max.y + 3.0f), ImGuiCond_Always);
    SetNextWindowSizeConstraints(ImVec2(frame_bb.GetWidth(), 0.0f), ImVec2(frame_bb.GetWidth(), FLT_MAX));
    return BeginPopupEx(popup_id, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
}

static bool ComboPopupItem(const char* text, bool selected)
{
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = GetCurrentWindow();
    ImDrawList* dl = window->DrawList;
    const float row_h = g.FontSize + 8.0f;

    const ImVec2 rp = GetCursorScreenPos();
    const float avail = GetContentRegionAvail().x;
    const ImRect row(rp, rp + ImVec2(avail, row_h));
    ItemSize(row.GetSize());
    const ImGuiID rid = window->GetID(text);
    if (!ItemAdd(row, rid))
        return false;

    bool hovered, held;
    const bool pressed = ButtonBehavior(row, rid, &hovered, &held);

    struct ItemAnim { float sel; float hov; };
    static std::map<ImGuiID, ItemAnim> anim;
    ItemAnim& a = anim[rid];
    const float t = ImSaturate(g.IO.DeltaTime / 0.12f);
    a.sel = ImLerp(a.sel, selected ? 1.0f : 0.0f, t);
    a.hov = ImLerp(a.hov, hovered ? 1.0f : 0.0f, t);

    ImVec4 hov_col = GetStyleColorVec4(ImGuiCol_FrameBgHovered);
    ImVec4 sel_col = GetStyleColorVec4(ImGuiCol_Separator);
    ImVec4 fill = ImLerp(hov_col, sel_col, a.sel);
    fill.w *= ImMax(a.hov * 0.55f, a.sel * 0.90f);
    if (fill.w > 0.01f)
        dl->AddRectFilled(row.Min, row.Max, GetColorU32(fill));

    const ImVec2 ts = CalcTextSize(text);
    dl->AddText(ImVec2(rp.x + 8.0f, rp.y + (row_h - ts.y) * 0.5f), GetColorU32(ImGuiCol_Text), text);
    return pressed;
}

bool ImAdd::BeginCombo(const char* label, const char* preview_value, ImGuiComboFlags)
{
    return BeginComboField(label, preview_value ? preview_value : "", -0.1f);
}
struct AnimatedNotification {
    bool active = false;
    float opacity = 1.0f;
    ImVec2 position = ImVec2(-300, -100); // Starting position off-screen (top-left corner)
    float duration = 0.0f;
};

void ImAdd::ShowNotification(const char* message, float duration) {
    static std::vector<AnimatedNotification> notifications;
    static float lastTime = 0.0f;
    float currentTime = ImGui::GetTime();

    // Add a new notification if it's not active and there's no notification currently showing
    if (currentTime - lastTime > duration && notifications.empty()) {
        AnimatedNotification notification;
        notification.active = true;
        notification.position = ImVec2(-300, -100); // Top-left off-screen
        notification.opacity = 0.0f;
        notification.duration = duration;
        notifications.push_back(notification);
        lastTime = currentTime;
        std::cout << "Notification triggered" << std::endl; // Debugging line
    }

    // Loop through all notifications and update their state
    for (auto& notification : notifications) {
        if (notification.active) {
            // Fade-in effect
            if (notification.opacity < 1.0f) {
                notification.opacity += ImGui::GetIO().DeltaTime * 2.0f; // Fade in over time
            }

            // Slide-in effect: Move the notification towards the top-left corner (0, 0)
            if (notification.position.x < 0.0f) {
                notification.position.x += ImGui::GetIO().DeltaTime * 300.0f; // Slide from left to right
            }
            if (notification.position.y < 0.0f) {
                notification.position.y += ImGui::GetIO().DeltaTime * 100.0f; // Slide from top to down
            }

            // After the notification duration ends, stop showing it
            if (currentTime - lastTime > notification.duration) {
                notification.active = false;
            }
        }
    }

    // Render notifications as debug boxes to check if positioning is working
    for (auto& notification : notifications) {
        if (notification.active) {
            // Draw a debug box to check if position and opacity are correct
            ImVec4 boxColor(1.0f, 0.0f, 0.0f, notification.opacity); // Red box with opacity
            ImGui::GetWindowDrawList()->AddRectFilled(
                notification.position,
                ImVec2(notification.position.x + 200, notification.position.y + 50),
                IM_COL32((int)(boxColor.x * 255), (int)(boxColor.y * 255), (int)(boxColor.z * 255), (int)(boxColor.w * 255))
            );
        }
    }
}


static float CalcMaxPopupHeightFromItemCount(int items_count)
{
    ImGuiContext& g = *GImGui;
    if (items_count <= 0)
        return FLT_MAX;
    return (g.FontSize + g.Style.ItemSpacing.y) * items_count - g.Style.ItemSpacing.y + (g.Style.WindowPadding.y * 2);
}

bool ImAdd::BeginComboPopup(ImGuiID popup_id, const ImRect& bb, ImGuiComboFlags flags)
{
    ImGuiContext& g = *GImGui;
    if (!IsPopupOpen(popup_id, ImGuiPopupFlags_None))
    {
        g.NextWindowData.ClearFlags();
        return false;
    }

    // Set popup size
    float w = bb.GetWidth();
    if (g.NextWindowData.HasFlags & ImGuiNextWindowDataFlags_HasSizeConstraint)
    {
        g.NextWindowData.SizeConstraintRect.Min.x = ImMax(g.NextWindowData.SizeConstraintRect.Min.x, w);
    }
    else
    {
        if ((flags & ImGuiComboFlags_HeightMask_) == 0)
            flags |= ImGuiComboFlags_HeightRegular;
        IM_ASSERT(ImIsPowerOfTwo(flags & ImGuiComboFlags_HeightMask_)); // Only one
        int popup_max_height_in_items = -1;
        if (flags & ImGuiComboFlags_HeightRegular)     popup_max_height_in_items = 8;
        else if (flags & ImGuiComboFlags_HeightSmall)  popup_max_height_in_items = 4;
        else if (flags & ImGuiComboFlags_HeightLarge)  popup_max_height_in_items = 20;
        ImVec2 constraint_min(0.0f, 0.0f), constraint_max(FLT_MAX, FLT_MAX);
        if ((g.NextWindowData.HasFlags & ImGuiNextWindowDataFlags_HasSize) == 0 || g.NextWindowData.SizeVal.x <= 0.0f) // Don't apply constraints if user specified a size
            constraint_min.x = w;
        if ((g.NextWindowData.HasFlags & ImGuiNextWindowDataFlags_HasSize) == 0 || g.NextWindowData.SizeVal.y <= 0.0f)
            constraint_max.y = CalcMaxPopupHeightFromItemCount(popup_max_height_in_items);
        SetNextWindowSizeConstraints(constraint_min, constraint_max);
    }

    // This is essentially a specialized version of BeginPopupEx()
    char name[16];
    ImFormatString(name, IM_ARRAYSIZE(name), "##Combo_%02d", g.BeginPopupStack.Size); // Recycle windows based on depth

    // Set position given a custom constraint (peak into expected window size so we can position it)
    // FIXME: This might be easier to express with an hypothetical SetNextWindowPosConstraints() function?
    // FIXME: This might be moved to Begin() or at least around the same spot where Tooltips and other Popups are calling FindBestWindowPosForPopupEx()?
    if (ImGuiWindow* popup_window = FindWindowByName(name))
        if (popup_window->WasActive)
        {
            // Always override 'AutoPosLastDirection' to not leave a chance for a past value to affect us.
            ImVec2 size_expected = CalcWindowNextAutoFitSize(popup_window);
            popup_window->AutoPosLastDirection = (flags & ImGuiComboFlags_PopupAlignLeft) ? ImGuiDir_Left : ImGuiDir_Down; // Left = "Below, Toward Left", Down = "Below, Toward Right (default)"
            ImRect r_outer = GetPopupAllowedExtentRect(popup_window);
            ImVec2 pos = FindBestWindowPosForPopupEx(bb.GetBL(), size_expected, &popup_window->AutoPosLastDirection, r_outer, bb, ImGuiPopupPositionPolicy_ComboBox);
            SetNextWindowPos(pos + ImVec2(0, g.Style.ItemSpacing.y));
        }

    // We don't use BeginPopupEx() solely because we have a custom name string, which we could make an argument to BeginPopupEx()
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_Popup | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove;
    PushStyleVar(ImGuiStyleVar_WindowPadding, g.Style.FramePadding);
    PushStyleVar(ImGuiStyleVar_PopupRounding, g.Style.FrameRounding);
    bool ret = Begin(name, NULL, window_flags);
    PopStyleVar(2);
    if (!ret)
    {
        EndPopup();
        IM_ASSERT(0);   // This should never happen as we tested for IsPopupOpen() above
        return false;
    }
    return true;
}

bool ImAdd::Combo(const char* label, int* current_item, const char* (*getter)(void* user_data, int idx), void* user_data, int items_count, int popup_max_height_in_items)
{
    ImGuiContext& g = *GImGui;

    // Call the getter to obtain the preview string which is a parameter to BeginCombo()
    const char* preview_value = NULL;
    if (*current_item >= 0 && *current_item < items_count)
        preview_value = getter(user_data, *current_item);

    // The old Combo() API exposed "popup_max_height_in_items". The new more general BeginCombo() API doesn't have/need it, but we emulate it here.
    if (popup_max_height_in_items != -1 && !(g.NextWindowData.HasFlags & ImGuiNextWindowDataFlags_HasSizeConstraint))
        SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(FLT_MAX, CalcMaxPopupHeightFromItemCount(popup_max_height_in_items)));

    if (!BeginComboField(label, preview_value ? preview_value : "none", -0.1f))
        return false;

    bool value_changed = false;
    for (int i = 0; i < items_count; i++)
    {
        const char* item_text = getter(user_data, i);
        if (item_text == NULL)
            item_text = "*Unknown item*";

        PushID(i);
        const bool item_selected = (i == *current_item);
        if (ComboPopupItem(item_text, item_selected) && *current_item != i)
        {
            value_changed = true;
            *current_item = i;
            CloseCurrentPopup();
        }
        if (item_selected)
            SetItemDefaultFocus();
        PopID();
    }

    EndPopup();

    if (value_changed)
        MarkItemEdited(g.LastItemData.ID);

    return value_changed;
}

// Getter for the old Combo() API: const char*[]
static const char* Items_ArrayGetter(void* data, int idx)
{
    const char* const* items = (const char* const*)data;
    return items[idx];
}

// Getter for the old Combo() API: "item1\0item2\0item3\0"
static const char* Items_SingleStringGetter(void* data, int idx)
{
    const char* items_separated_by_zeros = (const char*)data;
    int items_count = 0;
    const char* p = items_separated_by_zeros;
    while (*p)
    {
        if (idx == items_count)
            break;
        p += strlen(p) + 1;
        items_count++;
    }
    return *p ? p : NULL;
}

bool ImAdd::Combo(const char* label, int* current_item, const char* const items[], int items_count, int height_in_items)
{
    const bool value_changed = Combo(label, current_item, Items_ArrayGetter, (void*)items, items_count, height_in_items);
    return value_changed;
}

bool ImAdd::Combo(const char* label, int* current_item, const char* items_separated_by_zeros, float width, int height_in_items)
{
    int items_count = 0;
    const char* p = items_separated_by_zeros;       // FIXME-OPT: Avoid computing this, or at least only when combo is open
    while (*p)
    {
        p += strlen(p) + 1;
        items_count++;
    }

    const float w = CalcItemSize(ImVec2(width, 0), CalcItemWidth(), 0).x;
    PushItemWidth(w);
    bool value_changed = Combo(label, current_item, Items_SingleStringGetter, (void*)items_separated_by_zeros, items_count, height_in_items);
    PopItemWidth();

    return value_changed;
}

bool ImAdd::MultiCombo(const char* label, std::vector<int>* selected, std::vector<const char*> items, float width)
{
    if (selected->size() < items.size())
        selected->resize((int)items.size(), 0);

    std::string preview;
    int n_on = 0;
    for (int i = 0; i < (int)items.size(); i++)
    {
        if ((*selected)[i] != 1)
            continue;
        if (n_on)
            preview += ", ";
        preview += items[i];
        n_on++;
    }
    if (preview.empty())
        preview = "none";

    if (!BeginComboField(label, preview.c_str(), width))
        return false;

    bool changed = false;
    for (int i = 0; i < (int)items.size(); i++)
    {
        PushID(i);
        const bool on = (*selected)[i] == 1;
        if (ComboPopupItem(items[i], on))
        {
            (*selected)[i] = on ? 0 : 1;
            changed = true;
        }
        PopID();
    }

    EndPopup();
    return changed;
}

bool ImAdd::FrameRadioIcon(const char* label, bool* selected, const ImVec2& size_arg)
{
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);
    const ImVec2 label_size = CalcTextSize(label, NULL, true);

    ImVec2 pos = window->DC.CursorPos;
    ImVec2 size = CalcItemSize(size_arg, label_size.x + style.FramePadding.x * 2.0f, label_size.y + style.FramePadding.y * 2.0f);

    const ImRect bb(pos, pos + size);
    ItemSize(size, style.FramePadding.y);
    if (!ItemAdd(bb, id))
        return false;

    bool hovered, held;
    bool pressed = ButtonBehavior(bb, id, &hovered, &held);
    if (pressed) { *selected = !*selected; }

    // Colors
    const ImVec4 colBorder = GetStyleColorVec4((hovered && !*selected) ? ImGuiCol_SliderGrab : ImGuiCol_Border);
    const ImVec4 colFrame = GetStyleColorVec4(*selected ? ImGuiCol_FrameBgActive : hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg);
    const ImVec4 colText = GetStyleColorVec4(*selected ? ImGuiCol_SliderGrab : hovered ? ImGuiCol_Text : ImGuiCol_SliderGrabActive);

    // Animation
    struct stColors_State {
        ImVec4 Frame;
        ImVec4 Border;
        ImVec4 Text;
    };

    static std::map<ImGuiID, stColors_State> anim;
    auto it_anim = anim.find(id);

    if (it_anim == anim.end())
    {
        anim.insert({ id, stColors_State() });
        it_anim = anim.find(id);

        it_anim->second.Border = colBorder;
        it_anim->second.Frame = colFrame;
        it_anim->second.Text = colText;
    }

    it_anim->second.Frame = ImLerp(it_anim->second.Frame, colFrame, 1.0f / IMADD_ANIMATIONS_SPEED * ImGui::GetIO().DeltaTime);
    it_anim->second.Border = ImLerp(it_anim->second.Border, colBorder, 1.0f / IMADD_ANIMATIONS_SPEED * ImGui::GetIO().DeltaTime);
    it_anim->second.Text = ImLerp(it_anim->second.Text, colText, 1.0f / IMADD_ANIMATIONS_SPEED * ImGui::GetIO().DeltaTime);

    // Render
    RenderNavHighlight(bb, id);

    window->DrawList->AddRectFilled(pos, pos + size, GetColorU32(it_anim->second.Frame), style.FrameRounding);
    if (style.FrameBorderSize > 0) {
        window->DrawList->AddRect(pos, pos + size, GetColorU32(it_anim->second.Border), style.FrameRounding, 0, style.FrameBorderSize);
    }

    window->DrawList->AddText(pos + ImVec2(size.x / 2 - label_size.x / 2, (size.y - label_size.y) / 2), GetColorU32(it_anim->second.Text), label);

    return pressed;
}

void ImAdd::BeginChild(const char* label, const ImVec2& size_arg)
{
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return;

    ImGuiContext& g = *GImGui;
    ImGuiIO& io = GetIO(); (void)io;
    const ImGuiStyle& style = g.Style;

    ImGui::BeginChild(label, size_arg, ImGuiChildFlags_None, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground);
    {
        ImVec2 pos = ImGui::GetWindowPos();
        ImVec2 size = ImGui::GetWindowSize();
        ImDrawList* pDrawList = ImGui::GetWindowDrawList();

        float HeaderHeight = ImGui::GetFontSize() + style.WindowPadding.y * 2 + style.ChildBorderSize * 2;

        // Background
        pDrawList->AddRectFilled(pos, pos + size, GetColorU32(ImGuiCol_ChildBg));

        // Border
        if (style.ChildBorderSize > 0)
        {
            pDrawList->AddRect(pos, pos + size, GetColorU32(ImGuiCol_Border));
        }

        // Label with outline effect
        ImVec2 labelPos = pos + style.WindowPadding + ImVec2(0, style.ChildBorderSize * 2);
        pDrawList->AddText(labelPos + ImVec2(1, 1), GetColorU32(ImGuiCol_TitleBg), label);
        pDrawList->AddText(labelPos + ImVec2(-1, -1), GetColorU32(ImGuiCol_TitleBg), label);
        pDrawList->AddText(labelPos + ImVec2(1, -1), GetColorU32(ImGuiCol_TitleBg), label);
        pDrawList->AddText(labelPos + ImVec2(-1, 1), GetColorU32(ImGuiCol_TitleBg), label);
        pDrawList->AddText(labelPos, GetColorU32(ImGuiCol_Text), label);

        // Colored line with soft glow
        ImVec2 glowStart = pos + ImVec2(2, 2);
        ImVec2 glowEnd = pos + ImVec2(size.x - 2, 2);
        ImU32 baseCol = ImGui::GetColorU32(ImGuiCol_SliderGrab);

        // Main line
        pDrawList->AddLine(glowStart, glowEnd, baseCol);

        // Soft glow under the line
        for (int i = 1; i <= 4; ++i)
        {
            float alpha = 0.1f * (5 - i); // 0.4 to 0.1
            ImColor glowColor = ImColor(ImGui::GetStyleColorVec4(ImGuiCol_SliderGrab));
            glowColor.Value.w = alpha; // fade
            pDrawList->AddLine(glowStart + ImVec2(0, (float)i), glowEnd + ImVec2(0, (float)i), glowColor);
        }

        pDrawList->AddLine(glowStart + ImVec2(0, 1), glowEnd + ImVec2(0, 1), ImGui::GetColorU32(ImGuiCol_SliderGrabActive));

        ImGui::SetCursorScreenPos(pos + ImVec2(0, HeaderHeight - style.WindowPadding.y));
        ImGui::BeginChild(string(label + string("#Main")).c_str(), ImVec2(), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoBackground);
    }
}

void ImAdd::EndChild()
{
    ImGui::EndChild();
    ImGui::EndChild();
}

bool ImAdd::KeyBind(const char* str_id, int* k, float custom_width)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    ImGuiIO& io = g.IO;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(str_id);

    char buf_display[32] = "None";
    ImVec2 pos = window->DC.CursorPos;
    ImVec2 buf_display_size = ImGui::CalcTextSize(buf_display, NULL, true);
    float width = custom_width == 0 ? ImGui::CalcItemSize(ImVec2(-0.1f, 0), 0, 0).x : custom_width;
    float height = ImGui::GetFontSize();

    ImVec2 size = ImVec2(width, height);
    ImRect frame_bb(pos, pos + size);
    ImRect total_bb(pos, frame_bb.Max + ImVec2(size.x, 0));

    ImGui::ItemSize(total_bb);
    if (!ImGui::ItemAdd(total_bb, id))
        return false;

    const bool hovered = ImGui::ItemHoverable(frame_bb, id, 0);

    if (hovered)
    {
        ImGui::SetHoveredID(id);
        g.MouseCursor = ImGuiMouseCursor_Hand;
    }

    const bool user_clicked = hovered && io.MouseClicked[0];

    if (user_clicked)
    {
        ImGui::SetActiveID(id, window);
        ImGui::SetActiveIdUsingAllKeyboardKeys();
        ImGui::FocusWindow(window);
    }
    else if (io.MouseClicked[0] && g.ActiveId == id && !hovered)
            ImGui::ClearActiveID();

    const bool value_changed = BindKeyCapture(id, k);

    window->DrawList->AddRectFilled(frame_bb.Min, frame_bb.Max, ImGui::GetColorU32(ImGuiCol_FrameBg), style.FrameRounding);
    window->DrawList->AddRectFilledMultiColor(frame_bb.Min, frame_bb.Max, GetColorU32(ImGuiCol_BorderShadow, 0), GetColorU32(ImGuiCol_BorderShadow, 0), GetColorU32(ImGuiCol_BorderShadow), GetColorU32(ImGuiCol_BorderShadow));

    const float border_size = g.Style.FrameBorderSize;
    if (border_size > 0.0f)
    {
        window->DrawList->AddRect(frame_bb.Min + ImVec2(1, 1), frame_bb.Max + ImVec2(1, 1), ImGui::GetColorU32(ImGuiCol_BorderShadow), style.FrameRounding, 0, border_size);
        window->DrawList->AddRect(frame_bb.Min, frame_bb.Max, ImGui::GetColorU32(ImGuiCol_Border), style.FrameRounding, 0, border_size);
    }

    ImGui::RenderNavHighlight(total_bb, id);

    if (*k != 0 && g.ActiveId != id)
        strcpy_s(buf_display, sizeof buf_display, szKeyNames[*k]);
    else if (g.ActiveId == id)
        strcpy_s(buf_display, sizeof buf_display, "...");

    window->DrawList->AddText(pos + (frame_bb.Max - frame_bb.Min) / 2 - CalcTextSize(buf_display) / 2 - ImVec2(0, 1) + ImVec2(0, style.ChildBorderSize * 2) + ImVec2(1, 1), GetColorU32(ImGuiCol_TitleBg), buf_display);
    window->DrawList->AddText(pos + (frame_bb.Max - frame_bb.Min) / 2 - CalcTextSize(buf_display) / 2 - ImVec2(0, 1) + ImVec2(0, style.ChildBorderSize * 2) + ImVec2(-1, -1), GetColorU32(ImGuiCol_TitleBg), buf_display);
    window->DrawList->AddText(pos + (frame_bb.Max - frame_bb.Min) / 2 - CalcTextSize(buf_display) / 2 - ImVec2(0, 1) + ImVec2(0, style.ChildBorderSize * 2) + ImVec2(-1, 1), GetColorU32(ImGuiCol_TitleBg), buf_display);
    window->DrawList->AddText(pos + (frame_bb.Max - frame_bb.Min) / 2 - CalcTextSize(buf_display) / 2 - ImVec2(0, 1) + ImVec2(0, style.ChildBorderSize * 2) + ImVec2(1, -1), GetColorU32(ImGuiCol_TitleBg), buf_display);
    window->DrawList->AddText(pos + (frame_bb.Max - frame_bb.Min) / 2 - CalcTextSize(buf_display) / 2 - ImVec2(0, 1) + ImVec2(0, style.ChildBorderSize * 2), GetColorU32(ImGuiCol_Text), buf_display);

    return value_changed;
}

bool ImAdd::ColorEdit4(const char* label, float col[4])
{
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return false;

    const ImVec2 label_size = CalcTextSize(label, NULL, true);
    const float sw = ImMax(18.0f, GetFontSize() + 4.0f);
    BeginGroup();
    if (label_size.x > 0)
    {
        Text(GetStyleColorVec4(ImGuiCol_Text), "%s", label);
        SameLine(0.0f, 0.0f);
        SetCursorScreenPos(ImVec2(ImMin(window->InnerClipRect.Max.x, window->WorkRect.Max.x) - 6.0f - sw, GetItemRectMin().y));
    }
    PushID(label);
    const bool changed = ColorPicker("##picker", col, ImVec2(sw, GetFontSize()));
    PopID();
    EndGroup();
    return changed;
}

bool ImAdd::SearchBar(const char* str_id, char* buf, int buf_size, const char* hint, float width)
{
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return false;

    const float w = (width < 0.0f) ? GetContentRegionAvail().x : width;
    PushItemWidth(w);
    PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
    PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
    PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
    PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
    PushStyleColor(ImGuiCol_Text, GetStyleColorVec4(ImGuiCol_Separator));
    ImVec4 hint_col = GetStyleColorVec4(ImGuiCol_Separator);
    hint_col.w *= 0.45f;
    PushStyleColor(ImGuiCol_TextDisabled, hint_col);
    bool changed = InputTextWithHint(str_id, hint ? hint : "search features...", buf, (size_t)buf_size);
    PopStyleColor(6);
    PopStyleVar(2);
    PopItemWidth();
    return changed;
}
