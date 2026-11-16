# v0.12.0 工程审计处理记录

本轮针对 Chisel 提交的 19 项审计逐项核查。版本号表示新增 API 与描述符扩展；下游应重新编译。

| 编号 | 引擎改动 | 主要验证 |
| --- | --- | --- |
| P0-01 | 增加 C++/C99 source-over 混色；透明按钮使用带 Alpha 的前景覆盖层；表格 Hover 合成保留覆盖层 Alpha | 透明合成、行命中、选中态与背景覆盖测试 |
| P0-02 | 未指定指针/弹窗锚点使用 NaN；显式指针有效状态放行有限负坐标；拖拽、框选、平移和浮动标签沿用该状态 | 指针跨左上边缘的 delta 回归 |
| P0-03 | Modal 的 Escape 让位于活动 Popup；遮罩点击排除活动 Popup 边界 | 下拉框溢出 Modal 后点击与 Escape 分层关闭 |
| P0-04 | 非焦点指针目标自动避让拖窗；启用 window_drag_passthrough；自动客户区仅采集与标题栏相交的区域；增加最大化按钮命中 API | 非焦点点击区、拖窗穿透、HTMAXBUTTON 原生消息检查 |
| P0-05 | read_only 只扫描容量内文本，不补写 NUL；允许无终止符的只读切片 | 三字节只读无终止符缓冲回归 |
| P0-06 | 恢复前校验环绕字节段的完整 FNV 哈希，失败不改写调用方缓冲 | 400 轮变长快照、合并、分支、淘汰及故意损坏检查 |
| P1-01 | 表头和数据行共用当前布局的 Grid tracks；滑块填充、Tab/Dock 激活线、树引导线用布局后绘制；Dock 内容 Grow；Split 百分比分配扣除固定分隔条；Popup 支持绑定触发器与同步宽度 | 首帧及 resize 列宽、绘制回调、Split 和 Popup 绑定测试 |
| P1-02 | 保留已有 O(1) 尾部追加；新增 frame_arena ID 索引，供语义覆盖、树节点关联、UIA 存活检查使用 | 语义树与 Windows UIA 套件 |
| P1-03 | 无存活精灵时复位货架；饱和时紧缩存活精灵，重映射已提交图元 UV；清除已释放槽位的旧分配尺寸 | 持续变尺寸更新、饱和回收及 GPU 像素检查 |
| P1-04 | Popup 记录父级，仅级联删除指定节点及子级；拖放记录和文本使用可增长持久缓冲，帧内使用 Arena 切片 | 独立 Popup 关闭、40 个 3000 字节路径检查 |
| P1-05 | 多行输入接入滚轮与可拖拽滚动条；手动滚动与 caret reveal 分离；单行 Escape 先取消选择再失焦；输入框和代码编辑器复用编辑菜单 | 滚轮位移、复制/清空/撤销、焦点与编辑器套件 |
| P2-01 | 完整前景 Token；High Contrast/Industrial 明亮主色搭配深色 on_primary；勾选线遵循 Token；Switch 球增加描边 | 实际主按钮颜色对比度至少 4.5:1 |
| P2-02 | 固定栈对齐成对括号脚本；Tab/Dock 关闭和 Menu 勾选用矢量线；Dock 关闭指针反馈 | Unicode、桌面控件与视觉回归 |
| P2-03 | 先求实际可见终点，按截断处 Span 的字号预留省略号，裁剪 Pill 底色及装饰线 | 富文本视觉回归 |
| P2-04 | Kawase 中间 Pass 使用外扩后的 glass union_bounds 设置硬件 Scissor，保持完整纹理坐标系与滤波保护区 | D3D12/Vulkan GPU 及沙盒图像 |
| P2-05 | 纹理上传复用几何增长的 TransferBuffer，Map 使用 cycle，释放随 Context 生命周期 | 动态纹理更新与双后端 GPU 套件 |
| P3-01 | no-wrap 整行测量直接入缓存，结算阶段复用；缓存键加入 wrapMode | 精确宽度省略与同内容换行/不换行混用回归 |
| P3-02 | 内置词法器省去默认色 Token；长行 Token 存储与 IME 重映射溢出至 Arena | 1200 个语义 Token 及编辑器套件 |
| P3-03 | 增加 EtchLayoutDrawFn / etch_draw_after_layout | strict C99 编译及实际回调执行 |

## 核查校正与兼容说明

- A11y 的尾部追加在本轮核查前已是 O(1)。本轮消除的是仍存在的语义查找、树关联和 UIA 存活扫描；没有宣称已测得所有平台的 0.2 ms SLA。
- 撤销字节池的记录始终连续占用环形区间；删除尾部回退写指针，淘汰头部释放前缀。压力测试未复现审计提出的物理区间失步，因此保留该算法，仅补恢复校验和专门回归。
- 主按钮原先使用 accent_primary，未直接使用审计中引用的 accent。两种主题的底色与前景一起调整；不能仅把原深色底上的白字换成深色字。
- element_bounds 在 UI 构建期间仍是上次完成布局的结果。draw_after_layout 提供当前帧绘制尺寸；虚拟列表视口变化请求收敛帧。没有承诺构建阶段同步获得尚未结算的任意布局。
- 标题栏和包含标题栏的带 ID 布局容器应设 window_drag_passthrough=true；子按钮继续自动拥有客户区命中。Studio 示例已迁移并注册最大化按钮区域。
- Win32 测试验证 HTMAXBUTTON 消息结果；Snap Layouts 浮层的最终外观仍由 Windows 版本、系统设置和真实桌面决定。macOS/Metal 与 Linux 本轮未进行实机验证。
- 新增描述符字段与 C99 绑定同步；FrameDesc 默认指针与 open_popup 默认锚点改用 NaN。显式 {-1,-1} 是有效窗口外坐标。
- 纹理上传仍即时提交 CommandBuffer，只复用上传缓冲；没有把异步媒体队列或上传调度系统引入本轮。

## 验证记录（Windows，2026-11-16）

- MSVC Release 构建通过，使用 `/W4 /permissive- /GR- /EHs-c- /utf-8`，无编译警告；关闭 OS A11y 的独立构建也通过。
- 完整 CTest 11/11 通过，包括 strict C99 ABI、仓库规则及安装 / add_subdirectory 两种下游消费路径。随后针对回调返回数量边界补充回归，并重跑相关 CPU、控件、编辑器、C99 与仓库检查。
- D3D12 与 Vulkan 的 GPU 专项通过；两个后端的 `sandbox --headless` 均为 24/24，输出图像已检查，场景维持 2 次 Draw Call。
- 隔离仓库夹具验证：合法仓库与大文件通过；examples 头文件、公开头文件引入禁用 STL 均按预期失败并报告文件与规则。
- `codex review --uncommitted` 最终未发现具体待修问题；FlowGuard `flow_verify` 为 PASS / SATISFIED，antiPatterns 为空；`git diff --check` 通过。
- 本轮没有实测 macOS/Metal、Linux，也没有把单次 Windows 沙盒的布局耗时当作全场景性能 SLA 证明。
