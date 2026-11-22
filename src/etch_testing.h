#pragma once

#include "etch/etch.h"

namespace etch {

bool verify_text_undo_ring() noexcept;
bool verify_caret_deadline() noexcept;
bool verify_arena_exhausted_glyph_pipeline_and_caches() noexcept;

void inject_drop_file(Context* ctx, Str utf8_path, SDL_Window* window = nullptr) noexcept;
void inject_drop_text(Context* ctx, Str utf8_text, SDL_Window* window = nullptr) noexcept;
void inject_os_drop_begin(Context* ctx, Vec2 logical_pos, SDL_Window* window = nullptr) noexcept;
void inject_os_drop_move(Context* ctx, Vec2 logical_pos, SDL_Window* window = nullptr) noexcept;
void inject_os_drop_complete(Context* ctx, Vec2 logical_pos, SDL_Window* window = nullptr) noexcept;
void inject_file_dialog_result(Context* ctx, uint32_t request_tag, std::span<const Str> paths, bool cancelled = false, int32_t selected_filter = -1) noexcept;
void rebuild_gpu_batches_for_test(Context* ctx) noexcept;

} // namespace etch
