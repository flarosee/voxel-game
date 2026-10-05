#include "Transparency.hpp"
#include <algorithm>
#include <limits>

namespace voxel::mesh {
std::optional<Transparency> buildTransparency(std::vector<Quad> faces,
    std::vector<Vertex>& vertices,std::size_t maxVertices,const std::function<bool()>& cancelled) {
    Transparency result;
    const auto stop=[&]{ return cancelled && cancelled(); };
    if (stop()) return std::nullopt;
    if (maxVertices>std::numeric_limits<std::uint32_t>::max()) throw std::length_error("Transparent draw index overflow");
    for (const auto& q:faces) {
        if (q.axis<0 || q.axis>2 || (q.sign!=-1 && q.sign!=1) || q.width<1 || q.height<1 ||
            q.width>world::ChunkSide || q.height>world::ChunkSide)
            throw std::invalid_argument("Invalid transparent rectangle");
        for (int p:q.origin) if (p < -1048576 || p > 1048576)
            throw std::out_of_range("Transparent coordinates must be scene-relative");
    }
    std::size_t fragments=faces.size();
    if (vertices.size()>maxVertices || fragments>(maxVertices-vertices.size())/12)
        throw std::length_error("Transparent scene exceeds vertex budget");
    bool aborted=false;
    const auto build=[&](auto&& self,std::vector<Quad> input,int depth)->int {
        if (stop()) { aborted=true; return -1; }
        if (input.empty()) return -1;
        if (depth>256) throw std::length_error("Transparent partition depth exceeded");
        // Median of face planes on the axis with the widest plane distribution.
        // Each branch consumes a distinct integer plane, bounding stack depth by
        // the scene's three spatial extents rather than its number of faces.
        std::array<std::vector<int>,3> planes;
        for (const auto& q:input) planes[q.axis].push_back(q.origin[q.axis]);
        int axis=0, spread=-1;
        for (int a=0;a<3;++a) if (!planes[a].empty()) {
            auto [lo,hi]=std::minmax_element(planes[a].begin(),planes[a].end());
            if (*hi-*lo>spread) { axis=a; spread=*hi-*lo; }
        }
        auto& candidates=planes[axis];
        auto middle=candidates.begin()+candidates.size()/2;
        std::nth_element(candidates.begin(),middle,candidates.end());
        const int plane=*middle;
        const int index=static_cast<int>(result.nodes.size());
        result.nodes.push_back({axis,plane,-1,-1,{static_cast<std::uint32_t>(vertices.size()),0}});
        std::vector<Quad> low, high;
        std::size_t scanned=0;
        for (auto q:input) {
            if ((scanned++%256)==0 && stop()) { aborted=true; return -1; }
            if (q.axis==axis && q.origin[axis]==plane) {
                const auto count=static_cast<std::size_t>(6*(q.width+q.height));
                if (count>maxVertices-vertices.size()) throw std::length_error("Transparent fragments exceed vertex budget");
                appendQuad(vertices,q);
                result.nodes[index].faces.count+=static_cast<std::uint32_t>(count);
                continue;
            }
            const int extent=q.axis==axis ? 0 : ((q.axis+1)%3==axis ? q.width : q.height);
            const int start=q.origin[axis], end=start+extent;
            if (end<=plane) low.push_back(q);
            else if (start>=plane) high.push_back(q);
            else {
                if (++fragments>maxVertices/12) throw std::length_error("Too many transparent fragments");
                auto other=q;
                other.origin[axis]=plane;
                if ((q.axis+1)%3==axis) { q.width=plane-start; other.width=end-plane; }
                else { q.height=plane-start; other.height=end-plane; }
                low.push_back(q); high.push_back(other);
            }
        }
        // Release the parent input before descending so temporary storage stays bounded.
        std::vector<Quad>().swap(input);
        const int l=self(self,std::move(low),depth+1);
        if (aborted) return -1;
        const int h=self(self,std::move(high),depth+1);
        result.nodes[index].low=l; result.nodes[index].high=h;
        return index;
    };
    build(build,std::move(faces),0);
    if (aborted || stop()) return std::nullopt;
    return result;
}
std::vector<DrawRange> Transparency::backToFront(std::array<float,3> eye) const {
    std::vector<DrawRange> draws;
    if (nodes.empty()) return draws;
    struct Visit { int index; bool draw; };
    std::vector<Visit> stack{{0,false}};
    while (!stack.empty()) {
        const auto visit=stack.back(); stack.pop_back();
        if (visit.index<0) continue;
        const auto& node=nodes[visit.index];
        if (visit.draw) { if (node.faces.count) draws.push_back(node.faces); continue; }
        const bool positive=eye[node.axis]>=static_cast<float>(node.plane);
        stack.push_back({positive ? node.high : node.low,false});
        stack.push_back({visit.index,true});
        stack.push_back({positive ? node.low : node.high,false});
    }
    return draws;
}
} // namespace voxel::mesh
