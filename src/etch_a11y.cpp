// Per-window immutable snapshots; transient builders and overrides use frame storage.
namespace etch {
namespace {
A11yIndexEntry* a11y_index(Context* ctx, uint32_t id, bool insert) noexcept {
    if (insert && (ctx->a11y_index_count + 1) * 2 >= ctx->a11y_index_cap) {
        const uint32_t cap = ctx->a11y_index_cap ? ctx->a11y_index_cap * 2 : 256;
        auto* entries = ctx->frame_arena.alloc<A11yIndexEntry>(cap);
        if (!entries) return nullptr;
        for (uint32_t i=0;i<cap;++i) entries[i]={};
        for (uint32_t i=0;i<ctx->a11y_index_cap;++i) if (ctx->a11y_index[i].id) {
            uint32_t slot = (ctx->a11y_index[i].id * 2654435761u) & (cap-1);
            while (entries[slot].id) slot=(slot+1)&(cap-1);
            entries[slot]=ctx->a11y_index[i];
        }
        ctx->a11y_index=entries;ctx->a11y_index_cap=cap;
    }
    if (!ctx->a11y_index_cap) return nullptr;
    uint32_t slot=(id*2654435761u)&(ctx->a11y_index_cap-1);
    while (ctx->a11y_index[slot].id && ctx->a11y_index[slot].id!=id) slot=(slot+1)&(ctx->a11y_index_cap-1);
    auto* entry=&ctx->a11y_index[slot];
    if (!entry->id) { if (!insert) return nullptr; entry->id=id;++ctx->a11y_index_count; }
    return entry;
}
void a11y_patch(A11yNode& n,const A11yOverrideDesc& d) noexcept {
    if(d.has_role)n.role=d.role;
    if(d.has_label)n.label=d.label;
    if(d.has_value_text && !(n.state_flags&A11yState_Password))n.value_text=d.value_text;
    if(d.has_actions_mask)n.actions_mask=d.actions_mask;
    n.state_flags=(n.state_flags|d.add_states)&~(d.clear_states&~A11yState_Password);
}
A11yBuild* a11y_emit(Context* ctx,uint32_t id,A11yRole role,Str label,Str value,uint32_t states,uint32_t actions) noexcept {
    auto* b=ctx->frame_arena.alloc<A11yBuild>();if(!b)return nullptr;
    *b={};b->node.id=id;b->node.role=role;b->node.state_flags=states;b->node.actions_mask=actions;
    b->node.label=ctx->frame_arena.push_str(label.chars,label.length);b->node.value_text=ctx->frame_arena.push_str(value.chars,value.length);
    b->parent=ctx->a11y_parent;b->index=ctx->a11y_count++;b->clay_id=id;
    if(ctx->a11y_last)ctx->a11y_last->next=b;else ctx->a11y_first=b;ctx->a11y_last=b;
    if (auto* entry=a11y_index(ctx,id,true)) {
        entry->build=b;
        for(auto* p=entry->patches;p;p=p->next) a11y_patch(b->node,p->desc);
    }
    return b;
}
void a11y_begin(Context* ctx) noexcept {
    auto& ui=*ctx->active_ui;ui.frame_a11y_action_count=ui.a11y_action_count;
    std::memcpy(ui.frame_a11y_actions,ui.a11y_actions,size_t(ui.a11y_action_count)*sizeof(PendingA11yAction));ui.a11y_action_count=0;
    for(uint32_t i=0;i<ui.frame_a11y_action_count;++i) {
        auto& act=ui.frame_a11y_actions[i];if(act.consumed || !act.id || act.action!=A11yAction::Focus)continue;
        const auto* ws=state_map_find(&ui.states,act.id);
        const uint32_t target=(ws && ws->collapsible_initialized)?id_index(act.id,0xC011u):act.id;
        const auto* tws=(target==act.id)?ws:state_map_find(&ui.states,target);
        if(!ui.modal_scope || target==ui.modal_scope || (tws && tws->focus_scope==ui.modal_scope)){request_focus(target);act.consumed=true;}
    }
    ctx->a11y_index=nullptr;ctx->a11y_index_cap=ctx->a11y_index_count=0;
    ctx->a11y_first=ctx->a11y_last=ctx->a11y_parent=nullptr;ctx->a11y_patches=nullptr;ctx->a11y_count=0;
    const uint32_t root=id_index(id("etch_a11y_window"),ctx->active_window?SDL_GetWindowID(ctx->active_window):0u);
    const char* title=ctx->active_window?SDL_GetWindowTitle(ctx->active_window):nullptr;
    ctx->a11y_parent=a11y_emit(ctx,root,A11yRole::Window,str_cstr(title));
}
void a11y_finish_actions(Context* ctx,bool snapshot_updated) noexcept {
    auto& ui=*ctx->active_ui;
    PendingA11yAction kept[16]={};uint32_t keep_count=0;
    for(uint32_t i=0;i<ui.frame_a11y_action_count;++i) {
        auto act=ui.frame_a11y_actions[i];
        if(act.consumed || !act.id)continue;
        bool progress=false;
        for(uint32_t j=0;j<ui.frame_a11y_action_count;++j)
            if(ui.frame_a11y_actions[j].consumed && ui.frame_a11y_actions[j].id==act.id){progress=true;break;}
        bool alive=true;
        if(snapshot_updated) {
            alive=false;
            for(uint32_t k=0;k<ui.a11y_node_count;++k) {
                const auto& n=ui.a11y_nodes[k];
                if(n.id==act.id){alive=!(n.state_flags&A11yState_Disabled) && (n.actions_mask&a11y_action_bit(act.action))!=0u;break;}
            }
        }
        if(!alive || (!progress && act.age!=0u))continue;
        act.age=static_cast<uint8_t>(progress?0u:1u);
        if(keep_count<ETCH_COUNTOF(kept))kept[keep_count++]=act;
    }
    if(keep_count>0u) {
        const uint32_t room=uint32_t(ETCH_COUNTOF(ui.a11y_actions))-keep_count;
        const uint32_t tail=(ui.a11y_action_count<room)?ui.a11y_action_count:room;
        if(tail>0u)std::memmove(ui.a11y_actions+keep_count,ui.a11y_actions,size_t(tail)*sizeof(PendingA11yAction));
        std::memcpy(ui.a11y_actions,kept,size_t(keep_count)*sizeof(PendingA11yAction));
        ui.a11y_action_count=keep_count+tail;
        if(ui.redraw_budget<2u)ui.redraw_budget=2u;
        request_redraw(ctx);
    }
    ui.frame_a11y_action_count=0;
}
bool a11y_commit_snapshot(Context* ctx) noexcept {
    auto& ui=*ctx->active_ui;
    auto* tails=ctx->frame_arena.alloc<uint32_t>(ctx->a11y_count);if(!tails){return false;}
    size_t bytes=size_t(ctx->a11y_count)*sizeof(A11yNode);
    for(auto* b=ctx->a11y_first;b;b=b->next)bytes+=size_t(b->node.label.length)+b->node.value_text.length+2;
    if(bytes>ui.a11y_arena.capacity) {
        size_t capacity=ui.a11y_arena.capacity?ui.a11y_arena.capacity:65536;while(capacity<bytes)capacity*=2;
        Arena next=arena_create(capacity);if(!next.base){return false;}arena_destroy(&ui.a11y_arena);ui.a11y_arena=next;
    }
    ui.a11y_arena.reset();ui.a11y_nodes=ui.a11y_arena.alloc<A11yNode>(ctx->a11y_count);ui.a11y_node_count=ctx->a11y_count;
    for(uint32_t i=0;i<ctx->a11y_count;++i)tails[i]=0xFFFFFFFFu;
    for(auto* b=ctx->a11y_first;b;b=b->next) {
        auto& n=ui.a11y_nodes[b->index];n=b->node;
        n.label=ui.a11y_arena.push_str(n.label.chars,n.label.length);n.value_text=ui.a11y_arena.push_str(n.value_text.chars,n.value_text.length);
        if (!b->has_explicit_bounds) {
            bool box_found = false;
            const Rect box = layout_engine_bounds(ui.layout, b->clay_id, &box_found);
            if (box_found) n.bounds = box;
            else if (const auto* ws = state_map_find(&ui.states, n.id)) n.bounds = ws->last_box_logical;
        }
        if(b->parent) {
            const uint32_t pi=b->parent->index;n.parent_id=b->parent->node.id;
            if(tails[pi]==0xFFFFFFFFu)ui.a11y_nodes[pi].first_child_idx=b->index;else ui.a11y_nodes[tails[pi]].next_sibling_idx=b->index;
            tails[pi]=b->index;
        }else {
            const float dpi=(ui.dpi_scale>0.0f)?ui.dpi_scale:1.0f;
            n.bounds={0.0f,0.0f,static_cast<float>(ui.viewport_w)/dpi,static_cast<float>(ui.viewport_h)/dpi};
        }
        if(ui.focus_id==n.id)n.state_flags|=A11yState_Focused;
        if(hovered(n.id))n.state_flags|=A11yState_Hovered;
        if(active(n.id))n.state_flags|=A11yState_Pressed;
        if (auto* entry=a11y_index(ctx,n.id)) for(auto* p=entry->patches;p;p=p->next)
            n.state_flags=(n.state_flags|p->desc.add_states)&~(p->desc.clear_states&~A11yState_Password);
    }
    ctx->stats.a11y_node_count=ui.a11y_node_count;
    return true;
}
bool a11y_take_any(uint32_t id,uint32_t mask,PendingA11yAction* out) noexcept {
    if(!id || !mask)return false;
    auto& ui=*current_context()->active_ui;
    if(ui.modal_scope && id!=ui.modal_scope) {
        const auto* ws=state_map_find(&ui.states,id);
        const auto* mws=(ws && ws->collapsible_initialized)?state_map_find(&ui.states,id_index(id,0xC011u)):ws;
        if(!mws || mws->focus_scope!=ui.modal_scope)return false;
    }
    for(uint32_t i=0;i<ui.frame_a11y_action_count;++i) {
        auto& act=ui.frame_a11y_actions[i];
        if(!act.consumed && act.id==id && (a11y_action_bit(act.action)&mask)!=0u) {
            if(out) { *out=act; } act.consumed=true;return true;
        }
    }
    return false;
}
bool a11y_take(uint32_t id,A11yAction action,PendingA11yAction* out) noexcept {
    return a11y_take_any(id,a11y_action_bit(action),out);
}
void a11y_toggle(uint32_t id,bool* open) noexcept {
    PendingA11yAction c={};
    if(a11y_take_any(id,50u,&c))*open=(c.action==A11yAction::Press)?!*open:(c.action==A11yAction::Expand);
}
void a11y_semantic(uint32_t id,A11yRole role,Str label,uint32_t states,uint32_t actions,float value,float lo,float hi) noexcept {
    auto* ctx=current_context();
    auto* entry=a11y_index(ctx,id);
    if(auto* b=entry ? entry->build : nullptr) {
        b->node.role=role;b->node.label=ctx->frame_arena.push_str(label.chars,label.length);
        b->node.state_flags|=states;b->node.actions_mask=actions;b->node.value_num=value;b->node.min_value=lo;b->node.max_value=hi;
        for(auto* p=entry->patches;p;p=p->next)a11y_patch(b->node,p->desc);
        return;
    }
    set_a11y_override({.element_id=id,.role=role,.label=label,.add_states=states,.actions_mask=actions,.has_role=true,.has_label=true,.has_actions_mask=true});
}
}
void set_a11y_override(const A11yOverrideDesc& desc) noexcept {
    auto* ctx=current_context();if(!ctx || !ctx->in_frame || !desc.element_id || (desc.label.length && !desc.label.chars) || (desc.value_text.length && !desc.value_text.chars))return;
    auto* p=ctx->frame_arena.alloc<A11yPatch>();if(!p)return;p->desc=desc;p->next=nullptr;
    p->desc.label=ctx->frame_arena.push_str(desc.label.chars,desc.label.length);p->desc.value_text=ctx->frame_arena.push_str(desc.value_text.chars,desc.value_text.length);
    auto* entry=a11y_index(ctx,desc.element_id,true);if(!entry)return;
    if(entry->tail)entry->tail->next=p;else entry->patches=p;entry->tail=p;
    if(entry->build)a11y_patch(entry->build->node,p->desc);
}
std::span<const A11yNode> frame_a11y_nodes(const Context* ctx,SDL_Window* window) noexcept {
    if(!ctx) { ctx=current_context(); } if(!ctx) { return {}; } const auto* ui=(!window && ctx->headless)?&ctx->headless_ui:window_ui(ctx,window);return ui?std::span<const A11yNode>(ui->a11y_nodes,ui->a11y_node_count):std::span<const A11yNode>{};
}
const A11yNode* find_a11y_node(uint32_t id,const Context* ctx,SDL_Window* window) noexcept {
    for(const auto& n:frame_a11y_nodes(ctx,window)) { if(n.id==id) return &n; } return nullptr;
}
bool perform_a11y_action(uint32_t id,A11yAction action,float number,Str text,Context* ctx,SDL_Window* window) noexcept {
    if(!ctx) { ctx=current_context(); } if(!ctx || !id || static_cast<uint8_t>(action)>7 || !std::isfinite(number) || (text.length && !text.chars)) { return false; }
    auto* ui=(!window && ctx->headless)?&ctx->headless_ui:window_ui(ctx,window);const auto* n=find_a11y_node(id,ctx,window);
    if(!ui || !n || (n->state_flags&A11yState_Disabled) || !(n->actions_mask&a11y_action_bit(action)) || ui->a11y_action_count==16)return false;
    auto& c=ui->a11y_actions[ui->a11y_action_count++];c={};c.id=id;c.action=action;c.number=number;
    c.length=utf8_clamp_boundary(text.chars,text.length,text.length<1023?text.length:1023);if(c.length)std::memcpy(c.text,text.chars,c.length);
    ui->redraw_budget=2;request_redraw(ctx);wake_up(ctx);
    return true;
}
const char* a11y_role_name(A11yRole role) noexcept {
    static constexpr const char* names[]={"None","Window","Group","Region","Toolbar","MenuBar","Menu","MenuItem","TabList","Tab","Dialog","ScrollArea","List","ListItem","Table","Tree","TreeItem","StaticText","Button","Checkbox","Switch","Slider","SpinButton","ProgressBar","ComboBox","TextInput","CodeEditor","Canvas","Tooltip"};
    const uint32_t i=static_cast<uint32_t>(role);return i<ETCH_COUNTOF(names)?names[i]:"None";
}
} // namespace etch
