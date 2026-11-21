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
    if(local.translate.x!=0.0f || local.translate.y!=0.0f || local.scale.x!=1.0f || local.scale.y!=1.0f) {
        // Disable layout-space culling when a visual layer can move or scale content into view.
        if (ctx->active_ui->layout) ctx->active_ui->layout->culling_enabled = false;
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
    float parent_opacity=1, parent_blur=0; bool parent_animating=false, parent_plus_lighter=false;
    const VisualLayerNode* parent_blur_root = nullptr;
    if(node->parent) {
        resolve_layer_chain(ctx,node->parent,dpi);
        parent_scale=node->parent->resolved_scale;parent_offset=node->parent->resolved_offset_px;
        parent_opacity=node->parent->resolved_opacity;parent_animating=node->parent->resolved_animating;
        parent_blur=node->parent->resolved_blur_px;parent_plus_lighter=node->parent->resolved_plus_lighter;
        parent_blur_root=node->parent->resolved_blur_root;
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
                    if(!ws->anim_seen_frame) ws->anim_rect=target;
                    if(ws->anim_seen_frame!=ctx->active_ui->frame_index) {
                        moving |= advance_motion(ctx,&ws->anim_rect.x,&ws->anim_vel_pos.x,target.x,spec);
                        moving |= advance_motion(ctx,&ws->anim_rect.y,&ws->anim_vel_pos.y,target.y,spec);
                        moving |= advance_motion(ctx,&ws->anim_rect.w,&ws->anim_vel_size.x,target.w,spec);
                        moving |= advance_motion(ctx,&ws->anim_rect.h,&ws->anim_vel_size.y,target.h,spec);
                        ws->anim_seen_frame=ctx->active_ui->frame_index;
                    }
                    Rect animated=ws->anim_rect;
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
    node->resolved_opacity=parent_opacity*d.opacity;
    node->resolved_animating=parent_animating || d.animating;
    const float local_blur = d.blur_px > 0.05f ? d.blur_px * dpi : 0.0f;
    const float combined_blur = (parent_blur > 0.0f && local_blur > 0.0f)
        ? std::sqrt(parent_blur * parent_blur + local_blur * local_blur)
        : (parent_blur + local_blur);
    node->resolved_blur_px = std::fmin(64.0f, combined_blur);
    node->resolved_blur_root = parent_blur_root ? parent_blur_root : (d.blur_px > 0.05f ? node : nullptr);
    node->resolved_plus_lighter = parent_plus_lighter || d.blend_plus_lighter;
    node->resolved_frame=ctx->active_ui->frame_index;
}
Rect layer_rect(Context* ctx, Rect r) noexcept {
    if(const auto* layer=ctx->active_render_layer) {
        resolve_layer_chain(ctx,layer,ctx->active_ui->dpi_scale);
        const auto s=layer->resolved_scale,o=layer->resolved_offset_px;
        return {r.x*s.x+o.x,r.y*s.y+o.y,r.w*s.x,r.h*s.y};
    }
    return r;
}

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
    if(!ws) return {};
    if(!ws->presence_initialized) {
        ws->presence_initialized=true;
        ws->presence_pos=0;
        ws->presence_vel=0;
    }
    if(ws->presence_frame!=ctx->active_ui->frame_index) {
        ws->presence_frame=ctx->active_ui->frame_index;
        advance_motion(ctx,&ws->presence_pos,&ws->presence_vel,mounted ? 1.f : 0.f,spec);
    }
    const float opacity=std::fmin(1.f,std::fmax(0.f,ws->presence_pos));
    return {mounted || ws->presence_pos!=0 || ws->presence_vel!=0,opacity,!mounted && (ws->presence_pos!=0 || ws->presence_vel!=0)};
}
} // namespace etch
