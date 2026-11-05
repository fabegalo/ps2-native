// Original BSD-3-Clause implementation. Integer GS reference subset: flat
// sprites/triangles, CT32/24, alpha/depth tests, blending and known-bit joins.
RWStructuredBuffer<uint> memory_words : register(u0);
RWStructuredBuffer<uint> memory_known : register(u1);
RWStructuredBuffer<uint> result : register(u2);
struct Primitive {
    int left; int top; int right; int bottom;
    int x0; int y0; int x1; int y1; int x2; int y2;
    uint is_triangle; uint color; uint z; uint base; uint bw; uint psm; uint zbase; uint zpsm;
    uint test; uint frame_mask; uint alpha; uint alpha_fix; uint scanmask;
    uint fba; uint fba_known; uint conditional; uint saturate; uint pabe; uint blending; uint depth_write;
};
ConstantBuffer<Primitive> p : register(b0);
uint address(uint base,uint x,uint y) {
    uint page=(y/32u)*(p.bw/64u)+x/64u;
    uint block=((x>>3u)&1u)|((y>>2u)&2u)|((x>>2u)&4u)|((y>>1u)&8u)|((x>>1u)&16u);
    uint col=(x&1u)|((y&1u)<<1u)|((x&6u)<<1u)|((y&6u)<<3u);
    return (base+page*8192u+block*256u+col*4u)&0x3fffffu;
}
int64_t edge(int ax,int ay,int bx,int by,int x,int y) {
    return int64_t(bx-ax)*int64_t(y-ay)-int64_t(by-ay)*int64_t(x-ax);
}
bool inside(int ax,int ay,int bx,int by,int x,int y) {
    int64_t w=edge(ax,ay,bx,by,x,y);
    return w>int64_t(0)||(w==int64_t(0)&&(by<ay||(by==ay&&bx>ax)));
}
bool read_word(uint at,uint mask,out uint value) {
    value=0u; // Error batches are rejected before any host VRAM is published.
    if((memory_known[at]&mask)!=mask) {InterlockedOr(result[0],1u);return false;}
    value=memory_words[at]&mask;
    return true;
}
void write_word(uint at,uint value,uint mask,uint unknown) {
    uint old=memory_words[at],knowledge=memory_known[at];
    uint next=(old&~mask)|(value&mask);
    uint next_known=(knowledge&~mask)|(mask&~unknown);
    if(p.conditional!=0u)next_known&=knowledge&~(old^next);
    memory_words[at]=next;memory_known[at]=next_known;
    InterlockedAdd(result[1],1u);
}
[numthreads(8,8,1)]
void main(uint3 dispatch : SV_DispatchThreadID) {
    int x=p.left+int(dispatch.x),y=p.top+int(dispatch.y);
    if(x>=p.right||y>=p.bottom)return;
    if((p.scanmask==2u&&(y&1)==0)||(p.scanmask==3u&&(y&1)!=0))return;
    if(p.is_triangle!=0u&&(!inside(p.x0,p.y0,p.x1,p.y1,x*16,y*16)||
        !inside(p.x1,p.y1,p.x2,p.y2,x*16,y*16)||!inside(p.x2,p.y2,p.x0,p.y0,x*16,y*16)))return;
    uint color=p.color,write_mask=p.frame_mask;
    bool color_write=true,depth_write=p.depth_write!=0u;
    if((p.test&1u)!=0u) {
        uint a=color>>24u,reference=(p.test>>4u)&255u,func=(p.test>>1u)&7u;
        bool pass=func==1u||(func==2u&&a<reference)||(func==3u&&a<=reference)||
            (func==4u&&a==reference)||(func==5u&&a>=reference)||
            (func==6u&&a>reference)||(func==7u&&a!=reference);
        if(!pass) {
            uint fail=(p.test>>12u)&3u;
            if(fail==0u)return;
            if(fail==1u)depth_write=false;
            if(fail==2u)color_write=false;
            if(fail==3u){depth_write=false;write_mask|=0xff000000u;}
        }
    }
    uint za=(p.zbase+(address(0u,uint(x),uint(y))^6144u))&0x3fffffu;
    uint zmask=(p.zpsm&15u)==0u?0xffffffffu:0x00ffffffu;
    if((p.test&0x10000u)!=0u) {
        uint func=(p.test>>17u)&3u;
        if(func==0u)return;
        if(func>=2u) {
            uint old_z;
            if(!read_word(za/4u,zmask,old_z))return;
            if(p.z<old_z||(func==3u&&p.z==old_z))return;
        }
    }
    if(color_write) {
        uint at=address(p.base,uint(x),uint(y))/4u;
        bool blend=p.blending!=0u;
        if(blend&&(color&0x80000000u)==0u)blend=p.pabe==0u;
        if(blend) {
            uint aa=p.alpha&3u,ab=(p.alpha>>2u)&3u,ac=(p.alpha>>4u)&3u,ad=(p.alpha>>6u)&3u;
            int factor=int(color>>24u);
            if(aa==ab)factor=0;
            else if(ac==2u)factor=int(p.alpha_fix&255u);
            else if(ac==1u) {
                if(p.psm==1u)factor=128;
                else {uint old;if(!read_word(at,0xffffffffu,old))return;factor=int(old>>24u);}
            }
            int sc=(int(aa==0u)-int(ab==0u))*factor+128*int(ad==0u);
            int dc=(int(aa==1u)-int(ab==1u))*factor+128*int(ad==1u);
            uint dst=0u;
            if(dc!=0&&!read_word(at,0x00ffffffu,dst))return;
            uint output_color=color&0xff000000u;
            for(uint channel=0u;channel<3u;++channel) {
                int sum=sc*int((color>>(8u*channel))&255u)+dc*int((dst>>(8u*channel))&255u);
                int value=sum>=0?sum/128:(sum-127)/128;
                uint byte_value=p.saturate!=0u?uint(clamp(value,0,255)):(uint(value)&255u);
                output_color|=byte_value<<(8u*channel);
            }
            color=output_color;
        }
        uint unknown=p.fba_known==0u&&(color&0x80000000u)==0u?0x80000000u:0u;
        if(p.fba_known!=0u&&(p.fba&1u)!=0u)color|=0x80000000u;
        if(p.psm==1u)write_mask|=0xff000000u;
        write_word(at,color,~write_mask,unknown);
        if(p.conditional==0u)InterlockedAdd(result[2],1u);
    }
    if(depth_write)write_word(za/4u,p.z,zmask,0u);
}
