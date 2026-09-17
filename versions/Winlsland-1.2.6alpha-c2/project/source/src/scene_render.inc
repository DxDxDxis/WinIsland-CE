// Retained, host-owned draw data. Runs on the existing render thread on both D2D backends.
void Renderer::drawOpenScene(Scene& s,float left){
    std::vector<SceneNode> nodes=s.hostNodes;
    nodes.insert(nodes.end(),s.openPlan.created.begin(),s.openPlan.created.end());
    for(auto& n:nodes)n=s.openPlan.resolve(n);
    std::stable_sort(nodes.begin(),nodes.end(),[](auto& a,auto& b){return std::pair{a.n(WI_Z_ORDER),a.id}<std::pair{b.n(WI_Z_ORDER),b.id};});
    auto rgba=[](const SceneNode& n,uint32_t p,float alpha){return D2D1::ColorF((float)n.n(p,0),(float)n.n(p,1),(float)n.n(p,2),(float)n.n(p,3)*alpha);};
    std::set<WiElement> visiting;
    std::function<void(WiElement,D2D1::Matrix3x2F,float,float,float,Box)> visit;
    visit=[&](WiElement parent,D2D1::Matrix3x2F matrix,float opacity,float parentW,float parentH,Box inheritedClip){
        for(auto& n:nodes)if(n.parent()==parent){
            if(!visiting.insert(n.id).second)continue;
            if(n.n(WI_VISIBLE,0,1)==0){visiting.erase(n.id);continue;}
            float xx=(float)n.n(WI_RECT),yy=(float)n.n(WI_RECT,1);
            float w=(float)std::clamp(n.n(WI_RECT,2),n.n(WI_MIN_SIZE),std::max(n.n(WI_MIN_SIZE),n.n(WI_MAX_SIZE,0,16384)));
            float h=(float)std::clamp(n.n(WI_RECT,3),n.n(WI_MIN_SIZE,1),std::max(n.n(WI_MIN_SIZE,1),n.n(WI_MAX_SIZE,1,16384)));
            xx+=(float)(n.n(WI_ANCHOR)*(parentW-w)+n.n(WI_MARGIN));yy+=(float)(n.n(WI_ANCHOR,1)*(parentH-h)+n.n(WI_MARGIN,1));
            float a=opacity*(n.id==1?1.f:(float)n.n(WI_OPACITY,0,1));if(a<=0){visiting.erase(n.id);continue;}
            auto local=D2D1::Matrix3x2F::Scale((float)n.n(WI_SCALE,0,1),(float)n.n(WI_SCALE,1,1))*D2D1::Matrix3x2F::Rotation((float)n.n(WI_ROTATION),D2D1::Point2F(w/2,h/2))*D2D1::Matrix3x2F::Translation(xx,yy)*matrix;
            target->SetTransform(local);
            bool clip=n.n(WI_CLIP)!=0;if(clip)target->PushAxisAlignedClip(D2D1::RectF(0,0,w,h),D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            auto r=D2D1::RoundedRect(D2D1::RectF(0,0,w,h),(float)n.n(WI_RADIUS),(float)n.n(WI_RADIUS));
            if(n.n(WI_SHADOW,3)>0){brush->SetColor(D2D1::ColorF(0,0,0,(float)n.n(WI_SHADOW,3)*a));auto shadow=r;shadow.rect.left+=(float)n.n(WI_SHADOW);shadow.rect.right+=(float)n.n(WI_SHADOW);shadow.rect.top+=(float)n.n(WI_SHADOW,1);shadow.rect.bottom+=(float)n.n(WI_SHADOW,1);target->FillRoundedRectangle(shadow,brush.Get());}
            brush->SetColor(rgba(n,WI_BACKGROUND,a));target->FillRoundedRectangle(r,brush.Get());
            if(n.n(WI_BORDER_WIDTH)>0){brush->SetColor(rgba(n,WI_BORDER_COLOR,a));target->DrawRoundedRectangle(r,brush.Get(),(float)n.n(WI_BORDER_WIDTH));}
            auto type=(uint32_t)n.n(WI_ELEMENT_TYPE,0,n.type);
            if(type==WI_SCENE_BUTTON&&s.hover==(int)n.id){brush->SetColor(D2D1::ColorF(1,1,1,.10f*a));target->FillRoundedRectangle(r,brush.Get());}
            if(s.keyboard&&s.focus==(int)n.id&&n.n(WI_RECEIVE_INPUT)){brush->SetColor(D2D1::ColorF(.6f,.8f,1,a));target->DrawRoundedRectangle(r,brush.Get(),1);}
            if(type==WI_TEXT||type==WI_ICON||type==WI_SCENE_BUTTON||type==WI_INPUT){
                ComPtr<IDWriteTextFormat> format;auto family=wide(n.text(WI_FONT_FAMILY));if(family.empty())family=L"Microsoft YaHei UI";
                if(SUCCEEDED(write->CreateTextFormat(family.c_str(),nullptr,(DWRITE_FONT_WEIGHT)(int)n.n(WI_FONT_WEIGHT,0,400),DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,(float)n.n(WI_FONT_SIZE,0,14),L"zh-CN",&format))){
                    format->SetTextAlignment((DWRITE_TEXT_ALIGNMENT)std::clamp((int)n.n(WI_ALIGN),0,2));format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                    auto str=wide(n.text(WI_TEXT_VALUE));brush->SetColor(rgba(n,WI_TEXT_COLOR,a));
                    float px=(float)n.n(WI_PADDING),py=(float)n.n(WI_PADDING,1);
                    target->DrawText(str.c_str(),(UINT32)str.size(),format.Get(),D2D1::RectF(px,py,std::max(px,w-px),std::max(py,h-py)),brush.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);
                }
            }
            if(type==WI_PROGRESS||type==WI_SLIDER){brush->SetColor(rgba(n,WI_TEXT_COLOR,a));float value=(float)n.n(WI_VALUE);target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(0,h/2-2,w*value,h/2+2),2,2),brush.Get());if(type==WI_SLIDER)target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(w*value,h/2),6,6),brush.Get());}
            if(!n.pixels.empty()){
                ComPtr<ID2D1Bitmap> bitmap;auto props=D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));
                if(SUCCEEDED(target->CreateBitmap(D2D1::SizeU(n.pixelWidth,n.pixelHeight),n.pixels.data(),n.pixelWidth*4,props,&bitmap)))target->DrawBitmap(bitmap.Get(),D2D1::RectF(0,0,w,h),a);
            }
            ComPtr<ID2D1PathGeometry> path;ComPtr<ID2D1GeometrySink> pathSink;
            for(auto& d:n.draw){brush->SetColor(D2D1::ColorF(d.color[0],d.color[1],d.color[2],d.color[3]*a));auto rect=D2D1::RectF(d.x,d.y,d.x+d.width,d.y+d.height);
                if(d.kind==WI_PATH_BEGIN){path.Reset();pathSink.Reset();factory->CreatePathGeometry(&path);path->Open(&pathSink);pathSink->BeginFigure(D2D1::Point2F(d.x,d.y),D2D1_FIGURE_BEGIN_FILLED);}
                else if(d.kind==WI_PATH_LINE&&pathSink)pathSink->AddLine(D2D1::Point2F(d.x,d.y));
                else if(d.kind==WI_PATH_CUBIC&&pathSink)pathSink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(d.x,d.y),D2D1::Point2F(d.width,d.height),D2D1::Point2F(d.x2,d.y2)));
                else if(d.kind==WI_PATH_END&&pathSink){pathSink->EndFigure(d.reserved?D2D1_FIGURE_END_OPEN:D2D1_FIGURE_END_CLOSED);pathSink->Close();if(d.stroke>0)target->DrawGeometry(path.Get(),brush.Get(),d.stroke);else target->FillGeometry(path.Get(),brush.Get());pathSink.Reset();path.Reset();}
                else if(d.kind==WI_DRAW_RECT)target->FillRoundedRectangle(D2D1::RoundedRect(rect,d.radius,d.radius),brush.Get());
                else if(d.kind==WI_DRAW_ELLIPSE)target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(d.x+d.width/2,d.y+d.height/2),d.width/2,d.height/2),brush.Get());
                else if(d.kind==WI_DRAW_LINE)target->DrawLine(D2D1::Point2F(d.x,d.y),D2D1::Point2F(d.x2,d.y2),brush.Get(),std::max(.1f,d.stroke));
                else if(d.kind==WI_DRAW_TEXT)text(wide(d.text),d.x,d.y,d.width,d.height,std::max(1.f,d.fontSize),D2D1::ColorF(d.color[0],d.color[1],d.color[2],d.color[3]*a));
                else if(d.kind==WI_DRAW_TRIANGLE){ComPtr<ID2D1PathGeometry> g;ComPtr<ID2D1GeometrySink> sink;factory->CreatePathGeometry(&g);g->Open(&sink);sink->BeginFigure(D2D1::Point2F(d.x,d.y),D2D1_FIGURE_BEGIN_FILLED);sink->AddLine(D2D1::Point2F(d.x+d.width,d.y+d.height));sink->AddLine(D2D1::Point2F(d.x2,d.y2));sink->EndFigure(D2D1_FIGURE_END_CLOSED);sink->Close();target->FillGeometry(g.Get(),brush.Get());}
            }
            auto p0=local.TransformPoint({0,0}),p1=local.TransformPoint({w,0}),p2=local.TransformPoint({0,h}),p3=local.TransformPoint({w,h});
            float x0=std::min({p0.x,p1.x,p2.x,p3.x}),y0=std::min({p0.y,p1.y,p2.y,p3.y}),x1=std::max({p0.x,p1.x,p2.x,p3.x}),y1=std::max({p0.y,p1.y,p2.y,p3.y});
            x0=std::max(x0,inheritedClip.x);y0=std::max(y0,inheritedClip.y);x1=std::min(x1,inheritedClip.x+inheritedClip.w);y1=std::min(y1,inheritedClip.y+inheritedClip.h);
            x1=std::max(x0,x1);y1=std::max(y0,y1);
            auto displayed=n;displayed.values[WI_RECT]=sceneNumber(x0-left,y0,x1-x0,y1-y0);if(x1>x0&&y1>y0)s.visibleNodes.push_back(displayed);
            if(x1>x0&&y1>y0&&n.n(WI_RECEIVE_INPUT)!=0&&n.n(WI_ENABLED,0,1)!=0){auto inverse=local;if(inverse.Invert()){
                s.sceneHits.push_back({n.id,{x0,y0,x1-x0,y1-y0},inverse,w,h,n.n(WI_BLOCK_INPUT,0,1)!=0});
                s.buttons.emplace_back((int)n.id,Box{x0,y0,x1-x0,y1-y0});
            }}
            auto child= D2D1::Matrix3x2F::Translation((float)n.n(WI_PADDING),(float)n.n(WI_PADDING,1))*local;
            visit(n.id,child,a,w,h,clip?Box{x0,y0,x1-x0,y1-y0}:inheritedClip);target->SetTransform(local);if(clip)target->PopAxisAlignedClip();visiting.erase(n.id);
        }
    };
    visit(0,D2D1::Matrix3x2F::Translation(left,0),1,(float)s.width,(float)s.height,Box{left,0,(float)s.width,(float)s.height});
    std::erase_if(s.buttons,[](auto& button){return button.first==0;});
    for(auto& node:s.visibleNodes)if(node.id==10&&s.music){auto info=node.info();s.buttons.insert(s.buttons.begin(),{0,Box{left+info.x,info.y,info.width,std::min(info.height,48.384f*(float)s.contentScale)}});break;}
    target->SetTransform(D2D1::Matrix3x2F::Scale((float)s.contentScale,(float)s.contentScale));
}
