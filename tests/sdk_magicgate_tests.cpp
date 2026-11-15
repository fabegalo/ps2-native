#include "iop_compat_test_support.h"
#include "emulator/core/iop_magicgate.h"
using namespace iop_test;
using namespace ps2x::iop::detail;
namespace {
struct SecurityHost : IopHost {
    Host storage;
    bool readGuest(uint32_t a, void* p, size_t n) const override { return storage.readGuest(a, p, n); }
    bool writeGuest(uint32_t a, const void* p, size_t n) override { return storage.writeGuest(a, p, n); }
    bool zeroGuest(uint32_t a, size_t n) override { return storage.zeroGuest(a, n); }
    bool normalizeGuestAddress(uint32_t a, uint32_t& n) const override { return storage.normalizeGuestAddress(a, n); }
    uint32_t allocateIopHandle(IopHandleKind k) override { return storage.allocateIopHandle(k); }
    uint32_t allocateGuest(uint32_t n, uint32_t a) override { return storage.allocateGuest(n, a); }
    void freeGuest(uint32_t a) override { storage.freeGuest(a); }
    void audioCommand(uint32_t s, uint32_t f, GuestBuffer a, GuestBuffer b) override { storage.audioCommand(s, f, a, b); }
    std::string hostPath(HostPathKind k) const override { return storage.hostPath(k); }
    std::string translateGuestPath(std::string_view p) const override { return storage.translateGuestPath(p); }
    uint64_t openHostFile(std::string_view p) override { return storage.openHostFile(p); }
    bool hostFileSize(uint64_t h, uint64_t& n) const override { return storage.hostFileSize(h, n); }
    bool readHostFile(uint64_t h, uint64_t a, void* p, size_t n, size_t& r) override { return storage.readHostFile(h, a, p, n, r); }
    void closeHostFile(uint64_t h) override { storage.closeHostFile(h); }
    int32_t memoryCard(const MemoryCardRequest& r) override { return storage.memoryCard(r); }
    bool hasGuestFunction(uint32_t a) const override { return storage.hasGuestFunction(a); }
    bool invokeGuestFunction(uint64_t t, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t* r) override {
        return storage.invokeGuestFunction(t, a, b, c, d, e, r);
    }
    void log(LogLevel, std::string_view) override {}
    unsigned nonce=0;
    std::array<MgBlock,3> previous{};
    bool entropy=true;
    bool memoryCardKeyset(int port,uint8_t* data,size_t size) override {
        if(port<0||port>1||size!=72)return false;
        for(unsigned i=0;i<size;++i)data[i]=uint8_t(i*13+port*71+1);return true;
    }
    bool randomBytes(uint8_t* data,size_t size) override {
        if(!entropy)return false;
        for(size_t i=0;i<size;++i)data[i]=uint8_t(++nonce);return true;
    }
};
bool authenticate(SecurityHost& host,IopMagicGate& card,IopMagicGate& mecha,unsigned port,int tamper) {
    MgBlock empty{},out{};std::vector<uint8_t> response;
    const auto mc=[&](uint8_t mode,std::span<const uint8_t> input={}) {return card.card(host,port,0xf0,mode,input,out);};
    const auto sc=[&](uint8_t command,std::span<const uint8_t> input={}) {
        return mecha.mecha(host,command,input,response)&&!response.empty()&&response[0]==0;
    };
    const uint8_t cnum=uint8_t(port);
    if(!card.card(host,port,0xf3,0,{},empty)||!sc(0x80,{&cnum,1})||!card.card(host,port,0xf7,1,{},empty)||!mc(0)||!sc(0x81,{&cnum,1}))return false;
    if(!mc(1))return false;std::vector<uint8_t> material(out.begin(),out.end());
    if(!mc(2))return false;material.insert(material.end(),out.begin(),out.end());
    if(!sc(0x82,material)||!mc(3)||!mc(4)||!sc(0x83,out)||!mc(5)||!sc(0x8f)||!sc(0x84))return false;
    std::array<MgBlock,3> challenge{};std::copy_n(response.begin()+1,8,challenge[0].begin());std::copy_n(response.begin()+9,4,challenge[1].begin());
    if(!sc(0x85))return false;std::copy_n(response.begin()+1,4,challenge[1].begin()+4);std::copy_n(response.begin()+5,8,challenge[2].begin());
    if(tamper==3)challenge=host.previous;
    else if(tamper==0)host.previous=challenge;
    if(tamper==1)challenge[2][0]^=1;
    if(!mc(6,challenge[2])||!mc(7,challenge[1])||!mc(8)||!mc(9)||!mc(0xa)||!mc(0xb,challenge[0])||!mc(0xc)||!mc(0xd)||!mc(0xe)||!mc(0xf))return false;
    std::vector<uint8_t> proof(out.begin(),out.end());
    if(!mc(0x10)||!mc(0x11))return false;proof.insert(proof.end(),out.begin(),out.end());
    if(tamper==2)proof[0]^=1;
    if(!sc(0x86,proof)||!mc(0x12)||!mc(0x13)||!sc(0x87,out)||!sc(0x8f)||!mc(0x14)||!sc(0x88))return false;
    return true;
}
}
int main() {try {
    // Published DES vector; two equal TDEA keys reduce EDE to one DES encryption.
    const MgBlock plain{1,0x23,0x45,0x67,0x89,0xab,0xcd,0xef},cipher{0x85,0xe8,0x13,0x54,0x0f,0x0a,0xb4,5};
    const MgKey repeated{0x13,0x34,0x57,0x79,0x9b,0xbc,0xdf,0xf1,0x13,0x34,0x57,0x79,0x9b,0xbc,0xdf,0xf1};
    require(mgTripleDes(repeated,plain)==cipher&&mgTripleDes(repeated,cipher,true)==plain,"DES known vector failed");
    const MgKey key{1,0x23,0x45,0x67,0x89,0xab,0xcd,0xef,0xfe,0xdc,0xba,0x98,0x76,0x54,0x32,0x10};
    const MgBlock tdea{8,0xd7,0xb4,0xfb,0x62,0x9d,8,0x85};
    require(mgTripleDes(key,{})==tdea&&mgTripleDes(key,tdea,true)==MgBlock{},"Independent two-key 3DES vector failed");
    SecurityHost host;IopMagicGate card,mecha;
    for(unsigned port=0;port<2;++port) {
        require(authenticate(host,card,mecha,port,0),"Independent virtual devices could not mutually authenticate");
        require(!authenticate(host,card,mecha,port,3),"Card accepted a replayed challenge from an earlier nonce");
        require(!authenticate(host,card,mecha,port,1),"Card accepted an altered console challenge");
        require(!authenticate(host,card,mecha,port,2),"Console accepted an altered card response");
        card.reset();mecha.reset();
        require(authenticate(host,card,mecha,port,0),"Mutual authentication did not recover after reset");
    }
    MgBlock out{};require(!card.card(host,0,0xf0,0x14,{},out),"Card acknowledged an out-of-order authentication completion");
    mecha.reset();std::vector<uint8_t> response;
    require(mecha.mecha(host,0x88,{},response)&&response==std::vector<uint8_t>{0x80},"Console authenticated before receiving proof");
    host.entropy=false;bool rejected=false;
    try {(void)card.card(host,0,0xf0,0,{},out);}catch(const std::runtime_error&){rejected=true;}
    require(rejected,"Unavailable entropy silently produced an authentication nonce");
    std::cout<<"MagicGate DES/3DES vectors, mutual proof, tampering, order, entropy and reset passed\n";
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}}
