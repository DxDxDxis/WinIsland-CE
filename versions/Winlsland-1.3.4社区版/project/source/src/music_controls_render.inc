// Host button presentation. Scene IDs, transforms, clipping, input routing and
// accessibility remain on the existing scene graph; no extra child windows.
void Renderer::drawMusicControl(const Scene& s, const SceneNode& node, float w, float h, float alpha) {
    const int index = (int)node.id - 101;
    const auto& motion = s.musicControls;
    const float unit = std::max(0.f, std::min(w / 42.f, h / 30.f));
    if (unit <= 0) return;
    auto color = [&](float opacity) {
        return D2D1::ColorF((float)node.n(WI_TEXT_COLOR), (float)node.n(WI_TEXT_COLOR,1),
                           (float)node.n(WI_TEXT_COLOR,2), opacity * alpha * (float)node.n(WI_TEXT_COLOR,3));
    };
    const float enabled = node.n(WI_ENABLED,0,1) ? (float)motion.enabled[index] : 0.f;
    const float hover = (float)motion.hover[index], press = (float)motion.press[index];
    rounded({2*unit, 1*unit, w-4*unit, h-2*unit}, (h-2*unit)/2,
            color(.025f + .07f*hover + .08f*press));
    if (motion.focus[index] > .001) {
        brush->SetColor(D2D1::ColorF(.65f,.8f,1,alpha*(float)motion.focus[index]));
        target->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(unit,unit,w-unit,h-unit),
                                      h/2-unit,h/2-unit),brush.Get(),unit);
    }
    D2D1_MATRIX_3X2_F saved; target->GetTransform(&saved);
    const auto base = D2D1::Matrix3x2F(saved._11,saved._12,saved._21,saved._22,saved._31,saved._32);
    const float size = unit * (1 - .075f*press + .015f*hover);
    const auto icon = D2D1::Matrix3x2F::Scale(size,size) *
                      D2D1::Matrix3x2F::Translation(w/2,h/2) * base;
    target->SetTransform(icon);
    target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    auto polygon = [&](const auto& vertices, float radius, ComPtr<ID2D1PathGeometry>& path) {
        path.Reset(); hr(factory->CreatePathGeometry(&path));
        ComPtr<ID2D1GeometrySink> sink; hr(path->Open(&sink));
        const size_t count = vertices.size();
        auto cut = [&](MusicIconPoint v, MusicIconPoint other, float distance) {
            const float length = std::hypot(other.x-v.x,other.y-v.y);
            const float fraction = length > 0 ? std::min(.45f,distance/length) : 0;
            return D2D1::Point2F(v.x+(other.x-v.x)*fraction,v.y+(other.y-v.y)*fraction);
        };
        for (size_t i=0; i<count; ++i) {
            const auto v=vertices[i];
            auto incoming=cut(v,vertices[(i+count-1)%count],radius);
            auto outgoing=cut(v,vertices[(i+1)%count],radius);
            if (!i) sink->BeginFigure(incoming,D2D1_FIGURE_BEGIN_FILLED);
            else sink->AddLine(incoming);
            sink->AddBezier(D2D1::BezierSegment(
                D2D1::Point2F(incoming.x+(v.x-incoming.x)*2/3,incoming.y+(v.y-incoming.y)*2/3),
                D2D1::Point2F(outgoing.x+(v.x-outgoing.x)*2/3,outgoing.y+(v.y-outgoing.y)*2/3),outgoing));
        }
        sink->EndFigure(D2D1_FIGURE_END_CLOSED); hr(sink->Close());
    };
    const float opacity = .36f + .64f*enabled;
    brush->SetColor(color(opacity));
    if (index == 1) {
        if (!playContour || playContourAt != motion.pause) {
            polygon(musicPlayContour(motion.pause),1.65f,playContour);
            playContourAt=motion.pause;
        }
        const float known=(float)motion.known, pause=(float)motion.pause;
        brush->SetColor(color(opacity*known));
        target->FillGeometry(playContour.Get(),brush.Get());
        // The second rounded bar joins the contour morph continuously, without
        // erasing either shape or resetting progress on a rapid state reversal.
        rounded({.5f+pause, -4-4*pause, 3+1.5f*pause, 8+8*pause},1.65f,
                color(opacity*known*pause*pause));
        rounded({-5,-1.5f,10,3},1.5f,color(opacity*(1-known)));
    } else if (index == 0 || index == 2) {
        if (!skipContour) {
            const std::array<MusicIconPoint,3> triangle{{{-8,-7},{3,0},{-8,7}}};
            polygon(triangle,1.8f,skipContour);
        }
        target->SetTransform(D2D1::Matrix3x2F::Scale(index==0?-1.f:1.f,1)*icon);
        target->FillGeometry(skipContour.Get(),brush.Get());
        rounded({4,-7,3,14},1.5f,color(opacity));
    } else {
        if (!musicStroke) {
            auto style=D2D1::StrokeStyleProperties();
            style.startCap=style.endCap=style.dashCap=D2D1_CAP_STYLE_ROUND;
            style.lineJoin=D2D1_LINE_JOIN_ROUND;
            hr(factory->CreateStrokeStyle(style,nullptr,0,&musicStroke));
        }
        bool shuffle=s.music&&s.music->shuffle;
        auto& path=shuffle?shuffleContour:repeatContour;
        if (!path) {
            hr(factory->CreatePathGeometry(&path));
            ComPtr<ID2D1GeometrySink> sink; hr(path->Open(&sink));
            auto point=[](float x,float y){return D2D1::Point2F(x,y);};
            for (float sign : {-1.f,1.f}) {
                sink->BeginFigure(point(shuffle?-7:-7*sign,-4*sign),D2D1_FIGURE_BEGIN_HOLLOW);
                if(shuffle) sink->AddBezier(D2D1::BezierSegment(point(0,-4*sign),point(0,4*sign),point(7,4*sign)));
                else {sink->AddBezier(D2D1::BezierSegment(point(-9*sign,-7*sign),point(-2*sign,-6*sign),point(0,-6*sign)));sink->AddLine(point(7*sign,-6*sign));}
                sink->EndFigure(D2D1_FIGURE_END_OPEN);
                float y=(shuffle?4.f:-6.f)*sign;
                const float direction=shuffle?1:sign;
                sink->BeginFigure(point(4*direction,y-2.5f*direction),D2D1_FIGURE_BEGIN_HOLLOW);
                sink->AddLine(point(7*direction,y));sink->AddLine(point(4*direction,y+2.5f*direction));
                sink->EndFigure(D2D1_FIGURE_END_OPEN);
            }
            hr(sink->Close());
        }
        target->DrawGeometry(path.Get(),brush.Get(),1.7f,musicStroke.Get());
        if (s.music && !shuffle && s.music->repeat==1)
            rounded({-.7f,-2.5f,1.4f,5},.7f,color(opacity));
    }
    target->SetTransform(saved);
}
