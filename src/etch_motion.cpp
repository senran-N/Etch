// Analytic spring stepping and presence state; compiled via etch_unity.cpp.
namespace etch {
namespace {
bool step_spring_scalar(float* pos, float* vel, float target, float dt, SpringSpec spec) noexcept {
    const float x0 = *pos - target;
    const float v0 = *vel;
    if (std::fabs(x0) <= 0.0005f && std::fabs(v0) <= 0.005f) {
        *pos = target;
        *vel = 0.0f;
        return false;
    }

    if (dt <= 0.0f) {
        return true;
    }

    const float response = spec.response;
    const float zeta = spec.damping_ratio;
    const float omega = 6.28318530718f / response;

    float x1 = 0.0f;
    float v1 = 0.0f;
    if (std::fabs(zeta - 1.0f) < 1e-3f) {
        const float e = std::exp(-omega * dt);
        const float c1 = x0;
        const float c2 = v0 + omega * x0;
        const float linear = c1 + c2 * dt;
        x1 = linear * e;
        v1 = (c2 - omega * linear) * e;
    } else if (zeta < 1.0f) {
        const float omega_d = omega * std::sqrt(1.0f - zeta * zeta);
        const float e = std::exp(-zeta * omega * dt);
        const float c1 = x0;
        const float c2 = (v0 + zeta * omega * x0) / omega_d;
        const float s = std::sin(omega_d * dt);
        const float c = std::cos(omega_d * dt);
        x1 = e * (c1 * c + c2 * s);
        v1 = e * ((c2 * omega_d - c1 * zeta * omega) * c - (c1 * omega_d + c2 * zeta * omega) * s);
    } else {
        const float sq = std::sqrt(zeta * zeta - 1.0f);
        const float r1 = -omega * (zeta - sq);
        const float r2 = -omega * (zeta + sq);
        const float e1 = std::exp(r1 * dt);
        const float e2 = std::exp(r2 * dt);
        const float c2 = (v0 - r1 * x0) / (r2 - r1);
        const float c1 = x0 - c2;
        x1 = c1 * e1 + c2 * e2;
        v1 = c1 * r1 * e1 + c2 * r2 * e2;
    }

    if (std::fabs(x1) <= 0.0005f && std::fabs(v1) <= 0.005f) {
        *pos = target;
        *vel = 0.0f;
        return false;
    }

    *pos = target + x1;
    *vel = v1;
    return true;
}
SpringSpec valid_spring_spec(SpringSpec spec) noexcept {
    return {std::isfinite(spec.response) ? std::fmax(.01f,spec.response) : .2f,
            std::isfinite(spec.damping_ratio) ? std::fmax(.05f,spec.damping_ratio) : 1.f};
}
bool advance_motion(Context* ctx, float* pos, float* vel, float target, SpringSpec spec) noexcept {
    if(ctx->reduced_motion) { *pos=target; *vel=0; return false; }
    const bool moving=step_spring_scalar(pos,vel,target,ctx->active_ui->sim_dt,spec);
    if(moving && ctx->time_scale>0) ++ctx->active_ui->active_anim_count;
    return moving;
}
}
void push_layer(const LayerDesc& desc) noexcept {
    Context* ctx=current_context();
    if(!ctx || !ctx->in_frame) return;
    LayerDesc local=desc;
    if(!std::isfinite(local.opacity) || !std::isfinite(local.translate.x) || !std::isfinite(local.translate.y) ||
       !std::isfinite(local.scale.x) || !std::isfinite(local.scale.y) || local.scale.x<=0 || local.scale.y<=0 ||
       !std::isfinite(local.pivot_norm.x) || !std::isfinite(local.pivot_norm.y)) local={};
    local.opacity=std::fmin(1.f,std::fmax(0.f,local.opacity));
    local.blur_px=std::isfinite(local.blur_px) ? std::fmin(64.f,std::fmax(0.f,local.blur_px)) : 0.f;
    local.rotation_rad=std::isfinite(local.rotation_rad) ? local.rotation_rad : 0.f;
    if(static_cast<uint8_t>(local.blend_mode)>static_cast<uint8_t>(BlendMode::Screen)) local.blend_mode=BlendMode::Normal;
    if(local.blend_mode==BlendMode::Normal && local.blend_plus_lighter) local.blend_mode=BlendMode::Additive;
    if(static_cast<uint8_t>(local.alpha_mask.kind)>static_cast<uint8_t>(AlphaMaskKind::Texture)) local.alpha_mask.kind=AlphaMaskKind::None;
    if(local.alpha_mask.kind==AlphaMaskKind::Texture && !local.alpha_mask.texture) local.alpha_mask.kind=AlphaMaskKind::None;
    if(local.alpha_mask.kind!=AlphaMaskKind::None) {
        auto& m = local.alpha_mask;
        m.angle_deg = std::isfinite(m.angle_deg) ? m.angle_deg : 90.0f;
        m.start_t = std::isfinite(m.start_t) ? std::fmin(1.0f, std::fmax(0.0f, m.start_t)) : 0.0f;
        m.end_t = std::isfinite(m.end_t) ? std::fmin(1.0f, std::fmax(0.0f, m.end_t)) : 1.0f;
        m.alpha_start = std::isfinite(m.alpha_start) ? std::fmin(1.0f, std::fmax(0.0f, m.alpha_start)) : 1.0f;
        m.alpha_end = std::isfinite(m.alpha_end) ? std::fmin(1.0f, std::fmax(0.0f, m.alpha_end)) : 0.0f;
        if(!std::isfinite(m.center_norm.x)) m.center_norm.x = 0.5f;
        if(!std::isfinite(m.center_norm.y)) m.center_norm.y = 0.5f;
        if(!std::isfinite(m.radius_norm.x) || m.radius_norm.x <= 0.0f) m.radius_norm.x = 0.5f;
        if(!std::isfinite(m.radius_norm.y) || m.radius_norm.y <= 0.0f) m.radius_norm.y = 0.5f;
        if(!std::isfinite(m.uv.x) || !std::isfinite(m.uv.y) || !std::isfinite(m.uv.w) || !std::isfinite(m.uv.h)) m.uv = {0.0f, 0.0f, 1.0f, 1.0f};
        m.radius.tl = (std::isfinite(m.radius.tl) && m.radius.tl > 0.0f) ? m.radius.tl : 0.0f;
        m.radius.tr = (std::isfinite(m.radius.tr) && m.radius.tr > 0.0f) ? m.radius.tr : 0.0f;
        m.radius.br = (std::isfinite(m.radius.br) && m.radius.br > 0.0f) ? m.radius.br : 0.0f;
        m.radius.bl = (std::isfinite(m.radius.bl) && m.radius.bl > 0.0f) ? m.radius.bl : 0.0f;
    }
    if(local.translate.x!=0.0f || local.translate.y!=0.0f || local.scale.x!=1.0f || local.scale.y!=1.0f || local.rotation_rad!=0.0f) {
        // Disable layout-space culling when a visual layer can move, scale, or rotate content into view.
        if (ctx->active_ui->layout) ctx->active_ui->layout->culling_enabled = false;
        if (ctx->active_ui->subtree_depth > 0u) {
            if (SubtreeMeta* sm = ctx->active_ui->subtree_stack[ctx->active_ui->subtree_depth - 1u].meta) {
                sm->tainted = true;
            }
        }
    } else if(local.blend_mode!=BlendMode::Normal || local.alpha_mask.kind!=AlphaMaskKind::None) {
        if (ctx->active_ui->subtree_depth > 0u) {
            if (SubtreeMeta* sm = ctx->active_ui->subtree_stack[ctx->active_ui->subtree_depth - 1u].meta) {
                sm->tainted = true;
            }
        }
    }
    auto* node=ctx->frame_arena.alloc<VisualLayerNode>();
    if(!node) return;
    *node={}; node->parent=ctx->active_ui->current_layer; node->local=local;
    ctx->active_ui->current_layer=node;
    if(local.animating && ctx->time_scale>0) ++ctx->active_ui->active_anim_count;
}
void pop_layer() noexcept {
    Context* ctx=current_context();
    if(!ctx || !ctx->in_frame) return;
    auto& layer=ctx->active_ui->current_layer;
    layer=layer ? layer->parent : nullptr;
}
namespace {
void resolve_layer_chain(Context* ctx, const VisualLayerNode* node, float dpi) noexcept {
    if(node->resolved_frame==ctx->active_ui->frame_index) return;
    Vec2 parent_scale={1,1},parent_offset={}, inherited_scale={1,1},inherited_offset={};
    float parent_opacity=1, parent_blur=0, parent_rot=0; bool parent_animating=false, parent_has_mask=false;
    BlendMode parent_blend=BlendMode::Normal;
    float pm[6] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    const VisualLayerNode* parent_blur_root = nullptr;
    if(node->parent) {
        resolve_layer_chain(ctx,node->parent,dpi);
        parent_scale=node->parent->resolved_scale;parent_offset=node->parent->resolved_offset_px;
        parent_opacity=node->parent->resolved_opacity;parent_animating=node->parent->resolved_animating;
        parent_blur=node->parent->resolved_blur_px;parent_blend=node->parent->resolved_blend_mode;
        parent_has_mask=node->parent->resolved_has_alpha_mask;
        parent_blur_root=node->parent->resolved_blur_root;
        parent_rot=node->parent->resolved_rotation_rad;
        std::memcpy(pm,node->parent->resolved_m,sizeof(pm));
        inherited_scale=node->parent->inherited_scale;inherited_offset=node->parent->inherited_offset_px;
    }
    LayerDesc d=node->local;
    LayerDesc inherited=d;
    if(node->source_box) {
        const auto& box=*node->source_box;
        float hover=0,press=0; bool interaction_moving=false;
        if(const auto* own=state_map_find(&ctx->active_ui->states,box.desc.id)) {
            hover=own->hover_t;press=own->press_t;
            interaction_moving=own->hover_vel!=0 || own->press_vel!=0;
        }
        const uint32_t key=box.desc.hero_id ? box.desc.hero_id : box.desc.id;
        if(key) {
            auto* ws=state_map_get_or_insert(&ctx->active_ui->states,key,ctx->active_ui->frame_index);
            if(ws) {
                const auto spec=box.desc.transition;
                const Rect r=box.layout_rect_logical;
                Rect target=r;
                const BoxExtra* animated_parent=nullptr;
                if(!box.desc.hero_id) {
                    for(auto* parent=node->parent;parent;parent=parent->parent) {
                        if(parent->source_box && (parent->source_box->desc.animate_layout || parent->source_box->desc.hero_id)) {
                            animated_parent=parent->source_box;break;
                        }
                    }
                }
                // Animate descendants in their parent's layout coordinates so inherited
                // movement and resizing are applied once by the layer composition.
                if(animated_parent) {
                    const Rect p=animated_parent->layout_rect_logical;
                    if(p.w>0 && p.h>0) target={(r.x-p.x)/p.w,(r.y-p.y)/p.h,r.w/p.w,r.h/p.h};
                    else animated_parent=nullptr;
                }
                bool moving=false;
                if(box.desc.animate_layout || box.desc.hero_id) {
                    WidgetCold* cold=ensure_widget_cold(ws);
                    if(cold) {
                        if(!cold->anim_seen_frame) cold->anim_rect=target;
                        if(cold->anim_seen_frame!=ctx->active_ui->frame_index) {
                            moving |= advance_motion(ctx,&cold->anim_rect.x,&cold->anim_vel_pos.x,target.x,spec);
                            moving |= advance_motion(ctx,&cold->anim_rect.y,&cold->anim_vel_pos.y,target.y,spec);
                            moving |= advance_motion(ctx,&cold->anim_rect.w,&cold->anim_vel_size.x,target.w,spec);
                            moving |= advance_motion(ctx,&cold->anim_rect.h,&cold->anim_vel_size.y,target.h,spec);
                            cold->anim_seen_frame=ctx->active_ui->frame_index;
                        }
                        Rect animated=cold->anim_rect;
                        if(animated_parent) {
                            const Rect p=animated_parent->layout_rect_logical;
                            animated={p.x+animated.x*p.w,p.y+animated.y*p.h,animated.w*p.w,animated.h*p.h};
                        }
                        // Shared heroes retain absolute coordinates across parent changes.
                        // Remove the inherited transform before composing that absolute spring.
                        if(box.desc.hero_id && node->parent) {
                            animated={(animated.x*inherited_scale.x+(inherited_offset.x-parent_offset.x)/dpi)/parent_scale.x,
                                      (animated.y*inherited_scale.y+(inherited_offset.y-parent_offset.y)/dpi)/parent_scale.y,
                                      animated.w*inherited_scale.x/parent_scale.x,animated.h*inherited_scale.y/parent_scale.y};
                        }
                        if(r.w>0 && r.h>0) {
                            d.scale={animated.w/r.w,animated.h/r.h};
                            d.pivot_px={r.x,r.y};
                            d.translate={animated.x-r.x,animated.y-r.y};
                        }
                    }
                }
                const float interaction_scale=(1+(box.desc.hover_scale-1)*hover)*(1+(box.desc.press_scale-1)*press);
                d.scale={d.scale.x*interaction_scale,d.scale.y*interaction_scale};
                inherited.scale={inherited.scale.x*interaction_scale,inherited.scale.y*interaction_scale};
                d.animating |= moving || interaction_moving;
            }
        }
    }

    Vec2 pivot={0,0};
    if(node->source_box) {
        const Rect r=node->source_box->layout_rect_logical;
        pivot={(r.x+r.w*d.pivot_norm.x)*dpi,(r.y+r.h*d.pivot_norm.y)*dpi};
    } else if(node->pivot_box) {
        const Rect r=node->pivot_box->layout_rect_logical;
        pivot={(r.x+r.w*d.pivot_norm.x)*dpi,(r.y+r.h*d.pivot_norm.y)*dpi};
    }
    Vec2 inherited_pivot=pivot;
    if(std::isfinite(inherited.pivot_px.x) && std::isfinite(inherited.pivot_px.y)) inherited_pivot={inherited.pivot_px.x*dpi,inherited.pivot_px.y*dpi};
    const Vec2 inherited_local_offset={inherited.translate.x*dpi+inherited_pivot.x*(1-inherited.scale.x),inherited.translate.y*dpi+inherited_pivot.y*(1-inherited.scale.y)};
    node->inherited_scale={inherited_scale.x*inherited.scale.x,inherited_scale.y*inherited.scale.y};
    node->inherited_offset_px={inherited_offset.x+inherited_scale.x*inherited_local_offset.x,inherited_offset.y+inherited_scale.y*inherited_local_offset.y};
    if(std::isfinite(d.pivot_px.x) && std::isfinite(d.pivot_px.y)) pivot={d.pivot_px.x*dpi,d.pivot_px.y*dpi};
    const Vec2 offset={d.translate.x*dpi+pivot.x*(1-d.scale.x),d.translate.y*dpi+pivot.y*(1-d.scale.y)};
    node->resolved_scale={parent_scale.x*d.scale.x,parent_scale.y*d.scale.y};
    node->resolved_offset_px={parent_offset.x+parent_scale.x*offset.x,parent_offset.y+parent_scale.y*offset.y};
    if(parent_rot == 0.0f && d.rotation_rad == 0.0f) {
        node->resolved_rotation_rad = 0.0f;
        node->resolved_m[0] = node->resolved_scale.x;
        node->resolved_m[1] = 0.0f;
        node->resolved_m[2] = node->resolved_offset_px.x;
        node->resolved_m[3] = 0.0f;
        node->resolved_m[4] = node->resolved_scale.y;
        node->resolved_m[5] = node->resolved_offset_px.y;
    } else {
        const float local_rot = d.rotation_rad;
        const float c = std::cos(local_rot);
        const float s = std::sin(local_rot);
        const float la = c * d.scale.x;
        const float lb = -s * d.scale.y;
        const float ltx = pivot.x + d.translate.x * dpi - (la * pivot.x + lb * pivot.y);
        const float lc = s * d.scale.x;
        const float ld = c * d.scale.y;
        const float lty = pivot.y + d.translate.y * dpi - (lc * pivot.x + ld * pivot.y);
        node->resolved_m[0] = pm[0] * la + pm[1] * lc;
        node->resolved_m[1] = pm[0] * lb + pm[1] * ld;
        node->resolved_m[2] = pm[0] * ltx + pm[1] * lty + pm[2];
        node->resolved_m[3] = pm[3] * la + pm[4] * lc;
        node->resolved_m[4] = pm[3] * lb + pm[4] * ld;
        node->resolved_m[5] = pm[3] * ltx + pm[4] * lty + pm[5];
        const float* m = node->resolved_m;
        node->resolved_scale = { std::hypot(m[0], m[3]), std::hypot(m[1], m[4]) };
        float rot = std::atan2(m[3], m[0]);
        if (std::fabs(rot) <= 1e-4f) {
            rot = 0.0f;
            node->resolved_offset_px = { m[2], m[5] };
        }
        node->resolved_rotation_rad = rot;
    }
    node->resolved_opacity=parent_opacity*d.opacity;
    node->resolved_animating=parent_animating || d.animating;
    const float local_blur = d.blur_px > 0.05f ? d.blur_px * dpi : 0.0f;
    const float combined_blur = (parent_blur > 0.0f && local_blur > 0.0f)
        ? std::sqrt(parent_blur * parent_blur + local_blur * local_blur)
        : (parent_blur + local_blur);
    node->resolved_blur_px = std::fmin(64.0f, combined_blur);
    node->resolved_blur_root = parent_blur_root ? parent_blur_root : (d.blur_px > 0.05f ? node : nullptr);
    const BlendMode local_blend = (d.blend_mode != BlendMode::Normal)
        ? d.blend_mode
        : (d.blend_plus_lighter ? BlendMode::Additive : BlendMode::Normal);
    node->resolved_blend_mode = (local_blend != BlendMode::Normal) ? local_blend : parent_blend;
    node->resolved_has_alpha_mask = parent_has_mask || (d.alpha_mask.kind != AlphaMaskKind::None);
    node->cached_mask_clip_frame = 0u;
    node->cached_mask_clip_parent = UINT32_MAX;
    node->cached_mask_clip_idx = 0u;
    node->resolved_frame=ctx->active_ui->frame_index;
}
Rect layer_rect(Context* ctx, Rect r) noexcept {
    if(const auto* layer=ctx->active_render_layer) {
        resolve_layer_chain(ctx,layer,ctx->active_ui->dpi_scale);
        const auto s=layer->resolved_scale;
        if(std::fabs(layer->resolved_rotation_rad)>1e-4f) {
            const float rcx=r.x+r.w*0.5f;
            const float rcy=r.y+r.h*0.5f;
            const float* m=layer->resolved_m;
            const float cx=m[0]*rcx+m[1]*rcy+m[2];
            const float cy=m[3]*rcx+m[4]*rcy+m[5];
            const float vw=r.w*std::fabs(s.x);
            const float vh=r.h*std::fabs(s.y);
            return {cx-vw*0.5f,cy-vh*0.5f,vw,vh};
        }
        const auto o=layer->resolved_offset_px;
        return {r.x*s.x+o.x,r.y*s.y+o.y,r.w*s.x,r.h*s.y};
    }
    return r;
}

}
float eval_easing(EasingSpec spec, float t) noexcept {
    if (!std::isfinite(t) || t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    float x1 = 0.0f, y1 = 0.0f, x2 = 1.0f, y2 = 1.0f;
    switch (spec.kind) {
        case EasingKind::Linear:
            return t;
        case EasingKind::EaseIn:
            x1 = 0.42f; y1 = 0.0f; x2 = 1.0f; y2 = 1.0f;
            break;
        case EasingKind::EaseOut:
            x1 = 0.0f; y1 = 0.0f; x2 = 0.58f; y2 = 1.0f;
            break;
        case EasingKind::EaseInOut:
            x1 = 0.42f; y1 = 0.0f; x2 = 0.58f; y2 = 1.0f;
            break;
        case EasingKind::CubicBezier:
            x1 = std::isfinite(spec.x1) ? std::fmin(1.0f, std::fmax(0.0f, spec.x1)) : 0.0f;
            y1 = std::isfinite(spec.y1) ? spec.y1 : 0.0f;
            x2 = std::isfinite(spec.x2) ? std::fmin(1.0f, std::fmax(0.0f, spec.x2)) : 1.0f;
            y2 = std::isfinite(spec.y2) ? spec.y2 : 1.0f;
            break;
        default:
            return t;
    }
    if (x1 == y1 && x2 == y2) return t;

    const float cx = 3.0f * x1;
    const float bx = 3.0f * (x2 - x1) - cx;
    const float ax = 1.0f - cx - bx;
    const float cy = 3.0f * y1;
    const float by = 3.0f * (y2 - y1) - cy;
    const float ay = 1.0f - cy - by;

    float lo = 0.0f;
    float hi = 1.0f;
    float u = t;
    bool converged = false;
    for (int i = 0; i < 6; ++i) {
        const float x = ((ax * u + bx) * u + cx) * u;
        const float err = x - t;
        const float dx = (3.0f * ax * u + 2.0f * bx) * u + cx;
        if (std::fabs(err) < 1e-6f && std::fabs(err) <= 1e-6f * std::fabs(dx)) {
            converged = true;
            break;
        }
        if (err < 0.0f) lo = u;
        else hi = u;
        if (std::fabs(dx) > 1e-6f) {
            const float next = u - err / dx;
            if (next > lo && next < hi) {
                u = next;
                continue;
            }
        }
        u = 0.5f * (lo + hi);
    }
    if (!converged && (hi - lo) > 1e-6f) {
        const float x0 = ((ax * u + bx) * u + cx) * u;
        const float err0 = x0 - t;
        const float dx0 = (3.0f * ax * u + 2.0f * bx) * u + cx;
        if (!(std::fabs(err0) < 1e-6f && std::fabs(err0) <= 1e-6f * std::fabs(dx0))) {
            if (err0 < 0.0f) lo = u;
            else hi = u;
            for (int i = 0; i < 20 && (hi - lo) > 1e-6f; ++i) {
                u = 0.5f * (lo + hi);
                const float x = ((ax * u + bx) * u + cx) * u;
                const float err = x - t;
                if (err == 0.0f) break;
                if (err < 0.0f) lo = u;
                else hi = u;
            }
            u = 0.5f * (lo + hi);
        }
    }
    return ((ay * u + by) * u + cy) * u;
}

float stagger_delay(uint32_t index, float step_sec, float max_delay_sec) noexcept {
    const float step = (std::isfinite(step_sec) && step_sec > 0.0f) ? step_sec : 0.0f;
    const float delay = static_cast<float>(index) * step;
    if (!std::isfinite(delay)) {
        return (std::isfinite(max_delay_sec) && max_delay_sec > 0.0f) ? max_delay_sec : 0.0f;
    }
    if (std::isfinite(max_delay_sec) && max_delay_sec > 0.0f) {
        return std::fmin(delay, max_delay_sec);
    }
    return delay;
}

float tween(uint32_t id, float target, float duration_sec, EasingSpec spec, float delay_sec) noexcept {
    if (!std::isfinite(target)) return 0.0f;
    Context* ctx = current_context();
    if (!ctx || !ctx->active_ui || id == 0u) return target;
    const float dur = (std::isfinite(duration_sec) && duration_sec > 0.0f) ? duration_sec : 0.0f;
    const float del = (std::isfinite(delay_sec) && delay_sec > 0.0f) ? delay_sec : 0.0f;
    const uint32_t frame = ctx->active_ui->frame_index;
    WidgetState* ws = state_map_get_or_insert(&ctx->active_ui->states, id, frame);
    WidgetCold* c = ensure_widget_cold(ws);
    if (!c) return target;

    if (!c->tween_initialized) {
        c->tween_initialized = true;
        c->tween_start = target;
        c->tween_target = target;
        c->tween_value = target;
        c->tween_elapsed = 0.0f;
        c->tween_duration = 0.0f;
        c->tween_delay = 0.0f;
        c->tween_frame = frame;
        return c->tween_value;
    }

    if (ctx->reduced_motion || dur <= 1e-4f) {
        c->tween_start = target;
        c->tween_target = target;
        c->tween_value = target;
        c->tween_elapsed = 0.0f;
        c->tween_duration = 0.0f;
        c->tween_delay = 0.0f;
        c->tween_frame = frame;
        return c->tween_value;
    }

    const bool target_changed = std::fabs(target - c->tween_target) > 1e-5f;
    if (target_changed) {
        c->tween_start = c->tween_value;
        c->tween_target = target;
        c->tween_elapsed = 0.0f;
        c->tween_duration = dur;
        c->tween_delay = del;
        if (c->tween_frame == frame && ctx->time_scale > 0.0f) {
            ++ctx->active_ui->active_anim_count;
        }
    }

    if (c->tween_duration > 1e-4f && c->tween_frame != frame) {
        c->tween_frame = frame;
        c->tween_elapsed += ctx->active_ui->sim_dt;
        const float total = c->tween_delay + c->tween_duration;
        if (c->tween_elapsed >= total) {
            c->tween_value = c->tween_target;
            c->tween_start = c->tween_target;
            c->tween_elapsed = total;
            c->tween_duration = 0.0f;
        } else {
            const float raw_u = (c->tween_elapsed - c->tween_delay) / c->tween_duration;
            const float u = std::fmin(1.0f, std::fmax(0.0f, raw_u));
            c->tween_value = c->tween_start + (c->tween_target - c->tween_start) * eval_easing(spec, u);
            if (ctx->time_scale > 0.0f) {
                ++ctx->active_ui->active_anim_count;
            }
        }
    }
    return c->tween_value;
}
void set_time_scale(Context* ctx, float scale) noexcept {
    if(!ctx || !std::isfinite(scale) || scale<0 || scale>16) return;
    if(ctx->time_scale!=scale) {ctx->time_scale=scale;request_redraw(ctx);}
}
PresenceResult presence(uint32_t id, bool mounted, SpringSpec spec) noexcept {
    Context* ctx=current_context();
    if(!ctx || !ctx->in_frame || !id) return {};
    spec=valid_spring_spec(spec);
    auto* ws=state_map_get_or_insert(&ctx->active_ui->states,id,ctx->active_ui->frame_index);
    WidgetCold* c=ensure_widget_cold(ws);
    if(!c) return {};
    if(!c->presence_initialized) {
        c->presence_initialized=true;
        c->presence_pos=0;
        c->presence_vel=0;
    }
    if(c->presence_frame!=ctx->active_ui->frame_index) {
        c->presence_frame=ctx->active_ui->frame_index;
        advance_motion(ctx,&c->presence_pos,&c->presence_vel,mounted ? 1.f : 0.f,spec);
    }
    const float opacity=std::fmin(1.f,std::fmax(0.f,c->presence_pos));
    return {mounted || c->presence_pos!=0 || c->presence_vel!=0,opacity,!mounted && (c->presence_pos!=0 || c->presence_vel!=0)};
}
} // namespace etch
