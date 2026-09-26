#pragma once

// Container width is in XAML DIPs, so reflow also follows monitor scale changes.
// Keep the same controls alive across reflow: focus and bindings never get rebuilt.
inline Grid responsiveGrid(std::vector<FrameworkElement> items, int maximumColumns=2, double minimumCell=240) {
 Grid grid; grid.ColumnSpacing(12); grid.RowSpacing(12);
 for(int i=0;i<maximumColumns;++i){ColumnDefinition c;c.Width({1,GridUnitType::Star});grid.ColumnDefinitions().Append(c);}
 for(size_t i=0;i<items.size();++i){if(i%maximumColumns==0){RowDefinition r;r.Height({1,GridUnitType::Auto});grid.RowDefinitions().Append(r);}auto item=items[i];item.HorizontalAlignment(HorizontalAlignment::Stretch);Grid::SetRow(item,int(i)/maximumColumns);Grid::SetColumn(item,int(i)%maximumColumns);grid.Children().Append(item);}
 grid.SizeChanged([items,maximumColumns,minimumCell](winrt::Windows::Foundation::IInspectable const& sender,SizeChangedEventArgs const& e){
  auto g=sender.as<Grid>();int columns=std::clamp(int((e.NewSize().Width+12)/(minimumCell+12)),1,maximumColumns);
  if(maximumColumns==4&&columns==3)columns=2;
  auto rows=(items.size()+columns-1)/columns;
  while(g.RowDefinitions().Size()>rows)g.RowDefinitions().RemoveAtEnd();
  while(g.RowDefinitions().Size()<rows){RowDefinition row;row.Height({1,GridUnitType::Auto});g.RowDefinitions().Append(row);}
  for(int i=0;i<maximumColumns;++i)g.ColumnDefinitions().GetAt(i).Width(i<columns?GridLength{1,GridUnitType::Star}:GridLength{0,GridUnitType::Pixel});
  for(size_t i=0;i<items.size();++i){Grid::SetRow(items[i],int(i)/columns);Grid::SetColumn(items[i],int(i)%columns);}
 });return grid;
}

inline Grid actionGrid(std::vector<Button> buttons,int maximumColumns=2,double minimumCell=160){
 std::vector<FrameworkElement> items;
 for(auto b:buttons){auto text=b.Content().try_as<winrt::Windows::Foundation::IPropertyValue>();if(text){TextBlock t;t.Text(text.GetString());t.TextWrapping(TextWrapping::Wrap);t.TextAlignment(TextAlignment::Center);b.Content(t);}b.MinHeight(36);b.VerticalAlignment(VerticalAlignment::Stretch);items.push_back(b);}
 return responsiveGrid(std::move(items),maximumColumns,minimumCell);
}

inline ScrollViewer findScroll(DependencyObject const& parent){
 if(auto s=parent.try_as<ScrollViewer>())return s;
 for(int i=0;i<Media::VisualTreeHelper::GetChildrenCount(parent);++i)if(auto s=findScroll(Media::VisualTreeHelper::GetChild(parent,i)))return s;
 return nullptr;
}

// Retain ContentDialog's Fluent template and focus states, but suppress its
// entrance/exit transitions for the application's manual reduced-motion mode.
inline void reduceDialogTransitions(DependencyObject const& parent){
 if(auto element=parent.try_as<FrameworkElement>())for(auto group:VisualStateManager::GetVisualStateGroups(element))if(group.Name()==L"DialogShowingStates")group.Transitions().Clear();
 for(int i=0;i<Media::VisualTreeHelper::GetChildrenCount(parent);++i)reduceDialogTransitions(Media::VisualTreeHelper::GetChild(parent,i));
}
