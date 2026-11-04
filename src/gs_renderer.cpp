// Independently implemented from register/VRAM specifications, no emulator code.
#include "ps2native/gs_registers.hpp"
#include <algorithm>
#include <string>

namespace ps2native {
std::uint64_t GsRegisters::required(std::uint8_t index) const {
    if (!general_[index]) throw DeviceRefusal("GS drawing/transfer register " + std::to_string(index) +
                                             " has not been written since reset");
    return *general_[index];
}
void GsRegisters::reset_drawing() {
    general_ = {}; general_unknown_={}; display_ = {}; first_vertex_.reset(); triangle_vertices_count_=0; transfer_ = {};
    general_[0x22]=diagnostic_reset_scanmask_;
    clut_={};clut_base_={};sampled_pages_={};dirty_texture_pages_={};transfer_generation_=0;texture_flush_generation_.reset();
    imr_ = 0x7F00u;
}
void GsRegisters::start_transfer(std::uint64_t value) {
    if (value & ~3ull) throw DeviceRefusal("GS TRXDIR bits above 1 are reserved");
    if (finish_pending_)
        throw DeviceRefusal("GS transfer reprogrammed while FINISH waits for an incomplete upload");
    const auto direction = static_cast<unsigned>(value);
    if (direction == 3u) { transfer_ = {}; return; }
    const auto area = required(0x52);
    if (area & ~0xFFF00000FFFull) throw DeviceRefusal("GS TRXREG reserved bits set");
    const auto width = static_cast<unsigned>(area & 0xFFFu);
    const auto height = static_cast<unsigned>((area >> 32) & 0xFFFu);
    if (!width || !height) { transfer_ = {}; return; } // empty transfer, inherited hypothesis
    if (direction == 1u) throw DeviceRefusal("GS local-to-host transfer is not modeled");
    const auto buf = required(0x50), pos = required(0x51);
    const auto base = static_cast<unsigned>((buf >> 32) & 0x3FFFu) * 256u;
    const auto bw = static_cast<unsigned>((buf >> 48) & 63u) * 64u;
    const auto psm = static_cast<unsigned>((buf >> 56) & 63u);
    const auto x = static_cast<unsigned>((pos >> 32) & 2047u);
    const auto y = static_cast<unsigned>((pos >> 48) & 2047u);
    (void)GsVram::address(base, bw, psm, x, y);
    if (width % (psm == 0u ? 2u : psm==2u || psm==10u ? 4u : 8u) || (psm == 19u && (x & 1u)) || (psm == 20u && (x & 3u)))
        throw DeviceRefusal("GS transfer rectangle violates the hardware alignment");
    const auto order = static_cast<unsigned>((pos >> 59) & 3u);
    ++transfer_generation_;
    if (direction == 0u) {
        // TRXPOS.DIR applies only to local copies; uploads always run left to
        // right, top to bottom (GS manual 4.1.2).
        transfer_ = {true, base, bw, psm, x, y, width, height, 0u, 0u, 0u};
        return;
    }
    const auto sb = static_cast<unsigned>(buf & 0x3FFFu) * 256u;
    const auto sw = static_cast<unsigned>((buf >> 16) & 63u) * 64u;
    const auto sp = static_cast<unsigned>((buf >> 24) & 63u);
    const auto sx = static_cast<unsigned>(pos & 2047u), sy = static_cast<unsigned>((pos >> 16) & 2047u);
    if ((sp == 19u && (sx & 1u)) || (sp == 20u && (sx & 3u)))
        throw DeviceRefusal("GS indexed source violates hardware alignment");
    (void)GsVram::address(sb, sw, sp, sx, sy);
    if (sp != psm) throw DeviceRefusal("GS local-to-local mixed formats are not modeled");
    // DIR selects traversal, including overlapping source/destination regions.
    for (unsigned row = 0; row < height; ++row)
        for (unsigned col = 0; col < width; ++col) {
            const auto dx = (order & 2u) ? width - 1u - col : col;
            const auto dy = (order & 1u) ? height - 1u - row : row;
            const auto pixel = vram_.read(sb, sw, sp, (sx + dx) & 2047u, (sy + dy) & 2047u);
            texture_transfer_write(GsVram::address(base,bw,psm,(x+dx)&2047u,(y+dy)&2047u));
            vram_.write(base, bw, psm, (x + dx) & 2047u, (y + dy) & 2047u, pixel);
        }
    transfer_ = {};
}
void GsRegisters::transfer_data(std::uint64_t data) {
    auto& t = transfer_;
    // GS manual HWREG: outside an active host-to-local transfer, no operation.
    if (!t.active) return;
    if (t.psm == 20u) {
        for(unsigned i=0;i<16u && t.active;++i) {
            texture_transfer_write(GsVram::address(t.base,t.bw,t.psm,(t.x+t.pixel%t.width)&2047u,(t.y+t.pixel/t.width)&2047u));
            vram_.write(t.base,t.bw,t.psm,(t.x+t.pixel%t.width)&2047u,
                        (t.y+t.pixel/t.width)&2047u,static_cast<std::uint32_t>((data>>(i*4u))&15u));
            if(++t.pixel==t.width*t.height) {t.active=false;finish_transfer();}
        }
        return;
    }
    const unsigned bytes = t.psm == 19u ? 1u : t.psm==2u || t.psm==10u ? 2u : t.psm == 1u ? 3u : 4u;
    for (unsigned i = 0; i < 8u && t.active; ++i) {
        t.pending |= static_cast<std::uint32_t>((data >> (8u * i)) & 255u) << (8u * t.pending_bytes++);
        if (t.pending_bytes != bytes) continue;
        texture_transfer_write(GsVram::address(t.base,t.bw,t.psm,(t.x+t.pixel%t.width)&2047u,(t.y+t.pixel/t.width)&2047u));
        vram_.write(t.base, t.bw, t.psm, (t.x + t.pixel % t.width) & 2047u,
                     (t.y + t.pixel / t.width) & 2047u, t.pending);
        t.pending = 0u; t.pending_bytes = 0u;
        if (++t.pixel == t.width * t.height) { t.active = false; finish_transfer(); }
    }
}
void GsRegisters::image_write(std::uint64_t low, std::uint64_t high) {
    transfer_data(low);
    if (transfer_.active) transfer_data(high); // final quadword padding is discarded
}
void GsRegisters::load_clut(std::uint64_t tex) {
    const auto psm=static_cast<unsigned>((tex>>20)&63u),cld=static_cast<unsigned>(tex>>61);
    if((psm!=19u && psm!=20u) || !cld)return;
    if(cld>5u)throw DeviceRefusal("GS reserved CLUT load control");
    const auto cbp=static_cast<unsigned>((tex>>37)&0x3FFFu);
    const auto cpsm=static_cast<unsigned>((tex>>51)&15u),csa=static_cast<unsigned>((tex>>56)&31u);
    const bool csm2=(tex&(1ull<<55))!=0;
    if(cpsm || csa>15u || csm2 || (psm==19u && csa))
        throw DeviceRefusal("GS CLUT supports CSM1 CT32, T4 banks and T8 CSA=0 only");
    if(cld>=4u) {
        const auto old=clut_base_[cld-4u];
        if(!old)throw DeviceRefusal("GS conditional CLUT load compares an unknown CBP register");
        if(*old==cbp)return;
    }
    if(!texture_flush_generation_ || *texture_flush_generation_!=transfer_generation_)
        throw DeviceRefusal("GS CLUT load requires TEXFLUSH after VRAM transfers; stale texture cache is not modeled");
    auto next=clut_;
    const auto count=psm==20u ? 16u : 256u;
    for(unsigned i=0;i<count;++i) {
        // CSM1's 16x16 arrangement exchanges index bits 3/4. T4 uses
        // the first 8x2 rectangle. Source data is copied at TEX0/TEX2 time.
        const auto x=(i&7u)|((i&16u)>>1),y=((i&8u)>>3)|((i&0xE0u)>>4);
        next[csa*16u+i]=vram_.read(cbp*256u,64u,0,x,y);
    }
    clut_=next;
    sampled_pages_[cbp*256u/8192u]=true;
    sampled_pages_[((cbp*256u+8191u)%GsVram::kBytes)/8192u]=true;
    if(cld>=2u)clut_base_[(cld-2u)&1u]=cbp;
}
void GsRegisters::texture_write_guard(unsigned address) const {
    if(sampled_pages_[address/8192u])
        throw DeviceRefusal("GS drawing aliases sampled texture memory; cache feedback is not modeled");
}
void GsRegisters::texture_transfer_write(unsigned address) {
    // An upload/copy is legal even when the old texture is cached. Without
    // cached texel bytes, a later read must refuse until explicit TEXFLUSH.
    const auto page=address/8192u;
    if(sampled_pages_[page])dirty_texture_pages_[page]=true;
}
std::uint32_t GsRegisters::sample_texture(std::uint64_t tex,std::uint64_t clamp,int u,int v) {
    const auto psm=static_cast<unsigned>((tex>>20)&63u),tw=static_cast<unsigned>((tex>>26)&15u),th=static_cast<unsigned>((tex>>30)&15u);
    if((psm!=0u && psm!=19u && psm!=20u) || tw>10u || th>10u)
        throw DeviceRefusal("GS texture format or dimensions are not modeled");
    if(!texture_flush_generation_)
        throw DeviceRefusal("GS sampling requires an initial TEXFLUSH; reset texture cache validity is not established");
    const auto wrap=[](int p,unsigned bits,unsigned mode,unsigned low,unsigned high) {
        if(mode==0u)return static_cast<unsigned>(p)&((1u<<bits)-1u);
        if(mode==1u)return static_cast<unsigned>(std::clamp(p,0,static_cast<int>((1u<<bits)-1u)));
        if(mode==2u) {
            if(low>high)throw DeviceRefusal("GS inverted region clamp is not modeled");
            return static_cast<unsigned>(std::clamp(p,static_cast<int>(low),static_cast<int>(high)));
        }
        return (static_cast<unsigned>(p)&low)|high;
    };
    const auto x=wrap(u,tw,static_cast<unsigned>(clamp&3u),static_cast<unsigned>((clamp>>4)&1023u),static_cast<unsigned>((clamp>>14)&1023u));
    const auto y=wrap(v,th,static_cast<unsigned>((clamp>>2)&3u),static_cast<unsigned>((clamp>>24)&1023u),static_cast<unsigned>((clamp>>34)&1023u));
    const auto base=static_cast<unsigned>(tex&0x3FFFu)*256u,bw=static_cast<unsigned>((tex>>14)&63u)*64u;
    // Conservatively protect the entire source page, including a non-page-
    // aligned TBP. Drawing into it needs a cache model, not live VRAM reads.
    const auto pw=psm==0u ? 64u : 128u,ph=psm==0u ? 32u : psm==19u ? 64u : 128u;
    const auto page=(base+((y/ph)*(bw/pw)+x/pw)*8192u)%GsVram::kBytes;
    const auto first=page/8192u,last=((page+8191u)%GsVram::kBytes)/8192u;
    // TEXFLUSH invalidates all pages. A page not fetched since then remains
    // cold across unrelated VRAM transfers; there is no stale value to read.
    // Whole-page protection is conservative, including nonaligned TBP.
    if(dirty_texture_pages_[first] || dirty_texture_pages_[last])
        throw DeviceRefusal("GS sampling requires TEXFLUSH after a cached texture page changed; stale texture cache is not modeled");
    sampled_pages_[first]=true;sampled_pages_[last]=true;
    const auto raw=vram_.read(base,bw,psm,x,y);
    if(psm==0u)return raw;
    const auto csa=static_cast<unsigned>((tex>>56)&31u);
    if(((tex>>51)&15u) || (tex&(1ull<<55)) || csa>15u || (psm==19u && csa))
        throw DeviceRefusal("GS indexed sampling requires a modeled CT32 CLUT bank");
    const auto color=clut_[csa*16u+raw];
    if(!color)throw DeviceRefusal("GS texture consumes an unloaded CLUT entry");
    return *color;
}
void GsRegisters::vertex(std::uint8_t index, std::uint64_t value) {
    const auto prim = required(0x00);
    const auto control = required(0x1A);
    const auto mode = (control & 1u) ? prim : required(0x1B);
    const auto type = static_cast<unsigned>(prim & 7u);
    const bool triangle=type>=3u && type<=5u;
    if (type != 0u && type != 6u && !triangle) throw DeviceRefusal("GS line/reserved primitive is not modeled");
    const bool textured=(mode&16u)!=0u,blended=(mode&64u)!=0u;
    if(textured && !triangle && (type!=6u || !(mode&256u)))
        throw DeviceRefusal("GS texture drawing supports UV sprites only");
    const auto context = static_cast<std::uint8_t>((mode >> 9) & 1u);
    const auto offset = required(static_cast<std::uint8_t>(0x18u + context));
    const auto uv=textured && !triangle ? required(3) : general_[3].value_or(0);
    const Vertex v{static_cast<int>(value & 0xFFFFu) - static_cast<int>(offset & 0xFFFFu),
                   static_cast<int>((value >> 16) & 0xFFFFu) - static_cast<int>((offset >> 32) & 0xFFFFu),
                   static_cast<int>(uv&0x3FFFu),static_cast<int>((uv>>16)&0x3FFFu),
                   static_cast<std::uint32_t>(value>>32u)&((index&7u)==4u ? 0xFFFFFFu : 0xFFFFFFFFu),general_[1],general_[2],general_[3],general_unknown_[1]};
    std::array<Vertex,3> vertices{};
    const auto finish_triangle=[&] {
        if(type==3u)triangle_vertices_count_=0;
        else {
            if(type==4u)triangle_vertices_[0]=triangle_vertices_[1];
            triangle_vertices_[1]=v;triangle_vertices_count_=2;
        }
    };
    const auto edge=[](const Vertex& a,const Vertex& b,std::int64_t x,std::int64_t y) {
        return std::int64_t{b.x-a.x}*(y-a.y)-std::int64_t{b.y-a.y}*(x-a.x);
    };
    std::int64_t area=0;
    if(triangle) {
        // GS manual 3.2.3-3.2.5: every vertex kick snapshots the latches.
        // XYZ3 suppresses only this drawing kick and still advances the queue.
        if(triangle_vertices_count_<2u){triangle_vertices_[triangle_vertices_count_++]=v;return;}
        vertices={triangle_vertices_[0],triangle_vertices_[1],v};
        if(index==0x0Cu || index==0x0Du){finish_triangle();return;}
        area=edge(vertices[0],vertices[1],v.x,v.y);
        if(!area){finish_triangle();return;}
        if(area<0){std::swap(vertices[0],vertices[1]);area=-area;}
    }
    if (type == 6u && !first_vertex_) { first_vertex_ = v; return; }
    // XYZ3/XYZF3 advance the vertex queue without a drawing kick.
    if (index == 0x0Cu || index == 0x0Du) { first_vertex_.reset(); return; }
    if (mode & (32u | 128u) || (!triangle && (mode&8u)))
        throw DeviceRefusal("GS shading, fog or antialiasing is not modeled");
    if(textured && !(mode&256u))throw DeviceRefusal("GS STQ texture drawing precision is not established");
    const auto frame = required(static_cast<std::uint8_t>(0x4Cu + context));
    const auto scissor = required(static_cast<std::uint8_t>(0x40u + context));
    const auto test = required(static_cast<std::uint8_t>(0x47u + context));
    const auto zbuf = required(static_cast<std::uint8_t>(0x4Eu + context));
    if (test & (1u << 14))
        throw DeviceRefusal("GS alpha/depth pipeline: destination alpha test is not modeled");
    // Without blending or texture arithmetic, byte colors cannot overflow;
    // PABE/COLCLAMP and 16-bit dithering cannot affect a CT32/24 write.
    const bool conditional = !general_[0x22];
    const auto scanmask = general_[0x22].value_or(0u) & 3u;
    if (scanmask == 1u) throw DeviceRefusal("GS reserved scan mask");
    const auto fragment = static_cast<std::uint32_t>(required(0x01));
    const auto fba = general_[0x4Au + context];
    const auto base = static_cast<unsigned>(frame & 511u) * 8192u;
    const auto bw = static_cast<unsigned>((frame >> 16) & 63u) * 64u;
    const auto psm = static_cast<unsigned>((frame >> 24) & 63u);
    if (psm > 1u) throw DeviceRefusal("GS drawing framebuffer format is not modeled");
    (void)GsVram::address(base, bw, psm, 0u, 0u);
    const auto zbase = static_cast<unsigned>(zbuf & 511u) * 8192u;
    const auto zpsm = static_cast<unsigned>((zbuf >> 24) & 15u);
    const bool ztest = (test & (1u << 16)) != 0u;
    const auto zfunc = static_cast<unsigned>((test >> 17) & 3u);
    const bool zwrite = ztest && !(zbuf & (1ull << 32));
    const auto z = v.z;
    if (zwrite || (ztest && zfunc >= 2u)) {
        (void)GsVram::depth_address(zbase, bw, zpsm, 0u, 0u);
        if (zpsm == 2u) throw DeviceRefusal("GS PSMZ16 cannot share a drawing layout with PSMCT32/24");
        const auto zmax = zpsm == 0u ? 0xFFFFFFFFu : zpsm == 1u ? 0xFFFFFFu : 0xFFFFu;
        if (z > zmax) throw DeviceRefusal("GS out-of-range vertex depth is not modeled");
        if(triangle)for(const auto& source : vertices)if(source.z>zmax)throw DeviceRefusal("GS out-of-range vertex depth is not modeled");
    }
    const auto ceil16 = [](int n) { return n >= 0 ? (n + 15) / 16 : n / 16; };
    const auto floor16 = [](int n) { return n >= 0 ? n / 16 : (n - 15) / 16; };
    int left, top, right, bottom;
    if (type == 0u) {
        left = floor16(v.x); top = floor16(v.y); right = left + 1; bottom = top + 1;
    } else if(triangle) {
        left=ceil16(std::min({vertices[0].x,vertices[1].x,v.x}));right=ceil16(std::max({vertices[0].x,vertices[1].x,v.x}));
        top=ceil16(std::min({vertices[0].y,vertices[1].y,v.y}));bottom=ceil16(std::max({vertices[0].y,vertices[1].y,v.y}));
    } else {
        left = ceil16(std::min(first_vertex_->x, v.x)); top = ceil16(std::min(first_vertex_->y, v.y));
        right = ceil16(std::max(first_vertex_->x, v.x)); bottom = ceil16(std::max(first_vertex_->y, v.y));
    }
    left = std::max(left, static_cast<int>(scissor & 2047u));
    right = std::min(right, static_cast<int>((scissor >> 16) & 2047u) + 1);
    top = std::max(top, static_cast<int>((scissor >> 32) & 2047u));
    bottom = std::min(bottom, static_cast<int>((scissor >> 48) & 2047u) + 1);
    const auto tex=textured ? required(static_cast<std::uint8_t>(6u+context)) : 0;
    const auto clamp=textured ? required(static_cast<std::uint8_t>(8u+context)) : 0;
    bool bilinear=false;
    if(textured) {
        const auto lod=required(static_cast<std::uint8_t>(0x14u+context));
        const auto magnification=static_cast<unsigned>((lod>>5u)&1u),minification=static_cast<unsigned>((lod>>6u)&7u);
        if(minification>1u)throw DeviceRefusal("GS mipmap texture levels are not modeled");
        // GS manual 3.4.12: MMIN=0/1 always uses level zero. When both
        // filters agree, neither Q nor K can change the sampling operation.
        auto selected=magnification;
        if(magnification!=minification) {
            if(!(lod&1u))throw DeviceRefusal("GS texture filter selection requires proven dynamic LOD");
            const auto k=static_cast<unsigned>((lod>>32u)&0xFFFu);
            if(!k)throw DeviceRefusal("GS texture filter selection at the zero-LOD boundary is not established");
            selected=(k&0x800u) ? magnification : minification;
        }
        bilinear=selected!=0u;
        if(bilinear && (((tex>>26u)&15u)<3u || ((tex>>30u)&15u)<3u))
            throw DeviceRefusal("GS bilinear texture dimensions must be at least 8 by 8");
    }
    const auto tfx=static_cast<unsigned>((tex>>35)&3u);
    if(textured && tfx>1u)throw DeviceRefusal("GS texture function supports MODULATE/DECAL only");
    const auto alpha=blended ? required(static_cast<std::uint8_t>(0x42u+context)) : 0;
    const auto aa=static_cast<unsigned>(alpha&3u),ab=static_cast<unsigned>((alpha>>2)&3u),
               ac=static_cast<unsigned>((alpha>>4)&3u),ad=static_cast<unsigned>((alpha>>6)&3u);
    if(blended && (aa==3u || ab==3u || ac==3u || ad==3u))throw DeviceRefusal("GS reserved alpha selector");
    const bool saturate=blended && (required(0x46)&1u);
    const auto uv_at=[](int pixel,int p0,int p1,int t0,int t1) {
        auto den=std::int64_t{p1}-p0;
        auto num=std::int64_t{t0}*den+std::int64_t{t1-t0}*(pixel*16-p0);
        if(den<0){den=-den;num=-num;}
        // Exact 10.4 coordinates avoid inventing the GS DDA's precision for
        // fractional slopes. Scaled/flipped integer-step sprites are covered.
        if(!den || num%den)throw DeviceRefusal("GS fractional UV DDA precision is not modeled");
        const auto fixed=num/den;
        return static_cast<int>(fixed);
    };
    const auto filtered_texture=[&](int u,int v) {
        if(!bilinear)return sample_texture(tex,clamp,floor16(u),floor16(v));
        // GS manual 3.4.8: four texel centers, shifted by half a texel;
        // wrap/clamp applies independently to each tap, after the shift.
        const auto x=floor16(u-8),y=floor16(v-8);
        const auto fx=static_cast<unsigned>(u-8-x*16),fy=static_cast<unsigned>(v-8-y*16);
        const auto a=sample_texture(tex,clamp,x,y);
        const auto b=fx ? sample_texture(tex,clamp,x+1,y) : a;
        const auto c=fy ? sample_texture(tex,clamp,x,y+1) : a;
        const auto d=fx && fy ? sample_texture(tex,clamp,x+1,y+1) : fy ? c : b;
        std::uint32_t color=0;
        for(unsigned channel=0;channel<4u;++channel) {
            const auto av=(a>>(channel*8u))&255u,bv=(b>>(channel*8u))&255u,
                       cv=(c>>(channel*8u))&255u,dv=(d>>(channel*8u))&255u;
            const auto top=av*(16u-fx)+bv*fx,bottom=cv*(16u-fx)+dv*fx;
            const auto left=av*(16u-fy)+cv*fy,right=bv*(16u-fy)+dv*fy;
            const auto sum=top*(16u-fy)+bottom*fy;
            // No rounding hypothesis: accept only results also exact in
            // either separable evaluation order. Fractional cases remain
            // explicit until the GS truncation stages have evidence.
            if(top%16u || (fy && bottom%16u) || left%16u || (fx && right%16u) || sum%256u)
                throw DeviceRefusal("GS fractional bilinear truncation stages are not established");
            color|=(sum/256u)<<(channel*8u);
        }
        return color;
    };
    const auto inclusive=[](const Vertex& a,const Vertex& b){return b.y<a.y || (b.y==a.y && b.x>a.x);};
    const auto interpolate=[&](const std::array<std::int64_t,3>& weights,const std::array<std::uint32_t,3>& attribute) {
        const auto ab=std::int64_t{attribute[1]}-attribute[0],ac=std::int64_t{attribute[2]}-attribute[0];
        const auto dx=(ab*(vertices[2].y-vertices[0].y)-ac*(vertices[1].y-vertices[0].y))*16;
        const auto dy=((vertices[1].x-vertices[0].x)*ac-(vertices[2].x-vertices[0].x)*ab)*16;
        // Whole-number per-pixel gradients need no fractional DDA rounding.
        // An isolated integral barycentric result is insufficient proof when
        // either plane gradient itself would have been rounded by hardware.
        if(dx%area || dy%area)throw DeviceRefusal("GS fractional triangle attribute DDA precision is not established");
        std::uint64_t sum=0;
        for(unsigned n=0;n<3u;++n) {
            const auto weight=static_cast<std::uint64_t>(weights[n]);
            if(attribute[n] && weight>(UINT64_MAX-sum)/attribute[n])throw DeviceRefusal("GS triangle interpolation exceeds exact integer range");
            sum+=weight*attribute[n];
        }
        // Exact attribute results are independent of undocumented fractional
        // DDA precision. Keep fractional cases explicit until established.
        if(sum%static_cast<std::uint64_t>(area))throw DeviceRefusal("GS fractional triangle attribute DDA precision is not established");
        return static_cast<std::uint32_t>(sum/static_cast<std::uint64_t>(area));
    };
    for (int y = top; y < bottom; ++y) {
        if ((scanmask == 2u && !(y & 1)) || (scanmask == 3u && (y & 1))) continue;
        for (int x = left; x < right; ++x) {
            std::array<std::int64_t,3> weights{};
            auto pixel_z=z;
            if(triangle) {
                bool covered=true;
                for(unsigned n=0;n<3u;++n) {
                    const auto& a=vertices[(n+1u)%3u];const auto& b=vertices[(n+2u)%3u];
                    weights[n]=edge(a,b,x*16,y*16);
                    if(weights[n]<0 || (!weights[n] && !inclusive(a,b))){covered=false;break;}
                }
                if(!covered)continue;
                if(zwrite || (ztest && zfunc>=2u))pixel_z=interpolate(weights,{vertices[0].z,vertices[1].z,vertices[2].z});
            }
            auto color=fragment;
            if(triangle && (mode&8u)) {
                color=0;
                for(unsigned n=0;n<3u;++n)if(!vertices[n].rgbaq)throw DeviceRefusal("GS Gouraud consumes unwritten vertex color");
                for(const auto& source:vertices)if(source.rgbaq_unknown&0xFFFFFFFFull)
                    throw DeviceRefusal("GS Gouraud consumes unknown vertex color bits");
                for(unsigned c=0;c<4u;++c)color|=interpolate(weights,{
                    static_cast<std::uint32_t>((*vertices[0].rgbaq>>(c*8u))&255u),static_cast<std::uint32_t>((*vertices[1].rgbaq>>(c*8u))&255u),
                    static_cast<std::uint32_t>((*vertices[2].rgbaq>>(c*8u))&255u)})<<(c*8u);
            } else if(general_unknown_[1]&0xFFFFFFFFull)throw DeviceRefusal("GS drawing consumes unknown fragment color bits");
            if(textured) {
                int u=0,tv=0;
                if(triangle) {
                    for(unsigned n=0;n<3u;++n)if(!vertices[n].uv)throw DeviceRefusal("GS triangle consumes unwritten UV coordinates");
                    u=static_cast<int>(interpolate(weights,{static_cast<std::uint32_t>(vertices[0].u),static_cast<std::uint32_t>(vertices[1].u),static_cast<std::uint32_t>(vertices[2].u)}));
                    tv=static_cast<int>(interpolate(weights,{static_cast<std::uint32_t>(vertices[0].v),static_cast<std::uint32_t>(vertices[1].v),static_cast<std::uint32_t>(vertices[2].v)}));
                } else {u=uv_at(x,first_vertex_->x,v.x,first_vertex_->u,v.u);tv=uv_at(y,first_vertex_->y,v.y,first_vertex_->v,v.v);}
                const auto sampled=filtered_texture(u,tv);
                const auto shaded=color;
                color=0;
                for(unsigned c=0;c<4;++c) {
                    const auto f=(shaded>>(8u*c))&255u,t=(sampled>>(8u*c))&255u;
                    const auto component=c==3u && !(tex&(1ull<<34)) ? f :
                        tfx==1u ? t : std::min(255u,(f*t)>>7u);
                    color|=component<<(8u*c);
                }
            }
            bool color_write=true,depth_write=zwrite;
            auto write_mask=static_cast<std::uint32_t>(frame>>32);
            if(test&1u) {
                const auto a=color>>24,reference=static_cast<unsigned>((test>>4)&255u);
                bool pass=false;
                switch((test>>1)&7u) {
                    case 0: break; // NEVER
                    case 1: pass=true;break;
                    case 2: pass=a<reference;break;
                    case 3: pass=a<=reference;break;
                    case 4: pass=a==reference;break;
                    case 5: pass=a>=reference;break;
                    case 6: pass=a>reference;break;
                    case 7: pass=a!=reference;break;
                }
                if(!pass) {
                    switch((test>>12)&3u) {
                        case 0: continue; // KEEP, including no depth read
                        case 1: depth_write=false;break; // FB_ONLY
                        case 2: color_write=false;break; // ZB_ONLY
                        case 3: depth_write=false;write_mask|=0xFF000000u;break; // RGB_ONLY
                    }
                }
            }
            if (ztest && zfunc == 0u) continue;
            if (ztest && zfunc >= 2u) {
                const auto old = vram_.read_depth(zbase, bw, zpsm, static_cast<unsigned>(x), static_cast<unsigned>(y));
                if (pixel_z < old || (zfunc == 3u && pixel_z == old)) continue;
            }
            if(color_write) {
                bool blend=blended;
                if(blend && !(color&0x80000000u))blend=(required(0x49)&1u)==0u;
                if(blend) {
                    auto factor=static_cast<int>((color>>24)&255u);
                    if(aa==ab)factor=0;
                    else if(ac==2u)factor=static_cast<int>((alpha>>32)&255u);
                    else if(ac==1u)factor=psm==1u ? 128 : static_cast<int>(vram_.read(base,bw,psm,
                        static_cast<unsigned>(x),static_cast<unsigned>(y))>>24);
                    const auto sc=(int(aa==0u)-int(ab==0u))*factor+128*int(ad==0u);
                    const auto dc=(int(aa==1u)-int(ab==1u))*factor+128*int(ad==1u);
                    // Reduce the equation before reading destination RGB. For
                    // (Cs-Cd)*128/128+Cd the destination cancels exactly.
                    const auto dst=dc ? vram_.read(base,bw,1u,static_cast<unsigned>(x),static_cast<unsigned>(y)) : 0u;
                    auto result=color&0xFF000000u;
                    for(unsigned c=0;c<3;++c) {
                        const auto sum=sc*static_cast<int>((color>>(8u*c))&255u)+dc*static_cast<int>((dst>>(8u*c))&255u);
                        const auto channel=sum>=0 ? sum/128 : (sum-127)/128;
                        result|=(saturate ? static_cast<unsigned>(std::clamp(channel,0,255)) : static_cast<unsigned>(channel)&255u)<<(8u*c);
                    }
                    color=result;
                }
                const auto color_unknown=!fba && !(color&0x80000000u) ? 0x80000000u : 0u;
                if(fba && (*fba&1u))color|=0x80000000u;
                texture_write_guard(GsVram::address(base,bw,psm,static_cast<unsigned>(x),static_cast<unsigned>(y)));
                vram_.write(base, bw, psm, static_cast<unsigned>(x), static_cast<unsigned>(y), color,
                             write_mask, color_unknown, conditional);
            }
            if (depth_write) {
                texture_write_guard(GsVram::depth_address(zbase,bw,zpsm,static_cast<unsigned>(x),static_cast<unsigned>(y)));
                vram_.write_depth(zbase, bw, zpsm, static_cast<unsigned>(x), static_cast<unsigned>(y), pixel_z, conditional);
            }
        }
    }
    if(triangle)finish_triangle();else first_vertex_.reset();
}
Framebuffer GsRegisters::scanout(std::uint64_t cycle) const {
    if (!display_[0]) throw DeviceRefusal("GS scanout: PMODE not written");
    const auto pmode = *display_[0];
    const auto en = pmode & 3u;
    if (!en) throw DeviceRefusal("GS scanout: read circuits disabled");
    if (en == 3u) throw DeviceRefusal("GS scanout: dual-circuit merge is not modeled");
    if (((pmode >> 2) & 7u) != 1u || (en == 1u && (!(pmode & 32u) || ((pmode >> 8) & 255u) != 255u)))
        throw DeviceRefusal("GS scanout: CRTMD or alpha merge mode is not modeled");
    if (!mode_[1]) throw DeviceRefusal("GS scanout: SMODE2 not written");
    const bool interlaced = (*mode_[1] & 1u) != 0u;
    if (interlaced && !(*mode_[1] & 2u))
        throw DeviceRefusal("GS scanout: interlaced alternating-line fields are not modeled");
    if (*mode_[1] & 12u) throw DeviceRefusal("GS scanout: DPMS suspends output");
    const unsigned circuit = en == 1u ? 7u : 9u;
    if (!display_[circuit] || !display_[circuit + 1u])
        throw DeviceRefusal("GS scanout: DISPFB/DISPLAY not written for enabled circuit");
    const auto fb = *display_[circuit], display = *display_[circuit + 1u];
    const auto magh = static_cast<unsigned>((display >> 23) & 15u) + 1u;
    const auto magv = static_cast<unsigned>((display >> 27) & 3u) + 1u;
    const auto dw = static_cast<unsigned>((display >> 32) & 4095u) + 1u;
    const auto dh = static_cast<unsigned>((display >> 44) & 2047u) + 1u;
    // INT+FFMD reads consecutive source rows for one field. DISPLAY height
    // counts the two interlaced rasters; expose native field pixels without
    // fabricating the other field, doubling rows or applying a deinterlacer.
    const auto vertical_divisor = magv * (interlaced ? 2u : 1u);
    if (dw % magh || dh % vertical_divisor) throw DeviceRefusal("GS scanout: fractional display magnification is not modeled");
    Framebuffer f;
    f.width = dw / magh; f.height = dh / vertical_divisor; f.stride = f.width * 4u;
    f.ee_cycle = cycle; f.revision = vram_.revision();
    const auto clock = vertical_clock();
    if (clock.state == VerticalClock::State::Running)
        f.timing = FrameTiming{clock.epoch, clock.half_line, clock.field, clock.blank};
    const auto base = static_cast<unsigned>(fb & 511u) * 8192u;
    const auto bw = static_cast<unsigned>((fb >> 9) & 63u) * 64u;
    const auto psm = static_cast<unsigned>((fb >> 15) & 31u);
    if (psm > 1u) throw DeviceRefusal("GS scanout pixel format is not modeled");
    GsVram::validate_format(psm);
    const auto x0 = static_cast<unsigned>((fb >> 32) & 2047u), y0 = static_cast<unsigned>((fb >> 43) & 2047u);
    if (x0 + f.width > 2048u || y0 + f.height > 2048u)
        throw DeviceRefusal("GS scanout: wrapping display rectangle is not modeled");
    f.rgba.reserve(static_cast<std::size_t>(f.stride) * f.height);
    for (unsigned y = 0; y < f.height; ++y)
        for (unsigned x = 0; x < f.width; ++x) {
            // PCRTC single-circuit RGB does not consume framebuffer alpha.
            const auto pixel = vram_.read(base, bw, 1u, x0 + x, y0 + y);
            for (unsigned c = 0; c < 3u; ++c) f.rgba.push_back(static_cast<std::uint8_t>(pixel >> (c * 8u)));
            f.rgba.push_back(255u);
        }
    return f;
}
} // namespace ps2native
