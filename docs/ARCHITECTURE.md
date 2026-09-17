# 架构与交互关系

本页描述项目用途与较新主线架构，不将所有历史版本视为相同实现。

## 核心结构

```mermaid
flowchart LR
    U[用户 / 托盘 / 文件拖放] --> H[C++ Win32 宿主]
    W[Windows 媒体会话与通知] --> H
    L[歌词辅助服务与来源] --> H
    H --> S[场景状态与布局]
    S --> R[Direct2D / DirectComposition]
    R --> I[主灵动岛与中转组件]
    H --> D[统一数据根目录]
    E[Electron TypeScript 设置页] --> P[隔离 preload / IPC]
    P --> H
    H --> M[插件加载器 / 生命周期]
    M --> A[C ABI / 场景 / 设置 / 输入 / 媒体]
    A --> S
```

设置页负责呈现与用户输入；核心负责真实配置校验、保存和生效。渲染页不通过开放 Node 或文件系统权限来绕过宿主。主程序生命周期、插件 ABI 和设置 UI 版本是不同概念。

## 设置保存

```mermaid
sequenceDiagram
    participant U as 用户
    participant E as 设置页
    participant P as preload与main
    participant H as C++核心
    participant D as settings.xml
    U->>E: 改变选项
    E->>P: settings.write / revision / patch
    P->>H: 验证来源后发送IPC
    H->>H: 校验字段与版本冲突
    H->>D: 原子写入
    H-->>E: 实际值与新revision / 错误
    E-->>U: 确认真实状态或回退提示
```

开关动画只表示视觉变化，不能替代核心成功响应。“降低动画”是普通持久化设置，不暂停媒体或文件复制。

## 文件中转

```mermaid
flowchart TD
    A[外部文件拖入] --> B{是否启用主岛接收}
    B -- 否 --> C[主岛不接管]
    B -- 是 --> D[接收状态 / 待用户选择]
    D --> E{导入模式}
    E -- 仅中转 --> F[记录原路径与元数据]
    E -- 保存并中转 --> G[后台复制到staging]
    G --> H{成功校验}
    H -- 是 --> J[发布副本并原子更新索引]
    H -- 否 --> K[失败或取消状态 / 原文件保留]
    F --> L[统一条目数据]
    J --> L
    L --> M[设置列表]
    L --> N[停靠 / 浮动 / 更多文件视图]
    N --> O[Windows OLE 文件拖出]
```

内部拖出后回到来源属于取消或返回，不应重新走外部导入。不同视图共用数据而不是各自维护独立副本。文件详情展示元数据与操作；当前版已经移除内容预览。

## 技术演进

```mermaid
flowchart LR
    A[C# / WPF 早期社区版] --> B[C++ 原生核心]
    B --> C[歌词 / 布局 / 通知迭代]
    C --> D[插件包与开放场景]
    D --> E[TypeScript + Electron 设置]
    E --> F[插件管理与统一数据目录]
    F --> G[文件中转与1.3.1alpha]
```

产品快照、实验分支和源码片段分开归档；演进图表达主线关系，不代表每个阶段都有完整、可逐字节重建的历史源码。
