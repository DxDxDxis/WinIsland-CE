from pathlib import Path
for rel in ['source/src/core.cpp','source/src/mod_ui.cpp']:
 p=Path(rel);s=p.read_text('utf-8-sig')
 s=s.replace('version=\\"1.3.1alpha\\"><Resident>', 'version=\\"" << utf8(Version) << "\\"><Resident>')
 s=s.replace('L"WinIsland · 插件管理 · 1.3.2alpha-winUI（实验性）"','(L"WinIsland · 插件管理 · "+std::wstring(Version)).c_str()')
 p.write_text(s,encoding='utf-8')
p=Path('settings-winui/app.h');s=p.read_text('utf-8');s=s.replace('contents[4].Children().Append(clipboardEntry);','');s=s.replace('  contents[4].Children().Append(button(L"恢复默认布局"','  contents[4].Children().Append(clipboardEntry);\n  contents[4].Children().Append(button(L"恢复默认布局"');s=s.replace('closed=true;if(timer)timer.Stop();','closed=true;for(auto& [id,c]:clipCards)c->dispose();clipCards.clear();if(timer)timer.Stop();');p.write_text(s,encoding='utf-8')
p=Path('settings-winui/clipboard_page.inc');s=p.read_text('utf-8-sig')
s=s.replace('unsigned request=0;};','unsigned request=0;winrt::event_token expanded{},keyDown{},copying{};void dispose(){++request;row.Expanding(expanded);editor.KeyDown(keyDown);editor.CopyingToClipboard(copying);body.Children().Clear();row.Content(nullptr);}};')
s=s.replace('c->editor.KeyDown([','c->keyDown=c->editor.KeyDown([').replace('c->editor.CopyingToClipboard([','c->copying=c->editor.CopyingToClipboard([').replace('c->row.Expanding([','c->expanded=c->row.Expanding([').replace('++pair.second->request;return true;','pair.second->dispose();return true;')
p.write_text(s,encoding='utf-8')
