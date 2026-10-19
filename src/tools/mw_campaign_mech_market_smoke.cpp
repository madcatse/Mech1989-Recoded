// Real campaign handlers, original-instruction fixtures, RAM-only GAM buffers.
#include "../main.cpp"
#include <iostream>
#include <set>

namespace {
using namespace mw::battle;
size_t checks=0;
bool captureVisuals=false;
void capture(const std::string& name,const std::vector<uint32_t>& pixels) {
    if (!captureVisuals) return;
    fs::create_directories("outputs/market_audit");
    std::ofstream out("outputs/market_audit/"+name+".ppm",std::ios::binary);
    out<<"P6\n320 200\n255\n";
    for (auto c:pixels) {
        const char rgb[]={static_cast<char>(c>>16),static_cast<char>(c>>8),static_cast<char>(c)};
        out.write(rgb,3);
    }
}
void check(bool ok,const std::string& message) {
    ++checks; if (!ok) throw std::runtime_error(message);
}
template<class F> void rejects(F action,const char* message) {
    bool rejected=false; try { action(); } catch (const std::exception&) { rejected=true; }
    check(rejected,message);
}
const std::array<std::pair<size_t,size_t>,3> marketRanges{{{0x2a6,0x2ee},{0x342,0x500},{0x70e,0x712}}};
bool isMarketByte(size_t p) {
    for (const auto& r:marketRanges) if (p>=r.first && p<r.second) return true;
    return false;
}
std::vector<uint8_t> encoded(const OriginalMechMarket& market) {
    std::vector<uint8_t> gam(0x718,0); encodeOriginalMechMarket(gam,market); return gam;
}
OriginalMechMarket generate(const OriginalMarketTables& t,uint8_t tier,uint32_t seed) {
    OriginalContractRng rng{static_cast<uint16_t>(seed),static_cast<uint16_t>(seed>>16)};
    return generateOriginalMechMarket(t,tier,rng);
}
void oracleChecks(const OriginalMarketTables& t) {
    std::ifstream file("research/fixtures/original_mech_markets.txt");
    check(static_cast<bool>(file),"missing original market instruction fixtures");
    std::string line; size_t cases=0;
    while (std::getline(file,line)) {
        if (line.empty() || line[0]=='#') continue;
        std::istringstream input(line); unsigned tier,lo,hi,finalLo,finalHi; std::string hex;
        input>>tier>>lo>>hi>>finalLo>>finalHi>>hex;
        check(static_cast<bool>(input),"malformed original market fixture");
        std::vector<uint8_t> previous(0x718);
        encodeOriginalMechMarket(previous,{});
        for (size_t p=0x2a6;p<0x2ee;++p) previous[p]=static_cast<uint8_t>(p*17+3);
        for (size_t p=0x342;p<0x38a;++p) previous[p]=static_cast<uint8_t>(p*17+3);
        OriginalContractRng rng{static_cast<uint16_t>(lo),static_cast<uint16_t>(hi)};
        const auto board=generateOriginalMechMarket(t,static_cast<uint8_t>(tier),rng,decodeOriginalMechMarket(previous));
        check(rng.word3f5==finalLo && rng.word3f7==finalHi,"original market RNG consumption differs");
        const auto actual=encoded(board); size_t at=0;
        for (const auto& range:marketRanges) for (size_t p=range.first;p<range.second;++p) {
            const auto expected=std::stoul(hex.substr(at,2),nullptr,16); at+=2;
            check(actual[p]==expected,"original market byte mismatch: case "+std::to_string(cases)+" offset "+std::to_string(p));
        }
        check(at==hex.size(),"fixture has trailing market bytes"); ++cases;
    }
    check(cases==128,"incomplete original market oracle");
}
void pureChecks(const OriginalMarketTables& t) {
    oracleChecks(t);
    check(t.classMaximums==std::array<uint8_t,3>{2,2,3} && t.classStarts==std::array<uint8_t,3>{0,3,6},"original class tables");
    for (uint8_t tier=1;tier<=4;++tier) {
        std::set<size_t> counts,models,quotas;
        for (uint32_t seed=1;seed<=1024;++seed) {
            const auto m=generate(t,tier,seed*0x9e3779b9u);
            counts.insert(m.count()); quotas.insert(m.saleQuota);
            check(m.count()<=12u/tier && m.sales==0,"count/sales reset");
            for (const auto& mech:m.slots) {
                if (mech.chassis==0xffff) continue;
                models.insert(mech.chassis);
                check(std::any_of(mech.condition.begin()+20,mech.condition.end(),[](auto v){return v!=0;}),"market armor was pristine");
                for (size_t i=20;i<29;++i) check(mech.condition[i]!=2,"original armor wrote forbidden level two");
                for (size_t a=0;a<t.ammoCounts[mech.chassis];++a)
                    check(mech.ammunition[a]<=t.ammoCapacities[mech.chassis][a] && mech.ammoTypes[a]==t.ammoTypes[mech.chassis][a],"ammo table/quantity");
            }
            if (seed<=16) {
                const auto bytes=encoded(m);
                check(encoded(decodeOriginalMechMarket(bytes))==bytes,"market round trip");
                check(encoded(generate(t,tier,seed*0x9e3779b9u))==bytes,"market determinism");
            }
        }
        check(counts.size()==12u/tier+1,"stock count endpoints missing");
        check(quotas.size()==6u-tier && *quotas.begin()==1,"quota bounds");
        check(models==std::set<size_t>{0,2,3,4,6,7,8,9},"normal chassis selection/remap");
    }
    auto market=generate(t,1,0x12345678); auto gam=encoded(market); const auto before=gam;
    encodeOriginalGamOwnedMechAmmunition(gam,3,{0,0,0,0,15,200},{0,0,0,0,7,123});
    check(decodeOriginalGamOwnedMechAmmunition(gam,3,{0,0,0,0,15,200})==std::array<int,6>{0,0,0,0,7,123},"owned ammo split lost");
    for (size_t p=0;p<gam.size();++p) {
        bool owned=false;
        for (size_t a=0;a<3;++a) for (size_t base:{0x25eu,0x2fau})
            owned |= p==base+a*24+6 || p==base+a*24+7;
        if (!owned) check(gam[p]==before[p],"owned codec overwrote other slot/market/hold");
    }
    check(gam[0x2fa+6]==4 && gam[0x312+6]==5,"owned ammo descriptors not rebuilt");
    const auto valid=gam;
    rejects([&]{encodeOriginalGamOwnedMechAmmunition(gam,0,{1,1,1,1,0,0},{});},"four owned planes accepted");
    check(gam==valid,"invalid ammo write partially mutated GAM");
    rejects([&]{originalGamOwnedMechAmmunitionOffset(3,0);},"market plane exposed as owned");
    auto poison=std::vector<uint8_t>(0x718,0xa5); const auto raw=poison;
    encodeOriginalMechMarket(poison,market);
    for (size_t p=0;p<poison.size();++p) if (!isMarketByte(p)) check(poison[p]==raw[p],"market codec overwrote unrelated save data");
    auto invalid=gam; invalid[0x38a]=13;
    rejects([&]{decodeOriginalMechMarket(invalid);},"oversize stock accepted");
    invalid=gam; invalid[0x38c]=100; invalid[0x38d]=0;
    rejects([&]{decodeOriginalMechMarket(invalid);},"invalid stock chassis accepted");
    rejects([&]{generate(t,0,1);},"tier zero accepted");
    check(originalMarketPrice(t,0,1,501,true)==1503000,"price discount/rounding order");
    check(originalMarketPrice(t,0,1,100,false)==1353000,"sell rounding order");
    check(originalMarketPrice(t,9,4,0,true)==12615000,"BattleMaster original price");
    check(originalMarketPrice(t,0,1,2000000,true)==0,"repair discount underflow");
}
}

void runCampaignMechMarketSmoke() {
    App app(findResourceRoot()); app.loadResources();
    check(!app.planets_.empty() && app.mechMarket_.initialized,"campaign initialization");
    pureChecks(app.marketTables_);
    for (size_t i=0;i<app.planets_.size();++i) {
        app.currentPlanetIndex_=static_cast<int>(i); app.generateCurrentMechMarket();
        check(app.currentPlanetEconomyTier()==(app.planets_[i].terrainCode-1)/3+1,"planet tier derivation");
        const auto before=app.captureCampaignPersistentState().fingerprint;
        auto planetSave=app.baseGamSaveData(); app.writeRuntimeToGamSave(planetSave);
        check(planetSave[0x23]==(app.planets_[i].terrainCode-1)%3 &&
            planetSave[0x27]==app.currentPlanetEconomyTier()-1,"saved planet biome/tier");
        for (int repeat=0;repeat<3;++repeat) { app.openCurrentMechMarket(); app.renderMechBuyList(); }
        check(app.captureCampaignPersistentState().fingerprint==before,"opening/rendering rerolled stock");
        for (size_t row=0;row<app.mechMarket_.mechsForSale.size();++row) {
            app.selectedMarketMechIndex_=row; app.activateMechBuyListSelection();
            app.renderMechBuyStatus(); app.renderMechBuyDamageStatus();
            check(app.selectedMarketMech()->askingPrice==app.mechBuyPrice(app.selectedMarketMech()->mech),"displayed price");
        }
    }
    app.currentPlanetIndex_=0; app.currentMonthDayCounter_=0; app.currentPeriodic14DayCounter_=0;
    app.generateCurrentMechMarket(); const auto initial=encoded(app.mechMarket_.original);
    app.advanceCampaignDays(13); check(encoded(app.mechMarket_.original)==initial,"market refreshed early");
    app.mechMarket_.original.sales=99; app.advanceCampaignDays(1);
    check(app.mechMarket_.original.sales==0 && app.currentPeriodic14DayCounter_==0,"weekly refresh");
    const auto weekly=encoded(app.mechMarket_.original); app.advanceCampaignDays(0);
    check(encoded(app.mechMarket_.original)==weekly,"zero time rerolled stock");
    app.pendingTravelPlanetIndex_=31; app.pendingTravelDays_=0; app.pendingTravelCost_=0;
    app.mechMarket_.original.sales=99; app.completeTravel();
    check(app.mechMarket_.original.sales==0 && app.currentPlanetIndex_==31,"arrival refresh");
    app.pendingTravelPlanetIndex_=0; app.completeTravel();
    check(app.mechMarket_.original.sales==0,"return travel reused stale planet cache");

    // Original fixtures import even when current RAM contains a different board.
    for (const wchar_t* name:{L"234.GAM",L"CREW5.GAM",L"CREW6.GAM",L"FIGHTTST.GAM"}) {
        const auto original=readFile(app.resourceRoot_/name);
        app.applyGamSaveToRuntime(original);
        const auto expected=encoded(decodeOriginalMechMarket(original));
        check(encoded(app.mechMarket_.original)==expected,"original market import");
        auto saved=original; app.writeRuntimeToGamSave(saved);
        check(encoded(decodeOriginalMechMarket(saved))==expected,"original market export");
    }
    auto original=readFile(app.resourceRoot_/L"234.GAM");
    // Nonempty original-format board with holes and independently nonzero ammo.
    auto seeded=generate(app.marketTables_,1,0x12345678);
    if (seeded.count()==0) seeded=generate(app.marketTables_,1,0x87654321);
    check(seeded.count()>1,"nonempty save fixture"); seeded.slots[0].chassis=0xffff;
    encodeOriginalMechMarket(original,seeded); app.applyGamSaveToRuntime(original);
    auto saved=original; app.writeRuntimeToGamSave(saved);
    check(encoded(decodeOriginalMechMarket(saved))==encoded(seeded),"save lost stock slots/condition/ammo");
    app.openCurrentMechMarket(); app.render(); capture("stock",app.framebuffer_);
    app.selectedMarketMechIndex_=0; app.activateMechBuyListSelection(); app.render();
    capture("mech_status",app.framebuffer_);
    app.mechBuyMenuIndex_=1; app.activateMechBuyStatusSelection(); app.render();
    capture("damage",app.framebuffer_);
    App restored(findResourceRoot()); restored.loadResources(); restored.applyGamSaveToRuntime(saved);
    check(encoded(restored.mechMarket_.original)==encoded(seeded),"fresh-process representation rerolled stock");
    restored.repairSession_.rng=app.repairSession_.rng; // GAM cannot restore process RNG history.
    app.advanceCampaignDays(14); restored.advanceCampaignDays(14);
    check(encoded(app.mechMarket_.original)==encoded(restored.mechMarket_.original),"refresh depends on unsaved visit serial");

    // Sale quota / original time cost / refusal / reject / purchase transfer.
    app.currentPlanetIndex_=static_cast<int>(std::find_if(app.planets_.begin(),app.planets_.end(),
        [](const auto& p){return p.economyTier==3;})-app.planets_.begin());
    OriginalMechMarket empty; empty.saleQuota=2;
    app.mechMarket_=app.makeCurrentMechMarket(empty);
    app.openCurrentMechMarket(); app.render(); capture("empty",app.framebuffer_);
    app.ownedMechs_={app.makeMech(App::ChassisId::Warhammer,0),app.makeMech(App::ChassisId::Locust,-1),app.makeMech(App::ChassisId::Jenner,-1)};
    app.ownedMechs_[0].ammoPacks={0,0,0,0,7,123};
    app.ownedMechs_[0].armorDamage[0]=3; app.finalizeLoadedMech(app.ownedMechs_[0]);
    const auto saleMech=app.originalMarketMech(app.ownedMechs_[0]);
    app.currentMonthDayCounter_=0; app.currentPeriodic14DayCounter_=0; app.playerWealth_=100000000;
    app.selectedMechIndex_=0; app.mechStatusMenuIndex_=3; app.activateMechStatusSelection();
    check(app.state_==ScreenState::MechSellOffer && app.currentMonthDayCounter_==2,"sale negotiation time");
    app.sellOfferSelectionIndex_=1; app.activateMechSellOfferSelection();
    check(app.mechMarket_.original.sales==0 && app.ownedMechs_.size()==3,"reject counted as sale");
    app.mechStatusMenuIndex_=3; app.activateMechStatusSelection(); app.activateMechSellOfferSelection();
    check(app.mechMarket_.original.sales==1 && app.ownedMechs_.size()==2,"accepted sale failed");
    check(app.mechMarket_.original.slots[0].condition==saleMech.condition &&
        app.mechMarket_.original.slots[0].ammunition==saleMech.ammunition,"sale changed mech state");
    app.selectedMarketMechIndex_=0; app.buySelectedMarketMech();
    check(app.ownedMechs_.size()==3 && app.ownedMechs_.back().ammoPacks==std::array<int,6>{0,0,0,0,7,123} &&
        app.ownedMechs_.back().armorDamage[0]==3 && app.mechMarket_.original.count()==0,"buyback changed mech/ammo/slot");
    app.mechStatusMenuIndex_=3; app.activateMechStatusSelection(); app.activateMechSellOfferSelection();
    check(app.mechMarket_.original.sales==2,"second sale count");
    const auto wealth=app.playerWealth_; const auto owned=app.ownedMechs_.size();
    app.mechStatusMenuIndex_=3; app.activateMechStatusSelection(); app.render();
    check(app.state_==ScreenState::MechSellRefused && app.playerWealth_==wealth && app.ownedMechs_.size()==owned,"sale refusal missing");
    capture("sale_refused",app.framebuffer_);
    app.sellSelectedMech(); check(app.ownedMechs_.size()==owned,"direct sale bypassed quota");
    app.currentPeriodic14DayCounter_=12; app.mechStatusMenuIndex_=3; app.activateMechStatusSelection();
    check(app.state_==ScreenState::MechSellOffer && app.mechMarket_.original.sales==0,"time refresh must precede quota check");

    // Full market accepts a permitted sale but cannot grow a thirteenth slot.
    auto full=seeded; for (auto& m:full.slots) m=saleMech;
    full.sales=0; full.saleQuota=2; app.mechMarket_=app.makeCurrentMechMarket(full);
    app.selectedMechIndex_=0; app.sellSelectedMech();
    check(app.mechMarket_.original.count()==12 && app.mechMarket_.mechsForSale.size()==12,"sale exceeded twelve-slot board");
    // Funds/cap failures leave stock and funds untouched.
    app.selectedMarketMechIndex_=0; app.playerWealth_=0; const auto stock=encoded(app.mechMarket_.original);
    app.buySelectedMarketMech(); check(app.state_==ScreenState::MechBuyCannotAfford && encoded(app.mechMarket_.original)==stock,"poor buyer changed inventory");
    app.ownedMechs_.assign(12,app.makeMech(App::ChassisId::Locust,-1)); app.playerWealth_=100000000;
    app.buySelectedMarketMech(); check(app.state_==ScreenState::MechBuyTooMany && app.playerWealth_==100000000,"ownership cap bypass");

    // Sparse owned roster and pilot references produced by original sales.
    auto sparse=original;
    for (size_t i=0;i<12;++i) app.writeU16Le(sparse,0xea+i*2,0xffff);
    app.writeU16Le(sparse,0xe8,1); app.writeU16Le(sparse,0xea+11*2,7);
    app.writeU16Le(sparse,0xc2,11);
    encodeOriginalGamOwnedMechAmmunition(sparse,11,{0,0,0,0,15,200},{0,0,0,0,6,101});
    app.applyGamSaveToRuntime(sparse);
    check(app.ownedMechs_.size()==1 && app.ownedMechs_[0].chassis==App::ChassisId::Warhammer &&
        app.ownedMechs_[0].ammoPacks[5]==101 && app.assignedMechIndexForCrewSlot(0)==0,"sparse owned slot/pilot lost");
    app.writeU16Le(sparse,0xe8,0); app.writeU16Le(sparse,0xea+11*2,0xffff);
    app.applyGamSaveToRuntime(sparse); check(app.ownedMechs_.empty(),"empty roster spawned free Jenner");
    auto bad=original; app.writeU16Le(bad,0x38a,13);
    const auto unchanged=app.captureCampaignPersistentState().fingerprint;
    rejects([&]{app.applyGamSaveToRuntime(bad);},"corrupt market accepted by campaign");
    check(app.captureCampaignPersistentState().fingerprint==unchanged,"invalid import partially changed campaign");

    // Result commit refresh, rollback and duplicate guard are exercised by the
    // existing contract smoke; market now participates in its full fingerprint.
    std::cout<<"mw_campaign_mech_market_smoke: ok checks="<<checks
        <<" original_oracle=128 planets=145 original_gam=4 shop_transactions=yes save_preservation=yes\n";
}
int main(int argc,char** argv) {
    captureVisuals=argc>1 && std::string(argv[1])=="--visual";
    try { runCampaignMechMarketSmoke(); return 0; }
    catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
