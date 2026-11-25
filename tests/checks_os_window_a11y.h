#pragma once
#include "etch/etch.h"
#include "checks_media_emoji.h"
#include <SDL3/SDL.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>

#if defined(_WIN32) && defined(ETCH_ENABLE_OS_A11Y) && ETCH_ENABLE_OS_A11Y
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#    include <objbase.h>
#    include <oleauto.h>
#    include <UIAutomation.h>
#    ifdef small
#        undef small
#    endif
#    ifdef hyper
#        undef hyper
#    endif
#    ifdef near
#        undef near
#    endif
#    ifdef far
#        undef far
#    endif
#    ifdef min
#        undef min
#    endif
#    ifdef max
#        undef max
#    endif
#    ifdef CreateWindow
#        undef CreateWindow
#    endif
#    ifdef DrawText
#        undef DrawText
#    endif
#    ifdef LoadImage
#        undef LoadImage
#    endif
#    ifdef GetObject
#        undef GetObject
#    endif
#endif

namespace os_window_a11y_checks {

struct RefreshCheckState {
    etch::Context* ctx = nullptr;
    SDL_Window* expected_win = nullptr;
    uint32_t calls = 0u;
    bool wrong_window = false;
    bool reentered = false;
    bool push_nested_inside_cb = true;
};

inline void refresh_cb(etch::Context* ctx, SDL_Window* win, float dt_seconds, void* userdata) noexcept {
    (void)dt_seconds;
    auto* st = static_cast<RefreshCheckState*>(userdata);
    if (!st) {
        return;
    }
    if (win != st->expected_win || ctx != st->ctx) {
        st->wrong_window = true;
    }
    ++st->calls;
    if (st->push_nested_inside_cb) {
        st->push_nested_inside_cb = false;
        const uint32_t before = st->calls;
        SDL_Event nested = {};
        nested.type = SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED;
        nested.window.windowID = SDL_GetWindowID(win);
        SDL_PushEvent(&nested);
        if (st->calls != before) {
            st->reentered = true;
        }
    }
}

#if defined(_WIN32) && defined(ETCH_ENABLE_OS_A11Y) && ETCH_ENABLE_OS_A11Y

struct UiaWorkerData {
    HWND hwnd = nullptr;
    uint32_t btn_id = 0u;
    uint32_t chk_id = 0u;
    uint32_t sld_id = 0u;
    uint32_t txt_id = 0u;
    uint32_t pwd_id = 0u;
    uint32_t col_id = 0u;
    uint32_t ed_id = 0u;
    uint32_t scr_id = 0u;
    uint32_t tbl_id = 0u;
    uint32_t layout_id = 0u;
    uint32_t occluded_btn_id = 0u;
    uint32_t overlay_btn_id = 0u;
    POINT hit_screen_pt = {};
    bool test_hit_from_point = false;
    HANDLE ev_phase1_done = nullptr;
    HANDLE ev_phase2_go = nullptr;
    bool phase1_ok = false;
    bool phase2_ok = false;
    HRESULT stale_hr = S_OK;
    int fail_line = 0;
    volatile LONG stage = 0;
};

inline bool bstr_equals_w(BSTR b, const wchar_t* w) noexcept {
    if (!b || !w) {
        return false;
    }
    return std::wcscmp(b, w) == 0;
}

inline IUIAutomationElement* find_uia_by_id(
    IUIAutomationElement* root,
    IUIAutomationTreeWalker* walker,
    uint32_t target_id) noexcept
{
    if (!root || !walker || target_id == 0u) {
        return nullptr;
    }
    wchar_t expected[32] = {};
    std::swprintf(expected, 32u, L"etch_%08X", target_id);

    constexpr uint32_t kMaxQueue = 512u;
    IUIAutomationElement* queue[kMaxQueue] = {};
    uint32_t head = 0u;
    uint32_t tail = 0u;
    root->AddRef();
    queue[tail++] = root;

    IUIAutomationElement* found = nullptr;
    while (head < tail) {
        IUIAutomationElement* cur = queue[head++];
        BSTR auto_id = nullptr;
        if (SUCCEEDED(cur->get_CurrentAutomationId(&auto_id)) && auto_id) {
            if (std::wcscmp(auto_id, expected) == 0) {
                SysFreeString(auto_id);
                cur->AddRef();
                found = cur;
                break;
            }
            SysFreeString(auto_id);
        }
        IUIAutomationElement* child = nullptr;
        if (SUCCEEDED(walker->GetFirstChildElement(cur, &child)) && child) {
            while (child) {
                IUIAutomationElement* next = nullptr;
                walker->GetNextSiblingElement(child, &next);
                if (tail < kMaxQueue) {
                    queue[tail++] = child;
                } else {
                    child->Release();
                }
                child = next;
            }
        }
        cur->Release();
    }
    while (head < tail) {
        if (queue[head]) {
            queue[head]->Release();
        }
        ++head;
    }
    return found;
}

inline DWORD WINAPI uia_worker_proc(LPVOID param) noexcept {
    auto* d = static_cast<UiaWorkerData*>(param);
    if (!d) {
        return 1u;
    }
    const HRESULT hr_co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr_co)) {
        d->fail_line = __LINE__;
        if (d->ev_phase1_done) SetEvent(d->ev_phase1_done);
        return 1u;
    }

    auto fail_at = [&](int line) noexcept {
        if (d->fail_line == 0) {
            d->fail_line = line;
        }
        d->phase1_ok = false;
    };

    IUIAutomation* uia = nullptr;
    IUIAutomationElement* root = nullptr;
    IUIAutomationTreeWalker* walker = nullptr;
    IUIAutomationTreeWalker* ctrl_walker = nullptr;
    IUIAutomationElement* e_btn = nullptr;
    IUIAutomationElement* e_chk = nullptr;
    IUIAutomationElement* e_sld = nullptr;
    IUIAutomationElement* e_txt = nullptr;
    IUIAutomationElement* e_pwd = nullptr;
    IUIAutomationElement* e_col = nullptr;
    IUIAutomationElement* e_ed = nullptr;
    IUIAutomationElement* e_scr = nullptr;
    IUIAutomationElement* e_tbl = nullptr;
    IUIAutomationElement* e_layout = nullptr;
    IUIAutomationInvokePattern* stale_invoke = nullptr;

    InterlockedExchange(&d->stage, 1);
    HRESULT hr = CoCreateInstance(
        CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_IUIAutomation, reinterpret_cast<void**>(&uia));
    if (FAILED(hr) || !uia) {
        fail_at(__LINE__);
        goto done_phase1;
    }

    InterlockedExchange(&d->stage, 2);
    hr = uia->ElementFromHandle(d->hwnd, &root);
    if (FAILED(hr) || !root) {
        fail_at(__LINE__);
        goto done_phase1;
    }

    InterlockedExchange(&d->stage, 3);
    {
        CONTROLTYPEID ct = 0;
        if (FAILED(root->get_CurrentControlType(&ct)) || ct != UIA_WindowControlTypeId) {
            fail_at(__LINE__);
            goto done_phase1;
        }
        BSTR name = nullptr;
        if (FAILED(root->get_CurrentName(&name)) || !bstr_equals_w(name, L"Etch UIA Test")) {
            if (name) SysFreeString(name);
            fail_at(__LINE__);
            goto done_phase1;
        }
        SysFreeString(name);

        BSTR fw = nullptr;
        if (FAILED(root->get_CurrentFrameworkId(&fw)) || !bstr_equals_w(fw, L"Etch")) {
            if (fw) SysFreeString(fw);
            fail_at(__LINE__);
            goto done_phase1;
        }
        SysFreeString(fw);

        RECT rc = {};
        if (FAILED(root->get_CurrentBoundingRectangle(&rc)) || (rc.right - rc.left) <= 0 || (rc.bottom - rc.top) <= 0) {
            fail_at(__LINE__);
            goto done_phase1;
        }
    }

    InterlockedExchange(&d->stage, 4);
    hr = uia->get_RawViewWalker(&walker);
    if (FAILED(hr) || !walker) {
        fail_at(__LINE__);
        goto done_phase1;
    }
    hr = uia->get_ControlViewWalker(&ctrl_walker);
    if (FAILED(hr) || !ctrl_walker) {
        fail_at(__LINE__);
        goto done_phase1;
    }

    InterlockedExchange(&d->stage, 5);
    e_btn = find_uia_by_id(root, walker, d->btn_id);
    e_chk = find_uia_by_id(root, walker, d->chk_id);
    e_sld = find_uia_by_id(root, walker, d->sld_id);
    e_txt = find_uia_by_id(root, walker, d->txt_id);
    e_pwd = find_uia_by_id(root, walker, d->pwd_id);
    e_col = find_uia_by_id(root, walker, d->col_id);
    e_ed = find_uia_by_id(root, walker, d->ed_id);
    e_scr = find_uia_by_id(root, walker, d->scr_id);
    e_tbl = find_uia_by_id(root, walker, d->tbl_id);
    e_layout = find_uia_by_id(root, walker, d->layout_id);
    InterlockedExchange(&d->stage, 6);
    if (!e_btn || !e_chk || !e_sld || !e_txt || !e_pwd || !e_col || !e_ed || !e_scr || !e_tbl || !e_layout) {
        fail_at(__LINE__);
        goto done_phase1;
    }

    InterlockedExchange(&d->stage, 61);
    if (FAILED(e_btn->GetCurrentPatternAs(UIA_InvokePatternId, IID_IUIAutomationInvokePattern, reinterpret_cast<void**>(&stale_invoke))) || !stale_invoke) {
        fail_at(__LINE__);
        goto done_phase1;
    }
    if (FAILED(stale_invoke->Invoke())) {
        fail_at(__LINE__);
        goto done_phase1;
    }

    InterlockedExchange(&d->stage, 62);
    {
        IUIAutomationTogglePattern* p_toggle = nullptr;
        if (FAILED(e_chk->GetCurrentPatternAs(UIA_TogglePatternId, IID_IUIAutomationTogglePattern, reinterpret_cast<void**>(&p_toggle))) || !p_toggle) {
            fail_at(__LINE__);
            goto done_phase1;
        }
        ToggleState ts = ToggleState_Indeterminate;
        const bool ts_ok = SUCCEEDED(p_toggle->get_CurrentToggleState(&ts)) && ts == ToggleState_Off;
        const HRESULT hr_tog = ts_ok ? p_toggle->Toggle() : E_FAIL;
        p_toggle->Release();
        if (!ts_ok || FAILED(hr_tog)) {
            fail_at(__LINE__);
            goto done_phase1;
        }
    }

    InterlockedExchange(&d->stage, 63);
    {
        IUIAutomationRangeValuePattern* p_range = nullptr;
        const HRESULT hr_pat = e_sld->GetCurrentPatternAs(UIA_RangeValuePatternId, IID_IUIAutomationRangeValuePattern, reinterpret_cast<void**>(&p_range));
        if (FAILED(hr_pat) || !p_range) {
            fail_at(__LINE__);
            goto done_phase1;
        }
        InterlockedExchange(&d->stage, 631);
        double rmin = -1.0, rmax = -1.0;
        const HRESULT hr_min = p_range->get_CurrentMinimum(&rmin);
        InterlockedExchange(&d->stage, 632);
        const HRESULT hr_max = p_range->get_CurrentMaximum(&rmax);
        InterlockedExchange(&d->stage, 633);
        const bool bounds_ok = SUCCEEDED(hr_min) && SUCCEEDED(hr_max) &&
            std::fabs(rmin - 0.0) < 0.01 && std::fabs(rmax - 100.0) < 0.01;
        const HRESULT hr_set = bounds_ok ? p_range->SetValue(75.0) : E_FAIL;
        InterlockedExchange(&d->stage, 634);
        p_range->Release();
        if (!bounds_ok || FAILED(hr_set)) {
            fail_at(__LINE__);
            goto done_phase1;
        }
    }

    InterlockedExchange(&d->stage, 64);
    {
        IUIAutomationValuePattern* p_val = nullptr;
        if (FAILED(e_txt->GetCurrentPatternAs(UIA_ValuePatternId, IID_IUIAutomationValuePattern, reinterpret_cast<void**>(&p_val))) || !p_val) {
            fail_at(__LINE__);
            goto done_phase1;
        }
        BOOL focused = FALSE;
        BSTR cur_val = nullptr;
        const bool focus_ok = SUCCEEDED(e_txt->get_CurrentHasKeyboardFocus(&focused)) && focused == TRUE;
        const bool val_ok = SUCCEEDED(p_val->get_CurrentValue(&cur_val)) && bstr_equals_w(cur_val, L"hello");
        if (cur_val) SysFreeString(cur_val);
        BSTR next_val = SysAllocString(L"updated_\u4E2D\u6587");
        const HRESULT hr_set = (focus_ok && val_ok && next_val) ? p_val->SetValue(next_val) : E_FAIL;
        if (next_val) SysFreeString(next_val);
        wchar_t over_buf[1100];
        for (size_t oi = 0u; oi < 1099u; ++oi) over_buf[oi] = L'x';
        over_buf[1099] = L'\0';
        BSTR over_val = SysAllocString(over_buf);
        const HRESULT hr_over = over_val ? p_val->SetValue(over_val) : E_FAIL;
        if (over_val) SysFreeString(over_val);
        p_val->Release();
        if (!focus_ok || !val_ok || FAILED(hr_set) || SUCCEEDED(hr_over)) {
            fail_at(__LINE__);
            goto done_phase1;
        }
    }

    InterlockedExchange(&d->stage, 65);
    {
        BOOL is_pw = FALSE;
        if (FAILED(e_pwd->get_CurrentIsPassword(&is_pw)) || is_pw != TRUE) {
            fail_at(__LINE__);
            goto done_phase1;
        }
        IUIAutomationValuePattern* p_pwd = nullptr;
        if (FAILED(e_pwd->GetCurrentPatternAs(UIA_ValuePatternId, IID_IUIAutomationValuePattern, reinterpret_cast<void**>(&p_pwd))) || !p_pwd) {
            fail_at(__LINE__);
            goto done_phase1;
        }
        BSTR pw_val = nullptr;
        const bool masked_ok = SUCCEEDED(p_pwd->get_CurrentValue(&pw_val)) && pw_val &&
            std::wcscmp(pw_val, L"s3cr3t") != 0 && pw_val[0] == L'\u2022';
        if (pw_val) SysFreeString(pw_val);
        p_pwd->Release();
        if (!masked_ok) {
            fail_at(__LINE__);
            goto done_phase1;
        }
    }

    InterlockedExchange(&d->stage, 66);
    {
        IUIAutomationExpandCollapsePattern* p_ec = nullptr;
        if (FAILED(e_col->GetCurrentPatternAs(UIA_ExpandCollapsePatternId, IID_IUIAutomationExpandCollapsePattern, reinterpret_cast<void**>(&p_ec))) || !p_ec) {
            fail_at(__LINE__);
            goto done_phase1;
        }
        ExpandCollapseState ecs = ExpandCollapseState_LeafNode;
        const bool ecs_ok = SUCCEEDED(p_ec->get_CurrentExpandCollapseState(&ecs)) && ecs == ExpandCollapseState_Collapsed;
        const HRESULT hr_exp = ecs_ok ? p_ec->Expand() : E_FAIL;
        p_ec->Release();
        if (!ecs_ok || FAILED(hr_exp)) {
            fail_at(__LINE__);
            goto done_phase1;
        }
    }

    // 67. ITextProvider & ITextRangeProvider on text_input (plain & password) and code_editor.
    InterlockedExchange(&d->stage, 67);
    {
        IUIAutomationTextPattern* p_txt = nullptr;
        if (FAILED(e_txt->GetCurrentPatternAs(UIA_TextPatternId, IID_IUIAutomationTextPattern, reinterpret_cast<void**>(&p_txt))) || !p_txt) {
            fail_at(__LINE__);
            goto done_phase1;
        }
        SupportedTextSelection sts = SupportedTextSelection_None;
        IUIAutomationTextRange* doc = nullptr;
        IUIAutomationTextRange* found = nullptr;
        IUIAutomationTextRangeArray* sel_arr = nullptr;
        BSTR full_bstr = nullptr;
        BSTR short_bstr = nullptr;
        BSTR found_bstr = nullptr;
        BSTR sel_bstr = nullptr;
        SAFEARRAY* rects = nullptr;
        bool txt_pat_ok = SUCCEEDED(p_txt->get_SupportedTextSelection(&sts)) && sts == SupportedTextSelection_Single &&
            SUCCEEDED(p_txt->get_DocumentRange(&doc)) && doc != nullptr;
        if (txt_pat_ok) {
            txt_pat_ok = SUCCEEDED(doc->GetText(-1, &full_bstr)) && bstr_equals_w(full_bstr, L"hello") &&
                SUCCEEDED(doc->GetText(3, &short_bstr)) && bstr_equals_w(short_bstr, L"hel");
        }
        if (txt_pat_ok) {
            txt_pat_ok = SUCCEEDED(doc->GetBoundingRectangles(&rects)) && rects != nullptr;
            if (txt_pat_ok) {
                LONG lb = 0, ub = -1;
                double* vals = nullptr;
                txt_pat_ok = SUCCEEDED(SafeArrayGetLBound(rects, 1u, &lb)) &&
                    SUCCEEDED(SafeArrayGetUBound(rects, 1u, &ub)) && (ub - lb + 1) >= 4 &&
                    SUCCEEDED(SafeArrayAccessData(rects, reinterpret_cast<void**>(&vals))) && vals != nullptr &&
                    vals[2] > 0.0 && vals[3] > 0.0;
                if (vals) SafeArrayUnaccessData(rects);
            }
        }
        if (txt_pat_ok) {
            BSTR needle = SysAllocString(L"ell");
            txt_pat_ok = needle && SUCCEEDED(doc->FindText(needle, FALSE, FALSE, &found)) && found != nullptr &&
                SUCCEEDED(found->GetText(-1, &found_bstr)) && bstr_equals_w(found_bstr, L"ell") &&
                SUCCEEDED(found->Select()) &&
                SUCCEEDED(p_txt->GetSelection(&sel_arr)) && sel_arr != nullptr;
            if (needle) SysFreeString(needle);
            if (txt_pat_ok) {
                int slen = 0;
                IUIAutomationTextRange* sel0 = nullptr;
                txt_pat_ok = SUCCEEDED(sel_arr->get_Length(&slen)) && slen == 1 &&
                    SUCCEEDED(sel_arr->GetElement(0, &sel0)) && sel0 != nullptr &&
                    SUCCEEDED(sel0->GetText(-1, &sel_bstr)) && bstr_equals_w(sel_bstr, L"ell");
                if (sel0) sel0->Release();
            }
        }
        if (rects) SafeArrayDestroy(rects);
        if (sel_bstr) SysFreeString(sel_bstr);
        if (found_bstr) SysFreeString(found_bstr);
        if (short_bstr) SysFreeString(short_bstr);
        if (full_bstr) SysFreeString(full_bstr);
        if (sel_arr) sel_arr->Release();
        if (found) found->Release();
        if (doc) doc->Release();
        p_txt->Release();
        if (!txt_pat_ok) {
            fail_at(__LINE__);
            goto done_phase1;
        }
    }

    InterlockedExchange(&d->stage, 671);
    {
        IUIAutomationTextPattern* p_pwd_txt = nullptr;
        if (FAILED(e_pwd->GetCurrentPatternAs(UIA_TextPatternId, IID_IUIAutomationTextPattern, reinterpret_cast<void**>(&p_pwd_txt))) || !p_pwd_txt) {
            fail_at(__LINE__);
            goto done_phase1;
        }
        IUIAutomationTextRange* pdoc = nullptr;
        BSTR pbstr = nullptr;
        const bool pwd_txt_ok = SUCCEEDED(p_pwd_txt->get_DocumentRange(&pdoc)) && pdoc != nullptr &&
            SUCCEEDED(pdoc->GetText(-1, &pbstr)) && pbstr != nullptr &&
            std::wcscmp(pbstr, L"s3cr3t") != 0 && pbstr[0] == L'\u2022' && SysStringLen(pbstr) == 6u;
        if (pbstr) SysFreeString(pbstr);
        if (pdoc) pdoc->Release();
        p_pwd_txt->Release();
        if (!pwd_txt_ok) {
            fail_at(__LINE__);
            goto done_phase1;
        }
    }

    InterlockedExchange(&d->stage, 672);
    {
        IUIAutomationTextPattern* p_ed = nullptr;
        if (FAILED(e_ed->GetCurrentPatternAs(UIA_TextPatternId, IID_IUIAutomationTextPattern, reinterpret_cast<void**>(&p_ed))) || !p_ed) {
            fail_at(__LINE__);
            goto done_phase1;
        }
        IUIAutomationTextRange* edoc = nullptr;
        IUIAutomationTextRange* lrange = nullptr;
        IUIAutomationTextRangeArray* esel = nullptr;
        BSTR full_ed = nullptr;
        BSTR line0 = nullptr;
        BSTR line1 = nullptr;
        SAFEARRAY* erect = nullptr;
        bool ed_ok = SUCCEEDED(p_ed->get_DocumentRange(&edoc)) && edoc != nullptr &&
            SUCCEEDED(edoc->GetText(-1, &full_ed)) && bstr_equals_w(full_ed, L"alpha beta\ngamma delta") &&
            SUCCEEDED(edoc->GetBoundingRectangles(&erect)) && erect != nullptr &&
            SUCCEEDED(p_ed->GetSelection(&esel)) && esel != nullptr;
        if (ed_ok) {
            int moved = 0;
            ed_ok = SUCCEEDED(edoc->Clone(&lrange)) && lrange != nullptr &&
                SUCCEEDED(lrange->MoveEndpointByRange(TextPatternRangeEndpoint_End, edoc, TextPatternRangeEndpoint_Start)) &&
                SUCCEEDED(lrange->ExpandToEnclosingUnit(TextUnit_Line)) &&
                SUCCEEDED(lrange->GetText(-1, &line0)) && bstr_equals_w(line0, L"alpha beta") &&
                SUCCEEDED(lrange->Move(TextUnit_Line, 1, &moved)) && moved == 1 &&
                SUCCEEDED(lrange->GetText(-1, &line1)) && bstr_equals_w(line1, L"gamma delta");
        }
        if (erect) SafeArrayDestroy(erect);
        if (line1) SysFreeString(line1);
        if (line0) SysFreeString(line0);
        if (full_ed) SysFreeString(full_ed);
        if (esel) esel->Release();
        if (lrange) lrange->Release();
        if (edoc) edoc->Release();
        p_ed->Release();
        if (!ed_ok) {
            fail_at(__LINE__);
            goto done_phase1;
        }
    }

    // 68. IScrollProvider on scrollable box.
    InterlockedExchange(&d->stage, 68);
    {
        IUIAutomationScrollPattern* p_scr = nullptr;
        if (FAILED(e_scr->GetCurrentPatternAs(UIA_ScrollPatternId, IID_IUIAutomationScrollPattern, reinterpret_cast<void**>(&p_scr))) || !p_scr) {
            fail_at(__LINE__);
            goto done_phase1;
        }
        BOOL v_scr = FALSE;
        double v_view = 100.0, v_pct0 = -1.0, v_pct1 = -1.0, v_pct2 = -1.0;
        const bool scr_ok = SUCCEEDED(p_scr->get_CurrentVerticallyScrollable(&v_scr)) && v_scr == TRUE &&
            SUCCEEDED(p_scr->get_CurrentVerticalViewSize(&v_view)) && v_view > 0.0 && v_view < 100.0 &&
            SUCCEEDED(p_scr->get_CurrentVerticalScrollPercent(&v_pct0)) && std::fabs(v_pct0) < 1.0 &&
            SUCCEEDED(p_scr->Scroll(ScrollAmount_NoAmount, ScrollAmount_SmallIncrement)) &&
            SUCCEEDED(p_scr->get_CurrentVerticalScrollPercent(&v_pct1)) && v_pct1 > v_pct0 &&
            SUCCEEDED(p_scr->SetScrollPercent(UIA_ScrollPatternNoScroll, 50.0)) &&
            SUCCEEDED(p_scr->get_CurrentVerticalScrollPercent(&v_pct2)) && std::fabs(v_pct2 - 50.0) < 2.0;
        p_scr->Release();
        if (!scr_ok) {
            fail_at(__LINE__);
            goto done_phase1;
        }
    }

    // 69. IGridProvider / ITableProvider / IGridItemProvider / ITableItemProvider on table.
    InterlockedExchange(&d->stage, 69);
    {
        IUIAutomationGridPattern* p_grid = nullptr;
        IUIAutomationTablePattern* p_table = nullptr;
        IUIAutomationElement* e_cell = nullptr;
        IUIAutomationElementArray* col_hdrs = nullptr;
        IUIAutomationGridItemPattern* p_gitem = nullptr;
        IUIAutomationTableItemPattern* p_titem = nullptr;
        IUIAutomationElement* e_owner_grid = nullptr;
        IUIAutomationElementArray* cell_hdrs = nullptr;
        BSTR owner_id = nullptr;
        wchar_t expected_tbl[32] = {};
        std::swprintf(expected_tbl, 32u, L"etch_%08X", d->tbl_id);

        int rows = 0, cols = 0, hdr_cnt = 0;
        RowOrColumnMajor major = RowOrColumnMajor_Indeterminate;
        int cr = -1, cc = -1, rs = 0, cs = 0, ch_cnt = 0;
        bool tbl_ok = SUCCEEDED(e_tbl->GetCurrentPatternAs(UIA_GridPatternId, IID_IUIAutomationGridPattern, reinterpret_cast<void**>(&p_grid))) && p_grid &&
            SUCCEEDED(e_tbl->GetCurrentPatternAs(UIA_TablePatternId, IID_IUIAutomationTablePattern, reinterpret_cast<void**>(&p_table))) && p_table &&
            SUCCEEDED(p_grid->get_CurrentRowCount(&rows)) && rows == 3 &&
            SUCCEEDED(p_grid->get_CurrentColumnCount(&cols)) && cols == 2 &&
            SUCCEEDED(p_table->get_CurrentRowOrColumnMajor(&major)) && major == RowOrColumnMajor_RowMajor &&
            SUCCEEDED(p_table->GetCurrentColumnHeaders(&col_hdrs)) && col_hdrs &&
            SUCCEEDED(col_hdrs->get_Length(&hdr_cnt)) && hdr_cnt == 2 &&
            SUCCEEDED(p_grid->GetItem(1, 1, &e_cell)) && e_cell &&
            SUCCEEDED(e_cell->GetCurrentPatternAs(UIA_GridItemPatternId, IID_IUIAutomationGridItemPattern, reinterpret_cast<void**>(&p_gitem))) && p_gitem &&
            SUCCEEDED(e_cell->GetCurrentPatternAs(UIA_TableItemPatternId, IID_IUIAutomationTableItemPattern, reinterpret_cast<void**>(&p_titem))) && p_titem &&
            SUCCEEDED(p_gitem->get_CurrentRow(&cr)) && cr == 1 &&
            SUCCEEDED(p_gitem->get_CurrentColumn(&cc)) && cc == 1 &&
            SUCCEEDED(p_gitem->get_CurrentRowSpan(&rs)) && rs == 1 &&
            SUCCEEDED(p_gitem->get_CurrentColumnSpan(&cs)) && cs == 1 &&
            SUCCEEDED(p_gitem->get_CurrentContainingGrid(&e_owner_grid)) && e_owner_grid &&
            SUCCEEDED(e_owner_grid->get_CurrentAutomationId(&owner_id)) && bstr_equals_w(owner_id, expected_tbl) &&
            SUCCEEDED(p_titem->GetCurrentColumnHeaderItems(&cell_hdrs)) && cell_hdrs &&
            SUCCEEDED(cell_hdrs->get_Length(&ch_cnt)) && ch_cnt == 1;

        if (owner_id) SysFreeString(owner_id);
        if (cell_hdrs) cell_hdrs->Release();
        if (e_owner_grid) e_owner_grid->Release();
        if (p_titem) p_titem->Release();
        if (p_gitem) p_gitem->Release();
        if (e_cell) e_cell->Release();
        if (col_hdrs) col_hdrs->Release();
        if (p_table) p_table->Release();
        if (p_grid) p_grid->Release();
        if (!tbl_ok) {
            fail_at(__LINE__);
            goto done_phase1;
        }
    }

    // 70. Clean Control Tree: anonymous layout box is NOT a control/content element; labeled button IS.
    InterlockedExchange(&d->stage, 70);
    {
        BOOL layout_ctrl = TRUE, layout_content = TRUE, btn_ctrl = FALSE, btn_content = FALSE;
        IUIAutomationElement* ctrl_btn = find_uia_by_id(root, ctrl_walker, d->btn_id);
        IUIAutomationElement* ctrl_layout = find_uia_by_id(root, ctrl_walker, d->layout_id);
        const bool ctrl_tree_ok =
            SUCCEEDED(e_layout->get_CurrentIsControlElement(&layout_ctrl)) && layout_ctrl == FALSE &&
            SUCCEEDED(e_layout->get_CurrentIsContentElement(&layout_content)) && layout_content == FALSE &&
            SUCCEEDED(e_btn->get_CurrentIsControlElement(&btn_ctrl)) && btn_ctrl == TRUE &&
            SUCCEEDED(e_btn->get_CurrentIsContentElement(&btn_content)) && btn_content == TRUE &&
            ctrl_btn != nullptr && ctrl_layout == nullptr;
        if (ctrl_layout) ctrl_layout->Release();
        if (ctrl_btn) ctrl_btn->Release();
        if (!ctrl_tree_ok) {
            fail_at(__LINE__);
            goto done_phase1;
        }
    }

    // 71. Hitbox-backed ElementFromPoint hits floating overlay strictly instead of occluded button behind it.
    InterlockedExchange(&d->stage, 71);
    if (d->test_hit_from_point) {
        IUIAutomationElement* e_hit = nullptr;
        BSTR hit_id = nullptr;
        wchar_t expected_overlay[32] = {};
        std::swprintf(expected_overlay, 32u, L"etch_%08X", d->overlay_btn_id);
        const bool hit_ok = SUCCEEDED(uia->ElementFromPoint(d->hit_screen_pt, &e_hit)) && e_hit != nullptr &&
            SUCCEEDED(e_hit->get_CurrentAutomationId(&hit_id)) && bstr_equals_w(hit_id, expected_overlay);
        if (hit_id) SysFreeString(hit_id);
        if (e_hit) e_hit->Release();
        if (!hit_ok) {
            fail_at(__LINE__);
            goto done_phase1;
        }
    }

    d->phase1_ok = true;

done_phase1:
    if (e_layout) e_layout->Release();
    if (e_tbl) e_tbl->Release();
    if (e_scr) e_scr->Release();
    if (e_ed) e_ed->Release();
    if (e_col) e_col->Release();
    if (e_pwd) e_pwd->Release();
    if (e_txt) e_txt->Release();
    if (e_sld) e_sld->Release();
    if (e_chk) e_chk->Release();
    if (e_btn) e_btn->Release();
    if (ctrl_walker) ctrl_walker->Release();
    if (walker) walker->Release();
    if (root) root->Release();
    if (uia) uia->Release();

    if (d->ev_phase1_done) {
        SetEvent(d->ev_phase1_done);
    }
    if (d->ev_phase2_go) {
        WaitForSingleObject(d->ev_phase2_go, 5000u);
    }
    if (d->phase1_ok && stale_invoke) {
        d->stale_hr = stale_invoke->Invoke();
        d->phase2_ok = FAILED(d->stale_hr);
        if (!d->phase2_ok && d->fail_line == 0) {
            d->fail_line = __LINE__;
        }
    }
    if (stale_invoke) {
        stale_invoke->Release();
    }
    CoUninitialize();
    return 0u;
}

inline bool pump_until_event(HANDLE ev, DWORD timeout_ms) noexcept {
    const bool bounded = (timeout_ms != INFINITE);
    const ULONGLONG deadline = bounded ? (GetTickCount64() + static_cast<ULONGLONG>(timeout_ms)) : 0ull;
    for (;;) {
        MSG msg = {};
        while (PeekMessageW(&msg, nullptr, 0u, 0u, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (WaitForSingleObject(ev, 0u) == WAIT_OBJECT_0) {
            return true;
        }
        DWORD rem = INFINITE;
        if (bounded) {
            const ULONGLONG now = GetTickCount64();
            if (now >= deadline) {
                return false;
            }
            rem = static_cast<DWORD>(deadline - now);
        }
        const DWORD wait_res = MsgWaitForMultipleObjectsEx(1u, &ev, rem, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (wait_res == WAIT_OBJECT_0) {
            return true;
        }
        if (wait_res != WAIT_OBJECT_0 + 1u) {
            return false;
        }
    }
}

#endif // defined(_WIN32) && ETCH_ENABLE_OS_A11Y

} // namespace os_window_a11y_checks

inline bool verify_os_window_a11y_checks() noexcept {
    using namespace etch;
    using namespace os_window_a11y_checks;
    Context* previous = current_context();
    bool ok = true;
    const auto check = [&](bool pass, int line) noexcept {
        if (!pass) {
            ok = false;
            std::fprintf(stderr, "v080 os/window/a11y check line %d failed (%s)\n", line, SDL_GetError());
        }
    };

    // 1. Headless root A11yRole::Window logical bounds across DPI scales.
    {
        Context* hc = create_context({.headless = true});
        if (!hc) {
            return false;
        }
        set_current_context(hc);
        const float scales[] = {1.0f, 1.5f, 2.0f};
        for (float scale : scales) {
            begin_frame(hc, {.viewport_w = 1200u, .viewport_h = 900u, .dpi_scale = scale});
            box({.id = id("root_probe"), .width = Sizing::fixed(100.0f), .height = Sizing::fixed(40.0f)});
            end_frame(hc);
            const auto nodes = frame_a11y_nodes(hc);
            check(!nodes.empty() && nodes[0].role == A11yRole::Window, __LINE__);
            if (!nodes.empty()) {
                check(std::fabs(nodes[0].bounds.x) < 0.01f && std::fabs(nodes[0].bounds.y) < 0.01f, __LINE__);
                check(std::fabs(nodes[0].bounds.w - (1200.0f / scale)) < 0.05f, __LINE__);
                check(std::fabs(nodes[0].bounds.h - (900.0f / scale)) < 0.05f, __LINE__);
            }
        }
        destroy_context(hc);
        set_current_context(nullptr);
    }

    // 2. Multi-monitor DPI transition (1.0x -> 2.0x -> 1.25x): logical layout invariance, click hit accuracy, bitmap emoji PPEM promotion.
    {
        Context* dc = create_context({.headless = true});
        if (!dc) {
            set_current_context(previous);
            return false;
        }
        set_current_context(dc);
        const auto sbix = media_emoji_checks::font(false);
        const uint16_t fs = load_font_memory(dc, sbix.data, sbix.size);
        check(fs != kInvalidFont, __LINE__);

        uint32_t btn_hits = 0u;
        const auto run_dpi_frame = [&](uint32_t vw, uint32_t vh, float dpi, Vec2 ptr, bool down) noexcept {
            begin_frame(dc, {
                .viewport_w = vw,
                .viewport_h = vh,
                .dpi_scale = dpi,
                .dt_seconds = 1.0f / 60.0f,
                .pointer_pos = ptr,
                .pointer_down = down,
            });
            begin_box({
                .width = Sizing::fixed(320.0f),
                .height = Sizing::fixed(120.0f),
                .background = Color::rgba8(0u, 0u, 0u, 255u),
            });
            if (button({.id = id("dpi_btn"), .label = str_lit("Click"), .width = Sizing::fixed(100.0f), .height = Sizing::fixed(32.0f)})) {
                ++btn_hits;
            }
            draw_text({.text = str_lit("\xF0\x9F\x98\x80"), .pos = {10.0f, 70.0f}, .font_id = fs, .size = 14.0f});
            end_box();
            end_frame(dc);
        };

        const char* png_1x = "_etch_v080_dpi1.png";
        const char* png_2x = "_etch_v080_dpi2.png";
        run_dpi_frame(320u, 120u, 1.0f, {-1.0f, -1.0f}, false);
        const Rect b1 = element_bounds(id("dpi_btn"));
        check(b1.w > 10.0f && b1.h > 10.0f && frame_stats(dc).draw_call_count <= 8u, __LINE__);
        check(render_headless_png(dc, 320u, 120u, png_1x), __LINE__);
        if (SDL_Surface* s1 = SDL_LoadPNG(png_1x)) {
            check(media_emoji_checks::pixel(s1, 16, 76, 0x00FFFFu), __LINE__);
            SDL_DestroySurface(s1);
        } else {
            check(false, __LINE__);
        }
        std::remove(png_1x);

        const Vec2 btn_center = {b1.x + b1.w * 0.5f, b1.y + b1.h * 0.5f};
        run_dpi_frame(640u, 240u, 2.0f, {-1.0f, -1.0f}, false);
        const Rect b2 = element_bounds(id("dpi_btn"));
        check(std::fabs(b2.x - b1.x) < 0.5f && std::fabs(b2.y - b1.y) < 0.5f &&
              std::fabs(b2.w - b1.w) < 0.5f && std::fabs(b2.h - b1.h) < 0.5f, __LINE__);
        run_dpi_frame(640u, 240u, 2.0f, btn_center, true);
        run_dpi_frame(640u, 240u, 2.0f, btn_center, false);
        check(btn_hits == 1u && frame_stats(dc).draw_call_count <= 8u, __LINE__);
        check(render_headless_png(dc, 640u, 240u, png_2x), __LINE__);
        if (SDL_Surface* s2 = SDL_LoadPNG(png_2x)) {
            check(media_emoji_checks::pixel(s2, 32, 152, 0xFF00FFu), __LINE__);
            SDL_DestroySurface(s2);
        } else {
            check(false, __LINE__);
        }
        std::remove(png_2x);

        run_dpi_frame(400u, 150u, 1.25f, {-1.0f, -1.0f}, false);
        const Rect b125 = element_bounds(id("dpi_btn"));
        check(std::fabs(b125.x - b1.x) < 0.5f && std::fabs(b125.y - b1.y) < 0.5f &&
              std::fabs(b125.w - b1.w) < 0.5f && std::fabs(b125.h - b1.h) < 0.5f, __LINE__);
        run_dpi_frame(400u, 150u, 1.25f, btn_center, true);
        run_dpi_frame(400u, 150u, 1.25f, btn_center, false);
        check(btn_hits == 2u, __LINE__);

        destroy_context(dc);
        set_current_context(nullptr);
    }

    // 3, 4, 5. Window chrome/fullscreen/safe-area, refresh callback & re-entrancy, and Win32 UIA bridge.
    const Padding empty_insets = window_safe_insets(nullptr, nullptr);
    check(empty_insets.top == 0.0f && empty_insets.right == 0.0f &&
          empty_insets.bottom == 0.0f && empty_insets.left == 0.0f, __LINE__);

    SDL_Window* win = create_desktop_window("Etch UIA Test", 480, 320, SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE);
    if (win) {
        int pw=0, ph=0;
        SDL_GetWindowSizeInPixels(win,&pw,&ph);
        const float scale=SDL_GetWindowDisplayScale(win);
        check(std::fabs(pw/scale-480)<2 && std::fabs(ph/scale-320)<2, __LINE__);
        check((SDL_GetWindowFlags(win)&SDL_WINDOW_HIDDEN)!=0, __LINE__);
    }
    check(win != nullptr, __LINE__);
    Context* wctx = win ? create_context({.window = win, .headless = false, .enable_os_a11y = true}) : nullptr;
    check(wctx != nullptr && gpu_device(wctx) != nullptr, __LINE__);
    if (!wctx || !gpu_device(wctx)) {
        if (wctx) destroy_context(wctx);
        if (win) SDL_DestroyWindow(win);
        set_current_context(previous);
        return false;
    }
    set_current_context(wctx);

    // Simulate an old monitor scale through the helper's window-local state.
    SDL_SetWindowMinimumSize(win,120,80);
    const float coordinate_scale=SDL_GetWindowDisplayScale(win)/SDL_GetWindowPixelDensity(win);
    SDL_SetFloatProperty(SDL_GetWindowProperties(win),"etch.desktop.coordinate_scale",coordinate_scale*.5f);
    SDL_Event dpi_event={};dpi_event.type=SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED;dpi_event.window.windowID=SDL_GetWindowID(win);
    process_event(wctx,dpi_event);
    int min_w=0,min_h=0;SDL_GetWindowMinimumSize(win,&min_w,&min_h);
    check(min_w==240 && min_h==160,__LINE__);
    process_event(wctx,dpi_event);SDL_GetWindowMinimumSize(win,&min_w,&min_h);
    check(min_w==240 && min_h==160,__LINE__); // Duplicate events must not rescale twice.
    SDL_SetWindowMinimumSize(win,0,0);
    check(set_desktop_window_min_size(win,121,81),__LINE__);
    SDL_GetWindowMinimumSize(win,&min_w,&min_h);
    check(min_w==int(std::ceil(121*coordinate_scale)) && min_h==int(std::ceil(81*coordinate_scale)),__LINE__);
    SDL_SetFloatProperty(SDL_GetWindowProperties(win),"etch.desktop.coordinate_scale",coordinate_scale*.5f);
    process_event(wctx,dpi_event);SDL_GetWindowMinimumSize(win,&min_w,&min_h);
    check(min_w==int(std::ceil(121*coordinate_scale)) && min_h==int(std::ceil(81*coordinate_scale)),__LINE__);
    check(set_desktop_window_min_size(win,0,0),__LINE__);
    SDL_Window* target=create_desktop_window_on_display("Display target",160,100,SDL_GetPrimaryDisplay(),SDL_WINDOW_HIDDEN);
    check(target && SDL_GetDisplayForWindow(target)==SDL_GetPrimaryDisplay(),__LINE__);
    if(target) SDL_DestroyWindow(target);
    target=create_desktop_window_at_pointer("Pointer target",160,100,SDL_WINDOW_HIDDEN);
    check(target!=nullptr,__LINE__);if(target)SDL_DestroyWindow(target);


    // 3a. Chrome + safe insets + windowed vs fullscreen hit-testing
    set_window_chrome({
        .enabled = true,
        .resizable = true,
        .border_thickness = 8.0f,
        .corner_size = 16.0f,
        .macos_traffic_lights = true,
        .traffic_light_inset_px = -10.0f,
    }, win);
    begin_window_frame(wctx, win, 0.0f);
    window_drag_region({0.0f, 0.0f, 480.0f, 32.0f});
    box({.width = Sizing::fixed(480.0f), .height = Sizing::fixed(320.0f)});
    end_window_frame(wctx);

    const Padding win_insets = window_safe_insets(wctx, win);
    check(std::isfinite(win_insets.top) && win_insets.top >= 0.0f &&
          std::isfinite(win_insets.right) && win_insets.right >= 0.0f &&
          std::isfinite(win_insets.bottom) && win_insets.bottom >= 0.0f &&
          std::isfinite(win_insets.left) && win_insets.left >= 0.0f, __LINE__);
    check(evaluate_window_hit(wctx, {2.0f, 80.0f}, win) == WindowHit::ResizeLeft, __LINE__);
    check(evaluate_window_hit(wctx, {120.0f, 16.0f}, win) == WindowHit::Caption, __LINE__);

    begin_window_frame(wctx,win,0);
    window_drag_region({0,0,480,32});
    begin_box({.id=id("chrome.root_wrap"),.axis=Axis::Col,.width=Sizing::fixed(480),.height=Sizing::fixed(320)});
    begin_box({.width=Sizing::fixed(480),.height=Sizing::fixed(32)});
    box({.id=id("chrome.click"),.width=Sizing::fixed(40),.height=Sizing::fixed(32)});
    box({.id=id("chrome.pass"),.width=Sizing::fixed(40),.height=Sizing::fixed(32),.window_drag_passthrough=true});
    box({.id=id("chrome.max"),.width=Sizing::fixed(40),.height=Sizing::fixed(32)});
    window_maximize_button_region(id("chrome.max"));end_box();end_box();end_window_frame(wctx);
    check(evaluate_window_hit(wctx,{20,16},win)==WindowHit::Client,__LINE__);
    check(evaluate_window_hit(wctx,{60,16},win)==WindowHit::Caption,__LINE__);
    check(evaluate_window_hit(wctx,{240,16},win)==WindowHit::Caption,__LINE__);
    check(evaluate_window_hit(wctx,{100,16},win)==WindowHit::Maximize,__LINE__);
#if defined(_WIN32) && defined(ETCH_ENABLE_OS_A11Y) && ETCH_ENABLE_OS_A11Y
    {
        HWND native=static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(win),SDL_PROP_WINDOW_WIN32_HWND_POINTER,nullptr));
        const float factor=SDL_GetWindowDisplayScale(win)/SDL_GetWindowPixelDensity(win);
        auto hit_at=[&](float lx,float ly) noexcept -> LRESULT {
            POINT pt={LONG(lx*factor),LONG(ly*factor)};
            ClientToScreen(native,&pt);
            return SendMessageW(native,WM_NCHITTEST,0,MAKELPARAM(pt.x,pt.y));
        };
        POINT client_pt={LONG(100*factor),LONG(16*factor)};
        POINT screen_pt=client_pt;ClientToScreen(native,&screen_pt);
        const LPARAM screen_lp=MAKELPARAM(screen_pt.x,screen_pt.y);
        const LPARAM client_lp=MAKELPARAM(client_pt.x,client_pt.y);
        check(SendMessageW(native,WM_NCHITTEST,0,screen_lp)==HTMAXBUTTON,__LINE__);
        check(hit_at(240.0f,16.0f)==HTCAPTION,__LINE__);
        check(hit_at(60.0f,16.0f)==HTCAPTION,__LINE__);
        check(hit_at(20.0f,16.0f)==HTCLIENT,__LINE__);
        check(hit_at(2.0f,80.0f)==HTLEFT,__LINE__);

        auto pump_win_events=[&]() noexcept {
            SDL_Event ev={};
            while(SDL_PollEvent(&ev)) { process_event(wctx,ev); }
        };
        auto build_chrome_bar=[&]() noexcept -> bool {
            begin_window_frame(wctx,win,0);
            window_drag_region({0,0,480,32});
            begin_box({.width=Sizing::fixed(480),.height=Sizing::fixed(32)});
            box({.id=id("chrome.click"),.width=Sizing::fixed(40),.height=Sizing::fixed(32)});
            box({.id=id("chrome.pass"),.width=Sizing::fixed(40),.height=Sizing::fixed(32),.window_drag_passthrough=true});
            box({.id=id("chrome.max"),.width=Sizing::fixed(40),.height=Sizing::fixed(32)});
            window_maximize_button_region(id("chrome.max"));
            const bool did_click=clicked(id("chrome.max"));
            end_box();
            end_window_frame(wctx);
            return did_click;
        };

        // Regression A: Same-pump WM_NCMOUSEMOVE -> WM_NCLBUTTONDOWN -> immediate WM_NCMOUSELEAVE
        // -> WM_CAPTURECHANGED(native) -> WM_LBUTTONUP must preserve the click and fire clicked(chrome.max).
        pump_win_events();
        SendMessageW(native,WM_NCMOUSEMOVE,HTMAXBUTTON,screen_lp);
        SendMessageW(native,WM_NCLBUTTONDOWN,HTMAXBUTTON,screen_lp);
        SendMessageW(native,WM_NCMOUSELEAVE,0,0);
        SendMessageW(native,WM_CAPTURECHANGED,0,reinterpret_cast<LPARAM>(native));
        SendMessageW(native,WM_LBUTTONUP,0,client_lp);
        pump_win_events();
        check(build_chrome_bar(),__LINE__);

        // Regression B: Two-frame press + immediate WM_NCMOUSELEAVE + self WM_CAPTURECHANGED(native) + WM_LBUTTONUP.
        pump_win_events();
        SendMessageW(native,WM_NCMOUSEMOVE,HTMAXBUTTON,screen_lp);
        SendMessageW(native,WM_NCLBUTTONDOWN,HTMAXBUTTON,screen_lp);
        SendMessageW(native,WM_NCMOUSELEAVE,0,0);
        pump_win_events();
        begin_window_frame(wctx,win,0);
        window_drag_region({0,0,480,32});
        begin_box({.width=Sizing::fixed(480),.height=Sizing::fixed(32)});
        box({.id=id("chrome.click"),.width=Sizing::fixed(40),.height=Sizing::fixed(32)});
        box({.id=id("chrome.pass"),.width=Sizing::fixed(40),.height=Sizing::fixed(32),.window_drag_passthrough=true});
        box({.id=id("chrome.max"),.width=Sizing::fixed(40),.height=Sizing::fixed(32)});
        window_maximize_button_region(id("chrome.max"));
        const bool is_down_mid=pressed(id("chrome.max"));
        end_box();
        end_window_frame(wctx);
        check(is_down_mid,__LINE__);
        SendMessageW(native,WM_CAPTURECHANGED,0,reinterpret_cast<LPARAM>(native));
        SendMessageW(native,WM_LBUTTONUP,0,client_lp);
        pump_win_events();
        check(build_chrome_bar(),__LINE__);

        // Regression C: External WM_CAPTURECHANGED(lparam = 0) aborts press cleanly so release does NOT click.
        pump_win_events();
        SendMessageW(native,WM_NCMOUSEMOVE,HTMAXBUTTON,screen_lp);
        SendMessageW(native,WM_NCLBUTTONDOWN,HTMAXBUTTON,screen_lp);
        pump_win_events();
        check(!build_chrome_bar(),__LINE__);
        SendMessageW(native,WM_CAPTURECHANGED,0,0);
        SendMessageW(native,WM_LBUTTONUP,0,client_lp);
        pump_win_events();
        check(!build_chrome_bar(),__LINE__);
    }
#endif
#if defined(_WIN32) && defined(ETCH_ENABLE_OS_A11Y) && ETCH_ENABLE_OS_A11Y
    // Native frame changes preserve unrelated state and restore only frame style bits.
    if(SDL_Window* borderless=SDL_CreateWindow("Native frame lifecycle",320,200,SDL_WINDOW_HIDDEN|SDL_WINDOW_BORDERLESS|SDL_WINDOW_RESIZABLE)) {
        begin_window_frame(wctx,borderless,0);end_window_frame(wctx);
        HWND native=static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(borderless),SDL_PROP_WINDOW_WIN32_HWND_POINTER,nullptr));
        const LONG_PTR mask=WS_CAPTION|WS_THICKFRAME|WS_SYSMENU|WS_MINIMIZEBOX|WS_MAXIMIZEBOX;
        const LONG_PTR before=GetWindowLongPtrW(native,GWL_STYLE);
        set_window_chrome({.enabled=true},borderless);
        check((GetWindowLongPtrW(native,GWL_STYLE)&mask)==mask,__LINE__);
        SetWindowLongPtrW(native,GWL_STYLE,GetWindowLongPtrW(native,GWL_STYLE)|WS_DISABLED);
        set_window_chrome({.enabled=false},borderless);
        check((GetWindowLongPtrW(native,GWL_STYLE)&mask)==(before&mask),__LINE__);
        check((GetWindowLongPtrW(native,GWL_STYLE)&WS_DISABLED)!=0,__LINE__);
        set_window_chrome({.enabled=true},borderless);release_window(wctx,borderless);
        check((GetWindowLongPtrW(native,GWL_STYLE)&mask)==(before&mask),__LINE__);
        check((GetWindowLongPtrW(native,GWL_STYLE)&WS_DISABLED)!=0,__LINE__);
        SDL_DestroyWindow(borderless);
    } else check(false,__LINE__);
    // Non-resizable borderless window must not gain WS_THICKFRAME / WS_MAXIMIZEBOX; subclass detach must survive out-of-order teardown.
    if(SDL_Window* fixed_bl=SDL_CreateWindow("Fixed borderless",240,160,SDL_WINDOW_HIDDEN|SDL_WINDOW_BORDERLESS)) {
        begin_window_frame(wctx,fixed_bl,0);end_window_frame(wctx);
        HWND native=static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(fixed_bl),SDL_PROP_WINDOW_WIN32_HWND_POINTER,nullptr));
        set_window_chrome({.enabled=true},fixed_bl);
        const LONG_PTR style=GetWindowLongPtrW(native,GWL_STYLE);
        check((style&(WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX))==(WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX),__LINE__);
        check((style&(WS_THICKFRAME|WS_MAXIMIZEBOX))==0,__LINE__);
        // Multi-window set_theme must dirty ALL claimed windows.
        for(int i=0;i<4;++i){begin_window_frame(wctx,win,0);end_window_frame(wctx);begin_window_frame(wctx,fixed_bl,0);end_window_frame(wctx);}
        check(!needs_window_redraw(wctx,win) && !needs_window_redraw(wctx,fixed_bl),__LINE__);
        set_theme(Theme::light(),wctx);
        check(needs_window_redraw(wctx,win) && needs_window_redraw(wctx,fixed_bl),__LINE__);
        set_theme(Theme::dark(),wctx);
        // Out-of-order detach: disable OS A11y while chrome is active, then destroy window without explicit release_window.
        const uint32_t before_destroy=claimed_window_count(wctx);
        set_os_a11y_enabled(wctx,false);
        set_os_a11y_enabled(wctx,true);
        SDL_DestroyWindow(fixed_bl);
        check(claimed_window_count(wctx)+1u==before_destroy,__LINE__);
    } else check(false,__LINE__);
#endif
    const bool fs_ok = set_window_fullscreen(true, win);
    if (fs_ok && is_window_fullscreen(wctx, win)) {
        check(evaluate_window_hit(wctx, {2.0f, 80.0f}, win) == WindowHit::Client, __LINE__);
        check(evaluate_window_hit(wctx, {120.0f, 16.0f}, win) == WindowHit::Client, __LINE__);
        check(set_window_fullscreen(false, win), __LINE__);
        check(!is_window_fullscreen(wctx, win), __LINE__);
        check(evaluate_window_hit(wctx, {2.0f, 80.0f}, win) == WindowHit::ResizeLeft, __LINE__);
    }
    if (SDL_Window* fs_win = SDL_CreateWindow("Etch FS Test", 320, 200, SDL_WINDOW_HIDDEN | SDL_WINDOW_FULLSCREEN)) {
        begin_window_frame(wctx, fs_win, 0.0f);
        if (frame_in_progress(wctx)) {
            window_drag_region({0.0f, 0.0f, 320.0f, 32.0f});
            box({.width = Sizing::fixed(320.0f), .height = Sizing::fixed(200.0f)});
            end_window_frame(wctx);
            set_window_chrome({.enabled = true, .resizable = true, .border_thickness = 8.0f, .corner_size = 16.0f}, fs_win);
            if (is_window_fullscreen(wctx, fs_win)) {
                check(evaluate_window_hit(wctx, {2.0f, 80.0f}, fs_win) == WindowHit::Client, __LINE__);
                check(evaluate_window_hit(wctx, {120.0f, 16.0f}, fs_win) == WindowHit::Client, __LINE__);
            }
            release_window(wctx, fs_win);
        }
        SDL_DestroyWindow(fs_win);
    }

    // 4. Refresh callback + re-entrancy protection
    {
        RefreshCheckState rstate = {.ctx = wctx, .expected_win = win};
        set_window_refresh_callback(wctx, refresh_cb, &rstate);
        const SDL_WindowID wid = SDL_GetWindowID(win);
        auto push_win_ev = [&](Uint32 type, Sint32 data1 = 0) noexcept {
            SDL_Event ev = {};
            ev.type = type;
            ev.window.windowID = wid;
            ev.window.data1 = data1;
            SDL_PushEvent(&ev);
        };
        push_win_ev(SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED);
        check(rstate.calls == 1u && !rstate.wrong_window && !rstate.reentered, __LINE__);

        // Must ignore while in_frame is true.
        begin_window_frame(wctx, win, 0.0f);
        box({.width = Sizing::fixed(100.0f), .height = Sizing::fixed(40.0f)});
        push_win_ev(SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED);
        end_window_frame(wctx);
        check(rstate.calls == 1u, __LINE__);

        // Render settled frames to clear redraw budget, then test DISPLAY_SCALE_CHANGED.
        for (int i = 0; i < 3; ++i) {
            begin_window_frame(wctx, win, 0.0f);
            box({.width = Sizing::fixed(100.0f), .height = Sizing::fixed(40.0f)});
            end_window_frame(wctx);
        }
        push_win_ev(SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED);
        check(rstate.calls == 2u && needs_window_redraw(wctx, win), __LINE__);

        // Pure window move: EXPOSED(data1=1) with unchanged pixel size & DPI must NOT invoke refresh_cb or bump redraw_budget.
        for (int i = 0; i < 3; ++i) {
            begin_window_frame(wctx, win, 0.0f);
            box({.width = Sizing::fixed(100.0f), .height = Sizing::fixed(40.0f)});
            end_window_frame(wctx);
        }
        check(!needs_window_redraw(wctx, win), __LINE__);
#if defined(_WIN32) && defined(ETCH_ENABLE_OS_A11Y) && ETCH_ENABLE_OS_A11Y
        HWND move_hwnd = static_cast<HWND>(SDL_GetPointerProperty(
            SDL_GetWindowProperties(win), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
        RECT move_rc = {};
        if (move_hwnd) {
            GetWindowRect(move_hwnd, &move_rc);
            SendMessageW(move_hwnd, WM_ENTERSIZEMOVE, 0, 0);
            SendMessageW(move_hwnd, WM_MOVING, 0, reinterpret_cast<LPARAM>(&move_rc));
        }
#endif
        push_win_ev(SDL_EVENT_WINDOW_EXPOSED, 1);
        check(rstate.calls == 2u && !needs_window_redraw(wctx, win), __LINE__);

        // Resize with changed window pixel size + WM_SIZING + EXPOSED(data1=1) MUST invoke refresh_cb live.
        int orig_w = 0, orig_h = 0;
        SDL_GetWindowSize(win, &orig_w, &orig_h);
        if (orig_w > 0 && orig_h > 0) {
            set_window_refresh_callback(wctx, nullptr, nullptr);
            SDL_SetWindowSize(win, orig_w + 32, orig_h + 16);
            SDL_Event resize_drain = {};
            while (SDL_PollEvent(&resize_drain)) {}
            set_window_refresh_callback(wctx, refresh_cb, &rstate);
#if defined(_WIN32) && defined(ETCH_ENABLE_OS_A11Y) && ETCH_ENABLE_OS_A11Y
            if (move_hwnd) {
                GetWindowRect(move_hwnd, &move_rc);
                SendMessageW(move_hwnd, WM_SIZING, WMSZ_BOTTOMRIGHT, reinterpret_cast<LPARAM>(&move_rc));
            }
#endif
            push_win_ev(SDL_EVENT_WINDOW_EXPOSED, 1);
            check(rstate.calls == 3u && needs_window_redraw(wctx, win), __LINE__);
            // Subsequent EXPOSED(data1=1) once metrics match must not stall with redundant refresh.
            push_win_ev(SDL_EVENT_WINDOW_EXPOSED, 1);
            check(rstate.calls == 3u, __LINE__);

            set_window_refresh_callback(wctx, nullptr, nullptr);
            SDL_SetWindowSize(win, orig_w, orig_h);
            while (SDL_PollEvent(&resize_drain)) {}
            set_window_refresh_callback(wctx, refresh_cb, &rstate);
        }
#if defined(_WIN32) && defined(ETCH_ENABLE_OS_A11Y) && ETCH_ENABLE_OS_A11Y
        if (move_hwnd) {
            SendMessageW(move_hwnd, WM_EXITSIZEMOVE, 0, 0);
        }
#endif
        const uint32_t calls_before_px = rstate.calls;
        push_win_ev(SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED);
        check(rstate.calls == calls_before_px + 1u && needs_window_redraw(wctx, win), __LINE__);

        set_window_refresh_callback(wctx, nullptr, nullptr);
        push_win_ev(SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED);
        check(rstate.calls == calls_before_px + 1u, __LINE__);

        SDL_Event drain = {};
        while (SDL_PollEvent(&drain)) {}
    }

    // 5. OS Accessibility Bridge verification
#if defined(_WIN32) && defined(ETCH_ENABLE_OS_A11Y) && ETCH_ENABLE_OS_A11Y
    {
        set_window_chrome({.enabled = false}, win);
        check(os_a11y_enabled(wctx) && os_a11y_active(wctx, win), __LINE__);
        uint32_t uia_btn_hits = 0u;
        bool uia_chk_val = false;
        float uia_sld_val = 20.0f;
        char uia_txt_buf[64] = "hello";
        char uia_pwd_buf[64] = "s3cr3t";
        TextBuffer uia_doc = {};
        check(init_text_buffer(&uia_doc, str_lit("alpha beta\ngamma delta")), __LINE__);
        const TableColumnDesc tbl_cols[] = {
            {.title = str_lit("Name"), .width = 110.0f},
            {.title = str_lit("Role"), .width = 110.0f},
        };
        const uint32_t btn_id = id("uia_btn");
        const uint32_t chk_id = id("uia_chk");
        const uint32_t sld_id = id("uia_sld");
        const uint32_t txt_id = id("uia_txt");
        const uint32_t pwd_id = id("uia_pwd");
        const uint32_t col_id = id("uia_col");
        const uint32_t ed_id = id("uia_ed");
        const uint32_t scr_id = id("uia_scr");
        const uint32_t tbl_id = id("uia_tbl");
        const uint32_t layout_id = id("uia_layout_box");
        const uint32_t occluded_btn_id = id("uia_occluded_btn");
        const uint32_t overlay_btn_id = id("uia_overlay_btn");
        Rect overlay_rect = {};
        bool show_overlay = false;
        float motion_probe_val = 0.0f;
        bool run_motion_probe = false;

        const auto uia_step = [&]() noexcept {
            begin_window_frame(wctx, win, 1.0f / 60.0f);
            begin_box({.id = id("uia_panel"), .axis = Axis::Row, .width = Sizing::fixed(480.0f), .height = Sizing::fixed(320.0f), .gap = 8.0f});
            // Floating overlay declared BEFORE the background button so a11y_nodes order would pick the background button,
            // proving hitbox-backed ElementFromPoint respects z-index occlusion.
            if (show_overlay && overlay_rect.w > 1.0f && overlay_rect.h > 1.0f) {
                begin_box({
                    .id = id("uia_overlay_box"),
                    .width = Sizing::fixed(overlay_rect.w),
                    .height = Sizing::fixed(overlay_rect.h),
                    .background = Color::rgba8(32u, 32u, 44u, 255u),
                    .attach_to = AttachTo::Root,
                    .anchor_element = Anchor::TopLeft,
                    .anchor_parent = Anchor::TopLeft,
                    .floating_offset = {overlay_rect.x, overlay_rect.y},
                    .z_index = 60,
                });
                button({.id = overlay_btn_id, .label = str_lit("Overlay"), .width = Sizing::fixed(overlay_rect.w), .height = Sizing::fixed(overlay_rect.h)});
                end_box();
            }
            begin_box({.axis = Axis::Col, .width = Sizing::fixed(230.0f), .height = Sizing::fixed(320.0f), .gap = 4.0f});
            if (button({.id = btn_id, .label = str_lit("Run Action")})) ++uia_btn_hits;
            checkbox({.id = chk_id, .label = str_lit("Enable Sync"), .checked = &uia_chk_val});
            slider({.id = sld_id, .value = &uia_sld_val, .min_value = 0.0f, .max_value = 100.0f, .step = 1.0f});
            text_input({.id = txt_id, .buffer = uia_txt_buf, .capacity = sizeof(uia_txt_buf)});
            text_input({.id = pwd_id, .buffer = uia_pwd_buf, .capacity = sizeof(uia_pwd_buf), .password = true});
            if (begin_collapsible({.id = col_id, .title = str_lit("Details"), .default_open = false}).visible) {
                end_collapsible();
            }
            box({.id = layout_id, .width = Sizing::fixed(24.0f), .height = Sizing::fixed(10.0f)});
            button({.id = occluded_btn_id, .label = str_lit("Behind"), .width = Sizing::fixed(120.0f), .height = Sizing::fixed(28.0f)});
            end_box();

            begin_box({.axis = Axis::Col, .width = Sizing::fixed(236.0f), .height = Sizing::fixed(320.0f), .gap = 4.0f});
            code_editor({.id = ed_id, .buffer = &uia_doc, .width = Sizing::fixed(230.0f), .height = Sizing::fixed(64.0f)});
            begin_box({.id = scr_id, .axis = Axis::Col, .width = Sizing::fixed(230.0f), .height = Sizing::fixed(72.0f), .clip_children = true, .scroll_y = true});
            for (uint32_t si = 0u; si < 8u; ++si) {
                box({.width = Sizing::fixed(200.0f), .height = Sizing::fixed(28.0f)});
            }
            end_box();
            begin_table({
                .id = tbl_id,
                .columns = tbl_cols,
                .item_count = 3u,
                .header_height_px = 24.0f,
                .row_height_px = 24.0f,
                .width = Sizing::fixed(230.0f),
                .height = Sizing::fixed(120.0f),
                .show_scrollbar = false,
            });
            uint32_t row_idx = 0u;
            while (table_next_row(&row_idx)) {
                if (table_next_column()) text("Alice");
                if (table_next_column()) text("Admin");
            }
            end_table();
            end_box();
            if (run_motion_probe) {
                motion_probe_val = spring(id("uia_motion_probe"), 100.0f, 0.0f);
            }
            end_box();
            end_window_frame(wctx);
        };

        uia_step();
        overlay_rect = element_bounds(occluded_btn_id);
        check(overlay_rect.w > 10.0f && overlay_rect.h > 10.0f, __LINE__);
        show_overlay = true;
        check(perform_a11y_action(txt_id, A11yAction::Focus, 0.0f, {}, wctx, win), __LINE__);
        uia_step();
        uia_step();
        {
            const Rect ovb = element_bounds(overlay_btn_id);
            check(std::fabs(ovb.x - overlay_rect.x) < 1.0f && std::fabs(ovb.y - overlay_rect.y) < 1.0f, __LINE__);
        }

        HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(
            SDL_GetWindowProperties(win), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
        check(hwnd != nullptr, __LINE__);

        // Temporarily make window hit-testable at 1/255 alpha (invisible yet not click-through) for ElementFromPoint.
        const LONG_PTR orig_ex = hwnd ? GetWindowLongPtrW(hwnd, GWL_EXSTYLE) : 0;
        POINT hit_pt = {};
        bool can_hit_test = false;
        if (hwnd) {
            SetWindowLongPtrW(hwnd, GWL_EXSTYLE, orig_ex | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED);
            SetLayeredWindowAttributes(hwnd, 0u, 1u, LWA_ALPHA);
            SetWindowPos(hwnd, HWND_TOPMOST, 40, 40, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            MSG m = {};
            while (PeekMessageW(&m, nullptr, 0u, 0u, PM_REMOVE)) {
                TranslateMessage(&m);
                DispatchMessageW(&m);
            }
            const auto root_nodes = frame_a11y_nodes(wctx, win);
            RECT cr = {};
            GetClientRect(hwnd, &cr);
            POINT tl = {0, 0};
            ClientToScreen(hwnd, &tl);
            const double log_w = (!root_nodes.empty() && root_nodes[0].bounds.w > 1.0f) ? static_cast<double>(root_nodes[0].bounds.w) : 480.0;
            const double log_h = (!root_nodes.empty() && root_nodes[0].bounds.h > 1.0f) ? static_cast<double>(root_nodes[0].bounds.h) : 320.0;
            const double sx = static_cast<double>(cr.right - cr.left) / log_w;
            const double sy = static_cast<double>(cr.bottom - cr.top) / log_h;
            hit_pt.x = tl.x + static_cast<LONG>(std::floor((overlay_rect.x + overlay_rect.w * 0.5f) * sx + 0.5));
            hit_pt.y = tl.y + static_cast<LONG>(std::floor((overlay_rect.y + overlay_rect.h * 0.5f) * sy + 0.5));
            can_hit_test = (WindowFromPoint(hit_pt) == hwnd);
        }

        UiaWorkerData wd = {
            .hwnd = hwnd,
            .btn_id = btn_id,
            .chk_id = chk_id,
            .sld_id = sld_id,
            .txt_id = txt_id,
            .pwd_id = pwd_id,
            .col_id = col_id,
            .ed_id = ed_id,
            .scr_id = scr_id,
            .tbl_id = tbl_id,
            .layout_id = layout_id,
            .occluded_btn_id = occluded_btn_id,
            .overlay_btn_id = overlay_btn_id,
            .hit_screen_pt = hit_pt,
            .test_hit_from_point = can_hit_test,
            .ev_phase1_done = CreateEventW(nullptr, TRUE, FALSE, nullptr),
            .ev_phase2_go = CreateEventW(nullptr, TRUE, FALSE, nullptr),
        };
        HANDLE th = (wd.ev_phase1_done && wd.ev_phase2_go && hwnd)
            ? CreateThread(nullptr, 0u, uia_worker_proc, &wd, 0u, nullptr)
            : nullptr;
        check(th != nullptr, __LINE__);
        if (th) {
            const bool phase1_waited = pump_until_event(wd.ev_phase1_done, 5000u);
            if (hwnd) {
                ShowWindow(hwnd, SW_HIDE);
                SetWindowLongPtrW(hwnd, GWL_EXSTYLE, orig_ex);
                SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            }
            check(phase1_waited, __LINE__);
            if (phase1_waited) {
                check(wd.phase1_ok, __LINE__);
                if (!wd.phase1_ok) {
                    std::fprintf(stderr, "v080 Win32 UIA worker phase1 failed at worker line %d (stage=%ld)\n", wd.fail_line, wd.stage);
                }

                uia_step();
                uia_step();
                check(uia_btn_hits == 1u, __LINE__);
                check(uia_chk_val == true, __LINE__);
                check(std::fabs(uia_sld_val - 75.0f) < 0.05f, __LINE__);
                check(std::strcmp(uia_txt_buf, "updated_\xE4\xB8\xAD\xE6\x96\x87") == 0, __LINE__);
                check(is_collapsible_open(col_id), __LINE__);
                check(scroll_offset(scr_id).y > 40.0f, __LINE__);

                // Verify live WM_THEMECHANGED / WM_SETTINGCHANGE handling on the Win32 subclass.
                for (int si = 0; si < 4; ++si) {
                    begin_window_frame(wctx, win, 0.0f);
                    box({.width = Sizing::fixed(100.0f), .height = Sizing::fixed(40.0f)});
                    end_window_frame(wctx);
                }
                check(!needs_window_redraw(wctx, win), __LINE__);
                SendMessageW(hwnd, WM_THEMECHANGED, 0u, 0);
                check(needs_window_redraw(wctx, win), __LINE__);
                SendMessageW(hwnd, WM_SETTINGCHANGE, SPI_SETCLIENTAREAANIMATION, 0);
                BOOL anim_enabled = TRUE;
                if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0u, &anim_enabled, 0u)) {
                    run_motion_probe = true;
                    uia_step();
                    run_motion_probe = false;
                    if (anim_enabled == FALSE) {
                        check(std::fabs(motion_probe_val - 100.0f) < 0.05f, __LINE__);
                    } else {
                        check(motion_probe_val < 99.0f, __LINE__);
                    }
                }

                set_os_a11y_enabled(wctx, false);
                check(!os_a11y_enabled(wctx) && !os_a11y_active(wctx, win), __LINE__);
                set_os_a11y_enabled(wctx, true);
                check(os_a11y_enabled(wctx) && os_a11y_active(wctx, win), __LINE__);
            }

            release_window(wctx, win);
            check(!os_a11y_active(wctx, win), __LINE__);

            SetEvent(wd.ev_phase2_go);
            const bool worker_done = pump_until_event(th, 5000u);
            check(worker_done, __LINE__);
            if (!worker_done) {
                // Drain until the worker exits so stack-local `wd` and event handles stay valid.
                pump_until_event(th, INFINITE);
            }
            if (phase1_waited && worker_done) {
                check(wd.phase2_ok, __LINE__);
                if (!wd.phase2_ok) {
                    std::fprintf(stderr, "v080 Win32 UIA worker phase2 failed (fail_line=%d, stage=%ld, hr=0x%08lX)\n",
                        wd.fail_line, wd.stage, static_cast<unsigned long>(wd.stale_hr));
                }
            }
            CloseHandle(th);
        }
        if (wd.ev_phase1_done) CloseHandle(wd.ev_phase1_done);
        if (wd.ev_phase2_go) CloseHandle(wd.ev_phase2_go);
        free_text_buffer(&uia_doc);
    }
#else
    {
        const bool was_enabled = os_a11y_enabled(wctx);
        const bool was_active = os_a11y_active(wctx, win);
        set_os_a11y_enabled(wctx, false);
        check(!os_a11y_enabled(wctx) && !os_a11y_active(wctx, win), __LINE__);
        set_os_a11y_enabled(wctx, was_enabled);
        check(os_a11y_enabled(wctx) == was_enabled && os_a11y_active(wctx, win) == was_active, __LINE__);
    }
#endif

    destroy_context(wctx);
    SDL_DestroyWindow(win);
    set_current_context(previous);
    return ok;
}
