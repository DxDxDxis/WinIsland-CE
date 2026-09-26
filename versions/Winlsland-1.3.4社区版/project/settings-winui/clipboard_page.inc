 // WinUI clipboard management. The host owns history, DPAPI, clipboard and exports.
 client::ClipboardClient clipboardClient{transport};
 struct ClipCard {Expander row;StackPanel body;TextBlock summary;CheckBox selected;TextBox editor;Button language;int languageIndex=0;ComboBox exportPart;JsonObject saved;bool loaded=false,loading=false;unsigned request=0;winrt::event_token expanded{},collapsed{},keyDown{},copying{},languageClick{},edited{};void dispose(){++request;row.Expanding(expanded);row.Collapsed(collapsed);language.Click(languageClick);editor.TextChanged(edited);editor.KeyDown(keyDown);editor.CopyingToClipboard(copying);body.Children().Clear();row.Content(nullptr);}};
 std::map<std::wstring,std::shared_ptr<ClipCard>> clipCards;
 ListView clipList;TextBox clipSearch,clipExcluded,clipEndpoint;NumberBox clipMaximum;ComboBox clipTarget;TextBlock clipNote,clipModelNote;Button clipModelButton{nullptr},clipModelCancel{nullptr};ProgressBar clipModelProgress;bool clipModelPolling=false,clipModelWorking=false;
 std::map<std::wstring,ToggleSwitch> clipSwitches;Button clipReset{nullptr};
 JsonObject clipPreferences;bool clipSyncing=false,clipBusy=false,clipPolling=false,clipComposing=false,clipPrefsDirty=false,clipTranslating=false;
 double clipRevision=-1,clipSeen=0;hstring clipLastQuery;unsigned clipGeneration=0;
 std::vector<hstring> clipLanguageNames={L"自动（中英互译）",L"简体中文",L"繁体中文",L"英文",L"美式英文",L"日文",L"韩文",L"俄语",L"印地语",L"德语"};
 std::vector<hstring> clipLanguages={L"auto",L"zh-Hans",L"zh-Hant",L"en",L"en-US",L"ja",L"ko",L"ru",L"hi",L"de"};
 void clipLanguageChoices(ComboBox c){for(auto s:{L"自动（中英互译）",L"简体中文",L"繁体中文",L"英文",L"美式英文",L"日文",L"韩文",L"俄语",L"印地语",L"德语"})c.Items().Append(box_value(s));c.SelectedIndex(0);c.HorizontalAlignment(HorizontalAlignment::Stretch);}
 Windows::Foundation::IAsyncOperation<bool> clipConfirm(hstring message,hstring title=L"确认历史操作",hstring action=L"确认"){auto d=clipDialog(title);d.Content(label(message));d.PrimaryButtonText(action);d.CloseButtonText(L"取消");d.DefaultButton(ContentDialogButton::Close);co_return (co_await showSettingsDialog(d))==ContentDialogResult::Primary;}
 fire_and_forget clipRun(hstring action,JsonObject p=JsonObject{},hstring confirmation=L""){
  auto life=get_strong();if(clipBusy||closed)co_return;clipBusy=true;
  try{bool proceed=true;if(!confirmation.empty())proceed=co_await clipConfirm(confirmation);if(proceed&&!closed){auto r=JsonObject::Parse(co_await clipboardClient.call(action,p));if(!closed){clipNote.Text(r.GetNamedBoolean(L"cancelled",false)?L"已取消":L"操作已完成");clipRevision=-1;}}}catch(hresult_error const& e){error(e.message());}
  clipBusy=false;if(!closed)clipRefresh();
 }
 fire_and_forget clipSavePreferences(){auto life=get_strong();if(clipBusy||closed||clipPreferences.Size()==0)co_return;clipBusy=true;
  try{bool proceed=true;auto next=clipMaximum.Value();if(!std::isfinite(next)||next<4||next>200||std::floor(next)!=next)throw hresult_invalid_argument(L"历史上限须为 4–200 的整数");
   if(next<clipPreferences.GetNamedNumber(L"maximum"))proceed=co_await clipConfirm(L"降低上限会移除超出的最旧记录，是否继续？");
   if(proceed&&!closed){JsonObject prefs;for(auto& [key,c]:clipSwitches)put(prefs,key.c_str(),c.IsOn());put(prefs,L"maximum",next);put(prefs,L"excluded",clipExcluded.Text());put(prefs,L"target",clipLanguages.at(std::max(0,clipTarget.SelectedIndex())));put(prefs,L"translationEndpoint",clipEndpoint.Text());JsonObject p;p.Insert(L"preferences",prefs);put(p,L"confirmed",true);co_await clipboardClient.call(L"preferences",p);clipPrefsDirty=false;clipRevision=-1;if(!closed)clipNote.Text(L"偏好已保存");}
  }catch(hresult_error const& e){error(e.message());}clipBusy=false;if(!closed)clipRefresh();
 }
 fire_and_forget clipModelRefresh(){auto life=get_strong();winrt::apartment_context ui;if(closed||clipModelPolling)co_return;clipModelPolling=true;
  try{for(;;){auto r=JsonObject::Parse(co_await clipboardClient.call(L"translation-model-status"));if(closed)break;bool installed=r.GetNamedBoolean(L"installed",false),busy=r.GetNamedBoolean(L"busy",false);auto phase=text(r,L"phase");clipModelWorking=busy;
   clipModelButton.Content(box_value(installed?L"卸载本地离线翻译模型":L"下载本地离线翻译模型"));clipModelButton.IsEnabled(!busy);clipModelCancel.Visibility(busy&&phase!=L"uninstalling"?Visibility::Visible:Visibility::Collapsed);
   clipModelProgress.Visibility(busy?Visibility::Visible:Visibility::Collapsed);clipModelProgress.IsIndeterminate(phase!=L"downloading");clipModelProgress.Maximum(r.GetNamedNumber(L"total",1));clipModelProgress.Value(r.GetNamedNumber(L"received",0));
   hstring note=installed?L"已安装中英离线模型。开启“使用本地离线翻译”并保存偏好后，无需联网即可翻译。":L"未安装。下载包约 182 MB，安装需至少 1 GB 可用空间；仅支持简体中文与英文互译。";
   if(phase==L"preparing")note=L"正在准备下载…";else if(phase==L"downloading")note=L"正在下载："+str(std::floor(r.GetNamedNumber(L"received")/1048576))+L" / "+str(std::ceil(r.GetNamedNumber(L"total")/1048576))+L" MB";
   else if(phase==L"verifying")note=L"下载完成，正在校验完整性…";else if(phase==L"extracting")note=L"正在安装模型与本地运行库…";else if(phase==L"testing")note=L"正在验证模型能否在此电脑上运行…";
   else if(phase==L"uninstalling")note=L"正在结束本地翻译并卸载模型，剪贴板历史会保留…";else if(phase==L"cancelled")note=L"下载已取消，临时文件已清理。";else if(phase==L"removed")note=L"本地模型已卸载，剪贴板历史已保留。若要在线翻译，请关闭离线开关并保存。";
   if(!text(r,L"error").empty())note=text(r,L"error");clipModelNote.Text(note);if(!busy)break;co_await std::chrono::milliseconds(500);co_await ui;if(closed)break;}
  }catch(hresult_error const& e){if(!closed)clipModelNote.Text(e.message());}clipModelPolling=false;
 }
 fire_and_forget clipModelAction(){auto life=get_strong();if(closed||clipBusy||clipModelWorking)co_return;clipBusy=true;clipModelButton.IsEnabled(false);
  try{auto status=JsonObject::Parse(co_await clipboardClient.call(L"translation-model-status"));bool installed=status.GetNamedBoolean(L"installed",false);if(!status.GetNamedBoolean(L"busy",false)){
   hstring message=installed?L"是否卸载本地离线翻译模型和运行库？正在进行的本地翻译会结束；剪贴板历史和已保存译文不会删除。":L"是否下载中英离线翻译模型？下载约 "+str(std::ceil(status.GetNamedNumber(L"total")/1048576))+L" MB，需要至少 1 GB 可用空间。文件保存在统一数据目录；无需另装 Python。";
   if(co_await clipConfirm(message,installed?L"卸载离线模型":L"下载离线模型",installed?L"卸载":L"下载")){JsonObject p;put(p,L"confirmed",true);co_await clipboardClient.call(installed?L"translation-model-uninstall":L"translation-model-install",p);}
  }}catch(hresult_error const& e){if(!closed)clipModelNote.Text(e.message());}clipBusy=false;if(!closed){clipModelButton.IsEnabled(true);clipModelRefresh();}
 }
 fire_and_forget clipCancelModel(){auto life=get_strong();try{co_await clipboardClient.call(L"translation-model-cancel");if(!closed){clipModelNote.Text(L"正在取消下载并清理临时文件…");clipModelCancel.IsEnabled(false);}}catch(hresult_error const& e){if(!closed)clipModelNote.Text(e.message());}if(!closed){clipModelCancel.IsEnabled(true);clipModelRefresh();}}
 fire_and_forget clipSave(hstring id,std::shared_ptr<ClipCard> c){auto life=get_strong();if(clipBusy||!c->loaded||closed)co_return;clipBusy=true;
  try{auto draft=c->editor.Text();auto p=param(L"id",id);put(p,L"text",draft);put(p,L"version",c->saved.GetNamedNumber(L"version"));co_await clipboardClient.call(L"save",p);auto r=JsonObject::Parse(co_await clipboardClient.call(L"get",param(L"id",id)));if(!closed){c->saved=r.GetNamedObject(L"entry");clipRevision=-1;clipNote.Text(L"修改已保存");}}catch(hresult_error const& e){error(e.message());}clipBusy=false;if(!closed)clipRefresh();
 }
 ContentDialog clipDialog(hstring title){
  auto d=Markup::XamlReader::Load(LR"(<ContentDialog xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" CornerRadius="8" Background="{ThemeResource SolidBackgroundFillColorBaseBrush}" />)").as<ContentDialog>();
  d.XamlRoot(root.XamlRoot());d.RequestedTheme(root.ActualTheme());d.Title(box_value(title));
  if(blurDisabled)d.Background(Media::SolidColorBrush(appearance::color(0,0,0)));
  d.Loaded([this](winrt::Windows::Foundation::IInspectable const& sender,auto&&){settingsMotion.scan(sender.as<DependencyObject>());});
  return d;
 }
 Windows::Foundation::IAsyncOperation<ContentDialogResult> showSettingsDialog(ContentDialog d){auto life=get_strong();if(closed||activeSettingsDialog)co_return ContentDialogResult::None;activeSettingsDialog=d;ContentDialogResult result=ContentDialogResult::None;try{result=co_await d.ShowAsync();}catch(...){if(activeSettingsDialog==d)activeSettingsDialog=nullptr;throw;}if(activeSettingsDialog==d)activeSettingsDialog=nullptr;co_return result;}
 fire_and_forget clipChooseLanguage(std::shared_ptr<ClipCard> c){auto life=get_strong();if(closed||clipBusy||clipTranslating||activeSettingsDialog)co_return;clipBusy=true;auto serial=c->request;auto originalPage=page;
  try{auto d=clipDialog(L"选择翻译语言");uid(d,L"clipboard-language-dialog");ListView choices;choices.SelectionMode(ListViewSelectionMode::Single);choices.HorizontalContentAlignment(HorizontalAlignment::Stretch);for(auto name:clipLanguageNames)choices.Items().Append(box_value(name));choices.SelectedIndex(c->languageIndex);uid(choices,L"clipboard-language-list");
   choices.MaxHeight(std::max(100.,root.ActualHeight()-280));choices.MinWidth(200);d.Content(choices);d.MaxWidth(420);d.PrimaryButtonText(L"确定");d.CloseButtonText(L"返回");d.DefaultButton(ContentDialogButton::Primary);
   auto sizing=root.SizeChanged([choices](auto&&,SizeChangedEventArgs const& e){choices.MaxHeight(std::max(100.,double(e.NewSize().Height)-280));});d.Opened([choices](auto&&,auto&&){choices.Focus(FocusState::Programmatic);});
   ContentDialogResult result;try{result=co_await showSettingsDialog(d);}catch(...){root.SizeChanged(sizing);throw;}root.SizeChanged(sizing);
   if(result==ContentDialogResult::Primary&&!closed&&page==originalPage&&c->request==serial&&c->row.IsExpanded()){
    int selected=choices.SelectedIndex();if(selected>=0&&size_t(selected)<clipLanguages.size()){
     auto current=JsonObject::Parse(co_await clipboardClient.call(L"list"));auto prefs=current.GetNamedObject(L"preferences");put(prefs,L"target",clipLanguages[selected]);JsonObject request;request.Insert(L"preferences",prefs);put(request,L"confirmed",true);co_await clipboardClient.call(L"preferences",request);
     if(!closed){c->languageIndex=selected;c->language.Content(box_value(clipLanguageNames[selected]));put(clipPreferences,L"target",clipLanguages[selected]);clipSyncing=true;clipTarget.SelectedIndex(selected);clipSyncing=false;clipRevision=-1;}
    }
   }
  }catch(hresult_error const& e){if(!closed)error(e.message());}catch(...){if(!closed)error(L"语言设置保存失败，请重试。");}
  clipBusy=false;if(!closed&&page==originalPage&&c->row.IsExpanded())c->language.Focus(FocusState::Programmatic);
 }
 void clipCancelTranslation(){
  if(!clipTranslating||clipLoadingCancel)return;
  clipLoadingCancel=true;if(clipLoadingDialog)clipLoadingDialog.Hide();
 }
 fire_and_forget clipTranslate(hstring id,std::shared_ptr<ClipCard> c){
  auto life=get_strong();winrt::apartment_context ui;
  if(closed||page!=8||!c->loaded||clipTranslating||clipBusy||clipEndpointDialogOpen||activeSettingsDialog)co_return;
  clipTranslating=true;clipLoadingCancel=false;unsigned serial=++c->request;hstring failure;bool success=false;
  auto draft=c->editor.Text();auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(90);Windows::Foundation::IAsyncOperation<ContentDialogResult> shown{nullptr};ProgressRing ring;
  try{
   clipLoadingDialog=clipDialog(L"正在翻译中，请稍后");uid(clipLoadingDialog,L"clipboard-translation-loading");
   auto content=stack();content.HorizontalAlignment(HorizontalAlignment::Center);ring.Width(32);ring.Height(32);ring.IsActive(!reduced);content.Children().Append(ring);
   content.Children().Append(label(L"完成后将生成独立的剪贴板记录。",13));clipLoadingDialog.Content(content);clipLoadingDialog.CloseButtonText(L"返回");
   clipLoadingDialog.CloseButtonClick([this](auto&&,auto&&){clipCancelTranslation();});
   clipLoadingDialog.Closing([this](auto&&,auto&&){if(clipLoadingVisible)clipCancelTranslation();});
   shown=showSettingsDialog(clipLoadingDialog);clipLoadingVisible=true;
   auto p=param(L"id",id);auto selected=c->editor.SelectedText();put(p,L"text",selected.empty()?draft:selected);
   GUID guid;check_hresult(CoCreateGuid(&guid));wchar_t buffer[40];StringFromGUID2(guid,buffer,40);hstring requestId(buffer);put(p,L"requestId",requestId);put(p,L"source",L"auto");
   auto index=c->languageIndex;if(index<0||size_t(index)>=clipLanguages.size())throw hresult_invalid_argument(L"请选择翻译目标语言");put(p,L"target",clipLanguages[size_t(index)]);
   auto reply=JsonObject::Parse(co_await clipboardClient.call(L"translate-start",p));
   while(reply.GetNamedBoolean(L"pending",false)&&!closed&&!clipLoadingCancel&&page==8&&serial==c->request){
    co_await std::chrono::milliseconds(200);co_await ui;
    if(closed||clipLoadingCancel||page!=8||serial!=c->request)break;
    if(std::chrono::steady_clock::now()>deadline){co_await clipboardClient.call(L"cancel-translation");throw hresult_error(E_FAIL,L"翻译请求超时，请重试。");}
    reply=JsonObject::Parse(co_await clipboardClient.call(L"translation-read",param(L"requestId",requestId)));
   }
   if(clipLoadingCancel||page!=8||serial!=c->request){
    // Serialize cancellation after start/read, before permitting another request.
    // Otherwise a late cancellation response can cancel the next translation.
    if(!closed)co_await clipboardClient.call(L"cancel-translation");
   }
   if(!closed&&!clipLoadingCancel&&page==8&&serial==c->request){
    if(!text(reply,L"error").empty())throw hresult_error(E_FAIL,text(reply,L"error"));
    if(text(reply,L"entryId").empty())throw hresult_error(E_FAIL,L"翻译未创建历史记录，请刷新后重试");
    // The host commits a separate history entry; never replace an edited source.
    if(reply.HasKey(L"sourceVersion"))put(c->saved,L"version",reply.GetNamedNumber(L"sourceVersion"));
    success=true;clipRevision=-1;
   }
  }catch(hresult_error const& e){failure=e.message();}catch(std::exception const& e){failure=to_hstring(e.what());}catch(...){failure=L"翻译未完成，请重试";}
  clipLoadingVisible=false;ring.IsActive(false);if(clipLoadingDialog)clipLoadingDialog.Hide();
  if(shown){try{co_await shown;}catch(...){}}
  clipLoadingDialog=nullptr;bool cancelled=clipLoadingCancel;clipTranslating=false;
  if(closed||page!=8||cancelled||serial!=c->request)co_return;
  clipRefresh();
  try{
   if(success){auto done=clipDialog(L"翻译完成");done.Content(label(L"翻译文档已在剪贴板内，请在剪贴板内找寻。"));done.CloseButtonText(L"知道了");co_await showSettingsDialog(done);}
   else if(!failure.empty()){error(failure);auto failed=clipDialog(L"翻译未完成");failed.Content(label(failure));failed.PrimaryButtonText(L"重试");failed.CloseButtonText(L"返回");if((co_await showSettingsDialog(failed))==ContentDialogResult::Primary&&!closed&&page==8)clipTranslate(id,c);}
  }catch(hresult_error const& e){if(!closed)error(e.message());}
 }
 fire_and_forget clipCommitEndpoint(ContentDialog dialog,TextBox input,TextBlock validation,ContentDialogButtonClickEventArgs args){
  auto life=get_strong();auto deferral=args.GetDeferral();args.Cancel(true);dialog.IsPrimaryButtonEnabled(false);dialog.IsSecondaryButtonEnabled(false);hstring endpoint=input.Text();
  try{
   std::wstring address(endpoint);auto first=address.find_first_not_of(L" \t\r\n"),last=address.find_last_not_of(L" \t\r\n");
   if(first==std::wstring::npos)throw hresult_invalid_argument(L"请输入服务器完整地址，不能留空。");
   address=address.substr(first,last-first+1);Windows::Foundation::Uri uri{hstring(address)};
   if((uri.SchemeName()!=L"https"&&uri.SchemeName()!=L"http")||uri.Host().empty()||!uri.UserName().empty()||!uri.Password().empty()||!uri.Fragment().empty()||address.find_first_of(L" \t\r\n")!=std::wstring::npos)throw hresult_invalid_argument(L"请输入有效的 http:// 或 https:// 完整地址。");
   // Read latest preferences, replacing only the endpoint; other unsaved UI choices remain drafts.
   auto current=JsonObject::Parse(co_await clipboardClient.call(L"list"));auto prefs=current.GetNamedObject(L"preferences");put(prefs,L"translationEndpoint",hstring(address));JsonObject p;p.Insert(L"preferences",prefs);put(p,L"confirmed",true);
   co_await clipboardClient.call(L"preferences",p);
   if(!closed){clipSyncing=true;clipEndpoint.Text(hstring(address));clipSyncing=false;put(clipPreferences,L"translationEndpoint",hstring(address));clipRevision=-1;args.Cancel(false);clipNote.Text(L"翻译服务器地址已保存");}
  }catch(hresult_error const& e){if(!closed){validation.Text(L"无法保存："+e.message());validation.Visibility(Visibility::Visible);}}
  catch(...){if(!closed){validation.Text(L"地址格式无效或服务器设置保存失败。");validation.Visibility(Visibility::Visible);}}
  if(!closed){dialog.IsPrimaryButtonEnabled(true);dialog.IsSecondaryButtonEnabled(true);}deferral.Complete();
 }
 fire_and_forget clipEditEndpoint(){
  auto life=get_strong();if(closed||clipEndpointDialogOpen||clipTranslating||clipBusy)co_return;clipEndpointDialogOpen=true;clipBusy=true;
  try{auto d=clipDialog(L"其他翻译服务器地址");uid(d,L"clipboard-endpoint-dialog");auto content=stack();TextBox input;input.Header(box_value(L"在线翻译服务器地址"));input.Text(clipEndpoint.Text());input.PlaceholderText(L"https://example.com/translate");input.MaxLength(4096);input.MinWidth(220);uid(input,L"clipboard-endpoint-input");content.Children().Append(input);
   auto validation=label(L"",13);validation.Visibility(Visibility::Collapsed);content.Children().Append(validation);d.Content(content);d.PrimaryButtonText(L"确定");d.SecondaryButtonText(L"返回");d.DefaultButton(ContentDialogButton::Primary);
   d.PrimaryButtonClick([this,input,validation](ContentDialog const& dialog,ContentDialogButtonClickEventArgs const& e){clipCommitEndpoint(dialog,input,validation,e);});co_await showSettingsDialog(d);
  }catch(hresult_error const& e){error(e.message());}clipEndpointDialogOpen=false;clipBusy=false;if(!closed)clipRefresh();
 }
 fire_and_forget clipExpand(hstring id,std::shared_ptr<ClipCard> c){auto life=get_strong();if(c->loaded||c->loading||closed)co_return;c->loading=true;
  try{auto r=JsonObject::Parse(co_await clipboardClient.call(L"get",param(L"id",id)));if(!closed){c->saved=r.GetNamedObject(L"entry");c->editor.Text(text(c->saved,L"text"));c->loaded=true;}}catch(hresult_error const& e){error(e.message());}c->loading=false;
 }
 std::shared_ptr<ClipCard> clipCreate(hstring id){auto c=std::make_shared<ClipCard>();c->body=stack();c->row.HorizontalAlignment(HorizontalAlignment::Stretch);c->row.HorizontalContentAlignment(HorizontalAlignment::Stretch);c->row.Header(c->summary);c->row.Content(c->body);uid(c->row,L"clipboard-card-"+id);
  c->summary.TextWrapping(TextWrapping::Wrap);c->summary.MaxLines(3);c->summary.IsTextSelectionEnabled(false);c->summary.TextTrimming(TextTrimming::CharacterEllipsis);
  c->selected.Content(box_value(L"选择此记录"));uid(c->selected,L"clipboard-select-"+id);c->body.Children().Append(c->selected);
  c->body.Children().Append(label(L"完整内容 · 修改只在保存后写入历史",12));c->editor.AcceptsReturn(true);c->editor.TextWrapping(TextWrapping::Wrap);c->editor.MaxLength(262144);c->editor.Height(210);ScrollViewer::SetVerticalScrollBarVisibility(c->editor,ScrollBarVisibility::Auto);uid(c->editor,L"clipboard-editor-"+id);c->body.Children().Append(c->editor);
  c->keyDown=c->editor.KeyDown([this,id,c](auto&&,Input::KeyRoutedEventArgs const& e){if(e.Key()==Windows::System::VirtualKey::S&&GetKeyState(VK_CONTROL)<0){e.Handled(true);clipSave(id,c);}});
  c->copying=c->editor.CopyingToClipboard([this,c](auto&&,TextControlCopyingToClipboardEventArgs const& e){e.Handled(true);auto s=c->editor.SelectedText();if(!s.empty())clipRun(L"copy-text",param(L"text",s));});
  std::vector<Button> cardActions;auto add=[&](hstring name,std::function<void()> action,hstring aid){auto b=button(name,action);uid(b,aid+L"-"+id);cardActions.push_back(b);};
  add(L"保存修改",[this,id,c]{clipSave(id,c);},L"clipboard-save");
  add(L"取消修改",[c]{if(c->loaded)c->editor.Text(text(c->saved,L"text"));},L"clipboard-revert");
  add(L"重新读取已保存内容",[this,id,c]{clipReload(id,c);},L"clipboard-reload");
  add(L"复制草稿（不保存）",[this,c]{clipRun(L"copy-text",param(L"text",c->editor.Text()));},L"clipboard-copy");
  add(L"删除记录",[this,id]{clipRun(L"delete",param(L"id",id),L"删除本条历史与草稿？系统剪贴板不变。");},L"clipboard-delete");
  c->body.Children().Append(actionGrid(cardActions));cardActions.clear();auto target=text(clipPreferences,L"target");for(size_t i=0;i<clipLanguages.size();++i)if(clipLanguages[i]==target)c->languageIndex=int(i);c->language.Content(box_value(clipLanguageNames[c->languageIndex]));c->language.MinWidth(140);c->language.HorizontalAlignment(HorizontalAlignment::Left);uid(c->language,L"clipboard-language-"+id);c->languageClick=c->language.Click([this,c](auto&&,auto&&){clipChooseLanguage(c);});c->body.Children().Append(c->language);
  add(L"快捷翻译（优先选中文字）",[this,id,c]{clipTranslate(id,c);},L"clipboard-translate");
  c->body.Children().Append(actionGrid(cardActions));cardActions.clear();for(auto s:{L"当前已保存原文",L"译文",L"原文和译文"})c->exportPart.Items().Append(box_value(s));c->exportPart.SelectedIndex(0);c->body.Children().Append(c->exportPart);
  add(L"导出 TXT / MD…",[this,id,c]{auto p=param(L"id",id);const wchar_t* parts[]={L"original",L"translation",L"both"};put(p,L"part",parts[std::max(0,c->exportPart.SelectedIndex())]);clipRun(L"export",p);},L"clipboard-export");
  c->body.Children().Append(actionGrid(cardActions));c->body.Children().Append(label(L"点击翻译才会处理文字。在线模式将所选文字发送到配置的服务器，支持简繁中文、英语、美式英语、日语、韩语、俄语、印地语和德语。离线仅支持简体中文与英文，失败时不自动联网。Ctrl+S 保存。",12));
  c->expanded=c->row.Expanding([this,id,c](auto&&,auto&&){settingsMotion.scan(c->row);clipExpand(id,c);});c->collapsed=c->row.Collapsed([this,c](auto&&,auto&&){++c->request;clipCancelTranslation();if(activeSettingsDialog)activeSettingsDialog.Hide();});c->edited=c->editor.TextChanged([this,c](auto&&,auto&&){++c->request;if(clipTranslating)clipCancelTranslation();});
  MenuFlyout menu;for(auto [name,op]:std::vector<std::pair<hstring,hstring>>{{L"复制已保存内容",L"copy"},{L"删除记录",L"delete"}}){MenuFlyoutItem item;item.Text(name);item.Click([this,id,op](auto&&,auto&&){clipRun(op,param(L"id",id),op==L"delete"?L"删除本条历史？":L"");});menu.Items().Append(item);}c->row.ContextFlyout(menu);return c;
 }
 fire_and_forget clipReload(hstring id,std::shared_ptr<ClipCard> c){auto life=get_strong();if(clipBusy)co_return;clipBusy=true;try{if(co_await clipConfirm(L"重新读取会放弃此条未保存草稿，是否继续？")){c->loaded=false;clipExpand(id,c);}}catch(hresult_error const& e){error(e.message());}clipBusy=false;}
 void clipRender(JsonObject r,hstring query){std::set<std::wstring> ids;std::vector<Expander> rows;
  for(auto value:r.GetNamedArray(L"entries")){auto e=value.GetObject();auto id=text(e,L"id");std::wstring key(id);ids.insert(key);auto it=clipCards.find(key);if(it==clipCards.end())it=clipCards.emplace(key,clipCreate(id)).first;auto c=it->second;c->summary.Text(text(e,L"text")+L"\n"+str(e.GetNamedNumber(L"characters"))+L" 字符 · "+text(e,L"source")+(e.GetNamedBoolean(L"modified")?L" · 已修改":L""));rows.push_back(c->row);}
  if(query.empty())std::erase_if(clipCards,[&](auto& pair){if(ids.contains(pair.first))return false;pair.second->dispose();return true;});
  auto items=clipList.Items();for(uint32_t i=0;i<rows.size();i++){if(i<items.Size()&&items.GetAt(i)==rows[i])continue;uint32_t old;if(items.IndexOf(rows[i],old))items.RemoveAt(old);items.InsertAt(i,rows[i]);}while(items.Size()>rows.size())items.RemoveAtEnd();
  if(rows.empty())clipNote.Text(query.empty()?L"暂无历史，复制文字后会在这里显示。":L"没有匹配的文字。");
 }
 fire_and_forget clipRefresh(){auto life=get_strong();if(closed||clipBusy||clipPolling||clipComposing)co_return;clipPolling=true;unsigned generation=clipGeneration;auto query=page==8?clipSearch.Text():hstring{};
  try{auto r=JsonObject::Parse(co_await clipboardClient.call(L"list",param(L"query",query)));if(!closed&&generation==clipGeneration){clipPreferences=r.GetNamedObject(L"preferences");
   if(!clipPrefsDirty){clipSyncing=true;for(auto& [key,c]:clipSwitches)c.IsOn(clipPreferences.GetNamedBoolean(key));clipMaximum.Value(clipPreferences.GetNamedNumber(L"maximum"));clipExcluded.Text(text(clipPreferences,L"excluded"));clipEndpoint.Text(text(clipPreferences,L"translationEndpoint"));for(size_t i=0;i<clipLanguages.size();++i)if(clipLanguages[i]==text(clipPreferences,L"target"))clipTarget.SelectedIndex(int(i));clipSyncing=false;}
   clipReset.Visibility(r.GetNamedBoolean(L"recoveryRequired")?Visibility::Visible:Visibility::Collapsed);if(!text(r,L"error").empty())clipNote.Text(text(r,L"error"));
   if(page==8&&(r.GetNamedNumber(L"revision")!=clipRevision||query!=clipLastQuery)){clipRender(r,query);clipRevision=r.GetNamedNumber(L"revision");clipLastQuery=query;}
   auto request=r.GetNamedNumber(L"viewRequest");if(request>clipSeen){clipSeen=request;selectPage(8);clipRevision=-1;}
  }}catch(hresult_error const& e){if(page==8)error(e.message());}clipPolling=false;if(page==8&&!closed&&!clipModelPolling&&!clipBusy)clipModelRefresh();
 }
 void clipboardPage(){auto& panel=contents[8];panel.Children().Append(label(L"文字历史只保存在当前设备。提示、监听、保存独立控制；默认保留 20 条。",14));
  std::vector<FrameworkElement> switches;
  std::vector<hstring> hints={L"记录新复制的文字",L"在灵动岛显示复制粘贴提示",L"重启后保留已保存的历史",L"退出程序时清除文字历史"};size_t hintIndex=0;
  for(auto [key,name]:std::vector<std::pair<hstring,hstring>>{{L"listening",L"监听文字剪贴板"},{L"show",L"显示复制粘贴提示"},{L"save",L"保存历史到本机"},{L"clearOnExit",L"退出时清空历史"}}){
   ToggleSwitch c;uid(c,L"clipboard-"+key);Automation::AutomationProperties::SetName(c,name);c.VerticalAlignment(VerticalAlignment::Center);c.Toggled([this](auto&&,auto&&){if(!clipSyncing)clipPrefsDirty=true;});clipSwitches.emplace(std::wstring(key),c);
   Grid row;row.ColumnSpacing(12);ColumnDefinition info;info.Width({1,GridUnitType::Star});row.ColumnDefinitions().Append(info);ColumnDefinition control;control.Width({1,GridUnitType::Auto});row.ColumnDefinitions().Append(control);
   auto description=stack();description.Spacing(4);description.Children().Append(label(name));auto hint=label(hints[hintIndex++],12);hint.Opacity(.75);description.Children().Append(hint);row.Children().Append(description);Grid::SetColumn(c,1);row.Children().Append(c);
   auto card=appearance::card();card.Padding({16,12,16,12});card.Child(row);switches.push_back(card);
  }panel.Children().Append(responsiveGrid(switches,2,280));
  clipMaximum.Header(box_value(L"历史上限（4–200）"));clipMaximum.Minimum(4);clipMaximum.Maximum(200);clipMaximum.Value(20);uid(clipMaximum,L"clipboard-maximum");clipMaximum.ValueChanged([this](auto&&,auto&&){if(!clipSyncing)clipPrefsDirty=true;});
  clipExcluded.Header(box_value(L"排除应用（进程名，用分号分隔）"));clipExcluded.MaxLength(4096);uid(clipExcluded,L"clipboard-excluded");clipExcluded.TextChanged([this](auto&&,auto&&){if(!clipSyncing)clipPrefsDirty=true;});panel.Children().Append(responsiveGrid({clipMaximum,clipExcluded},2,250));
  clipLanguageChoices(clipTarget);clipTarget.Header(box_value(L"默认翻译目标"));clipTarget.SelectionChanged([this](auto&&,auto&&){if(!clipSyncing)clipPrefsDirty=true;});panel.Children().Append(clipTarget);
  auto endpoint=button(L"其他翻译服务器地址",[this]{clipEditEndpoint();});uid(endpoint,L"clipboard-endpoint-open");endpoint.HorizontalAlignment(HorizontalAlignment::Left);panel.Children().Append(endpoint);
  clipModelButton=button(L"下载本地离线翻译模型",[this]{clipModelAction();});uid(clipModelButton,L"clipboard-translation-model");panel.Children().Append(clipModelButton);clipModelNote=label(L"正在读取本地模型状态…",12);panel.Children().Append(clipModelNote);clipModelProgress.Visibility(Visibility::Collapsed);panel.Children().Append(clipModelProgress);clipModelCancel=button(L"取消下载",[this]{clipCancelModel();});uid(clipModelCancel,L"clipboard-model-cancel");clipModelCancel.Visibility(Visibility::Collapsed);panel.Children().Append(clipModelCancel);ToggleSwitch offline;offline.Header(box_value(L"使用本地离线翻译（不发送文字到服务器）"));uid(offline,L"clipboard-translationOffline");offline.Toggled([this](auto&&,auto&&){if(!clipSyncing)clipPrefsDirty=true;});clipSwitches.emplace(L"translationOffline",offline);panel.Children().Append(offline);
  auto save=button(L"保存剪贴板偏好",[this]{clipSavePreferences();});uid(save,L"clipboard-preferences-save");panel.Children().Append(save);
  clipSearch.PlaceholderText(L"搜索完整文字");uid(clipSearch,L"clipboard-search");clipSearch.TextCompositionStarted([this](auto&&,auto&&){clipComposing=true;});clipSearch.TextCompositionEnded([this](auto&&,auto&&){clipComposing=false;++clipGeneration;clipRefresh();});clipSearch.TextChanged([this](auto&&,auto&&){++clipGeneration;if(!clipComposing)clipRefresh();});panel.Children().Append(clipSearch);
  auto remove=button(L"删除所选",[this]{JsonArray ids;for(auto& [key,c]:clipCards)if(c->selected.IsChecked().Value())ids.Append(JsonValue::CreateStringValue(key));if(!ids.Size())return;JsonObject p;p.Insert(L"ids",ids);clipRun(L"delete",p,L"删除所有选中的历史与草稿？系统剪贴板不变。");});uid(remove,L"clipboard-delete-selected");
  auto clear=button(L"清空历史",[this]{JsonObject p;put(p,L"confirmed",true);clipRun(L"clear",p,L"清空全部历史与草稿？系统剪贴板不变。");});uid(clear,L"clipboard-clear");panel.Children().Append(actionGrid({remove,clear},2,140));
  clipReset=button(L"备份并重置损坏历史",[this]{JsonObject p;put(p,L"confirmed",true);clipRun(L"reset-corrupt",p,L"备份损坏文件并创建空历史？");});clipReset.Visibility(Visibility::Collapsed);panel.Children().Append(clipReset);panel.Children().Append(clipNote);
  clipList.SelectionMode(ListViewSelectionMode::None);clipList.HorizontalContentAlignment(HorizontalAlignment::Stretch);clipList.Height(420);uid(clipList,L"clipboard-list");panel.Children().Append(clipList);clipModelRefresh();
 }
