from pathlib import Path
p=Path('source/src/settings_host.inc');s=p.read_text('utf-8').replace('                    if(action=="cancel-translation")','                    else if(action=="cancel-translation")');p.write_text(s,encoding='utf-8')
p=Path('settings-winui/app.h');s=p.read_text('utf-8')
s=s.replace('pages[8];StackPanel contents[8]','pages[9];StackPanel contents[9]').replace('for(int i=0;i<8;i++)pages','for(int i=0;i<9;i++)pages')
s=s.replace(' void lists(){',' #include "clipboard_page.inc"\n void lists(){')
s=s.replace(' void OnLaunched(', ' void OnLaunched(')
needle='  nav.SelectionChanged([this]'
pos=s.index(needle)
s=s[:pos]+'''  NavigationViewItem clipboardItem;clipboardItem.Content(box_value(L"复制粘贴"));clipboardItem.Icon(SymbolIcon(Symbol::Paste));clipboardItem.Tag(box_value(8));uid(clipboardItem,L"nav-clipboard");nav.MenuItems().Append(clipboardItem);
  contents[8]=stack();contents[8].Margin({28,16,28,28});contents[8].MaxWidth(960);contents[8].Children().Append(label(L"复制粘贴",28));pages[8].Content(contents[8]);pages[8].HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);host.Children().Append(pages[8]);clipboardPage();
  auto clipboardEntry=button(L"复制粘贴",[this]{nav.SelectedItem(nav.MenuItems().GetAt(7));});uid(clipboardEntry,L"clipboard-open");contents[4].Children().Append(clipboardEntry);contents[8].Children().Append(button(L"返回设置",[this]{nav.SelectedItem(nav.MenuItems().GetAt(previous));}));
'''+s[pos:]
s=s.replace('scheduleIndicator(true);poll();}', 'scheduleIndicator(!reduced);poll();clipRefresh();}')
s=s.replace('motion();poll();}});','motion();poll();clipRefresh();}});')
s=s.replace('transport.stop();material.close();','clipboardClient.call(L"cancel-translation");transport.stop();material.close();')
# Keep closure cancellation bounded: client no longer sends callbacks after shutdown.
s=s.replace('clipboardClient.call(L"cancel-translation");transport.stop();','transport.stop();')
p.write_text(s,encoding='utf-8')
