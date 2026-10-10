// Nicholas Stash: a standalone Guild Wars mod (load it from GW Launcher's mods tab, next to GWToolbox++).
// When the game builds the tooltip of an item Nicholas the Traveler collects, this adds a short section
// saying how many of that item you have and where they are (bags, Xunlai storage pane + slot, material storage).
//
// It only READS inventory data. It never moves, uses, sells or drops items, sends nothing to the game server,
// opens no network connections and writes no files.
//
// It borrows GWToolbox++'s copy of GWCA (gwca.dll) to look up items and to run code on the game's own thread,
// so GWToolbox++ must be loaded too. If anything it needs is missing, it quietly does nothing.

#include <Windows.h>

#include <MinHook.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

    // ---- Minimal copies of the game structures we read (layouts match GWCA's GameEntities/Item.h) ----------

    struct Item;

    struct ItemArray {
        Item** buffer;
        uint32_t capacity;
        uint32_t size;
        uint32_t param;
    };

    struct Bag {
        uint32_t bag_type;
        uint32_t index;
        uint32_t unknown0;
        uint32_t container_item;
        uint32_t items_count;
        void* inventory;
        ItemArray items;
    };
    static_assert(sizeof(Bag) == 40);

    struct Item {
        uint32_t item_id;
        uint8_t unknown0[0x34 - 4];
        wchar_t* name_enc;           // +0x34
        uint8_t unknown1[0x4C - 0x38];
        uint16_t quantity;           // +0x4C
        uint8_t equipped;            // +0x4E
        uint8_t profession;          // +0x4F
        uint8_t slot;                // +0x50
        uint8_t unknown2[3];
    };
    static_assert(sizeof(Item) == 84);
    static_assert(offsetof(Item, name_enc) == 0x34);
    static_assert(offsetof(Item, quantity) == 0x4C);
    static_assert(offsetof(Item, slot) == 0x50);

    // GW::Constants::Bag
    constexpr uint8_t BagBackpack = 1;
    constexpr uint8_t BagEquipmentPack = 5;
    constexpr uint8_t BagMaterialStorage = 6;
    constexpr uint8_t BagUnclaimedItems = 7;
    constexpr uint8_t BagStorage1 = 8;
    constexpr uint8_t BagStorage14 = 21;

    // GW::Constants::InstanceType
    constexpr int InstanceOutpost = 0;

    // ---- Items Nicholas the Traveler / Nicholas Sandford collect, plus crafting ingredients -------------------
    // Encoded item names copied from GWToolbox++ (GWToolboxdll/Constants/EncStrings.h, DailyQuestsWindow.cpp).

    const wchar_t* const nicholas_item_names[] = {
        L"\x271E\xDBDF\xBBD8\x34CB", // RedIrisFlowers
        L"\x294F", // FeatheredAvicaraScalps
        L"\x8101\x43E8\xBEBD\xF09A\x4455", // MargoniteMasks
        L"\x8102\x26DC\xADF9\x8D4B\x6575", // QuetzalCrests
        L"\x56D3\xA490\xE607\x11B6", // PlagueIdols
        L"\x294D", // AzureRemains
        L"\x8101\x52E9\xA18F\x8FE1\x3EE9", // MandragorRootCake
        L"\x2960", // MahgoClaw
        L"\x56EF\xD1D8\xC773\x2C26", // MantidPincers
        L"\x8101\x43DE\xD124\xA4D9\x7D4A", // SentientSeeds
        L"\x8102\x26EA\x8A6F\xD31C\x31DD", // StoneGrawlNecklaces
        L"\x8102\x26D1", // Herring
        L"\x5702\xA954\x959D\x51B8", // NagaSkins
        L"\x296A", // GloomSeed
        L"\x2882", // CharrHide
        L"\x8101\x43F3\xF5F8\xC245\x41F2", // RubyDjinnEssence
        L"\x2930", // ThornyCarapaces
        L"\x56DD\xC82C\xB7E0\x3EB9", // BoneCharms
        L"\x8102\x26E0\xA884\xE2D3\x7E01", // ModniirManes
        L"\x8102\x26E9\x96D3\x8E81\x64D1", // SuperbCharrCarvings
        L"\x22EE\xF65A\x86E6\x1C6C", // RollsofParchment
        L"\x8101\x5208\xA22C\xC074\x2373", // RoaringEtherClaws
        L"\x8101\x5721", // BranchesofJuniBerries
        L"\x2945", // ShiverpeakManes
        L"\x293C", // FetidCarapaces
        L"\x6CCD\xC6FD\xA37B\x3529", // MoonShells
        L"\x2921", // MassiveJawbone
        L"\x8102\x26FA\x8E00\xEA86\x3A1D", // ChromaticScale
        L"\x292B", // MursaatTokens
        L"\x8101\x43FA\xA429\xC255\x23C4", // SentientLodestone
        L"\x2934", // JungleTrollTusks
        L"\x8101\x57DD\xF97D\xB5AD\x21FF", // SapphireDjinnEssence
        L"\x56E6\xB928\x9FA2\x43E1", // StoneCarving
        L"\x2919", // FeatheredCaromiScalps
        L"\x8101\x52EE\xBF76\xE319\x2B39", // PillagedGoods
        L"\x56D5\x8B0F\xAB5B\x8A6", // GoldCrimsonSkullCoin
        L"\x56D7\xDD87\x8A67\x167D", // JadeBracelets
        L"\x2924", // MinotaurHorns
        L"\x294A", // FrostedGriffonWings
        L"\x8101\x43E6\xBE4C\xE956\x780", // SilverBullionCoins
        L"\x56DF\xFEE4\xCA2D\x27A", // Truffle
        L"\x8102\x26DD\x85C5\xD98F\x5CCB", // SkelkClaws
        L"\x2923", // DessicatedHydraClaws
        L"\x294B", // FrigidHearts
        L"\x570A\x9453\x84A6\x64D4", // CelestialEssences
        L"\x2937", // PhantomResidue
        L"\x8101\x42D1\xFB15\xD39E\x5A26", // DrakeKabob
        L"\x55D0\xF8B7\xB108\x6018", // AmberChunks
        L"\x2914", // GlowingHearts
        L"\x8102\x26D8\xB5B9\x9AF6\x42D6", // SaurianBones
        L"\x8101\x5207\xEBD7\xB733\x2E27", // BehemothHides
        L"\x8101\x4E35\xD63F\xCAB4\xDD1", // LuminousStone
        L"\x2950", // IntricateGrawlNecklaces
        L"\x55D1\xD189\x845A\x7164", // JadeiteShards
        L"\x8101\x43E5\xA891\xA83A\x426D", // GoldDoubloon
        L"\x291B", // ShriveledEyes
        L"\x28EF", // IcyLodestones
        L"\x5701\xD258\xC958\x506F", // KeenOniTalon
        L"\x2910", // HardenedHumps
        L"\x8102\x26E7\xC330\xC111\x4058", // PilesofElementalDust
        L"\x56F2\x876E\xEACB\x730", // NagaHides
        L"\x22F3\xA11C\xC924\x5E15", // SpiritwoodPlanks
        L"\x293A", // StormyEye
        L"\x8101\x43F0\xFF3B\x8E3E\x20B1", // SkreeWings
        L"\x5706\xC61F\xF23D\x3C4", // SoulStones
        L"\x290F", // SpikedCrest
        L"\x56E5\x922D\xCF17\x7258", // DragonRoot
        L"\x8102\x26E3\xB76F\xE56C\x1A2", // BerserkerHorns
        L"\x292E", // BehemothJaw
        L"\x8101\x42D2\xE08B\xB81A\x604", // BowlofSkalefinSoup
        L"\x2915", // ForestMinotaurHorns
        L"\x56ED\xE607\x9B27\x7299", // PutridCysts
        L"\x2926", // JadeMandibles
        L"\x292F", // MaguumaManes
        L"\x56E0\xFBEB\xA429\x7B5", // SkullJuju
        L"\x8101\x5840\xB4F5\xB2A7\x5E0F", // MandragorSwamproots
        L"\x8101\x52EA", // BottleofVabbianWine
        L"\x8102\x26DA\x950E\x82F1\xA3D", // WeaverLegs
        L"\x291F", // TopazCrest
        L"\x56FC\xD503\x9D77\x730C", // RotWallowTusks
        L"\x2946", // FrostfireFangs
        L"\x8101\x43E7\xD854\xC981\x54DD", // DemonicRelic
        L"\x2917", // AbnormalSeeds
        L"\x8101\x43EA\xE72E\xAA23\x3C54", // DiamondDjinnEssence
        L"\x2928", // ForgottenSeals
        L"\x56D4\x8663\xA244\x50F5", // CopperCrimsonSkullCoins
        L"\x2932", // MossyMandibles
        L"\x2954", // EnslavementStones
        L"\x22E6\xE8F4\xA898\x75CB", // ElonianLeatherSquares
        L"\x8101\x43EC\x8335\xBAA8\x153C", // CobaltTalons
        L"\x288C", // MaguumaSpiderWeb
        L"\x56EB\xB8B7\xF734\x2985", // ForgottenTrinketBoxes
        L"\x2947", // IcyHumps
        L"\x8101\x43D2\x8CB3\xFC99\x602F", // SandblastedLodestone
        L"\x56FB\xA16B\x9DAD\x62B6", // BlackPearls
        L"\x8101\x43F7\xFD85\x9D52\x6DFA", // InsectCarapaces
        L"\x2911", // MergoyleSkulls
        L"\x2957", // DecayedOrrEmblems
        L"\x22E2\xCE9B\x8771\x7DC7", // TemperedGlassVials
        L"\x2943", // ScorchedLodestones
        L"\x8101\x583C\xD7B3\xDD92\x598F", // WaterDjinnEssence
        L"\x5703\xE1CC\xFE29\x4525", // GuardianMoss
        L"\x22C1", // DwarvenAles
        L"\x8102\x26D9\xABE9\x9082\x4999", // AmphibianTongues
        L"\x294E", // AlpineSeeds
        L"\x2931", // TangledSeeds
        L"\x56FA\xE3AB\xA19E\x5D6A", // StolenSupplies
        L"\x8101\x42D3\xD9E7\xD4E3\x259E", // PahnaiSalad
        L"\x5707\xF70E\xCAA2\x5CC5", // VerminHides
        L"\x8101\x52ED\x86E9\xCEF3\x69D3", // RoaringEtherHeart
        L"\x2941", // LeatheryClaws
        L"\x56FE\xF2B0\x8B62\x116A", // AzureCrest
        L"\x8102\x26E2\xC8E7\x8B1F\x716A", // JotunPelt
        L"\x8101\x583E\xE3F5\x87A2\x194F", // HeketTongues
        L"\x2951", // MountainTrollTusks
        L"\x22E7\xC1DA\xF2C1\x452A", // VialsofInk
        L"\x8101\x43E9\xDBD0\xA0C6\x4AF1", // KournanPendants
        L"\x293D", // SingedGargoyleSkulls
        L"\x56E4\xDF8C\xAD76\x3958", // DredgeIncisors
        L"\x2955", // StoneSummitBadges
        L"\x8102\x26F4\xE764\xC908\x52E2", // KraitSkins
        L"\x8101\x43D0\x843D\x98D1\x775C", // InscribedShards
        L"\x56F6\xB464\x9A9E\x11EF", // FeatheredScalps
        L"\x8101\x43EB\xF92D\xD469\x73A8", // MummyWrappings
        L"\x2916", // ShadowyRemnants
        L"\x570B\xFE7B\xBD8A\x7CF4", // AncientKappaShells
        L"\x8101\x5206\x8286\xFEFA\x191C", // Geode
        L"\x8102\x26E8\xC0DB\xD26E\x4711", // FibrousMandragorRoots
        L"\x293F", // GruesomeRibcages
        L"\x56FD\xC65F\xF6F1\x26B4", // KrakenEyes
        L"\x2918", // BogSkaleFins
        L"\x8101\x583D\xB904\xF476\x59A7", // SentientSpores
        L"\x292D", // AncientEyes
        L"\x8101\x43E4\x8D6E\x83E5\x4C07", // CopperShillings
        L"\x8102\x26DF\xF8E8\x8ACB\x58B4", // FrigidMandragorHusks
        L"\x22D5\x8371\x8ED5\x56B4", // BoltsofLinen
        L"\x28EE", // CharrCarvings
        L"\x290A", // GrawlNecklaces
        L"\x290C", // BakedHusks
        L"\x2907", // SkeletalLimbs
        L"\x2903", // UnnaturalSeeds
        L"\x2908", // EnchantedLodestones
        L"\x2906", // SkaleFinPreSearing
        L"\x28F8", // GargoyleSkulls
        L"\x28F0", // DullCarapaces
        L"\x28EA", // SpiderLegs
        L"\x28FE", // WornBelts
        L"\x8101\x4C7D\xAD41\xFB45\x79A8", // SkaleFins
        L"\x8101\x583B\xDE0B\xE373\x1070", // ChunkOfDrakeFlesh
        L"\x8101\x4C7C\xE7C2\xCDDF\x4BF3", // IbogaPetals
        L"\x8101\x5842\xB18D\xE040\x1169", // MandragorRoot
    };

    // ---- Functions borrowed from GWToolbox++'s gwca.dll ------------------------------------------------------

    struct HookEntry {
        uint8_t unused;
    };
    struct HookStatus;

    using GetBagFn = Bag*(__cdecl*)(uint8_t bag_id);
    using GetItemByIdFn = Item*(__cdecl*)(uint32_t item_id);
    using GetInstanceTypeFn = int(__cdecl*)();
    using GameThreadTaskFn = void(__cdecl*)();
    using GameThreadTickFn = void(__cdecl*)(HookStatus* status);
    using EnqueueFn = void(__cdecl*)(GameThreadTaskFn task, bool force_enqueue);
    using RegisterGameThreadCallbackFn = void(__cdecl*)(HookEntry* entry, GameThreadTickFn callback, int altitude);

    GetBagFn GetBag = nullptr;
    GetItemByIdFn GetItemById = nullptr;
    GetInstanceTypeFn GetInstanceType = nullptr;
    EnqueueFn Enqueue = nullptr;
    RegisterGameThreadCallbackFn RegisterGameThreadCallback = nullptr;
    HMODULE gwca_module = nullptr;

    // Looks up the functions in whichever gwca.dll is loaded right now. Nothing is changed unless all are found.
    bool ResolveGwca()
    {
        const auto module = GetModuleHandleW(L"gwca.dll");
        if (!module) return false;
        const auto get_bag = reinterpret_cast<GetBagFn>(GetProcAddress(module, "?GetBag@Items@GW@@YAPAUBag@2@W43Constants@2@@Z"));
        const auto get_item_by_id = reinterpret_cast<GetItemByIdFn>(GetProcAddress(module, "?GetItemById@Items@GW@@YAPAUItem@2@I@Z"));
        const auto get_instance_type = reinterpret_cast<GetInstanceTypeFn>(GetProcAddress(module, "?GetInstanceType@Map@GW@@YA?AW4InstanceType@Constants@2@XZ"));
        // Plain C exports GWCA provides for code that doesn't share its C++ types.
        const auto enqueue = reinterpret_cast<EnqueueFn>(GetProcAddress(module, "Enqueue"));
        const auto register_callback = reinterpret_cast<RegisterGameThreadCallbackFn>(GetProcAddress(module, "RegisterGameThreadCallback"));
        if (!(get_bag && get_item_by_id && get_instance_type && enqueue && register_callback)) return false;
        GetBag = get_bag;
        GetItemById = get_item_by_id;
        GetInstanceType = get_instance_type;
        Enqueue = enqueue;
        RegisterGameThreadCallback = register_callback;
        gwca_module = module;
        return true;
    }

    bool GwcaStillLoaded()
    {
        return gwca_module && GetModuleHandleW(L"gwca.dll") == gwca_module;
    }

    // ---- Finding the game's "get item description" function -------------------------------------------------
    // Same pattern GWToolbox++ uses (ItemDescriptionHandler.cpp).

    using GetItemDescriptionFn = void(__cdecl*)(uint32_t item_id, uint32_t flags, uint32_t quantity, uint32_t unk, wchar_t** out_name, wchar_t** out_desc);
    GetItemDescriptionFn GetItemDescription_Ret = nullptr;
    void __cdecl OnGetItemDescription(uint32_t item_id, uint32_t flags, uint32_t quantity, uint32_t unk, wchar_t** out_name, wchar_t** out_desc);

    uint8_t* text_start = nullptr;
    size_t text_size = 0;

    bool FindTextSection()
    {
        const auto base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
        const auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        const auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        auto section = IMAGE_FIRST_SECTION(nt);
        for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
            if (memcmp(section->Name, ".text", 5) == 0) {
                text_start = base + section->VirtualAddress;
                text_size = section->Misc.VirtualSize;
                return true;
            }
        }
        return false;
    }

    const uint8_t* JumpDestination(const uint8_t* p)
    {
        if (p[0] != 0xE9) return nullptr;
        return p + 5 + *reinterpret_cast<const int32_t*>(p + 1);
    }

    // True if the address is inside GWToolbox++'s own DLL. It is recognised by its "GWToolboxVersion" export,
    // so it still counts if the file was renamed.
    bool IsInToolbox(const uint8_t* address)
    {
        HMODULE owner = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(address), &owner)) return false;
        return owner && GetProcAddress(owner, "GWToolboxVersion") != nullptr;
    }

    // The jump GWToolbox++ writes over the start of a function it hooks.
    bool IsToolboxJump(const uint8_t* p)
    {
        const auto destination = JumpDestination(p);
        return destination && IsInToolbox(destination);
    }

    // A function start is the usual "push ebp; mov ebp, esp" (the rule GWCA uses), or GWToolbox++'s jump sitting
    // where those bytes were. Functions in Gw.exe start after padding (0xCC) or on a 16-byte boundary, so a 0xE9
    // that just happens to be part of another instruction isn't mistaken for that jump.
    bool IsFunctionStart(const uint8_t* p)
    {
        if (p[0] == 0x55 && p[1] == 0x8B && p[2] == 0xEC) return true;
        const bool after_padding = p[-1] == 0xCC || p[-1] == 0x90 || p[-1] == 0xC3;
        const bool aligned = reinterpret_cast<uintptr_t>(p) % 16 == 0;
        return (after_padding || aligned) && IsToolboxJump(p);
    }

    uint8_t* FindItemDescriptionFunction()
    {
        constexpr uint8_t pattern[] = {0x25, 0xFD, 0x00, 0x00, 0x00, 0x3C, 0xFD};
        uint8_t* match = nullptr;
        for (size_t i = 0; i + sizeof(pattern) <= text_size; ++i) {
            if (memcmp(text_start + i, pattern, sizeof(pattern)) == 0) {
                if (match) return nullptr; // more than one match: the game changed, don't guess
                match = text_start + i;
            }
        }
        if (!match) return nullptr;
        for (uint8_t* p = match; p > match - 0xFF && p > text_start; --p) {
            if (IsFunctionStart(p)) return p;
        }
        return nullptr;
    }

    // ---- Counting the hovered item -----------------------------------------------------------------------------

    struct Location {
        std::wstring place;
        uint32_t quantity;
    };

    // Views of the names in nicholas_item_names (which live for the whole session), so lookups don't allocate.
    std::unordered_set<std::wstring_view> nicholas_names;
    // Storage contents as last seen in a town, per item name. Storage can't be read outside towns.
    std::unordered_map<std::wstring_view, std::vector<Location>> storage_snapshot;
    bool have_storage_snapshot = false;
    bool seen_town = false; // been in a town since the mod started, so a missing snapshot means storage wasn't readable

    std::wstring PlaceName(const uint8_t bag_id, const uint32_t slot)
    {
        switch (bag_id) {
            case 1: return std::format(L"Backpack, slot {}", slot + 1);
            case 2: return std::format(L"Belt pouch, slot {}", slot + 1);
            case 3: return std::format(L"Bag 1, slot {}", slot + 1);
            case 4: return std::format(L"Bag 2, slot {}", slot + 1);
            case 5: return std::format(L"Equipment pack, slot {}", slot + 1);
            case BagMaterialStorage: return L"Xunlai material storage";
            default: break;
        }
        const uint32_t pane = bag_id - BagStorage1 + 1;
        if (bag_id == BagStorage14) return std::format(L"Xunlai storage pane {} (anniversary), slot {}", pane, slot + 1);
        return std::format(L"Xunlai storage pane {}, slot {}", pane, slot + 1);
    }

    template <typename Fn>
    void ForEachItemInBags(const uint8_t first_bag, const uint8_t last_bag, Fn&& fn)
    {
        for (uint8_t bag_id = first_bag; bag_id <= last_bag; ++bag_id) {
            if (bag_id == BagUnclaimedItems) continue;
            const auto bag = GetBag(bag_id);
            if (!(bag && bag->items.buffer) || bag->items.size > bag->items.capacity) continue;
            for (uint32_t i = 0; i < bag->items.size; ++i) {
                const auto item = bag->items.buffer[i];
                if (item && item->name_enc) fn(bag_id, item);
            }
        }
    }

    bool IsStorageLoaded()
    {
        for (uint8_t bag_id = BagMaterialStorage; bag_id <= BagStorage14; ++bag_id) {
            if (bag_id == BagUnclaimedItems) continue;
            const auto bag = GetBag(bag_id);
            if (bag && bag->items.buffer) return true;
        }
        return false;
    }

    // Only called on the game thread while in a town. Returns false, keeping the previous snapshot, if storage
    // can't be read here (not loaded yet, or not available in this town).
    bool RefreshStorageSnapshot()
    {
        if (!IsStorageLoaded()) return false;
        std::unordered_map<std::wstring_view, std::vector<Location>> fresh;
        ForEachItemInBags(BagMaterialStorage, BagStorage14, [&](const uint8_t bag_id, const Item* item) {
            const auto known = nicholas_names.find(item->name_enc);
            if (known == nicholas_names.end()) return;
            fresh[*known].push_back({PlaceName(bag_id, item->slot), item->quantity});
        });
        storage_snapshot = std::move(fresh);
        have_storage_snapshot = true;
        return true;
    }

    // ---- Writing into the tooltip (GW's encoded string format, same helpers GWToolbox++ uses) ----------------

    std::wstring EncodedLiteral(const std::wstring_view text)
    {
        return std::format(L"\x108\x107{}\x1", text);
    }

    std::wstring BuildTooltipSection(const std::wstring_view name, const bool storage_is_live)
    {
        std::vector<Location> locations;
        ForEachItemInBags(BagBackpack, BagEquipmentPack, [&](const uint8_t bag_id, const Item* item) {
            if (name == item->name_enc) locations.push_back({PlaceName(bag_id, item->slot), item->quantity});
        });
        if (const auto stored = storage_snapshot.find(name); stored != storage_snapshot.end()) {
            locations.insert(locations.end(), stored->second.begin(), stored->second.end());
        }

        uint32_t total = 0;
        for (const auto& location : locations) total += location.quantity;

        std::wstring lines;
        if (total == 0) {
            lines = L"You have none of these yet.";
        }
        else {
            lines = std::format(L"You have {} of these:", total);
            for (const auto& location : locations) {
                lines += std::format(L"\n  {}: {}", location.place, location.quantity);
            }
        }
        if (!storage_is_live) {
            if (have_storage_snapshot) lines += L"\n(Xunlai storage as last seen in a town)";
            else if (seen_town) lines += L"\n(Xunlai storage not available)"; // e.g. pre-Searing
            else lines += L"\n(Xunlai storage not read yet: visit a town)";
        }

        std::wstring encoded;
        size_t start = 0;
        while (start <= lines.size()) {
            const auto end = lines.find(L'\n', start);
            const auto line = std::wstring_view(lines).substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
            if (!encoded.empty()) encoded += L"\x2\x102\x2";
            encoded += EncodedLiteral(line);
            if (end == std::wstring::npos) break;
            start = end + 1;
        }
        return EncodedLiteral(L"<brx>") + L"\x2" + EncodedLiteral(L"<c=#80C8FF>") + L"\x2" + encoded + L"\x2" + EncodedLiteral(L"</c>");
    }

    // Reused between tooltips (like GWToolbox++'s own buffer), so the game always gets a pointer that stays valid.
    std::wstring tooltip_buffer;

    void AppendStashInfo(const uint32_t item_id, wchar_t** out_desc)
    {
        if (!GwcaStillLoaded()) return;
        if (*out_desc && *out_desc == tooltip_buffer.data()) return;
        const auto item = GetItemById(item_id);
        if (!(item && item->name_enc)) return;
        const auto known = nicholas_names.find(item->name_enc);
        if (known == nicholas_names.end()) return;

        const bool in_town = GetInstanceType() == InstanceOutpost;
        if (in_town) seen_town = true;
        const bool storage_is_live = in_town && RefreshStorageSnapshot();
        const auto section = BuildTooltipSection(*known, storage_is_live);

        const std::wstring_view original = *out_desc ? std::wstring_view(*out_desc) : std::wstring_view();
        std::wstring description;
        description.reserve(original.size() + 1 + section.size());
        description.append(original);
        if (!description.empty()) description += L"\x2";
        description += section;
        tooltip_buffer.assign(description);
        *out_desc = tooltip_buffer.data();
    }

    bool AppendStashInfoSafely(const uint32_t item_id, wchar_t** out_desc)
    {
        __try {
            AppendStashInfo(item_id, out_desc);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    bool disabled_after_error = false;

    void __cdecl OnGetItemDescription(uint32_t item_id, uint32_t flags, uint32_t quantity, uint32_t unk, wchar_t** out_name, wchar_t** out_desc)
    {
        GetItemDescription_Ret(item_id, flags, quantity, unk, out_name, out_desc);
        if (disabled_after_error || !out_desc) return;
        if (!AppendStashInfoSafely(item_id, out_desc)) {
            disabled_after_error = true;
            OutputDebugStringW(L"[NicholasStash] unexpected error reading items; switched off for this session\n");
        }
    }

    // ---- Installing and keeping the hook (runs on the game's own thread) -------------------------------------
    // We only ever hook on top of GWToolbox++'s hook, never before it: GWToolbox++ looks for the start of this
    // function by its "push ebp; mov ebp, esp" bytes, and our jump there would hide them.

    uint8_t* target = nullptr;
    HookEntry tick_entry{};
    std::atomic<bool> installed = false;
    bool hook_created = false;
    uint8_t toolbox_jump[5]{};          // the GWToolbox++ jump we chained onto
    uint8_t seen_jump[5]{};             // the jump seen at the target while waiting for it to settle
    ULONGLONG seen_jump_since = 0;
    ULONGLONG next_hook_check = 0;
    ULONGLONG next_storage_refresh = 0;

    constexpr ULONGLONG hook_check_every_ms = 1000;
    constexpr ULONGLONG settle_ms = 3000;
    constexpr ULONGLONG storage_refresh_every_ms = 100;

    bool TargetJumpsToUs()
    {
        return JumpDestination(target) == reinterpret_cast<const uint8_t*>(&OnGetItemDescription);
    }

    void KeepHooked(const ULONGLONG now)
    {
        if (TargetJumpsToUs()) return;
        if (!IsToolboxJump(target)) {
            seen_jump_since = 0; // GWToolbox++'s hook isn't there (yet): wait for it
            return;
        }

        if (hook_created) {
            // GWToolbox++ switched its hooks off and on again (for example on PvP maps), which wiped our jump.
            // If its jump is the same one we chained onto, put ours back; otherwise leave things alone.
            if (memcmp(target, toolbox_jump, sizeof(toolbox_jump)) != 0) return;
            MH_DisableHook(target);
            MH_EnableHook(target);
            return;
        }

        if (seen_jump_since == 0 || memcmp(target, seen_jump, sizeof(seen_jump)) != 0) {
            memcpy(seen_jump, target, sizeof(seen_jump));
            seen_jump_since = now;
            return;
        }
        if (now - seen_jump_since < settle_ms) return;

        memcpy(toolbox_jump, target, sizeof(toolbox_jump));
        if (MH_CreateHook(target, reinterpret_cast<LPVOID>(&OnGetItemDescription), reinterpret_cast<LPVOID*>(&GetItemDescription_Ret)) != MH_OK) return;
        hook_created = true;
        if (MH_EnableHook(target) == MH_OK) OutputDebugStringW(L"[NicholasStash] ready\n");
    }

    void Tick()
    {
        const auto now = GetTickCount64();
        const bool check_hook = now >= next_hook_check;
        const bool refresh_storage = now >= next_storage_refresh;
        if (!(check_hook || refresh_storage)) return;
        if (!GwcaStillLoaded() || disabled_after_error) return;
        if (check_hook) {
            next_hook_check = now + hook_check_every_ms;
            KeepHooked(now);
        }
        if (refresh_storage) {
            // Often, so moving items in storage just before leaving town is still caught.
            next_storage_refresh = now + storage_refresh_every_ms;
            if (GetInstanceType() == InstanceOutpost) {
                seen_town = true;
                RefreshStorageSnapshot();
            }
        }
    }

    void __cdecl OnGameThreadTick(HookStatus*)
    {
        __try {
            Tick();
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            disabled_after_error = true;
        }
    }

    // May be queued more than once (see StartUp); only the first run that succeeds does anything.
    void __cdecl InstallOnGameThread()
    {
        if (installed) return;
        if (MH_Initialize() != MH_OK) return;
        RegisterGameThreadCallback(&tick_entry, &OnGameThreadTick, 0);
        installed = true;
    }

    // Only catches the case the guard is for: gwca.dll was unloaded just before the call, so it faults on the very
    // first instruction, before any GWCA code runs or takes a lock. Anything else is left alone: swallowing an error
    // thrown while GWCA holds its lock would freeze the game.
    int OnlyIfEnqueueIsGone(const EXCEPTION_POINTERS* exception, const EnqueueFn enqueue)
    {
        const auto record = exception->ExceptionRecord;
        const bool gone = record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->ExceptionAddress == reinterpret_cast<void*>(enqueue);
        return gone ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
    }

    bool EnqueueInstallSafely()
    {
        const auto enqueue = Enqueue;
        __try {
            enqueue(&InstallOnGameThread, true);
            return true;
        }
        __except (OnlyIfEnqueueIsGone(GetExceptionInformation(), enqueue)) {
            return false;
        }
    }

    // ---- Start-up --------------------------------------------------------------------------------------------

    DWORD WINAPI StartUp(LPVOID)
    {
        for (const auto name : nicholas_item_names) nicholas_names.insert(name);

        if (!FindTextSection()) return 0;
        target = FindItemDescriptionFunction();
        if (!target) {
            OutputDebugStringW(L"[NicholasStash] tooltip function not found; doing nothing\n");
            return 0;
        }

        // Wait until GWToolbox++ has hooked the tooltip function (it does that only once its GWCA is fully
        // running), then hand everything else over to the game thread. If the game thread hasn't picked the task
        // up after a while, queue it again. Without GWToolbox++ this simply waits forever.
        while (!installed) {
            if ((GwcaStillLoaded() || ResolveGwca()) && IsToolboxJump(target)) {
                if (!EnqueueInstallSafely()) {
                    OutputDebugStringW(L"[NicholasStash] couldn't reach GWToolbox++'s gwca.dll; doing nothing\n");
                    return 0;
                }
                for (int waited = 0; waited < 20 && !installed; ++waited) Sleep(500);
            }
            else {
                Sleep(500);
            }
        }
        return 0;
    }
}

BOOL WINAPI DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        if (const auto thread = CreateThread(nullptr, 0, StartUp, nullptr, 0, nullptr)) CloseHandle(thread);
    }
    return TRUE;
}
