#pragma once
#include "outline.h"
namespace wi {
using IslandContour = std::array<OutlineCorner,4>;
// The same cubics as the main island, expressed in a dock's local coordinates.
// Only the outline rotates. Text, controls, and their hit rectangles stay upright.
inline IslandContour transferContour(double width,double height,double radius,double shoulder,
                                      std::array<double,3> attachment) {
    auto result=islandOutline(0,width,height,radius,radius);
    const auto detached=result;
    for(int edge=0;edge<3;++edge){
        double weight=attachment[edge];if(weight<=0)continue;
        bool side=edge!=0;double length=side?height:width,depth=side?width:height;
        double arc=std::clamp(shoulder,0.,std::max(0.,std::min(length,depth)/4));
        auto attached=islandOutline(arc,length-2*arc,depth,radius,-arc);
        auto map=[&](OutlinePoint p)->OutlinePoint {
            return edge==1?OutlinePoint{p.y,length-p.x}:edge==2?OutlinePoint{depth-p.y,p.x}:p;
        };
        for(auto& c:attached){c={map(c.start),map(c.c1),map(c.c2),map(c.end)};}
        if(edge==1)std::rotate(attached.begin(),attached.begin()+1,attached.end());
        if(edge==2)std::rotate(attached.begin(),attached.begin()+3,attached.end());
        for(size_t i=0;i<4;++i){
            auto add=[&](OutlinePoint& p,OutlinePoint q,OutlinePoint base){p.x+=(q.x-base.x)*weight;p.y+=(q.y-base.y)*weight;};
            add(result[i].start,attached[i].start,detached[i].start);
            add(result[i].c1,attached[i].c1,detached[i].c1);
            add(result[i].c2,attached[i].c2,detached[i].c2);
            add(result[i].end,attached[i].end,detached[i].end);
        }
    }
    return result;
}
}
