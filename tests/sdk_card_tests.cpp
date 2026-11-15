#include "Stubs/Ps2CardImage.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <memory>

namespace {
void require(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
template<class F> void rejects(F operation) {
    bool failed=false; try { operation(); } catch(const std::exception&) { failed=true; }
    require(failed,"Invalid memory-card operation was accepted");
}
}
int main(int argc,char** argv) {
    if(argc!=2) return 2;
    const auto scratch=std::filesystem::absolute(argv[1]);
    // Refuse an existing directory: cleanup owns only this newly created fixture.
    if(!std::filesystem::create_directories(scratch)) return 2;
    try {
        const auto root=scratch / std::filesystem::path(u8"cart\u00e3o");
        std::filesystem::create_directories(root / "BASAVE");
        { std::ofstream original(root / "BASAVE/data",std::ios::binary); original << "original-save"; }
        uint32_t free=0;
        std::array<uint8_t,72> identity{};
        {
            ps2card::Image card(root);
            identity=card.securityKeyset();
            require(card.securityKeyset()==identity,"Virtual-card identity changed between reads");
            require(std::any_of(identity.begin(),identity.end(),[](uint8_t value){return value!=0;}),"Virtual-card identity was not generated");
            const auto pending=root/".ps2n-card.keys.pending";
            require(std::filesystem::create_directory(pending),"Cannot inject an identity publication failure");
            rejects([&] { (void)card.securityKeyset(); });
            require(std::filesystem::remove(pending),"Cannot remove owned identity failure injection");
            require(card.securityKeyset()==identity,"Recovery replaced an existing virtual-card identity");
            const auto keys=root/".ps2n-card.keys";
            {std::fstream corrupt(keys,std::ios::in|std::ios::out|std::ios::binary);corrupt.seekp(16);corrupt.put(char(identity[8]^1));}
            rejects([&] { (void)card.securityKeyset(); });
            {std::fstream restore(keys,std::ios::in|std::ios::out|std::ios::binary);restore.seekp(16);restore.put(char(identity[8]));}
            require(card.securityKeyset()==identity,"Corrupt identity was regenerated instead of preserved");
            require(card.formatted(),"Imported memory card is not formatted");
            require(card.list("/").size()==1,"Imported directory was lost");
            std::array<uint8_t,32> data{};
            require(card.read("/BASAVE/data",0,data)==13&&std::string(data.begin(),data.begin()+13)=="original-save","Legacy save contents changed");
            rejects([&] { ps2card::Image duplicate(root); });
            free=card.freeClusters();
            card.create("/new",true); card.create("/new/file",false);
            const std::array<uint8_t,4> bytes{1,2,3,4}; card.write("/new/file",2046,bytes);
            require(card.freeClusters()==free-5,"File allocation or directory growth is wrong");
            std::array<uint8_t,2050> read{}; read.fill(0xff);
            require(card.read("/new/file",0,read)==read.size(),"File crossing cluster boundary was truncated");
            require(std::all_of(read.begin(),read.begin()+2046,[](auto v){return v==0;}),"Sparse file exposed previous data");
            require(std::equal(bytes.begin(),bytes.end(),read.end()-4),"Cluster boundary write changed data");
            const auto entry=card.stat("/new/file");
            // This formatted image's alloc_offset is 41; read the same bytes through NAND.
            const auto rawAddress=(41+entry.cluster())*2*ps2card::Image::RawPageBytes;
            std::array<uint8_t,528> page{}; card.readRaw(rawAddress,page);
            require(page[512]==0x77&&page[513]==0x7f&&page[514]==0x7f,"Zero-page ECC differs from PS2 NAND parity");
            const std::array<uint8_t,1> erased{0xff},changed{0x42};
            card.write("/new/file",0,erased); card.writeRaw(rawAddress,changed);
            require(card.read("/new/file",0,data)>0&&data[0]==0x42,"File service did not observe SIO2 NAND write");
            const auto fatAddress=18*528+entry.cluster()*4;
            std::array<uint8_t,16*528> prior{}; card.readRaw(16*528,prior);
            std::array<uint8_t,4> cycle{};
            const auto next=entry.cluster()|0x80000000;
            for(unsigned i=0;i<4;++i) cycle[i]=uint8_t(next>>(8*i));
            auto corrupted=prior;
            std::copy(cycle.begin(),cycle.end(),corrupted.begin()+fatAddress-16*528);
            card.eraseBlock(16); card.writeRaw(16*528,corrupted);
            rejects([&] { card.read("/new/file",0,data); });
            card.eraseBlock(16); card.writeRaw(16*528,prior);
            rejects([&] { card.write("/new/file",8*1024*1024-1,changed); });
            card.rename("/new/file","/BASAVE/moved");
            require(card.stat("/BASAVE/moved").size()==2050,"Cross-directory rename lost file data");
            rejects([&] { card.rename("/BASAVE","/BASAVE/inside"); });
            rejects([&] { card.remove("/BASAVE"); });
            rejects([&] { card.readRaw(ps2card::Image::Pages*528,data); });
            rejects([&] { card.eraseBlock(1); });
            rejects([&] { card.create("/../escape",false); });
            card.flush();
        }
        {
            ps2card::Image reopened(root); std::array<uint8_t,1> read{};
            require(reopened.securityKeyset()==identity,"Virtual-card identity did not survive reopening");
            require(reopened.read("/BASAVE/moved",0,read)==1&&read[0]==0x42,"NAND/file coherence did not survive reopening");
            reopened.remove("/BASAVE/moved"); reopened.remove("/new");
            require(reopened.freeClusters()==free,"Removing files leaked memory-card clusters");
            reopened.format(false); reopened.flush();
        }
        {
            ps2card::Image reopened(root);
            require(!reopened.formatted(),"Unformat did not persist");
            require(reopened.securityKeyset()==identity,"Formatting replaced the virtual-card identity");
            rejects([&] { reopened.stat("/"); });
        }
        std::ifstream original(root / "BASAVE/data"); std::string text; original >> text;
        require(text=="original-save","Migration modified the original save file");
        original.close();
        {
            const auto longRoot=scratch/std::string(90,'a')/std::string(90,'b')/std::string(90,'c');
            ps2card::Image longCard(longRoot); longCard.create("/long-path",false);
            const std::array<uint8_t,1> value{0x5a}; longCard.write("/long-path",0,value); longCard.flush();
            std::array<uint8_t,1> read{};
            require(longCard.read("/long-path",0,read)==1&&read==value,"Long host path lost card data");
        }
#ifdef _WIN32
        std::filesystem::remove_all(std::filesystem::path(L"\\\\?\\"+scratch.native()));
#else
        std::filesystem::remove_all(scratch);
#endif
        std::cout << "Memory-card import, NAND/file coherence, ECC, bounds, lease and persistence passed\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
#ifdef _WIN32
        std::filesystem::remove_all(std::filesystem::path(L"\\\\?\\"+scratch.native()));
#else
        std::filesystem::remove_all(scratch);
#endif
        return 1;
    }
}
