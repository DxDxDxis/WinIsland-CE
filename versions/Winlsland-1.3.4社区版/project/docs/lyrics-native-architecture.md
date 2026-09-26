# 原生 C++ 多平台歌词架构

1.3.4 将歌词请求放在 `source/src/lyrics_providers.*` 的 `LyricsProviderRegistry` 中。主程序的 `music_sources.cpp` 只负责歌曲状态、请求 generation 和旧缓存兼容；WinUI 不直接访问平台接口。

## Provider 和格式

自动顺序为网易云、QQ、酷狗、汽水、Apple Music，最后使用 LRCLIB 兜底；歌曲平台匹配的 Provider 会提前。手动来源使用兼容的历史索引并扩展 QQ、酷狗、汽水和 Apple Music。解析器支持 LRC、QRC、KRC、YRC、TTML、翻译和罗马音，并统一转换到 `LyricsDocument/LyricsLine/LyricsSyllable`。

QQ QRC 使用项目内原生协议解密和 Deflate；酷狗 KRC 使用 Base64、固定公开 XOR 密钥和 Deflate；网易云请求使用 Windows CNG 的 AES/MD5/RSA 兼容实现；Apple Music 的 Media User Token 和 Access/Developer Token 由宿主 DPAPI 加密保存，不写入日志或界面回显。

## 请求和缓存

每次请求带 generation 校验和唯一 request id。歌曲切换时旧请求取消，WinHTTP 请求有超时、大小上限和有限重试。缓存键包含 Provider、标题、艺人、专辑和时长，缓存文件保存来源、格式、偏移和逐字信息。逐字歌词缺失时保留逐行结果并标记降级。

新增 Provider 时实现候选搜索、匹配、歌词获取和格式解析，再在 `LyricsProviderRegistry::providers()` 注册能力声明，并为解密、解析、空响应、取消和过期缓存添加离线 fixture；不要把平台 Token 或 Cookie 写入源码。
