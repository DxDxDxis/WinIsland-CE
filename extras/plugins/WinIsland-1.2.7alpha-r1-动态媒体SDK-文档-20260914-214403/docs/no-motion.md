# 1.2.6alpha-c2 no-motion

设置分类与插件管理页面采用立即切换：分类点击同步更新页索引、布局、可见控件、焦点和滚动位置，不再启动 JS/GSAP 页面动画或分类定时器。插件管理嵌入设置窗口客户区后直接显示，返回直接销毁子页并恢复设置控件。

保留 `AnimationRuntime`、GSAP 和插件动画 ABI，供插件动画与主程序其他动画使用；它们不再参与设置分类和插件管理页面切换。

构建：`source/build.ps1 -OutputDirectory source/build-no-motion`
