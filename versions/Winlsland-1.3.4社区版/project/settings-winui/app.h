#pragma once
#include <shobjidl.h>
#include <map>
#include <limits>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <functional>
#include <cwctype>
#include <winrt/Windows.UI.ViewManagement.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Composition.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include "clients.h"
#include "../source/src/product_version.h"
#include "fields.h"
#include "appearance.h"
#include "layout.h"
#include "motion.h"
using namespace winrt::Windows::Data::Json;
using client::put;using client::text;
static hstring str(double n){auto s=std::to_wstring(n);while(s.size()>1&&s.back()==L'0')s.pop_back();if(s.back()==L'.')s.pop_back();return hstring(s);}
static hstring lower(hstring s){std::wstring v(s);for(auto& c:v)c=towlower(c);return hstring(v);}
static void uid(DependencyObject const& o,hstring s){Automation::AutomationProperties::SetAutomationId(o,s);}
static TextBlock label(hstring s,double size=14){TextBlock t;t.Text(s);t.FontSize(size);t.TextWrapping(TextWrapping::Wrap);t.IsTextSelectionEnabled(true);return t;}
static StackPanel stack(){StackPanel p;p.Spacing(12);return p;}
static Button button(hstring name,std::function<void()> fn){Button b;b.Content(box_value(name));b.Click([fn](auto&&,auto&&){fn();});return b;}
struct SettingsApp:ApplicationT<SettingsApp,Markup::IXamlMetadataProvider>{
 XamlTypeInfo::XamlControlsXamlMetaDataProvider provider;
 client::Transport transport{g_pipe,g_token};client::SettingsClient settingsClient{transport};client::PluginClient pluginClient{transport};client::TransferClient transferClient{transport};
 Window window{nullptr};Grid root,body,navFrame;NavigationView nav;Border navIndicator{nullptr};NavigationViewItemBase selectedNavItem{nullptr};InfoBar status;ScrollViewer pages[10];StackPanel contents[10];
 Grid contentHost;Border modSettingsLayer{nullptr};Grid modSettingsPanel;bool modSettingsOpen=false;hstring modSettingsKey;
 unsigned modSettingsGeneration=0;weak_ref<Control> modReturnFocus;double modPageOffset=0,modListOffset=0;
 std::map<std::wstring,Button> modSettingsButtons;std::map<std::wstring,TextBlock> modStatus;
 SettingsMotion settingsMotion;bool motionQueued=false,blurDisabled=false;ElementTheme beforeOpaqueTheme=ElementTheme::Default;ResourceDictionary opaqueResources{nullptr};
 int animationReturnPage=0;double animationReturnOffset=0;weak_ref<Control> animationReturnFocus;
 ContentDialog activeSettingsDialog{nullptr};
 appearance::Material material;ToggleSwitch themeSwitch;Button systemTheme{nullptr};bool syncingTheme=false;Grid titleRow,titleDrag;
 ListView modList,transferList;TextBox modSearch,transferSearch;TextBlock modNote,transferNote,mediaNote,diagNote;
 ToggleSwitch transferEnabled,sideAuto,saveMode,remember;StackPanel modePanel;
 std::map<std::wstring,Control> controls;std::vector<hstring> displays;
 std::map<std::wstring,JsonObject> mods,entries;JsonArray resources;JsonObject settings,caps,media;
 std::map<std::wstring,Expander> modCards,fileCards;std::map<std::wstring,TextBlock> modHeaders,fileHeaders;
 std::map<std::wstring,StackPanel> modDetails,fileDetails;
 double revision=0;int page=0,previous=4;bool syncing=false,busy=false,closed=false,polling=false,dragging=false,reduced=false,loaded=false;
 DispatcherTimer timer{nullptr};HWND hwnd{};bool indicatorQueued=false;bool indicatorPlaced=false;
 ContentDialog clipLoadingDialog{nullptr};bool clipLoadingVisible=false;bool clipLoadingCancel=false;bool clipEndpointDialogOpen=false;
 Markup::IXamlType GetXamlType(Windows::UI::Xaml::Interop::TypeName const& t){return provider.GetXamlType(t);}
 Markup::IXamlType GetXamlType(hstring const& t){return provider.GetXamlType(t);}
 com_array<Markup::XmlnsDefinition> GetXmlnsDefinitions(){return provider.GetXmlnsDefinitions();}
 void error(hstring s){if(closed)return;status.Severity(InfoBarSeverity::Error);status.Title(L"操作未确认");status.Message(s);status.IsOpen(true);}
 void lock(bool value){busy=value;for(auto& [key,c]:controls)c.IsEnabled(!value);}
 JsonObject param(wchar_t const* k,hstring v){JsonObject p;put(p,k,v);return p;}
 Windows::UI::Color opaqueThemeColor() const {
  return root.ActualTheme()==ElementTheme::Dark ? appearance::color(32,32,32)
                                                : appearance::color(243,243,243);
 }
 void refreshOpaqueResources(){
  if(!opaqueResources)return;
  auto brush=Media::SolidColorBrush(opaqueThemeColor());
  for(auto key:{L"SolidBackgroundFillColorBaseBrush",L"SolidBackgroundFillColorSecondaryBrush",L"AcrylicBackgroundFillColorDefaultBrush",L"AcrylicInAppFillColorDefaultBrush",L"ContentDialogBackground",L"MenuFlyoutPresenterBackground",L"ComboBoxDropDownBackground",L"ExpanderContentBackground",L"LayerFillColorDefaultBrush",L"CardBackgroundFillColorDefaultBrush"})
   opaqueResources.Insert(box_value(key),brush);
 }
 void applyTheme(){
  if(closed||!window)return;
  if(blurDisabled){material.close();refreshOpaqueResources();root.Background(Media::SolidColorBrush(opaqueThemeColor()));}
  else material.apply(window,root);
  if(modSettingsLayer)modSettingsLayer.Background(Media::SolidColorBrush(blurDisabled?opaqueThemeColor():root.ActualTheme()==ElementTheme::Dark?appearance::color(32,32,32):appearance::color(243,243,243)));
  syncingTheme=true;themeSwitch.IsOn(root.ActualTheme()==ElementTheme::Dark);syncingTheme=false;
  themeSwitch.IsEnabled(true);systemTheme.IsEnabled(root.RequestedTheme()!=ElementTheme::Default);
  if(navIndicator)navIndicator.Background(Media::SolidColorBrush(appearance::color(root.ActualTheme()==ElementTheme::Dark?154:0,root.ActualTheme()==ElementTheme::Dark?124:99,root.ActualTheme()==ElementTheme::Dark?255:177)));
 }
 void updateBlur(){
  bool next=settings.GetNamedBoolean(L"settingsDisableBlur",false);if(next==blurDisabled){if(next)applyTheme();return;}blurDisabled=next;
  if(next){material.close();opaqueResources=ResourceDictionary{};refreshOpaqueResources();Resources().MergedDictionaries().Append(opaqueResources);root.Background(Media::SolidColorBrush(opaqueThemeColor()));}
  else{uint32_t index;if(opaqueResources&&Resources().MergedDictionaries().IndexOf(opaqueResources,index))Resources().MergedDictionaries().RemoveAt(index);opaqueResources=nullptr;material.attach(window);}
  applyTheme();
 }
 void scanMotion(){if(closed)return;settingsMotion.scan(root);if(root.XamlRoot())for(auto popup:Media::VisualTreeHelper::GetOpenPopupsForXamlRoot(root.XamlRoot())){settingsMotion.scan(popup.Child());}settingsMotion.prune();}
 void queueMotion(){if(!reduced||motionQueued||closed)return;motionQueued=true;window.DispatcherQueue().TryEnqueue([this]{motionQueued=false;if(!closed)scanMotion();});}
 void selectPage(int id){for(auto x:nav.MenuItems())if(auto item=x.try_as<NavigationViewItem>())if(unbox_value<int>(item.Tag())==id){nav.SelectedItem(item);return;}for(auto x:nav.FooterMenuItems())if(auto item=x.try_as<NavigationViewItem>())if(unbox_value<int>(item.Tag())==id){nav.SelectedItem(item);return;}}
 void returnFromAnimations(){if(page!=9)return;int target=animationReturnPage;selectPage(target);pages[target].ChangeView(nullptr,animationReturnOffset,nullptr,true);if(auto c=animationReturnFocus.get())c.Focus(FocusState::Programmatic);}
 void placeIndicator(bool animate=true){
  if(closed||!navIndicator||!navFrame.XamlRoot())return;
  auto item=selectedNavItem;if(!item)return;
  auto point=item.TransformToVisual(navFrame).TransformPoint({0,0});
  double height=item.ActualHeight();if(height<=0)return;
  float target=float(point.Y+std::max(0.0,(height-24.0)/2.0));
  auto visual=Hosting::ElementCompositionPreview::GetElementVisual(navIndicator);visual.StopAnimation(L"Offset");
  auto from=visual.Offset();float delta=target-from.y;
  if(!indicatorPlaced||!animate){visual.Offset({0,target,0});visual.Opacity(1);indicatorPlaced=true;return;}
  if(std::abs(delta)<0.5f){visual.Offset({0,target,0});visual.Opacity(1);return;}
  auto compositor=visual.Compositor();auto animation=compositor.CreateVector3KeyFrameAnimation();
  animation.InsertKeyFrame(0.0f,from);
  auto easing=compositor.CreateCubicBezierEasingFunction({0.20f,0.80f},{0.20f,1.0f});
  animation.InsertKeyFrame(1.0f,{0,target,0},easing);
  auto milliseconds=std::clamp(150.0+std::abs(double(delta))*0.72,150.0,360.0);
  animation.Duration(std::chrono::milliseconds(static_cast<int>(milliseconds)));
  visual.Opacity(1);visual.StartAnimation(L"Offset",animation);
 }
 void scheduleIndicator(bool animate=true){
  if(closed||indicatorQueued||!window)return;indicatorQueued=true;
  window.DispatcherQueue().TryEnqueue(winrt::Microsoft::UI::Dispatching::DispatcherQueueHandler([this,animate]{indicatorQueued=false;if(!closed)placeIndicator(animate);}));
 }
 void appearanceHeader(){
  titleRow.Height(56);titleRow.Padding({20,0,144,0});
  ColumnDefinition titleColumn;titleColumn.Width({1,GridUnitType::Star});titleRow.ColumnDefinitions().Append(titleColumn);ColumnDefinition actionsColumn;actionsColumn.Width({1,GridUnitType::Auto});titleRow.ColumnDefinitions().Append(actionsColumn);
  titleDrag.Background(Media::SolidColorBrush(appearance::color(0,0,0,0)));auto title=label(L"WinIsland · 设置",14);title.IsTextSelectionEnabled(false);title.VerticalAlignment(VerticalAlignment::Center);titleDrag.Children().Append(title);titleRow.Children().Append(titleDrag);
  auto actions=stack();actions.Orientation(Orientation::Horizontal);actions.Spacing(16);actions.VerticalAlignment(VerticalAlignment::Center);
  themeSwitch.OnContent(box_value(L"深色"));themeSwitch.OffContent(box_value(L"浅色"));uid(themeSwitch,L"appearance-dark");Automation::AutomationProperties::SetName(themeSwitch,L"深色模式");ToolTipService::SetToolTip(themeSwitch,box_value(L"切换设置窗口的浅色和深色外观"));
  themeSwitch.Toggled([this](auto&&,auto&&){if(syncingTheme)return;root.RequestedTheme(themeSwitch.IsOn()?ElementTheme::Dark:ElementTheme::Light);applyTheme();});actions.Children().Append(themeSwitch);
  systemTheme=button(L"跟随系统",[this]{root.RequestedTheme(ElementTheme::Default);applyTheme();});uid(systemTheme,L"appearance-system");actions.Children().Append(systemTheme);Grid::SetColumn(actions,1);titleRow.Children().Append(actions);
  root.Children().Append(titleRow);window.ExtendsContentIntoTitleBar(true);window.SetTitleBar(titleDrag);
  titleRow.SizeChanged([this](auto&&,auto&&){double scale=root.XamlRoot()?root.XamlRoot().RasterizationScale():1.0;titleRow.Padding({20,0,std::max(136.0,window.AppWindow().TitleBar().RightInset()/scale+16),0});});
  root.ActualThemeChanged([this](auto&&,auto&&){applyTheme();});window.Activated([this](auto&&,WindowActivatedEventArgs const& e){material.active(e.WindowActivationState()!=WindowActivationState::Deactivated);});
 }
 void motion(){
  try{
   bool next=settings.GetNamedBoolean(L"settingsReduceMotion",false)||!Windows::UI::ViewManagement::UISettings().AnimationsEnabled();
   if(next!=reduced){reduced=next;settingsMotion.set(reduced);if(reduced){for(auto p:pages){auto v=Hosting::ElementCompositionPreview::GetElementVisual(p);v.StopAnimation(L"Opacity");v.Opacity(1);}placeIndicator(false);if(modSettingsLayer){auto v=Hosting::ElementCompositionPreview::GetElementVisual(modSettingsLayer);v.StopAnimation(L"Opacity");v.Opacity(1);}}scanMotion();}
   updateBlur();
  }catch(...){
   // A stale visual or unavailable system UISettings must never terminate the
   // settings process while loading a persisted preference.
   bool persisted=false;try{persisted=settings.GetNamedBoolean(L"settingsReduceMotion",false);}catch(...){ }
   reduced=persisted;settingsMotion.set(reduced);
  }
 }
 void navigate(int n){if(n==9&&page!=9){animationReturnPage=page;animationReturnOffset=pages[page].VerticalOffset();if(auto f=Input::FocusManager::GetFocusedElement(root.XamlRoot()).try_as<Control>())animationReturnFocus=make_weak(f);}if(activeSettingsDialog&&page!=n)activeSettingsDialog.Hide();if(n!=8&&page==8)clipCancelTranslation();if(modSettingsOpen)closeModSettings();if(n>=5&&page<5)previous=page;page=n;for(int i=0;i<10;i++)pages[i].Visibility(i==n?Visibility::Visible:Visibility::Collapsed);
  auto v=Hosting::ElementCompositionPreview::GetElementVisual(pages[n]);v.StopAnimation(L"Opacity");if(!reduced){auto a=v.Compositor().CreateScalarKeyFrameAnimation();a.InsertKeyFrame(1,1);a.Duration(std::chrono::milliseconds(180));v.Opacity(.4f);v.StartAnimation(L"Opacity",a);}else v.Opacity(1);scheduleIndicator(!reduced);poll();clipRefresh();}
 fire_and_forget saveAppleCredentials(PasswordBox mediaToken,PasswordBox accessToken,TextBox storefront,TextBox language){
  auto life=get_strong();if(closed||busy||!loaded)co_return;lock(true);try{JsonObject patch;
    // Credentials are write-only. A blank editor preserves the encrypted value.
    if(!mediaToken.Password().empty())patch.Insert(L"appleMusicToken",JsonValue::CreateStringValue(mediaToken.Password()));
    if(!accessToken.Password().empty())patch.Insert(L"appleAccessToken",JsonValue::CreateStringValue(accessToken.Password()));
    patch.Insert(L"appleStorefront",JsonValue::CreateStringValue(storefront.Text().empty()?L"us":storefront.Text()));
    patch.Insert(L"appleLanguage",JsonValue::CreateStringValue(language.Text().empty()?L"en-US":language.Text()));
    auto r=JsonObject::Parse(co_await settingsClient.write(revision,patch));if(!closed){settings=r.GetNamedObject(L"settings");revision=r.GetNamedNumber(L"revision");status.IsOpen(false);}}
  catch(hresult_error const& e){error(e.message());}catch(...){error(L"Apple Music 配置保存失败");}if(!closed){lock(false);syncSettings();}}
 fire_and_forget openAppleMusicSettings(){
  auto life=get_strong();if(closed||activeSettingsDialog||busy)co_return;
  auto origin=Input::FocusManager::GetFocusedElement(root.XamlRoot()).try_as<Control>();
  try{auto d=clipDialog(L"Apple Music 设置");uid(d,L"apple-music-dialog");
  auto panel=stack();panel.Spacing(10);panel.Children().Append(label(L"Apple Music 歌词需要外部 Token。保存后由宿主加密保管，界面不会回显已保存内容。",12));
  PasswordBox mediaToken;mediaToken.MaxLength(8192);mediaToken.Header(box_value(L"Media User Token"));mediaToken.PasswordRevealMode(PasswordRevealMode::Hidden);mediaToken.PlaceholderText(settings.GetNamedBoolean(L"appleMusicTokenConfigured",false)?L"已配置（留空保持不变）":L"尚未配置");panel.Children().Append(mediaToken);
  PasswordBox accessToken;accessToken.MaxLength(8192);accessToken.Header(box_value(L"Access / Developer Token"));accessToken.PasswordRevealMode(PasswordRevealMode::Hidden);accessToken.PlaceholderText(settings.GetNamedBoolean(L"appleAccessTokenConfigured",false)?L"已配置（留空保持不变）":L"尚未配置");panel.Children().Append(accessToken);
  TextBox storefront;storefront.Header(box_value(L"地区 Storefront"));storefront.Text(settings.GetNamedString(L"appleStorefront",L"us"));storefront.MaxLength(16);panel.Children().Append(storefront);
  TextBox language;language.Header(box_value(L"歌词语言"));language.Text(settings.GetNamedString(L"appleLanguage",L"en-US"));language.MaxLength(32);panel.Children().Append(language);
  ScrollViewer scroll;scroll.Content(panel);scroll.MaxHeight(std::max(100.,root.ActualHeight()-240));
  auto sizing=root.SizeChanged([scroll](auto&&,SizeChangedEventArgs const& e){scroll.MaxHeight(std::max(100.,double(e.NewSize().Height)-240));});
  d.Content(scroll);d.PrimaryButtonText(L"保存");d.CloseButtonText(L"返回");d.Opened([mediaToken](auto&&,auto&&){mediaToken.Focus(FocusState::Programmatic);});
  ContentDialogResult result;try{result=co_await showSettingsDialog(d);}catch(...){root.SizeChanged(sizing);throw;}root.SizeChanged(sizing);
  if(!closed&&result==ContentDialogResult::Primary)saveAppleCredentials(mediaToken,accessToken,storefront,language);
  }catch(hresult_error const& e){if(!closed)error(e.message());}catch(...){if(!closed)error(L"无法打开 Apple Music 设置，请重试。");}
  if(!closed&&origin)origin.Focus(FocusState::Programmatic);
 }
 void syncSettings(){if(!loaded)return;syncing=true;for(auto const& f:fields){auto c=controls.at(f.key);auto v=settings.GetNamedValue(f.key);if(f.type==0)c.as<ToggleSwitch>().IsOn(v.GetBoolean());else if(f.type==1)c.as<NumberBox>().Value(v.GetNumber());else if(f.type==2)c.as<TextBox>().Text(v.GetString());else if(f.type==5){c.as<PasswordBox>().Password(L"");c.as<PasswordBox>().PlaceholderText(settings.GetNamedBoolean(hstring(f.key)+L"Configured",false)?L"已配置（不显示内容）":L"尚未配置");}else if(f.type==3)c.as<ComboBox>().SelectedIndex(int(v.GetNumber()));else{auto it=std::find(displays.begin(),displays.end(),v.GetString());c.as<ComboBox>().SelectedIndex(it==displays.end()?0:int(it-displays.begin()));}}motion();syncing=false;}
 fire_and_forget save(hstring key,IJsonValue value){auto life=get_strong();if(syncing||closed||!loaded)co_return;if(busy){syncSettings();co_return;}lock(true);hstring failure;
  try{JsonObject patch;patch.Insert(key,value);auto r=JsonObject::Parse(co_await settingsClient.write(revision,patch));if(!closed){settings=r.GetNamedObject(L"settings");revision=r.GetNamedNumber(L"revision");status.IsOpen(false);}}
  catch(hresult_error const& e){failure=e.message();}
  if(!failure.empty()){error(failure);try{auto r=JsonObject::Parse(co_await settingsClient.read());settings=r.GetNamedObject(L"settings");revision=r.GetNamedNumber(L"revision");}catch(...){}}
  if(!closed){syncSettings();lock(false);}
 }
 fire_and_forget execute(hstring command,JsonObject p=JsonObject{}){auto life=get_strong();if(busy||closed)co_return;lock(true);try{auto r=JsonObject::Parse(co_await transport.call(command,p));if(closed)co_return;
   if(command==L"mods.log"){ContentDialog d;d.XamlRoot(root.XamlRoot());d.Title(box_value(L"插件日志"));ScrollViewer scroll;scroll.MaxHeight(450);scroll.Content(label(text(r,L"text")));d.Content(scroll);d.CloseButtonText(L"关闭");co_await d.ShowAsync();}
   else if(command==L"layout.reset"){settings=r.GetNamedObject(L"settings");revision=r.GetNamedNumber(L"revision");syncSettings();}status.IsOpen(false);
  }catch(hresult_error const& e){error(e.message());}catch(...){error(L"操作失败，请刷新状态");}if(!closed){lock(false);poll();}}
 void field(Field const& f){
  auto grid=Grid{};grid.ColumnSpacing(24);grid.RowSpacing(12);
  ColumnDefinition infoColumn;infoColumn.Width({1,GridUnitType::Star});grid.ColumnDefinitions().Append(infoColumn);
  ColumnDefinition controlColumn;controlColumn.Width({1,GridUnitType::Auto});grid.ColumnDefinitions().Append(controlColumn);
  for(int i=0;i<2;i++){RowDefinition row;row.Height({1,GridUnitType::Auto});grid.RowDefinitions().Append(row);}
  auto description=stack();description.Spacing(4);description.VerticalAlignment(VerticalAlignment::Center);description.Children().Append(label(f.name,14));
  if(*f.hint){auto hint=label(f.hint,12);hint.Opacity(.75);description.Children().Append(hint);}grid.Children().Append(description);
  auto input=stack();input.Spacing(8);input.VerticalAlignment(VerticalAlignment::Center);hstring key(f.key);
  if(f.type==0){ToggleSwitch c;uid(c,key);Automation::AutomationProperties::SetName(c,f.name);c.Toggled([this,key,c](auto&&,auto&&){save(key,JsonValue::CreateBooleanValue(c.IsOn()));});controls.insert_or_assign(f.key,c);input.Children().Append(c);}
  else if(f.type==1){NumberBox c;c.Minimum(f.min);c.Maximum(f.max);c.SpinButtonPlacementMode(NumberBoxSpinButtonPlacementMode::Inline);c.HorizontalAlignment(HorizontalAlignment::Stretch);uid(c,key);Automation::AutomationProperties::SetName(c,f.name);controls.insert_or_assign(f.key,c);input.Children().Append(c);input.Children().Append(button(L"保存",[this,key,c]{save(key,JsonValue::CreateNumberValue(c.Value()));}));}
  else if(f.type==2){TextBox c;c.MaxLength(4096);uid(c,key);Automation::AutomationProperties::SetName(c,f.name);controls.insert_or_assign(f.key,c);input.Children().Append(c);input.Children().Append(button(L"保存",[this,key,c]{save(key,JsonValue::CreateStringValue(c.Text()));}));}
  else if(f.type==5){PasswordBox c;c.MaxLength(8192);c.PasswordRevealMode(PasswordRevealMode::Hidden);uid(c,key);Automation::AutomationProperties::SetName(c,f.name);controls.insert_or_assign(f.key,c);input.Children().Append(c);input.Children().Append(button(L"保存",[this,key,c]{save(key,JsonValue::CreateStringValue(c.Password()));}));}
  else{ComboBox c;c.HorizontalAlignment(HorizontalAlignment::Stretch);uid(c,key);Automation::AutomationProperties::SetName(c,f.name);for(auto& option:f.options)c.Items().Append(box_value(option));controls.insert_or_assign(f.key,c);input.Children().Append(c);c.SelectionChanged([this,key,c,type=f.type](auto&&,auto&&){if(syncing||c.SelectedIndex()<0)return;if(type==4)save(key,JsonValue::CreateStringValue(displays.at(c.SelectedIndex())));else save(key,JsonValue::CreateNumberValue(c.SelectedIndex()));});}
  input.Width(f.type==0?112:240);Grid::SetColumn(input,1);grid.Children().Append(input);
  grid.SizeChanged([input,type=f.type](auto&&,SizeChangedEventArgs const& e){bool narrow=e.NewSize().Width<540;Grid::SetColumn(input,narrow?0:1);Grid::SetRow(input,narrow?1:0);Grid::SetColumnSpan(input,narrow?2:1);input.Width(narrow?std::numeric_limits<double>::quiet_NaN():(type==0?112:240));input.HorizontalAlignment(narrow?HorizontalAlignment::Stretch:HorizontalAlignment::Right);});
  auto border=appearance::card();border.Child(grid);contents[f.page].Children().Append(border);
 }
 std::vector<hstring> pick(bool multiple,wchar_t const* filter=L"*.*"){
  com_ptr<IFileOpenDialog> dialog;check_hresult(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(dialog.put())));DWORD flags=0;dialog->GetOptions(&flags);dialog->SetOptions(flags|FOS_FORCEFILESYSTEM|FOS_FILEMUSTEXIST|(multiple?FOS_ALLOWMULTISELECT:0));COMDLG_FILTERSPEC spec{L"文件",filter};dialog->SetFileTypes(1,&spec);
  auto hr=dialog->Show(hwnd);if(hr==HRESULT_FROM_WIN32(ERROR_CANCELLED))return {};check_hresult(hr);com_ptr<IShellItemArray> result;check_hresult(dialog->GetResults(result.put()));DWORD n=0;result->GetCount(&n);std::vector<hstring> paths;for(DWORD i=0;i<n;i++){com_ptr<IShellItem> item;result->GetItemAt(i,item.put());PWSTR path{};check_hresult(item->GetDisplayName(SIGDN_FILESYSPATH,&path));paths.emplace_back(path);CoTaskMemFree(path);}return paths;
 }
 fire_and_forget importFiles(std::vector<hstring> paths){auto life=get_strong();if(paths.empty()||busy||closed)co_return;lock(true);try{for(size_t i=0;i<paths.size();){JsonArray a;size_t chars=0;while(i<paths.size()&&a.Size()<128&&chars+paths[i].size()<12000){chars+=paths[i].size();a.Append(JsonValue::CreateStringValue(paths[i++]));}if(!a.Size())throw hresult_invalid_argument(L"路径过长，无法通过设置协议提交");JsonObject p;p.Insert(L"paths",a);co_await transferClient.call(L"import",p);}}catch(hresult_error const& e){error(e.message());}if(!closed){lock(false);poll();}}
 void selectFiles(bool plugin){try{auto paths=pick(!plugin,plugin?L"*.wimod":L"*.*");if(paths.empty())return;if(plugin)execute(L"mods.import",param(L"path",paths[0]));else importFiles(std::move(paths));}catch(hresult_error const& e){error(e.message());}}
 fire_and_forget drop(DragEventArgs e){auto life=get_strong();auto deferral=e.GetDeferral();try{if(!dragging&&e.DataView().Contains(Windows::ApplicationModel::DataTransfer::StandardDataFormats::StorageItems())){auto items=co_await e.DataView().GetStorageItemsAsync();std::vector<hstring> paths;for(auto item:items)if(item.IsOfType(Windows::Storage::StorageItemTypes::File))paths.push_back(item.Path());if(!closed)importFiles(std::move(paths));}}catch(hresult_error const& err){error(err.message());}deferral.Complete();}
 fire_and_forget choose(){auto life=get_strong();if(busy)co_return;lock(true);try{JsonArray ids;for(auto& [key,e]:entries)if(text(e,L"state")==L"pending")ids.Append(JsonValue::CreateStringValue(key));for(uint32_t i=0;i<ids.Size();){JsonArray batch;while(i<ids.Size()&&batch.Size()<128)batch.Append(ids.GetAt(i++));JsonObject p;p.Insert(L"ids",batch);put(p,L"mode",saveMode.IsOn()?L"saved":L"reference");put(p,L"remember",remember.IsOn());co_await transferClient.call(L"choose",p);}}catch(hresult_error const& e){error(e.message());}if(!closed){lock(false);poll();}}
 fire_and_forget pluginAction(hstring action,hstring key){auto life=get_strong();if(busy)co_return;lock(true);try{bool cascade=false;if(action==L"disable"||action==L"unload"||action==L"reload"){auto affected=JsonObject::Parse(co_await transport.call(L"mods.affected",param(L"id",key)));auto ids=affected.GetNamedArray(L"ids");if(ids.Size()>1){ContentDialog d;d.XamlRoot(root.XamlRoot());d.Title(box_value(L"此操作会影响依赖模组"));hstring list;for(auto x:ids)list=list+x.GetString()+L"\n";d.Content(label(list));d.PrimaryButtonText(L"同时停止依赖模组");d.CloseButtonText(L"取消");cascade=(co_await d.ShowAsync())==ContentDialogResult::Primary;if(!cascade){lock(false);co_return;}}}co_await pluginClient.action(action,key,cascade);}catch(hresult_error const& e){error(e.message());}if(!closed){lock(false);poll();}}
 bool hasModSettings(hstring key){for(auto value:resources)if(text(value.GetObject(),L"owner")==key)return true;return false;}
 void appendModResourceSettings(hstring key,StackPanel panel){
  for(auto value:resources){auto r=value.GetObject();if(text(r,L"owner")!=key)continue;
   auto kind=int(r.GetNamedNumber(L"kind")),type=int(r.GetNamedNumber(L"type"));auto handle=text(r,L"handle");
   if(kind==2){panel.Children().Append(button(text(r,L"label"),[this,handle]{auto p=param(L"handle",handle);put(p,L"value",L"");execute(L"mods.invoke",p);}));continue;}
   panel.Children().Append(label(text(r,L"label"),13));
   std::function<hstring()> get;Control input=nullptr;
   if(type==3){ToggleSwitch c;c.IsOn(text(r,L"value")==L"1");get=[c]{return hstring(c.IsOn()?L"1":L"0");};input=c;}
   else if(type==4){ComboBox c;for(auto x:r.GetNamedArray(L"choices"))c.Items().Append(box_value(x.GetString()));c.SelectedItem(box_value(text(r,L"value")));get=[c]{return c.SelectedItem()?unbox_value<hstring>(c.SelectedItem()):hstring{};};input=c;}
   else{TextBox c;c.Text(text(r,L"value"));c.MaxLength(4096);get=[c]{return c.Text();};input=c;}
   uid(input,L"mod-input-"+text(r,L"key"));Automation::AutomationProperties::SetName(input,text(r,L"label"));input.HorizontalAlignment(HorizontalAlignment::Stretch);panel.Children().Append(input);panel.Children().Append(button(L"保存模组设置",[this,handle,get]{auto p=param(L"handle",handle);put(p,L"value",get());execute(L"mods.invoke",p);}));
  }
 }
 void openModSettings(hstring key){
  if(closed||!modSettingsLayer||modSettingsOpen)return;
  auto found=mods.find(std::wstring(key));if(found==mods.end()){error(L"模组已移除，请刷新列表");return;}
  ++modSettingsGeneration;modSettingsOpen=true;modSettingsKey=key;
  modPageOffset=pages[5].VerticalOffset();if(auto scroll=findScroll(modList))modListOffset=scroll.VerticalOffset();
  if(auto focus=Input::FocusManager::GetFocusedElement(root.XamlRoot()).try_as<Control>())modReturnFocus=make_weak(focus);
  modSettingsPanel.Children().Clear();modSettingsPanel.RowDefinitions().Clear();modSettingsPanel.Margin({28,20,28,28});modSettingsPanel.RowSpacing(16);
  RowDefinition top;top.Height({1,GridUnitType::Auto});modSettingsPanel.RowDefinitions().Append(top);modSettingsPanel.RowDefinitions().Append(RowDefinition{});
  auto back=button(L"返回插件管理",[this]{closeModSettings();});uid(back,L"mod-settings-back");
  auto title=label(L"模组设置 · "+text(found->second,L"name"),24);title.VerticalAlignment(VerticalAlignment::Center);
  modSettingsPanel.Children().Append(responsiveGrid({title,back},2,240));
  ScrollViewer scroll;scroll.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);scroll.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
  auto inner=stack();
  try{appendModResourceSettings(key,inner);if(!hasModSettings(key))inner.Children().Append(label(L"此模组没有提供可配置项，启用后可刷新查看。"));}
  catch(hresult_error const& e){inner.Children().Append(label(L"模组设置加载失败："+e.message()));}
  scroll.Content(inner);Grid::SetRow(scroll,1);modSettingsPanel.Children().Append(scroll);
  bool wasVisible=modSettingsLayer.Visibility()==Visibility::Visible;
  modSettingsLayer.Visibility(Visibility::Visible);nav.IsEnabled(false);
  auto v=Hosting::ElementCompositionPreview::GetElementVisual(modSettingsLayer);
  if(reduced){v.StopAnimation(L"Opacity");v.Opacity(1);}else{if(!wasVisible)v.Opacity(0);auto a=v.Compositor().CreateScalarKeyFrameAnimation();a.InsertKeyFrame(1,1);a.Duration(std::chrono::milliseconds(180));v.StartAnimation(L"Opacity",a);}
  back.Focus(FocusState::Programmatic);
 }
 fire_and_forget closeModSettings(){
  auto life=get_strong();winrt::apartment_context ui;if(!modSettingsOpen||closed)co_return;
  modSettingsOpen=false;auto generation=++modSettingsGeneration;auto v=Hosting::ElementCompositionPreview::GetElementVisual(modSettingsLayer);
  if(!reduced){auto a=v.Compositor().CreateScalarKeyFrameAnimation();a.InsertKeyFrame(1,0);a.Duration(std::chrono::milliseconds(180));v.StartAnimation(L"Opacity",a);co_await std::chrono::milliseconds(190);co_await ui;}
  if(closed||generation!=modSettingsGeneration)co_return;
  v.StopAnimation(L"Opacity");v.Opacity(1);modSettingsLayer.Visibility(Visibility::Collapsed);modSettingsPanel.Children().Clear();nav.IsEnabled(true);
  if(page==5){pages[5].ChangeView(nullptr,modPageOffset,nullptr,true);if(auto scroll=findScroll(modList))scroll.ChangeView(nullptr,modListOffset,nullptr,true);if(auto focus=modReturnFocus.get())focus.Focus(FocusState::Programmatic);}
  poll();
 }
 void details(bool plugin,hstring key){auto k=std::wstring(key);auto& data=plugin?mods:entries;auto it=data.find(k);if(it==data.end())return;auto e=it->second;auto panel=plugin?modDetails.at(k):fileDetails.at(k);panel.Children().Clear();
  std::vector<FrameworkElement> metadata;
  auto line=[&](wchar_t const* name,hstring value){auto t=label(hstring(name)+L"："+value);t.TextAlignment(TextAlignment::Left);t.HorizontalAlignment(HorizontalAlignment::Stretch);if(plugin)metadata.push_back(t);else panel.Children().Append(t);};
  if(plugin){for(auto pair:std::vector<std::pair<wchar_t const*,wchar_t const*>>{{L"ID",L"id"},{L"作者",L"author"},{L"模组版本",L"version"},{L"入口文件",L"entry"},{L"插件包",L"path"},{L"签名",L"signature"},{L"运行状态",L"status"},{L"说明",L"description"},{L"错误",L"error"}})line(pair.first,text(e,pair.second));line(L"API 版本",str(e.GetNamedNumber(L"apiVersion")));line(L"宿主 ABI",str(e.GetNamedNumber(L"hostAbi")));line(L"实际 ABI",str(e.GetNamedNumber(L"actualAbi")));
   for(auto d:e.GetNamedArray(L"dependencyDetails")){auto o=d.GetObject();line(L"依赖",text(o,L"id")+L" · "+text(o,L"required")+L" · "+text(o,L"reason"));}
   panel.Children().Append(responsiveGrid(metadata,2,240));std::vector<Button> actions;
   for(auto [op,name]:std::vector<std::pair<hstring,hstring>>{{L"enable",L"启用"},{L"disable",L"禁用"},{L"unload",L"卸载实例（保留包与配置）"},{L"reload",L"重新加载"},{L"reinstall",L"重装"}})actions.push_back(button(name,[this,op,key]{pluginAction(op,key);}));
   actions.push_back(button(L"查看日志",[this,key]{execute(L"mods.log",param(L"id",key));}));panel.Children().Append(actionGrid(actions));
  }else{for(auto pair:std::vector<std::pair<wchar_t const*,wchar_t const*>>{{L"完整文件名",L"name"},{L"格式",L"extension"},{L"原位置",L"original"},{L"保存位置",L"saved"},{L"状态",L"state"},{L"错误",L"error"}})line(pair.first,text(e,pair.second));line(L"大小（字节）",str(e.GetNamedNumber(L"size")));line(L"导入时间（Unix 秒）",str(e.GetNamedNumber(L"imported")));
   std::vector<Button> actions;
   for(bool original:{true,false}){auto b=button(original?L"打开原位置":L"打开保存位置",[this,key,original]{auto p=param(L"id",key);put(p,L"action",L"location");put(p,L"original",original);execute(L"transfer.call",p);});b.IsEnabled(original||!text(e,L"saved").empty());actions.push_back(b);}
   for(auto [op,name]:std::vector<std::pair<hstring,hstring>>{{L"cancel",L"取消复制"},{L"remove",L"从中转站移除（保留文件）"}})actions.push_back(button(name,[this,key,op]{auto p=param(L"id",key);put(p,L"action",op);execute(L"transfer.call",p);}));
   panel.Children().Append(actionGrid(actions,2,120));
   panel.Children().Append(button(L"重新定位原文件",[this,key]{try{auto paths=pick(false);if(paths.empty())return;auto p=param(L"id",key);put(p,L"path",paths[0]);put(p,L"action",L"relink");execute(L"transfer.call",p);}catch(hresult_error const& ex){error(ex.message());}}));
   panel.Children().Append(label(L"按住标题并移动可拖出真实文件，默认复制。",12));
  }
 }
 Expander card(bool plugin,hstring key){auto k=std::wstring(key);auto& cards=plugin?modCards:fileCards;if(cards.contains(k))return cards.at(k);Expander c;c.HorizontalAlignment(HorizontalAlignment::Stretch);c.HorizontalContentAlignment(HorizontalAlignment::Stretch);auto header=label(key,16);header.IsTextSelectionEnabled(false);c.Header(header);auto panel=stack();panel.Margin({8,8,8,8});c.Content(panel);uid(c,(plugin?L"mod-":L"file-")+key);
  if(plugin){
   modHeaders[k]=header;modDetails[k]=panel;auto sub=label(L"",12);modStatus[k]=sub;
   Grid summary;summary.ColumnSpacing(12);ColumnDefinition info;info.Width({1,GridUnitType::Star});summary.ColumnDefinitions().Append(info);ColumnDefinition action;action.Width({1,GridUnitType::Auto});summary.ColumnDefinitions().Append(action);
   auto textPanel=stack();textPanel.Spacing(4);for(auto t:{header,sub}){t.MaxLines(1);t.TextWrapping(TextWrapping::NoWrap);t.TextTrimming(TextTrimming::CharacterEllipsis);t.TextAlignment(TextAlignment::Left);t.IsTextSelectionEnabled(false);textPanel.Children().Append(t);}summary.Children().Append(textPanel);
   auto settingsButton=button(L"模组设置",[this,key]{openModSettings(key);});uid(settingsButton,L"mod-settings-"+key);settingsButton.VerticalAlignment(VerticalAlignment::Center);Grid::SetColumn(settingsButton,1);summary.Children().Append(settingsButton);modSettingsButtons[k]=settingsButton;c.Header(summary);
   c.SizeChanged([summary](auto&&,SizeChangedEventArgs const& e){summary.Width(std::max(120.0,double(e.NewSize().Width)-88));});
  }else{header.MaxLines(2);header.TextTrimming(TextTrimming::CharacterEllipsis);fileHeaders[k]=header;fileDetails[k]=panel;
   struct Press{uint64_t time=0;Windows::Foundation::Point point{};};auto press=std::make_shared<Press>();
   header.PointerPressed([press,header](auto&&,Input::PointerRoutedEventArgs const& e){if(e.GetCurrentPoint(header).Properties().IsLeftButtonPressed()){press->time=GetTickCount64();press->point=e.GetCurrentPoint(header).Position();}});
   header.PointerReleased([press](auto&&,auto&&){press->time=0;});header.PointerCanceled([press](auto&&,auto&&){press->time=0;});
   header.PointerMoved([this,press,header,key](auto&&,Input::PointerRoutedEventArgs const& e){auto p=e.GetCurrentPoint(header);if(!p.Properties().IsLeftButtonPressed()){press->time=0;return;}if(press->time&&GetTickCount64()-press->time>=400&&(abs(p.Position().X-press->point.X)>6||abs(p.Position().Y-press->point.Y)>6)){press->time=0;drag(key);}});
  }
  c.Expanding([this,plugin,key](auto&&,auto&&){details(plugin,key);});cards[k]=c;return c;
 }
 fire_and_forget drag(hstring key){auto life=get_strong();if(busy||dragging)co_return;dragging=true;try{co_await transferClient.call(L"drag",param(L"id",key));}catch(hresult_error const& e){error(e.message());}dragging=false;}
 void filter(bool plugin){auto& data=plugin?mods:entries;auto list=plugin?modList:transferList;auto query=lower(plugin?modSearch.Text():transferSearch.Text());std::vector<hstring> matches;for(auto& [key,o]:data)if(query.empty()||std::wstring(lower(text(o,L"name")+L" "+hstring(key)+L" "+text(o,L"extension"))).find(std::wstring(query))!=std::wstring::npos)matches.emplace_back(key);
  bool same=list.Items().Size()==matches.size();if(same)for(uint32_t i=0;i<matches.size();i++)if(unbox_value<hstring>(list.Items().GetAt(i).as<FrameworkElement>().Tag())!=matches[i]){same=false;break;}
  if(!same){list.Items().Clear();for(auto& key:matches){auto c=card(plugin,key);c.Tag(box_value(key));list.Items().Append(c);}}
 }
 void headers(){for(auto& [key,o]:mods)if(modHeaders.contains(key)){
   auto first=text(o,L"name")+L" · "+(o.GetNamedBoolean(L"enabled",false)?L"已启用":L"已禁用")+L" · v"+text(o,L"version")+L" · "+text(o,L"author");
   auto deps=o.GetNamedArray(L"dependencyDetails");unsigned missing=0;for(auto dep:deps)if(!dep.GetObject().GetNamedBoolean(L"satisfied",false))++missing;
   hstring dependency=deps.Size()?(missing?L"依赖未满足 "+to_hstring(missing)+L" 项":L"依赖已满足 "+to_hstring(deps.Size())+L" 项"):L"无依赖";
   auto second=text(o,L"status")+L" · "+dependency+L" · 签名："+text(o,L"signature");
   if(o.GetNamedBoolean(L"pendingRestart",false))second=second+L" · 重启后生效";if(!text(o,L"error").empty())second=second+L" · "+text(o,L"error");
   modHeaders.at(key).Text(first);modStatus.at(key).Text(second);ToolTipService::SetToolTip(modHeaders.at(key),box_value(first));ToolTipService::SetToolTip(modStatus.at(key),box_value(second));
   auto settingsButton=modSettingsButtons.at(key);bool available=hasModSettings(hstring(key));settingsButton.IsEnabled(available);ToolTipService::SetToolTip(settingsButton,box_value(available?L"打开模组设置":L"此模组当前未提供可配置项"));
  }
  bool pending=false;for(auto& [key,o]:entries){auto state=text(o,L"state");pending|=state==L"pending";if(fileHeaders.contains(key))fileHeaders.at(key).Text(text(o,L"name")+L"\n"+text(o,L"extension")+L" · "+str(o.GetNamedNumber(L"size"))+L" B · "+(text(o,L"mode")==L"saved"?L"保存并中转":L"仅中转")+L" · "+state+(state==L"copying"?L" "+str(int(o.GetNamedNumber(L"progress")*100))+L"%":L""));}modePanel.Visibility(pending?Visibility::Visible:Visibility::Collapsed);
 }
 fire_and_forget poll(){auto life=get_strong();if(polling||closed||dragging||modSettingsOpen)co_return;polling=true;try{
   if(page==5){auto r=JsonObject::Parse(co_await pluginClient.list());if(closed)co_return;resources=r.GetNamedArray(L"resources");mods.clear();for(auto x:r.GetNamedArray(L"mods")){auto o=x.GetObject();mods[std::wstring(text(o,L"id"))]=o;}modNote.Text(r.GetNamedBoolean(L"busy")?L"宿主正在处理操作…":text(r,L"message"));if(!text(r,L"operationError").empty())error(text(r,L"operationError"));filter(true);headers();}
   else if(page==6){JsonObject p;put(p,L"interaction",true);auto r=JsonObject::Parse(co_await transferClient.call(L"list",p));if(closed)co_return;syncing=true;transferEnabled.IsOn(r.GetNamedBoolean(L"enabled"));sideAuto.IsOn(r.GetNamedObject(L"preferences").GetNamedBoolean(L"sideAutoExpand",false));syncing=false;entries.clear();for(auto x:r.GetNamedArray(L"entries")){auto o=x.GetObject();entries[std::wstring(text(o,L"id"))]=o;}transferNote.Text(text(r,L"error"));filter(false);headers();}
   else if(page==1||page==3){media=JsonObject::Parse(co_await transport.call(L"media.read"));if(!closed){mediaNote.Text(text(media,L"title")+L"\n"+text(media,L"artist")+L" · "+text(media,L"source")+L"\n"+text(media,L"lyricStatus")+L"\n"+text(media,L"lyric"));diagNote.Text(text(media,L"monitorMessage")+L"\n"+text(media,L"exportPath"));}}
  }catch(hresult_error const& e){error(e.message());}polling=false;
 }
 fire_and_forget load(){auto life=get_strong();lock(true);try{caps=JsonObject::Parse(co_await transport.call(L"capabilities.read"));auto r=JsonObject::Parse(co_await settingsClient.read());if(closed)co_return;settings=r.GetNamedObject(L"settings");revision=r.GetNamedNumber(L"revision");syncing=true;auto combo=controls.at(L"monitorDevice").as<ComboBox>();combo.Items().Append(box_value(L"自动选择"));displays.emplace_back(L"");for(auto x:caps.GetNamedArray(L"displays")){auto o=x.GetObject();displays.push_back(text(o,L"id"));combo.Items().Append(box_value(text(o,L"id")));}loaded=true;syncSettings();status.IsOpen(false);lock(false);}catch(hresult_error const& e){error(e.message());lock(true);}}
 #include "clipboard_page.inc"
 void lists(){
  modSearch.PlaceholderText(L"按名称或 ID 搜索插件");uid(modSearch,L"plugin-search");modSearch.TextChanged([this](auto&&,auto&&){filter(true);});contents[5].Children().Append(modSearch);
  auto import=button(L"导入插件",[this]{selectFiles(true);});uid(import,L"plugins-import");
  auto scan=button(L"扫描插件 / 模组",[this]{auto p=param(L"action",L"scan");put(p,L"id",L"");execute(L"mods.action",p);});uid(scan,L"plugins-scan");
  auto folder=button(L"打开插件目录",[this]{execute(L"mods.folder");});uid(folder,L"plugins-folder");auto back=button(L"返回设置",[this]{selectPage(previous);});uid(back,L"plugins-back");
  contents[5].Children().Append(actionGrid({import,scan,folder,back},4,130));contents[5].Children().Append(modNote);
  transferEnabled.Header(box_value(L"启用文件中转"));uid(transferEnabled,L"transfer-enabled");transferEnabled.Toggled([this](auto&&,auto&&){if(syncing)return;auto p=param(L"action",L"enable");put(p,L"enabled",transferEnabled.IsOn());execute(L"transfer.call",p);});
  sideAuto.Header(box_value(L"侧边吸附后自动展开"));sideAuto.Toggled([this](auto&&,auto&&){if(syncing)return;auto p=param(L"action",L"side-auto");put(p,L"enabled",sideAuto.IsOn());execute(L"transfer.call",p);});contents[6].Children().Append(responsiveGrid({transferEnabled,sideAuto},2,220));
  transferSearch.PlaceholderText(L"按文件名或扩展名搜索");uid(transferSearch,L"transfer-search");transferSearch.TextChanged([this](auto&&,auto&&){filter(false);});contents[6].Children().Append(transferSearch);
  auto addFiles=button(L"添加文件",[this]{selectFiles(false);});uid(addFiles,L"add-files");
  auto refresh=button(L"检查文件",[this]{execute(L"transfer.call",param(L"action",L"refresh"));});uid(refresh,L"transfer-refresh");
  auto forget=button(L"清除记住的模式",[this]{execute(L"transfer.call",param(L"action",L"forget"));});
  auto transferBack=button(L"返回设置",[this]{selectPage(previous);});
  contents[6].Children().Append(actionGrid({addFiles,refresh,forget,transferBack},4,130));
  modePanel=stack();saveMode.Header(box_value(L"保存并中转"));saveMode.OnContent(box_value(L"复制到数据目录，不移动原文件"));saveMode.OffContent(box_value(L"仅中转：只记录原位置"));remember.Header(box_value(L"记住此次选择"));modePanel.Children().Append(responsiveGrid({saveMode,remember},2,220));modePanel.Children().Append(button(L"确认导入",[this]{choose();}));contents[6].Children().Append(modePanel);contents[6].Children().Append(transferNote);
  for(bool plugin:{true,false}){auto list=plugin?modList:transferList;list.SelectionMode(ListViewSelectionMode::None);list.HorizontalContentAlignment(HorizontalAlignment::Stretch);list.Height(400);uid(list,plugin?L"plugin-list":L"transfer-list");contents[plugin?5:6].Children().Append(list);}
  contents[6].AllowDrop(true);contents[6].DragOver([this](auto&&,DragEventArgs const& e){if(!dragging&&e.DataView().Contains(Windows::ApplicationModel::DataTransfer::StandardDataFormats::StorageItems()))e.AcceptedOperation(Windows::ApplicationModel::DataTransfer::DataPackageOperation::Copy);});contents[6].Drop([this](auto&&,DragEventArgs e){drop(e);});
 }
 void OnLaunched(LaunchActivatedEventArgs const& args){
  try { buildWindow(args); }
  catch(hresult_error const& e){MessageBoxW(nullptr,e.message().c_str(),L"WinIsland WinUI 页面初始化失败",MB_ICONERROR);Exit();}
 }
 void buildWindow(LaunchActivatedEventArgs const&){
  Resources().MergedDictionaries().Append(XamlControlsResources{});window=Window{};window.Title(L"WinIsland · 设置");window.as<IWindowNative>()->get_WindowHandle(&hwnd);
  for(int i=0;i<2;i++){RowDefinition row;row.Height({1,GridUnitType::Auto});root.RowDefinitions().Append(row);}RowDefinition fill;fill.Height({1,GridUnitType::Star});root.RowDefinitions().Append(fill);appearanceHeader();status.IsClosable(true);Grid::SetRow(status,1);root.Children().Append(status);Grid::SetRow(body,2);root.Children().Append(body);
  appearance::configureNavigation(nav);nav.IsSettingsVisible(false);nav.IsBackButtonVisible(NavigationViewBackButtonVisible::Collapsed);nav.PaneDisplayMode(NavigationViewPaneDisplayMode::Auto);nav.OpenPaneLength(200);nav.CompactModeThresholdWidth(720);nav.ExpandedModeThresholdWidth(900);auto host=Grid{};nav.Content(host);contentHost=host;
  navFrame=Grid{};navFrame.Children().Append(nav);navIndicator=Border{};navIndicator.Width(3);navIndicator.Height(24);navIndicator.CornerRadius({2,2,2,2});navIndicator.HorizontalAlignment(HorizontalAlignment::Left);navIndicator.VerticalAlignment(VerticalAlignment::Top);navIndicator.Margin({4,0,0,0});navIndicator.IsHitTestVisible(false);navIndicator.Opacity(0);navFrame.Children().Append(navIndicator);body.Children().Append(navFrame);
  navFrame.SizeChanged([this](auto&&,auto&&){scheduleIndicator(false);});nav.SizeChanged([this](auto&&,auto&&){scheduleIndicator(false);});
  std::vector<hstring> names={L"界面与通知",L"音乐与歌词",L"实时信息",L"运行诊断",L"其他设置",L"插件管理",L"文件中转",L"关于此程序"};
  for(int i=0;i<7;i++){NavigationViewItem item;item.Content(box_value(names[i]));item.Icon(SymbolIcon(i==5?Symbol::Library:i==6?Symbol::Folder:i==1?Symbol::Audio:i==3?Symbol::Manage:Symbol::Setting));item.Tag(box_value(i));uid(item,L"nav-"+to_hstring(i));nav.MenuItems().Append(item);contents[i]=stack();contents[i].Margin({28,16,28,28});contents[i].MaxWidth(960);contents[i].Spacing(8);auto heading=label(names[i],28);heading.Margin({0,0,0,16});contents[i].Children().Append(heading);pages[i].Content(contents[i]);pages[i].HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);host.Children().Append(pages[i]);}
  NavigationViewItem animations;animations.Content(box_value(L"动画设置"));animations.Icon(SymbolIcon(Symbol::Setting));animations.Tag(box_value(9));uid(animations,L"nav-animations");nav.MenuItems().InsertAt(4,animations);
  contents[9]=stack();contents[9].Margin({28,16,28,28});contents[9].MaxWidth(960);contents[9].Children().Append(label(L"动画设置",28));auto animationBack=button(L"返回",[this]{returnFromAnimations();});uid(animationBack,L"animations-back");contents[9].Children().Append(animationBack);pages[9].Content(contents[9]);pages[9].HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);host.Children().Append(pages[9]);
  NavigationViewItem aboutItem;auto aboutLabel=stack();aboutLabel.Orientation(Orientation::Horizontal);aboutLabel.Spacing(8);auto aboutText=label(names[7]);aboutText.IsTextSelectionEnabled(false);aboutText.VerticalAlignment(VerticalAlignment::Center);aboutLabel.Children().Append(aboutText);FontIcon help;help.Glyph(L"\xE897");help.FontSize(14);help.VerticalAlignment(VerticalAlignment::Center);uid(help,L"about-help-icon");aboutLabel.Children().Append(help);aboutItem.Content(aboutLabel);Automation::AutomationProperties::SetName(aboutItem,names[7]);aboutItem.Tag(box_value(7));uid(aboutItem,L"nav-about");nav.FooterMenuItems().Append(aboutItem);
  contents[7]=stack();contents[7].Margin({28,16,28,28});contents[7].MaxWidth(960);contents[7].Spacing(8);auto aboutHeading=label(names[7],28);aboutHeading.Margin({0,0,0,16});contents[7].Children().Append(aboutHeading);auto aboutCard=appearance::card();auto aboutBody=stack();aboutBody.Spacing(10);aboutBody.Children().Append(label(L"WinIsland 社区版",20));aboutBody.Children().Append(label(hstring(L"版本：")+WI_VERSION_W,14));aboutBody.Children().Append(label(L"作者：daxian\n官方版作者：SeeLe\n联系方式：725513212@qq.com",14));aboutBody.Children().Append(label(L"© daxian · © SeeLe",13));aboutBody.Children().Append(label(L"WinIsland 是一款面向 Windows 桌面的轻量灵动岛工具，提供音乐、通知、插件与文件中转等功能。",13));aboutBody.Children().Append(label(L"歌曲识别与歌词解析基于 WXRIW/Lyricify-Lyrics-Helper，作者：WXRIW，Apache-2.0。",12));aboutCard.Child(aboutBody);contents[7].Children().Append(aboutCard);contents[7].Children().Append(button(L"返回设置",[this]{selectPage(0);}));pages[7].Content(contents[7]);pages[7].HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);host.Children().Append(pages[7]);
  NavigationViewItem clipboardItem;clipboardItem.Content(box_value(L"复制粘贴"));clipboardItem.Icon(SymbolIcon(Symbol::Paste));clipboardItem.Tag(box_value(8));uid(clipboardItem,L"nav-clipboard");nav.MenuItems().Append(clipboardItem);
  contents[8]=stack();contents[8].Margin({28,16,28,28});contents[8].MaxWidth(960);contents[8].Children().Append(label(L"复制粘贴",28));pages[8].Content(contents[8]);pages[8].HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);host.Children().Append(pages[8]);clipboardPage();
  auto clipboardEntry=button(L"复制粘贴",[this]{selectPage(8);});uid(clipboardEntry,L"clipboard-open");contents[8].Children().Append(button(L"返回设置",[this]{selectPage(previous);}));
  nav.SelectionChanged([this](auto&&,NavigationViewSelectionChangedEventArgs const& e){if(auto item=e.SelectedItemContainer()){selectedNavItem=item;navigate(unbox_value<int>(item.Tag()));}});for(auto& f:fields)field(f);
  auto appleButton=button(L"Apple Music 设置",[this]{openAppleMusicSettings();});uid(appleButton,L"apple-music-settings");ToolTipService::SetToolTip(appleButton,box_value(L"配置 Apple Music 歌词所需的授权和地区"));contents[1].Children().Append(appleButton);
  contents[1].Children().Append(mediaNote);contents[1].Children().Append(button(L"载入当前歌曲歌词…",[this]{try{auto files=pick(false,L"*.lrc");if(files.empty())return;auto p=param(L"path",files[0]);put(p,L"key",text(media,L"key"));execute(L"lyrics.import",p);}catch(hresult_error const& e){error(e.message());}}));contents[1].Children().Append(button(L"清除当前歌词",[this]{execute(L"lyrics.clear",param(L"key",text(media,L"key")));}));
  contents[3].Children().Append(label(L"每 5 秒采样性能与诊断信息，不记录消息正文、歌曲名称或凭据。"));contents[3].Children().Append(diagNote);for(auto [op,name]:std::vector<std::pair<hstring,hstring>>{{L"start",L"启动监测"},{L"stop",L"停止监测"},{L"export",L"导出日志"}})contents[3].Children().Append(button(name,[this,op]{execute(L"diagnostics.action",param(L"action",op));}));
  for(int n:{5,6}){auto b=button(names[n],[this,n]{selectPage(n);});uid(b,n==5?L"plugins-open":L"transfer-open");contents[4].Children().Append(b);}
  modSettingsLayer=Markup::XamlReader::Load(LR"(<Border xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" Background="{ThemeResource SolidBackgroundFillColorBaseBrush}" CornerRadius="12" />)").as<Border>();
  uid(modSettingsLayer,L"mod-settings-layer");modSettingsLayer.Visibility(Visibility::Collapsed);modSettingsLayer.Child(modSettingsPanel);body.Children().Append(modSettingsLayer);
  modSettingsLayer.KeyDown([this](auto&&,Input::KeyRoutedEventArgs const& e){if(e.Key()==Windows::System::VirtualKey::Escape){e.Handled(true);closeModSettings();}});
  contents[4].Children().Append(clipboardEntry);
  contents[4].Children().Append(button(L"恢复默认布局",[this]{JsonObject p;put(p,L"revision",revision);execute(L"layout.reset",p);}));
  nav.DisplayModeChanged([this](auto&&,NavigationViewDisplayModeChangedEventArgs const& e){for(auto content:contents)content.Margin({28,e.DisplayMode()==NavigationViewDisplayMode::Minimal?52.0:16.0,28,28});});
  lists();selectPage(0);window.Content(root);material.attach(window);applyTheme();window.AppWindow().Resize({1000,820});window.Closed([this](auto&&,auto&&){clipCancelTranslation();if(activeSettingsDialog)activeSettingsDialog.Hide();closed=true;++modSettingsGeneration;for(auto& [id,c]:clipCards)c->dispose();clipCards.clear();if(timer)timer.Stop();transport.stop();material.close();});window.Activate();
  timer=DispatcherTimer{};timer.Interval(std::chrono::milliseconds(1000));timer.Tick([this](auto&&,auto&&){if(!IsIconic(hwnd)&&IsWindowVisible(hwnd)){motion();poll();clipRefresh();}});timer.Start();load();
 }
};
