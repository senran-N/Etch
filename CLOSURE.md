# 历轮审计闭环清单（v0.13.0）

覆盖 v0.9.1 起的六类缺陷、v0.10.0 边界审计、桌面生产能力与视觉建议，以及 v0.11.0 的 19 项审计。实现、自动回归、平台实测分别记录。

## 历史机制与能力补齐

| 工作项 | 最终实现 | 主要回归证据 |
| --- | --- | --- |
| 逻辑尺寸建窗与 DPI | 隐藏探测窗口坐标密度后设置最终尺寸，读取 UsableBounds；新增目标显示器、光标显示器和逻辑最小尺寸 API；跨 DPI 同步，避免 SDL 3.4 Windows 重复缩放 | OS 窗口套件：目标/光标建窗、最小尺寸缩放与重复事件无漂移 |
| Pixel snapping | 轴对齐 Rect、Texture、Glass 与布局 Clip 同步；边框量化；正面积窄条吸附归零时保留原始几何 | 精度套件、双后端图像与 GPU 检查 |
| SDF 边框 | 一致内外 AA，分区预乘覆盖，暴露 softness；新增 Inside / Center / Outside 描边位置 | 半透明覆盖回归；GPU 像素验证三种描边位置 |
| RingArc 平头 | 圆环与切向半平面相交，径向切口内外保持连续 | 视觉质量 GPU 套件 |
| Immediate 文本 | max_width_px / ellipsis，非省略模式保留原始对齐；共享 UAX #9 / HarfBuzz；字形局部坐标裁剪 | 精度、Unicode、视觉套件；当前 TextDrawDesc 未暴露旋转 API，着色器同时修正底层旋转字形裁剪 |
| spring 入场 | initial_value 重载与 C99 spring_from | 精度与 C99 套件 |
| 表格标题 | 右对齐排序标题预留箭头区域、列状态按身份维护、Grid 当前帧宽度；可隐藏列线与配置标题字号 | 首帧 / resize / 排序 / 精确宽度测试 |
| 自适应收缩 | shrink_width / shrink_height 对称支持，显式弹性内容可小于固有尺寸；wrap 行容器与固定尾部控件 | 100px 纵向容器中 300px 内容收为 80px，20px Footer 保留 |
| 无边框窗口 | 激活 window_drag_passthrough，非焦点交互元素避让；HTMAXBUTTON、DWM 圆角/暗色和 native frame；恢复时仅修改边框样式位 | Win32 消息命中、启停与释放原生样式、无关运行状态保留；关闭 A11y 仍可编译运行 |
| 表格横向滚动 | 表头与行共用水平视口，Fixed 与 Stretch 最小列宽不被挤掉 | 表头水平滚轮使数据行同步移动 |
| 表格键盘与多选 | 上下 / PageUp / PageDown / Home / End、自动滚入视野；调用方 bitmap 支持 Shift、Primary、Primary+A | 100 行焦点、范围选择、末行可见、全选位图；旧选中 API 与多选位图混用时以显式选择为准；strict C99 查询 |
| 横向滚动条 | 水平滑块绘制、轨道点击与拖拽；隐藏滚动条不抢命中 | 600px 内容在 200px 视口拖动；隐藏条区域保持内容命中 |
| Split 热区与键盘 | divider_size_px 与 divider_hit_px 分离，默认 8px 热区；Tab 聚焦后方向键 / Home / End；无障碍聚焦 / 调值；同帧计算最小面板限制 | 1px 视觉 / 8px 热区、邻近命中、键盘比例变化 |
| Combo 大列表 | 虚拟化可见行；焦点归属 O(1) 查询；键盘翻页与端点选择滚入视野 | 10000 项、初选 9000、End / Enter、提交字形数小于 300 |
| 合批与容量 | 每批 8 张外部纹理 + Atlas；64 字体槽、1024 图标槽，保留 500 / 600 槽 | 8 纹理 1 次 Draw Call / 9 纹理 2 次；300 自动图标、槽 1023 与字体槽 63 |
| 视觉质感 | Continuous superellipse corners、shadow_offset / spread、500 / 600 静态字重、HarfBuzz tnum、Slug 暗字补偿 | 视觉质量 CPU/GPU、字体功能与图像回归 |
| 非对称圆角子项裁剪 | GPU 索引裁剪链保留四角及嵌套父级曲线；零圆角子裁剪不会抹掉父级圆角；Prim 仍为 64B | 非对称 32/0/24/8 四角 + 嵌套裁剪逐像素验证 |
| 现代列表外观 | 可隐藏列线、圆角、斑马行、可配置字号与 Hover / Selection | 桌面控件与沙盒；采用建议中的表格方案，未另造滚动边缘渐隐 API |
| 动态纹理上传 | 复用 cycled TransferBuffer，多次更新共用待提交 CommandBuffer，渲染前统一提交；外部消费者可显式 flush | 同纹理连续更新后取最新像素；双后端 GPU |
| C99 与 shader 同步 | 所有新增描述符 / API 同步 C99；DXIL、SPIR-V、Metal 源码从同一 HLSL 重新生成 | strict C99、下游安装 / 子项目消费、关闭现场 shader 编译的 fallback 构建 |

## v0.11.0 的 19 项审计

详细修改及原始验证记录见 [AUDIT_v0.12.md](AUDIT_v0.12.md)。本版本保留并重跑相应套件，新增机制见上表。

| 编号 | 结果 |
| --- | --- |
| P0-01 | source-over 混色；透明按钮覆盖层与表格 Hover 保留 Alpha |
| P0-02 | NaN 未指定坐标及显式指针状态；允许有限负坐标捕获拖拽 |
| P0-03 | Modal 让位于内部 Popup 的点击和 Escape |
| P0-04 | 生效的拖窗穿透、交互避让与原生最大化命中；本轮再补 native frame 生命周期 |
| P0-05 | read_only 不写缓冲，支持容量内无 NUL 只读切片 |
| P0-06 | Undo 恢复前校验完整环绕哈希，损坏不写输出；变长压力 / 分支 / 淘汰回归通过。原环池失步未复现，不以猜测替换已证明连续占用的算法 |
| P1-01 | 内置装饰绘制迁至 draw_after_layout；Dock Grow、Split 当前帧比例及 Popup 触发器绑定 |
| P1-02 | A11y 尾指针追加、Arena ID 索引、UIA 存活索引，消除列出的二次扫描 |
| P1-03 | 空 Atlas 复位，饱和后紧缩存活精灵并修正当帧 UV |
| P1-04 | Popup 父子关闭；文件拖放使用可增长持久缓冲 |
| P1-05 | 多行滚轮 / 滑块、Escape 失焦、输入 / 编辑器上下文菜单与输入框可选清空 |
| P2-01 | 完整主题前景 Token、勾选线与 Switch 描边；主按钮对比度回归 |
| P2-02 | 成对括号脚本对齐；关闭和勾选使用矢量图形并提供 Hover / Hand |
| P2-03 | 按截断 Span 预留省略号，Pill / 下划线按可见终点裁剪 |
| P2-04 | 局部 Blur union_bounds 外扩后映射各 Pass Scissor |
| P2-05 | 持久上传缓冲；本版本进一步合并待提交上传命令 |
| P3-01 | no-wrap 整行测量缓存，结算复用；wrapMode 参与缓存键 |
| P3-02 | 长行语义 Token 溢出至 Arena，不限于 256 Token |
| P3-03 | C99 draw_after_layout 导出及真实回调执行检查 |

## 契约和验收边界

- UI 构建阶段的 element_bounds 仍返回上次完成布局结果；当前帧尺寸由 draw_after_layout 和引擎 Grid / 约束结算提供。任意尚未结算的布局不能提前查询。虚拟列表视口变化请求收敛帧，空闲休眠不会吞掉它。
- 静态 500 / 600 字体与 tnum 已实现；可变字体 wght 轴不属于本轮实现。Continuous corners 是距离近似，不声称与 Apple 原生轮廓完全相同。
- 描述符与 packed Prim header 扩展要求重新编译下游，并使用同版本 shader；Prim=64B、GlobalsUniform=32B 保持不变。
- 上传像素在 API 返回前已复制；外部 SDL GPU 消费者应先调用 flush_texture_uploads，再提交消费命令；所有操作保持同一 GPU 所属线程。
- 当前设备是 Windows。macOS / Metal 与 Linux / Wayland / X11 尚无本机实测；Metal 完成离线转译和资源绑定检查。真实跨显示器拖动、Win11 Snap 浮层及 DWM 外观仍需相应桌面硬件验收。
- 沙盒已验证空闲标志与合批；本机本轮并发验证期间沙盒布局读数约 0.55–1.74ms，不将其宣称为已达到全场景 <0.2ms SLA。

## 本轮验证记录

Windows，2026-11-16：

- MSVC Release 完整干净构建和后续补丁构建均通过，`/W4 /permissive- /GR- /EHs-c- /utf-8`，无编译警告。`git diff --check` 通过。
- 最终完整 CTest **11/11**，包含 pure C99、repo_guardrails、安装包与 add_subdirectory 下游消费（267.19s）。随后补充显式多选覆盖旧选中态的边界，相关布局、控件、A11y、C99、仓库与下游 **6/6** 再通过（76.25s）。
- 主构建 headless GPU 套件通过；显式 Vulkan 的完整 GPU 专项通过。8 / 9 纹理合批、连续纹理更新、非对称嵌套圆角和三种描边位置均有像素 / Draw Call 断言。
- D3D12 与 Vulkan 最终 `sandbox --headless` 都通过 **24/24**，场景均为 **2 Draw Calls**；输出图像已打开检查。图像保留在 build/ 下。
- `ETCH_ENABLE_OS_A11Y=OFF`、`ETCH_COMPILE_SHADERS=OFF` 的独立 fallback 构建通过，布局 / A11y / C99 / GPU **4/4** 通过；最终交互与选中态补丁再跑布局 / A11y / C99 **3/3** 通过（5.59s）。
- 隔离仓库夹具：合法源码与大文件正例通过；examples 头文件、公开头禁用 STL、C99 头引入 stdio 的反例均失败并报告对应文件 / 规则。没有向工作源码注入违规。
- `codex review --uncommitted` 最终完整源码审查和多选兼容边界专项审查均未发现具体待修问题。早期审查发现的 fallback shader、负 row_gap、隐藏滚动条命中问题已修复并有回归。
- FlowGuard `flow_verify` 为 **PASS / SATISFIED**，antiPatterns 为空。这是静态契约检查，不替代上述行为测试。
- 验证日志与图像保留在 ignored build/ 内。提交前清理根目录临时图像，提交后确认工作树干净。

上述源码与本机自动回归已闭环；本文件列明的跨平台实机、多显示器、原生浮层外观与性能目标不冒充已验收。
