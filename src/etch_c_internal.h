// Private internal C++ bridge declarations shared between etch_unity.cpp and etch_c.cpp.
#ifndef ETCH_C_INTERNAL_H
#define ETCH_C_INTERNAL_H

#include "etch/etch.h"
#include "etch/etch_c.h"

namespace etch {

using CTokenEmitFn = void (*)(void*, uint32_t, uint32_t, uint32_t, Color) noexcept;
using CTerminalCellReadFn = TerminalCell (*)(const void*, size_t) noexcept;
using TextEditReadFn = TextEdit (*)(const void*, size_t) noexcept;
bool apply_text_edits(TextBuffer*, uint64_t, const void*, size_t, TextEditReadFn) noexcept;

bool c_bridge_can_draw_after_layout(const Context* ctx) noexcept;
void c_bridge_set_window_refresh(Context* ctx, EtchWindowRefreshFn fn, void* user_data) noexcept;
void c_bridge_set_gpu_device_reset(Context* ctx, EtchGpuDeviceResetFn fn, void* user_data) noexcept;
uint32_t c_bridge_tokenize_cpp_hlsl(Str line, void* out, uint32_t cap, CTokenEmitFn emit) noexcept;
uint32_t c_bridge_tokenize_language(Str language, Str line, void* out, uint32_t cap, CTokenEmitFn emit) noexcept;
uint32_t c_bridge_tokenize_language_stateful(Str language, Str line, uint8_t in_state, uint8_t* out_state, void* out, uint32_t cap, CTokenEmitFn emit) noexcept;
TerminalGridResult c_bridge_terminal_grid(const TerminalGridDesc& desc, const void* cells, size_t count, CTerminalCellReadFn read) noexcept;
VirtualListSlice begin_virtual_list_with_c_measure(const VirtualListDesc& desc, EtchVirtualItemHeightFn c_measure_fn) noexcept;

} // namespace etch

#endif // ETCH_C_INTERNAL_H
