#pragma once

#include "etch/etch.h"

namespace etch {

bool verify_text_undo_ring() noexcept;
bool verify_caret_deadline() noexcept;

void inject_drop_file(Context* ctx, Str utf8_path, SDL_Window* window = nullptr) noexcept;
void inject_drop_text(Context* ctx, Str utf8_text, SDL_Window* window = nullptr) noexcept;
void inject_os_drop_begin(Context* ctx, Vec2 logical_pos, SDL_Window* window = nullptr) noexcept;
void inject_os_drop_move(Context* ctx, Vec2 logical_pos, SDL_Window* window = nullptr) noexcept;
void inject_os_drop_complete(Context* ctx, Vec2 logical_pos, SDL_Window* window = nullptr) noexcept;

} // namespace etch
