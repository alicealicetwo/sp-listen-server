// ============================================================================
//  sp_listen.cpp  (v170)  --  listen server for BravoHotelClient 1.3.0.473797
//
//  v170 is a CLEAN RESTRUCTURE of v169. Every hook, byte patch, RVA, offset,
//  default and switch that v169 ran with is here, arranged by subject instead
//  of by the order in which it was discovered. What is gone is the
//  measurement-only code and the experiments that were OFF by default (see
//  sp_listen_v169_backup.cpp, which stays next to this file, and
//  SP_LISTEN_NOTES.md for the list). Gameplay behaviour is intended to be
//  identical to v169 in the default configuration.
//
//  Layout (search for "//  == " to jump between sections):
//    1  logging, memory probes, patch helpers
//    2  RVA / offset tables
//    3  switches (one struct, one parser)
//    4  engine glue: names, liveness, actor lists, reflection function lists
//    5  the standalone mask
//    6  remote player registry
//    7  start-up byte patches
//    8  net mode: GetNetMode, PossessedBy, GetFunctionCallspace
//    9  world: origin pin, replay recorder, FindPlayerStart
//   10  streaming around remote players
//   11  dormancy
//   12  replication graph: AddNetworkActor, viewer, channel open, ReplicateNow
//   13  blue zone: find, tick, zone row pick, view type
//   14  loot around remote players
//   15  weapons: current weapon, magazine, reload gate, ProcessEvent
//   16  gold and loadout
//   17  pills
//   18  command file (admin panel)
//   19  crash guards and the crash reporter
//   20  Listen(), notify redirect, TickFlush, watchdog
//   21  Main / DllMain
//
//  Conventions: comments in English, log text German (ASCII only). Every
//  behaviour that changes the game has a switch that restores the previous
//  behaviour; nothing new is ON by default. All RVAs are for the exact
//  executable 1.3.0.473797 (SHA-256 16B8B421...22051F, see the notes) and are
//  verified byte-for-byte before a hook is installed.
//
//  Rules learned the hard way (v58h, v59b, v130, v132), kept as rules:
//    1. never write the RESULT field of an engine state machine, only the
//       decision in front of it (an input the engine reads);
//    2. never call engine functions from the watchdog thread -- game-thread
//       work runs from MyTickFlush, inside the MaskOff window;
//    3. never call FlushNetDormancy / ForceNetUpdate / the graph's dormancy
//       path for an actor at DORM_DormantAll (2) or DormantPartial (3) from
//       outside the graph's own per-connection pass;
//    4. every hook target is byte-verified, every trampoline is built from
//       position-independent bytes ending on an instruction boundary.
// ============================================================================
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cwchar>
#include <share.h>
#include <intrin.h>     // _ReturnAddress
#include <cmath>        // sqrtf
#include <ctime>        // time()

// ============================================================================
//  == 1. logging, memory probes, patch helpers
// ============================================================================
static FILE* g_log = nullptr;
static int   g_lines = 0;
static const int MAX_LOG_LINES = 30000;
static const int TAG_QUOTA     = 400;      // per [xx] tag, diagnostics only

struct TagCount { char tag[4]; int n; };
static TagCount g_tagCount[64];
static int      g_tagUsed = 0;

// Tags that carry decisions or answers are never throttled.
static bool TagExempt(const char* t) {
    static const char* keep[] = {
        "+", "!", "i", "e", "cmd", "gg", "ax", "cg", "gd", "mg", "ms", "mp", "sc", "ep",
        "bf", "zt", "bz", "dz", "dw", "om", "ac", "bp", "lo", "cp", "et", "vt", "pe",
        "rr", "pill", "loot", "ws", "ls", "fx", "ct", "vw", "gs", "rg", "or", "nm",
        "tf", "re", "pb", "fps", "rp", "lv", "wd", "jo", "zc", "zp", "gz", "zf", "wk",
        "lq", "ww", "wl", "sw", "mv" };
    for (int i = 0; i < (int)(sizeof(keep) / sizeof(keep[0])); ++i)
        if (strcmp(t, keep[i]) == 0) return true;
    return false;
}

static void L(const char* f, ...) {
    if (!g_log) return;
    if (g_lines == MAX_LOG_LINES) { fputs("\n[!] Log-Limit erreicht.\n", g_log); fflush(g_log); g_lines++; return; }
    if (g_lines > MAX_LOG_LINES) return;
    // Per-tag quota. Every call site starts its format string with the tag,
    // so it can be read straight off 'f'.
    if (f[0] == '[') {
        char tag[5] = { 0, 0, 0, 0, 0 };
        int k = 0;
        while (k < 4 && f[1 + k] && f[1 + k] != ']') { tag[k] = f[1 + k]; ++k; }
        if (f[1 + k] == ']' && !TagExempt(tag)) {
            TagCount* slot = nullptr;
            for (int i = 0; i < g_tagUsed; ++i)
                if (strcmp(g_tagCount[i].tag, tag) == 0) { slot = &g_tagCount[i]; break; }
            if (!slot && g_tagUsed < 64 && k <= 3) {
                slot = &g_tagCount[g_tagUsed++];
                memcpy(slot->tag, tag, 4);
                slot->n = 0;
            }
            if (slot) {
                if (slot->n > TAG_QUOTA) return;
                if (slot->n == TAG_QUOTA) {
                    ++slot->n;
                    fprintf(g_log, "[!] Tag [%s] hat sein Kontingent von %d Zeilen erreicht -- wird ab jetzt unterdrueckt.\n", tag, TAG_QUOTA);
                    fflush(g_log); ++g_lines;
                    return;
                }
                ++slot->n;
            }
        }
    }
    va_list a; va_start(a, f); vfprintf(g_log, f, a); va_end(a);
    fputc('\n', g_log); fflush(g_log); g_lines++;
}

static uintptr_t g_base = 0, g_end = 0;   // the game executable

// SEH-guarded memory access. Everything that touches game memory goes through
// these; a bad pointer yields false / null instead of a fault.
static bool SafeCopy(const void* s, void* d, size_t n) {
    __try { memcpy(d, s, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static void*   SafePtr(const void* a) { void* v = nullptr; return SafeCopy(a, &v, 8) ? v : nullptr; }
static int32_t SafeI32(const void* a) { int32_t v = 0;     return SafeCopy(a, &v, 4) ? v : 0; }
static bool SafeWriteU8(void* at, uint8_t v) {
    __try { *(volatile uint8_t*)at = v; return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static void SafeStore(void* at, void* v) {
    __try { *(void**)at = v; } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
static bool InModule(const void* p) { return (uintptr_t)p >= g_base && (uintptr_t)p < g_end; }

// ---- UE4 types we touch by layout ----------------------------------------
struct FString  { wchar_t* Data; int32_t Num; int32_t Max; };
struct FNameRaw { uint32_t Comparison, Display, Number; };   // 12 bytes

// ---- code patching --------------------------------------------------------
static bool Poke(void* at, const void* src, size_t n) {
    DWORD old;
    if (!VirtualProtect(at, n, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy(at, src, n);
    VirtualProtect(at, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, n);
    return true;
}

// Trampoline: copies the first `len` bytes of the target (they must be
// position independent and end on an instruction boundary) and jumps back
// behind them, so the original stays callable after its entry is overwritten.
static void* MakeTrampoline(uintptr_t target, const uint8_t* expect, size_t len) {
    if (memcmp((void*)target, expect, len) != 0) return nullptr;
    auto* st = (uint8_t*)VirtualAlloc(nullptr, 0x100, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!st) return nullptr;
    memset(st, 0xCC, 0x100);
    memcpy(st, (void*)target, len);
    st[len] = 0xFF; st[len + 1] = 0x25; *(uint32_t*)(st + len + 2) = 0;
    uint64_t back = target + len;
    memcpy(st + len + 6, &back, 8);
    return st;
}

// Redirects a function with a 14-byte absolute jump. The first 14 bytes must
// match `expect` (an already redirected entry is accepted as success).
static bool InstallJmp(const char* what, uintptr_t rva, const void* dest, const uint8_t* expect) {
    uint8_t* p = (uint8_t*)(g_base + rva);
    L("[i] %s @base+0x%llX: %02X %02X %02X %02X %02X %02X %02X %02X",
      what, (unsigned long long)rva, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
    uint8_t patch[14];
    patch[0] = 0xFF; patch[1] = 0x25; *(uint32_t*)(patch + 2) = 0;
    memcpy(patch + 6, &dest, 8);
    if (memcmp(p, expect, 14) != 0) {
        if (p[0] == 0xFF && p[1] == 0x25) { L("[i] %s bereits umgeleitet", what); return true; }
        L("[!] %s: unerwartete Bytes -> NICHT gepatcht", what);
        return false;
    }
    if (!Poke(p, patch, sizeof(patch))) { L("[!] %s: Patch fehlgeschlagen", what); return false; }
    L("[+] %s -> %p", what, dest);
    return true;
}

// Trampoline + jump in one step. Returns the trampoline (the callable
// original) or null; on a failed jump the trampoline is discarded too.
static void* HookFunction(const char* what, uintptr_t rva, const void* mine,
                          const uint8_t* prolog, size_t prologLen) {
    void* tr = MakeTrampoline(g_base + rva, prolog, prologLen);
    if (!tr) { L("[!] %s: Prolog passt nicht -> kein Trampolin", what); return nullptr; }
    if (!InstallJmp(what, rva, mine, prolog)) return nullptr;
    return tr;
}

// Capture stub: "mov [stub+0x80], rcx" in front of the copied prologue. Used
// once, on UEngine::LoadMap, to learn the GEngine pointer.
static uint8_t* MakeCapture(uintptr_t target, const uint8_t* expect, size_t len) {
    if (memcmp((void*)target, expect, len) != 0) return nullptr;
    auto* st = (uint8_t*)VirtualAlloc(nullptr, 0x100, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!st) return nullptr;
    memset(st, 0xCC, 0x100);
    size_t k = 0;
    st[k++] = 0x48; st[k++] = 0x89; st[k++] = 0x0D;
    *(int32_t*)(st + k) = (int32_t)(0x80 - (k + 4)); k += 4;          // mov [rip+d], rcx -> +0x80
    memcpy(st + k, (void*)target, len); k += len;
    st[k++] = 0xFF; st[k++] = 0x25; *(uint32_t*)(st + k) = 0; k += 4;
    uint64_t back = target + len; memcpy(st + k, &back, 8);
    *(void**)(st + 0x80) = nullptr;
    uint8_t patch[32]; memset(patch, 0x90, sizeof(patch));
    patch[0] = 0xFF; patch[1] = 0x25; *(uint32_t*)(patch + 2) = 0;
    void* d = st; memcpy(patch + 6, &d, 8);
    return Poke((void*)target, patch, len) ? st : nullptr;
}

// A two- to ten-byte in-place patch that is only applied when the bytes at
// the site are exactly what the analysis expected (or already patched).
static bool PatchBytes(const char* tag, const char* what, uintptr_t rva,
                       const uint8_t* expect, const uint8_t* patched, size_t n, size_t writeAt, size_t writeN) {
    auto* p = (uint8_t*)(g_base + rva);
    char hex[64] = ""; int hl = 0;
    for (size_t i = 0; i < n && hl < 56; ++i) hl += sprintf_s(hex + hl, sizeof(hex) - hl, "%02X ", p[i]);
    L("[%s] %s @base+0x%llX: %s", tag, what, (unsigned long long)rva, hex);
    if (memcmp(p, expect, n) != 0) {
        if (memcmp(p, patched, n) == 0) { L("[%s] %s bereits gepatcht", tag, what); return true; }
        L("[!] [%s] %s: unerwartete Bytes -> NICHT gepatcht", tag, what);
        return false;
    }
    if (!Poke(p + writeAt, patched + writeAt, writeN)) { L("[!] [%s] %s: Patch fehlgeschlagen", tag, what); return false; }
    return true;
}

// ============================================================================
//  == 2. RVA / offset tables
//
//  One line of provenance per entry; the full reasoning is in the v169
//  backup and in the FINDING-*.md files. "code" = read out of the binary's own
//  instructions, "sdk" = the runtime reflection dump (SDK.zip / Preservation
//  SDK), "measured" = found by watching the live object, "handoff" = from the
//  SP-hand-off notes and confirmed by disassembly.
// ============================================================================
namespace RVA {
    // --- engine entry points used by Listen() --------------------------------
    constexpr uintptr_t LoadMap                   = 0x0046B91F0;  // UEngine::LoadMap, this == GEngine (code)
    constexpr uintptr_t LoadMap_CallListen        = 0x0046BC9B1;  // "E8 0A 15 A6 FC" = call UWorld::Listen (code)
    constexpr uintptr_t CreateNamedNetDrv_World   = 0x0046A3C40;  // (GEngine, UWorld*, FName*, FName*) -- the UWorld* overload (code)
    constexpr uintptr_t GetWorldContextFromW      = 0x0046AD690;  // (GEngine, UWorld*) -> FWorldContext* (code)
    constexpr uintptr_t NotifyControlMessage      = 0x00473A100;  // UWorld::NotifyControlMessage (code)
    constexpr uintptr_t NotifyAcceptingConnection = 0x04739F50;   // UWorld::NotifyAcceptingConnection, reads World->NetDriver unchecked (code)
    constexpr uintptr_t NetModeFoldProof          = 0x04729C5E;   // "mov eax,3": UWorld::InternalGetNetMode folded to NM_Client (code)
    constexpr uintptr_t IpNetDriver_InitListen    = 0x00132C420;  // UIpNetDriver::InitListen (code)
    constexpr uintptr_t DriverInitListen_Override = 0x0012B9DC0;  // USteamNetDriver::InitListen, vtable index 83 (code)
    constexpr uintptr_t SteamPassthrough_JE       = 0x0012B9E0C;  // je -> jmp: always real UDP sockets (code)
    constexpr uintptr_t SteamSubscribedCheck_JE   = 0x0012BA736;  // BIsSubscribed() branch, 6 x NOP (code)
    constexpr uintptr_t UNetDriver_SetWorld       = 0x0043FFBB0;  // registers the driver at the world tick delegates (code)
    constexpr uintptr_t FindPlayerStart           = 0x04248DA0;   // AGameModeBase::FindPlayerStart_Implementation (code, via log string)
    constexpr uintptr_t GetFunctionCallspace      = 0x03F8CF60;   // AActor::GetFunctionCallspace, vtable slot 67 (code)
    constexpr uintptr_t UNetDriver_TickFlush      = 0x04401050;   // vtable index 98, holds ServerReplicateActors (code)
    constexpr uintptr_t RepGraph_ServerReplicateActors = 0x013C6A50; // UBasicReplicationGraph::ServerReplicateActors (code)
    constexpr uintptr_t RealRepGraph_ServerReplicate   = 0x013C6C60; // UReplicationGraph::ServerReplicateActors (code)
    constexpr uintptr_t FName_ToString            = 0x02A7A590;   // FName::ToString(FString&) (code)
    constexpr uintptr_t FMemory_Free              = 0x0261AE00;   // FMemory::Free (code)
    constexpr uintptr_t FMemory_Realloc           = 0x0016FC4F0;  // FMemory::Realloc(ptr,size,align) thunk (code)
    constexpr uintptr_t FName_Ctor                = 0x02A595B0;   // FName::FName(out, const WCHAR*, EFindName) (code)
    constexpr uintptr_t WeakObjectPtr_Get         = 0x02D26750;   // FWeakObjectPtr::Get (code)
    constexpr uintptr_t GUObjectArray_Chunks      = 0x0762F708;   // GUObjectArray.ObjObjects.Objects (code)
    constexpr uintptr_t GUObjectArray_NumElems    = 0x0762F71C;   // GUObjectArray.ObjObjects.NumElements (code)
    // --- net mode / roles ---------------------------------------------------
    constexpr uintptr_t AActor_GetNetMode         = 0x03F92730;   // real, non-inlined (code)
    constexpr uintptr_t APawn_PossessedBy         = 0x044AE710;   // (code, via SetAutonomousProxy string)
    constexpr uintptr_t AActor_SetReplicates      = 0x03FA0EB0;   // (AActor*, bool) (code)
    constexpr uintptr_t AActor_SetAutonomousProxy = 0x03F9FEC0;   // (AActor*, bool, bool) (code)
    constexpr uintptr_t APlayerController_ClassPtr = 0x07798F28;  // global holding APlayerController's UClass (code)
    constexpr uintptr_t DoInAircraft_GetNetModeRet = 0x01FEC9E1;  // return address of GetNetMode inside DoInAircraft (code)
    constexpr uintptr_t BuildingBeginPlay_GetNetModeRet = 0x01FA586C; // spawn-timer gate in ABravoHotelBuilding::BeginPlay (code)
    constexpr uintptr_t VehicleBeginPlay_GetNetModeRet  = 0x022BFA71; // same in ABravoHotelVehicleSpawnActor::BeginPlay (code)
    // --- world -------------------------------------------------------------
    constexpr uintptr_t GameInstance_StartRecordingReplay = 0x0426AF90; // crash-dump replay recorder (code)
    constexpr uintptr_t UWorld_SetNewWorldOrigin  = 0x04744230;   // origin rebasing choke point (code)
    // --- streaming ----------------------------------------------------------
    constexpr uintptr_t WorldComp_UpdateStreamingState = 0x04760F20; // (comp, FVector* locs, FVector* dirs, int n, int lod, float scale) (code)
    constexpr uintptr_t WorldComp_CommitTile      = 0x04756450;   // CommitTileStreamingState(comp, world, idx, loaded, visible, block, lod, a8, a9) (code)
    constexpr uintptr_t LevelStreaming_SetShouldBeVisible = 0x0433E0F0; // (code)
    constexpr uintptr_t LevelStreaming_SetLevelLODIndex   = 0x0433DE00; // (code)
    constexpr size_t    LevelStreaming_SetShouldBeLoadedVT = 0x268;  // vtable slot (code)
    constexpr uintptr_t CVar_SubPointDistanceScale = 0x06F7B128;  // s.SubPointLevelStreamingDistanceScale, float (code)
    // --- replication --------------------------------------------------------
    constexpr uintptr_t NetDriver_AddNetworkActor = 0x043DE4B0;   // UNetDriver::AddNetworkActor (code)
    constexpr uintptr_t FNetViewer_Ctor           = 0x043DB130;   // FNetViewer(out, conn, dt) (code)
    constexpr uintptr_t ActorChannel_SetChannelActor = 0x041AC3C0; // UActorChannel::SetChannelActor (code)
    constexpr uintptr_t GlobalActorInfoMap_Get    = 0x01395A80;   // FGlobalActorReplicationInfoMap::Get(map, AActor* const&) (code)
    constexpr uintptr_t RepGraph_ClassPolicyLookup = 0x01395990;  // (map, UClass*) -> uint8* policy or null (v58j) (code)
    constexpr uintptr_t AActor_FlushNetDormancy   = 0x03F891D0;   // (code)
    constexpr uintptr_t AActor_ForceNetUpdate     = 0x03F896C0;   // (code)
    constexpr uintptr_t AActor_SetNetDormancy     = 0x03FA09C0;   // the only runtime writer of NetDormancy (code)
    constexpr uintptr_t PackageMap_LevelVisibleJne = 0x043FEF4C;  // "Using None instead of replicated reference" branch (code)
    constexpr uintptr_t Hello_TokenCheck          = 0x0473A92D;   // NMT_Hello EncryptionToken test (code)
    constexpr uintptr_t MapFind_Guarded           = 0x011E8200;   // generic TMap find that crashed on a null map (code)
    constexpr uintptr_t ActorChanPrep             = 0x03F81A20;   // actor -> connection channel layer (code)
    constexpr uintptr_t ActorChanPrep_Body        = 0x03F81A29;   // first position-independent byte after its rel32 jump (code)
    constexpr uintptr_t GraphPerActor             = 0x013C3DB0;   // UReplicationGraph per-actor function (code)
    constexpr uintptr_t AActor_ProcessEvent       = 0x03F98AF0;   // AActor::ProcessEvent, 15-byte prologue (code)
    // --- game: loot, zone, weapons, gold ------------------------------------
    constexpr uintptr_t World_GetFirstPlayerController = 0x04732E90; // 21 bytes, re-implemented in the hook (code)
    constexpr uintptr_t Building_CheckSpawn       = 0x01FA5E60;   // ABravoHotelBuilding::CheckSpawnByStandalone (code)
    constexpr uintptr_t Vehicle_CheckSpawn        = 0x022BEF20;   // ABravoHotelVehicleSpawnActor::CheckSpawnByStandalone (code)
    constexpr uintptr_t Actor_GameState           = 0x0424C130;   // GameState of an actor's world (code)
    constexpr uintptr_t ItemSpawnBoxComp_StaticClass = 0x023C5C40; // UBravoHotelDetectItemSpawnBoxComponent::StaticClass (code)
    constexpr uintptr_t ItemSpawnComp_Spawn       = 0x02142DA0;   // (comp, float delay, uint8 mode), six gates (code)
    constexpr uintptr_t ItemSpawnComp_SpawnInner  = 0x02141FD0;   // (comp, uint8 mode), no gates (code)
    constexpr uintptr_t Character_GetCurrentWeapon = 0x02017300;  // void* (character) (handoff + code)
    constexpr uintptr_t Weapon_GetClientSetMagazineFn = 0x02526BF0; // lazy UFunction getter (code)
    constexpr uintptr_t GM_InitTableSetting       = 0x01CB7D80;   // ABattleRoyaleGameMode::InitTableSetting(int) (code)
    constexpr uintptr_t GM_InitNewPlayer          = 0x001CC38E0;  // ABattleRoyaleGameMode::InitNewPlayer (code)
    constexpr uintptr_t GM_Logout                 = 0x01CB9110;   // ABattleRoyaleGameMode::Logout (code)
    constexpr uintptr_t PS_SetGold                = 0x0222F200;   // BravoHotelPlayerState::SetGold (code)
    constexpr uintptr_t PC_PayResult              = 0x0220D240;   // OnPayResult(PC, ok, amount, action) (code)
    constexpr uintptr_t CurrencyPay_Jne           = 0x021EFA77;   // jne +4 -> jne +0x22 (code)
    constexpr uintptr_t MaterialPay_Jne           = 0x021F0457;   // same for MaterialPay (code)
    constexpr int       PlayZone_IdBase           = 520100000;    // TBL-PlayZone_OrbIsland row ID base (code)
    // --- vtable slots -------------------------------------------------------
    constexpr size_t    VT_ProcessEvent           = 66 * 8;       // UObject::ProcessEvent (code)
    constexpr size_t    AActor_GetComponentByClass_VT = 0x660;    // (code)
    constexpr size_t    AActor_ForceNetUpdate_VT  = 0x638;        // (code, PossessedBy)
    constexpr size_t    AActor_GetWorld_VT        = 0x150;        // (code, GetNetMode)
    constexpr size_t    NetDriver_GetNetMode_VT   = 0x278;        // index 79 (code)
    constexpr size_t    RepDriver_ServerReplicateActors_VT = 0x2F0; // (code)
    constexpr size_t    TickFlush_VT              = 98;           // index (code)
    constexpr size_t    InitListen_VT             = 0x298;        // index 83, fallback only (code)
    // --- ULevel actor slot obfuscation (handoff + code) ----------------------
    constexpr uint64_t  ActorPtr_XorKey           = 0x6B4D7C809C6BBCDFULL;
    constexpr uint64_t  ActorPtr_AddKey           = 0x185100513866A525ULL;
}

namespace OFF {
    // UObject / UStruct / UFunction (code)
    constexpr uintptr_t UObject_InternalIndex   = 0x00C;
    constexpr uintptr_t UObject_Name            = 0x010;   // FNameRaw, 12 bytes
    constexpr uintptr_t UObject_Class           = 0x020;
    constexpr uintptr_t UObject_Outer           = 0x028;
    constexpr uintptr_t UStruct_BaseChain       = 0x038;
    constexpr uintptr_t UStruct_NumBasesM1      = 0x040;
    constexpr uintptr_t UStruct_SuperStruct     = 0x048;
    constexpr uintptr_t UFunction_FunctionFlags = 0x0C8;
    constexpr uintptr_t UClass_DefaultObject    = 0x138;
    constexpr uintptr_t UObjectItem_Size        = 40;
    constexpr uintptr_t UObjectItem_Flags       = 0x020;
    // UWorld (sdk, three entries verified in code)
    constexpr uintptr_t UWorld_NetDriver        = 0x058;
    constexpr uintptr_t UWorld_DemoNetDriver    = 0x130;
    constexpr uintptr_t UWorld_AuthGameMode     = 0x1D0;
    constexpr uintptr_t UWorld_GameState        = 0x1D8;
    constexpr uintptr_t UWorld_Levels           = 0x1F0;   // TArray<ULevel*>
    constexpr uintptr_t UWorld_LevelCollections = 0x200;   // TArray<FLevelCollection>, stride 0x88
    constexpr size_t    LevelCollectionStride   = 0x88;
    constexpr uintptr_t LevelCollection_NetDriver = 0x010;
    constexpr uintptr_t UWorld_ControllerList   = 0x270;   // Data +0x270, Num +0x278
    constexpr uintptr_t UWorld_TimeSeconds      = 0x5C8;
    constexpr uintptr_t UWorld_OriginLocation   = 0x5E0;   // FIntVector
    constexpr uintptr_t UWorld_RequestedOriginLocation = 0x5EC;
    constexpr uintptr_t UWorld_WorldComposition = 0x608;
    constexpr uintptr_t ULevel_Actors           = 0x210;   // obfuscated slots
    constexpr uintptr_t UWorldComposition_World = 0x028;   // Outer
    // engine / context / driver (code)
    constexpr uintptr_t UEngine_NetDriverDefs   = 0xD48;
    constexpr uintptr_t Ctx_World               = 0x280;
    constexpr uintptr_t Ctx_ActiveNetDrivers    = 0x228;
    constexpr size_t    NamedNetDriverStride    = 0x10;
    constexpr uintptr_t UNetDriver_ClientConnData = 0x098;
    constexpr uintptr_t UNetDriver_ClientConnNum  = 0x0A0;
    constexpr uintptr_t UNetDriver_World        = 0x148;
    constexpr uintptr_t UNetDriver_Name         = 0x198;   // FName
    constexpr uintptr_t UNetDriver_RepDriver    = 0x708;
    constexpr uintptr_t UNetConn_PlayerController = 0x038;
    constexpr uintptr_t UNetConn_OwningActor    = 0x118;
    constexpr uintptr_t UNetConn_State          = 0x1BC;
    constexpr uintptr_t FNetViewer_ViewTarget   = 0x010;
    constexpr uintptr_t FNetViewer_ViewLocation = 0x018;
    constexpr uintptr_t RepGraph_GlobalInfoMap  = 0x0C0;   // FGlobalActorReplicationInfoMap inside the graph
    constexpr uintptr_t RepGraph_ClassPolicyMap = 0x240;   // TClassMap<EClassRepNodeMapping> inside the graph (v58j)
    // v173, from the Preservation SDK (reflection, this exe):
    constexpr uintptr_t ACharacter_Mesh              = 0x3A0;  // USkeletalMeshComponent* (read only -- never write +0x3A4)
    constexpr uintptr_t SkinnedMesh_AnimTickOption   = 0x684;  // EVisibilityBasedAnimTickOption (byte): 0 AlwaysTickPoseAndRefreshBones, 1 AlwaysTickPose, 2 OnlyTickMontagesWhenNotRendered, 3 OnlyTickPoseWhenRendered
    constexpr uintptr_t SkinnedMesh_UROByte          = 0x688;  // bEnableUpdateRateOptimizations = bit 0x02
    constexpr uintptr_t GlobalInfo_CullDistance = 0x090;   // and +0x94 squared, +0x14 bWantsToBeDormant
    // AActor (code)
    constexpr uintptr_t AActor_RootComponent    = 0x158;
    constexpr uintptr_t AActor_RemoteRole       = 0x191;
    constexpr uintptr_t AActor_bAlwaysRelevant  = 0x1CC;   // bit 0
    constexpr uintptr_t AActor_NetDormancy      = 0x20D;   // 1 Awake, 2 DormantAll, 3 DormantPartial, 4 Initial
    constexpr uintptr_t AActor_Role             = 0x20F;
    constexpr uintptr_t AActor_bOnlyRelevantToOwner = 0x214;
    constexpr uintptr_t AActor_bReplicates      = 0x289;   // bit 0
    constexpr uintptr_t AActor_Owner            = 0x2B0;
    constexpr uintptr_t AActor_bNetStartup      = 0x2D0;   // bit 1
    constexpr uintptr_t AActor_NetCullDistanceSquared = 0x2D4;
    constexpr uintptr_t APawn_Controller        = 0x358;
    constexpr uintptr_t AController_PlayerState = 0x390;
    constexpr uintptr_t USceneComp_Translation  = 0x110;
    constexpr uintptr_t UActorComp_Owner        = 0x0A8;
    constexpr uintptr_t UActorComp_World        = 0x0B0;
    // ULevelStreaming (code)
    constexpr uintptr_t LevelStreaming_LODIndex = 0x0C0;
    constexpr uintptr_t LevelStreaming_CurState = 0x0C8;   // 3 Loading, 5 MakingVisible, 6 LoadedVisible
    constexpr uintptr_t LevelStreaming_LoadedLvl = 0x140;
    // world composition tiles (code)
    constexpr uintptr_t WorldComp_Tiles         = 0x048;   // TArray, stride 0xE0
    constexpr uintptr_t WorldComp_TilesStreaming = 0x168;  // TArray<ULevelStreaming*>
    constexpr size_t    TileStride              = 0xE0;
    // game classes (sdk + code)
    constexpr uintptr_t GameState_GameViewType  = 0x4B0;   // 1 TPP, 2 FPP
    constexpr uintptr_t GameState_ZoneRef       = 0x8D0;   // measured, refreshed by FindBlueZone
    constexpr uintptr_t GameState_Phase         = 0x9F2;   // EBattleRoyaleState: 1 Waiting .. 4 Play, 5 MatchEnd
    constexpr uintptr_t GameState_SpawnedNum    = 0xAD8;   // Num of the spawned-building list
    constexpr uintptr_t GameMode_GameState      = 0x370;
    constexpr uintptr_t GameMode_GameViewType   = 0x9E8;
    constexpr uintptr_t PC_InBoundLevelsLoaded  = 0x1928;  // byte set by ServerInBoundLevelsAreLoaded
    constexpr uintptr_t PC_CheatManager         = 0x620;   // sdk
    constexpr uintptr_t PS_Wallet               = 0x5E0;   // BHReplicatedPlayStat
    constexpr uintptr_t Wallet_Gold             = 0x0E0;
    constexpr uintptr_t PS_Ledger               = 0x6E8;   // GamePlayStatistics
    constexpr uintptr_t Ledger_Rows             = 0x0F0;   // TArray<{int32 value; uint8 type}>
    constexpr uintptr_t Options_Data            = 0x1418;  // FString on the InitNewPlayer object
    constexpr uintptr_t Options_Num             = 0x1420;
    constexpr uintptr_t Options_Max             = 0x1424;
    constexpr uintptr_t Building_SpawnTimer     = 0x470;
    constexpr uintptr_t SpawnComp_Time          = 0x51C;
    constexpr uintptr_t SpawnComp_ModeA         = 0x528;
    constexpr uintptr_t SpawnComp_ModeB         = 0x529;
    constexpr uintptr_t Zone_SelectedInfoIndex  = 0x7D4;   // on the zone actor (sdk)
    constexpr uintptr_t Zone_PhaseListNum       = 0x7F0;   // ClientPlayZonePhaseList.Num
    constexpr uintptr_t Weapon_EquipFlags       = 0x94C;   // bit0 bIsEquipped, bit1 bIsServerChanged (sdk)
    constexpr uintptr_t Weapon_Magazine         = 0xDD0;   // CPF_Net + RepNotify (code)
    constexpr uintptr_t Weapon_BackPackAmmo     = 0xE50;   // CPF_Net (code)
    constexpr uintptr_t Weapon_MagazineCapacity = 0x1A28;  // not replicated (code)
    constexpr uintptr_t Weapon_PendingBolt      = 0x1FDC;  // bit0, OnRep_BoltAction (sdk)
    constexpr uintptr_t Weapon_PendingReload    = 0x24EC;  // bit0, OnRep_Reload (sdk)
    constexpr uintptr_t Weapon_State            = 0x2249;  // EWeaponState (sdk)
    enum : int     { NM_Standalone = 0, NM_DedicatedServer = 1, NM_ListenServer = 2, NM_Client = 3 };
    enum : uint8_t { ROLE_None = 0, ROLE_SimulatedProxy = 1, ROLE_AutonomousProxy = 2, ROLE_Authority = 3 };
    enum : uint8_t { DORM_Awake = 1, DORM_DormantAll = 2, DORM_DormantPartial = 3, DORM_Initial = 4 };
    enum : uint32_t { FUNC_Net = 0x40, FUNC_NetMulticast = 0x4000, FUNC_NetServer = 0x200000, FUNC_NetClient = 0x01000000 };
    enum : int     { CS_Absorbed = 0, CS_Remote = 1, CS_Local = 2 };   // bit flags (code, v45)
}

namespace PILL {   // v104, all verified to land on function starts in this exe
    constexpr uintptr_t FindBuffRow      = 0x01B6F620;
    constexpr uintptr_t FindBuffIndex    = 0x011657C0;
    constexpr uintptr_t FindSpawnRow     = 0x01F77F50;
    constexpr uintptr_t MakeFName        = 0x02A595B0;
    constexpr uintptr_t Singleton        = 0x01E9A020;
    constexpr uintptr_t GetBuffTable     = 0x01B76070;
    constexpr uintptr_t GetSpawnTable    = 0x01B941F0;
    constexpr uintptr_t AddPerkExpClass  = 0x02327E90;
    constexpr uintptr_t ConsumeRet       = 0x01BE68FC;
    constexpr uintptr_t PickerRet        = 0x01F850FC;
}

// ============================================================================
//  == 3. switches
//
//  Every field is a v169 default. A switch only ever restores the previous
//  behaviour; nothing here turns a new behaviour on. Matching is wcsstr on the
//  command line, exactly as in v169 (so "-nomagpush" also contains "-magpush";
//  the order of the tests below preserves the v169 precedence).
// ============================================================================
struct Config {
    // net mode / RPCs
    bool mask          = true;   // -nomask       standalone mask (world hides its NetDriver)
    bool actorMode     = true;   // -noactormode  AActor::GetNetMode -> NM_ListenServer for remote-owned actors
    bool rpcFix        = true;   // -norpc        client/multicast RPCs additionally routed Remote
    bool rpcRemoteOnly = false;  // -remoteonly   client RPCs ONLY remote (host loses its own)
    bool ownMode       = true;   // -noownmode    remote-owned actors (weapon, items, PlayerState) answer NM_ListenServer
    bool aircraftFix   = true;   // -noaircraftfix DoInAircraft answers NM_DedicatedServer for remote pawns
    bool beginPlayFix  = true;   // -nobeginplayfix spawn-timer BeginPlay gate always sees NM_Standalone
    // world
    bool blockReplay   = true;   // -allowreplay  crash-dump replay recorder off
    bool noRebase      = true;   // -allowrebase  world origin pinned at {0,0,0}
    bool levelVis      = true;   // -nolevelvis   always send references into streaming sublevels
    bool ignoreEncToken = true;  // -noenctoken   NMT_Hello with EncryptionToken takes the token-less path
    bool payPatch      = true;   // -nopay        in-round gold/material payments succeed at once
    // streaming
    bool streamRemote  = true;   // -nostreamremote tiles stream around remote players too
    bool remoteFirst   = true;   // -nohostfirst  remote players are the primary streaming point
    bool hostPoint     = false;  // -hostpoint    host keeps a point even in the aircraft
    bool subScale      = true;   // -nosubscale   s.SubPointLevelStreamingDistanceScale -> 1.0
    bool commitFix     = true;   // -nocommitfix  tiles at a remote player: loaded, visible, LOD -1
    bool blockLoad     = true;   // -noblockload  tile under a landed remote player loads blocking (max 24)
    bool pullTiles     = true;   // -nopulltiles  tiles around remote players requested by us
    bool viewFix       = true;   // -noviewfix    graph viewpoint of a remote connection moved onto its pawn
    // dormancy
    bool dormBlock     = true;   // -nodormblock  SetNetDormancy(DormantAll/Partial) refused for dynamic actors
    bool dormBlockLevel = false; // -dormlevel    ... for level actors as well
    bool dormAllowWeapons = false; // -dormallowweapons weapons may sleep (A/B)
    bool dormWake      = true;   // -nodormwake   level actors at DORM_Initial woken before the graph sees them
    bool wakeDoorsOnly = false;  // -wakedoors    ... only doors/windows/breakables
    bool dormFix       = true;   // -nodormfix    FlushNetDormancy/ForceNetUpdate run with the driver visible
    bool wakeUp        = true;   // -nowakeup     dormant actors around remote players flushed + registered
    // zone
    bool zoneFix       = true;   // -nozonefix    the blue zone work in ZoneTick
    bool zoneRelevant  = true;   // -nozonerelevant zone class/instance bAlwaysRelevant, graph cull copy zeroed
    bool gsKick        = true;   // -nogskick     GameState ForceNetUpdate during prematch
    bool zonePin       = true;   // -nozonepin    zone actor kept DORM_Awake
    bool zoneToggle    = true;   // -nozonetoggle one-frame reference change when the zone channel opens
    bool ownRelevant   = true;   // -noownrelevant graph cull copy zeroed for remote-owned actors
    int  zonePlayers   = 0;      // -zoneplayers=N fixed table (0 = tiers, or game random when tiers off)
    int  zoneId        = -1;     // -zoneid=N     fixed row
    bool zoneScale     = true;   // off once -zoneplayers= / -zoneid= is given
    int  forceView     = 0;      // -fpp 2 / -tpp 1
    // loot
    bool spawnRemote   = true;   // -nospawnremote building/vehicle spawn check run for remote pawns
    bool lootScan      = true;   // -nolootscan   actors near remote players asked for a spawn component
    bool directSpawn   = true;   // -nodirectspawn ... and fired
    bool gateBypass    = true;   // -nogatebypass gates 1/2/3/5/6 of 0x2142DA0 stepped over
    // weapons
    bool magTrack      = true;   // -nomagtrack   host magazine decremented per ServerFireProjectile
    bool magPush       = true;   // -nomagpush    ClientSetMagazine on increase
    bool servChanged   = true;   // -noservchanged bIsServerChanged set on the held weapon
    bool boltFinish    = true;   // -noboltfinish bPendingBoltAction cleared after boltHoldMs
    bool reloadFinish  = true;   // -noreloadfinish bPendingReload cleared after 6 s
    int  boltHoldMs    = 600;    // -boltholdms=N
    bool emptyPulse    = true;   // -noemptypulse bIsServerChanged pulsed 1->0->1 while a weapon is held
    bool pulseOnlyEmpty = false; // -pulseonlyempty ... only while the magazine is empty
    int  emptyPulseMs  = 900;    // -emptypulsems=N
    bool peLog         = true;   // -nopelog      [pe]/[rr] lines from the ProcessEvent hook
    bool rpcNames      = false;  // -rpcnames     v171: log every distinct RPC/function name once ([rn] lines)
    bool perkWake      = true;   // -noperkwake   v171: ForceNetUpdate remote pawn/controller/PlayerState on phase 3 and 4
    bool perkPush      = true;   // -noperkpush   v172: ForceNetUpdate pawn/controller/PlayerState right after ClientAddPerkLevel & co.
    bool doorPush      = true;   // -nodoorpush   v172: doors/destructibles near a remote player: cull 0 + flush + ForceNetUpdate on use
    bool zonePolicy    = false;  // -zonepolicy   v172: route BP_BlueZone_C to the graph's RelevantAllConnections node (policy byte 1)
    bool animTick      = true;   // -noanimtick   v173: remote pawns' mesh always ticks its animation on the host (montage notifies: kick, reload, ...)
    // gold / loadout / panel
    bool goldGain      = true;   // -nogoldgain   looted gold booked at Logout
    bool loadout       = false;  // -loadout      loadouts.txt injected into the join options
    bool cmdFile       = false;  // -cmdfile[=path]
    wchar_t cmdPath[512] = L"";
    // guards
    bool crashLog      = true;   // -nocrashlog
    bool mapGuard      = true;   // -nomapguard
    bool chanGuard     = true;   // -nochanguard
    bool graphGuard    = true;   // -nographguard
    bool pills         = true;   // -nopills
};
static Config g_cfg;

struct ZoneTier { int rows; int maxPassengers; };
static ZoneTier g_zoneTiers[8] = { {16,16},{24,24},{40,40},{64,64},{80,999} };
static int      g_zoneTierN    = 5;

static void ParseZoneTiers(const wchar_t* spec) {
    int n = 0;
    while (spec && *spec && n < 8) {
        int rows = _wtoi(spec); const wchar_t* c = wcschr(spec, L':'); if (!c) break;
        int mx = _wtoi(c + 1);
        if (rows > 0 && mx > 0) { g_zoneTiers[n].rows = rows; g_zoneTiers[n].maxPassengers = mx; ++n; }
        const wchar_t* k = wcschr(c, L','); if (!k) break; spec = k + 1;
    }
    if (n > 0) g_zoneTierN = n;
}

static void ParseCommandLine() {
    const wchar_t* cl = GetCommandLineW();
    if (!cl) return;
    auto has = [cl](const wchar_t* s) { return wcsstr(cl, s) != nullptr; };
    Config& c = g_cfg;
    if (has(L"-nomask"))        { c.mask = false;          L("[i] -nomask erkannt -- Maske aus"); }
    if (has(L"-noactormode"))   { c.actorMode = false;     L("[i] -noactormode erkannt -- Actor-Netzmodus-Hook AUS"); }
    if (has(L"-norpc"))         { c.rpcFix = false;        L("[i] -norpc erkannt -- RPC-Weiche AUS"); }
    if (has(L"-remoteonly"))    { c.rpcRemoteOnly = true;  L("[i] -remoteonly erkannt -- Client-RPCs NUR verschicken (Laptop = reiner Server)"); }
    if (has(L"-noownmode"))     { c.ownMode = false;       L("[i] -noownmode erkannt -- eigene Actors des Remote-Spielers bekommen wieder NM_Standalone"); }
    if (has(L"-noaircraftfix")) { c.aircraftFix = false;   L("[i] -noaircraftfix erkannt -- DoInAircraft bleibt NM_ListenServer (kein ClientInAircraft)"); }
    if (has(L"-nobeginplayfix")){ c.beginPlayFix = false;  L("[i] -nobeginplayfix erkannt -- BeginPlay-NetMode der Spawn-Timer bleibt wie im Original"); }
    if (has(L"-allowreplay"))   { c.blockReplay = false;   L("[i] -allowreplay erkannt -- Replay-Recorder NICHT unterdrueckt (Crash-Risiko!)"); }
    if (has(L"-allowrebase"))   { c.noRebase = false;      L("[i] -allowrebase erkannt -- Welt-Ursprung darf wieder wandern"); }
    if (has(L"-nolevelvis"))    { c.levelVis = false;      L("[i] -nolevelvis erkannt -- Sublevel-Referenzen bleiben wie im Original"); }
    if (has(L"-noenctoken"))    { c.ignoreEncToken = false;L("[i] -noenctoken erkannt -- EncryptionToken geht wieder an den Delegate (Lobby-Joins werden abgewiesen)"); }
    if (has(L"-nopay"))         { c.payPatch = false;      L("[i] -nopay erkannt -- Gold-/Materialkauf im Match bleibt ohne Ergebnis"); }
    if (has(L"-nostreamremote")){ c.streamRemote = false;  L("[i] -nostreamremote erkannt -- Host streamt nur um sich selbst"); }
    if (has(L"-nohostfirst"))   { c.remoteFirst = false;   L("[i] -nohostfirst erkannt -- Host ist Primaerpunkt, Remote-Spieler nur Unterpunkt"); }
    if (has(L"-hostpoint"))     { c.hostPoint = true;      L("[i] -hostpoint erkannt -- der Host behaelt immer einen eigenen Streaming-Punkt"); }
    if (has(L"-nosubscale"))    { c.subScale = false;      L("[i] -nosubscale erkannt -- Sub-Point-Streaming-Skala bleibt 0.5"); }
    if (has(L"-nocommitfix"))   { c.commitFix = false;     L("[i] -nocommitfix erkannt -- Kacheln um den Remote-Spieler bekommen die LOD-Entscheidung der Engine"); }
    if (has(L"-noblockload"))   { c.blockLoad = false;     L("[i] -noblockload erkannt -- die Kachel unter dem gelandeten Remote-Spieler wird nicht blockierend geladen"); }
    if (has(L"-nopulltiles"))   { c.pullTiles = false;     L("[i] -nopulltiles erkannt -- Kacheln um den Remote-Spieler werden nicht selbst nachgefordert"); }
    if (has(L"-noviewfix"))     { c.viewFix = false;       L("[i] -noviewfix erkannt -- Blickpunkt der Verbindungen wird nur gemessen, nicht korrigiert"); }
    if (has(L"-nodormblock"))   { c.dormBlock = false;     L("[i] -nodormblock erkannt -- SetNetDormancy(DormantAll) bleibt erlaubt"); }
    if (has(L"-dormlevel"))     { c.dormBlockLevel = true; L("[i] -dormlevel erkannt -- auch im Level platzierte Actors duerfen nicht mehr einschlafen"); }
    if (has(L"-dormallowweapons")) { c.dormAllowWeapons = true; L("[i] -dormallowweapons erkannt -- Waffen duerfen wieder einschlafen (A/B-Test)"); }
    if (has(L"-nodormwake"))    { c.dormWake = false;      L("[i] -nodormwake erkannt -- DORM_Initial-Level-Actors bleiben fuer den Graphen unsichtbar"); }
    if (has(L"-wakedoors"))     { c.wakeDoorsOnly = true;  L("[i] -wakedoors erkannt -- nur Tueren/Fenster/Zerbrechliches werden geweckt"); }
    if (has(L"-nodormfix"))     { c.dormFix = false;       L("[i] -nodormfix erkannt -- Dormancy-Weckrufe bleiben maskiert"); }
    if (has(L"-nowakeup"))      { c.wakeUp = false;        L("[i] -nowakeup erkannt -- schlafende Actors um den Remote-Spieler werden nicht geweckt"); }
    if (has(L"-nozonefix"))     { c.zoneFix = false;       L("[i] -nozonefix erkannt -- BlueZone wird nicht angestupst"); }
    if (has(L"-nozonerelevant")){ c.zoneRelevant = false;  L("[i] -nozonerelevant erkannt -- Zonenklasse wird NICHT bAlwaysRelevant"); }
    if (has(L"-nogskick"))      { c.gsKick = false;        L("[i] -nogskick erkannt -- GameState wird nicht zusaetzlich angestossen"); }
    if (has(L"-nozonepin"))     { c.zonePin = false;       L("[i] -nozonepin erkannt -- die Zone wird NICHT wachgehalten"); }
    if (has(L"-nozonetoggle"))  { c.zoneToggle = false;    L("[i] -nozonetoggle erkannt -- die Zonen-Referenz wird NICHT neu angestossen"); }
    if (has(L"-noownrelevant")) { c.ownRelevant = false;   L("[i] -noownrelevant erkannt -- die Graph-Cull-Kopie der eigenen Actors bleibt, wie sie ist"); }
    if (const wchar_t* z = wcsstr(cl, L"-zoneplayers=")) { c.zonePlayers = _wtoi(z + 13); c.zoneScale = false;
        L("[i] -zoneplayers=%d erkannt -- %s", c.zonePlayers, c.zonePlayers ? "feste Tabelle dieser Spielerzahl" : "Zonenwahl bleibt dem Spiel ueberlassen"); }
    if (const wchar_t* z = wcsstr(cl, L"-zoneid="))      { c.zoneId = _wtoi(z + 8); c.zoneScale = false; L("[i] -zoneid=%d erkannt -- feste Zonen-Zeile", c.zoneId); }
    if (const wchar_t* z = wcsstr(cl, L"-zonetiers="))   { ParseZoneTiers(z + 11); L("[i] -zonetiers erkannt -- %d Stufen", g_zoneTierN); }
    if (c.zoneScale) {
        char t[160] = "";
        for (int i = 0; i < g_zoneTierN; ++i) { char b[32]; sprintf_s(b, "%s%d:<=%d", i ? ", " : "", g_zoneTiers[i].rows, g_zoneTiers[i].maxPassengers); strcat_s(t, b); }
        L("[i] Zonenstufen (Tabelle:<=Passagiere): %s -- waechst mit den Beitritten bis zum Start", t);
    }
    if (has(L"-fpp"))           { c.forceView = 2;         L("[i] -fpp erkannt -- die Runde wird auf FPP (GameViewType 2) gezwungen"); }
    if (has(L"-tpp"))           { c.forceView = 1;         L("[i] -tpp erkannt -- die Runde wird auf TPP (GameViewType 1) gezwungen"); }
    if (has(L"-nospawnremote")) { c.spawnRemote = false;   L("[i] -nospawnremote erkannt -- Loot-Spawn-Pruefung nur fuer den Host"); }
    if (has(L"-nolootscan"))    { c.lootScan = false;      L("[i] -nolootscan erkannt -- Actors werden nicht auf Loot-Komponenten geprueft"); }
    if (has(L"-nodirectspawn")) { c.directSpawn = false;   L("[i] -nodirectspawn erkannt -- gefundene Loot-Quellen werden nur gezaehlt, nicht ausgeloest"); }
    if (has(L"-nogatebypass"))  { c.gateBypass = false;    L("[i] -nogatebypass erkannt -- Loot-Quellen nur ueber 0x2142DA0, kein Umgehen der Tore"); }
    if (has(L"-nomagtrack"))    { c.magTrack = false;      L("[i] -nomagtrack erkannt -- Schuesse werden NICHT vom Magazin abgezogen"); }
    if (has(L"-nomagpush"))     { c.magPush = false;       L("[i] -nomagpush erkannt -- ClientSetMagazine-Push AUS"); }
    if (has(L"-noservchanged")) { c.servChanged = false;   L("[i] -noservchanged erkannt -- bIsServerChanged wird NICHT gesetzt"); }
    if (has(L"-noboltfinish"))  { c.boltFinish = false;    L("[i] -noboltfinish erkannt -- bPendingBoltAction wird NICHT mehr beendet"); }
    if (has(L"-noreloadfinish")){ c.reloadFinish = false;  L("[i] -noreloadfinish erkannt -- ein haengendes bPendingReload wird nicht mehr geloescht"); }
    if (const wchar_t* q = wcsstr(cl, L"-boltholdms="))   { int v = _wtoi(q + 12); if (v >= 0 && v <= 5000) { c.boltHoldMs = v; L("[i] -boltholdms=%d erkannt", v); } }
    if (has(L"-noemptypulse"))  { c.emptyPulse = false;    L("[i] -noemptypulse erkannt -- bIsServerChanged wird NICHT mehr gepulst"); }
    if (has(L"-pulseonlyempty")){ c.pulseOnlyEmpty = true; L("[i] -pulseonlyempty erkannt -- Nachlade-Freigabe nur bei leerem Magazin erneuert (v165-Verhalten)"); }
    if (const wchar_t* q = wcsstr(cl, L"-emptypulsems=")) { int v = _wtoi(q + 14); if (v >= 200 && v <= 10000) { c.emptyPulseMs = v; L("[i] -emptypulsems=%d erkannt", v); } }
    if (has(L"-nopelog"))       { c.peLog = false;         L("[i] -nopelog erkannt -- ProcessEvent wird nicht mitgeschrieben (die Schusszaehlung laeuft weiter)"); }
    if (has(L"-rpcnames"))      { c.rpcNames = true;       L("[i] -rpcnames erkannt -- jeder Funktionsname wird einmal mitgeschrieben ([rn], Diagnose fuer Fensterbruch/Bewegung/Perks)"); }
    if (has(L"-noperkwake"))    { c.perkWake = false;      L("[i] -noperkwake erkannt -- kein ForceNetUpdate der Remote-Spieler beim Phasenwechsel"); }
    if (has(L"-noperkpush"))    { c.perkPush = false;      L("[i] -noperkpush erkannt -- kein ForceNetUpdate nach den Perk-RPCs"); }
    if (has(L"-nodoorpush"))    { c.doorPush = false;      L("[i] -nodoorpush erkannt -- Tueren/Fenster werden nach Benutzung nicht nachgeschoben"); }
    if (has(L"-zonepolicy"))    { c.zonePolicy = true;     L("[i] -zonepolicy erkannt -- Zonenklasse wird im Graphen auf RelevantAllConnections (1) gesetzt"); }
    if (has(L"-noanimtick"))    { c.animTick = false;      L("[i] -noanimtick erkannt -- Animations-Tick der Remote-Pawns bleibt wie vom Spiel gesetzt"); }
    if (has(L"-nogoldgain"))    { c.goldGain = false;      L("[i] -nogoldgain erkannt -- Beute aus der Runde bleibt in der Runde"); }
    if (has(L"-loadout"))       { c.loadout = true;        L("[i] -loadout erkannt -- Gold/Level/Outfit werden aus loadouts.txt in die Join-Optionen eingespielt"); }
    if (const wchar_t* k = wcsstr(cl, L"-cmdfile")) {
        c.cmdFile = true;
        if (k[8] == L'=') {
            const wchar_t* p = k + 9;
            bool q = (*p == L'"'); if (q) ++p;
            int n = 0;
            while (*p && n < 500 && (q ? *p != L'"' : *p != L' ')) c.cmdPath[n++] = *p++;
            c.cmdPath[n] = 0;
        }
        L("[i] -cmdfile erkannt -- Befehle des Admin-Panels werden aus %ls gelesen", c.cmdPath[0] ? c.cmdPath : L"sp_listen_cmd.txt neben dieser DLL");
    }
    if (has(L"-nocrashlog"))    { c.crashLog = false;      L("[i] -nocrashlog erkannt -- Abstuerze werden nicht mitgeschrieben"); }
    if (has(L"-nomapguard"))    { c.mapGuard = false;      L("[i] -nomapguard erkannt -- Null-Map-Schutz AUS"); }
    if (has(L"-nochanguard"))   { c.chanGuard = false;     L("[i] -nochanguard erkannt -- Kanal-Schutz AUS"); }
    if (has(L"-nographguard"))  { c.graphGuard = false;    L("[i] -nographguard erkannt -- Graph-Actor-Schutz AUS"); }
    if (has(L"-nopills"))       { c.pills = false;         L("[i] -nopills erkannt -- Kapsel-/Loot-Reparatur bleibt aus"); }
}

// ============================================================================
//  == 4. engine glue: names, liveness, actor lists, reflection function lists
// ============================================================================
typedef void (__fastcall* tFNameToString)(void* fnamePtr, void* fstringOut);
typedef void (__fastcall* tFree)(void* ptr);
typedef void (__fastcall* tProcessEvent)(void* obj, void* fn, void* params);
typedef int  (__fastcall* tCallspace)(void* actor, void* fn, void* stack);

// This build has a shifted UObject layout, so the NamePrivate offset is probed
// once at runtime and cached.
static int       g_nameOff = -1;
static const int g_nameOffTry[] = { 0x18, 0x10, 0x20, 0x28, 0x0C, 0x30 };

// Name of any UObject (UFunction, UClass, ...). Probes the NamePrivate offset
// until one yields printable text, then caches it.
static bool ResolveFuncName(void* fn, wchar_t* out, int cap) {
    if (!fn) return false;
    auto toStr  = (tFNameToString)(g_base + RVA::FName_ToString);
    auto freeFn = (tFree)(g_base + RVA::FMemory_Free);
    int order[8]; int n = 0;
    if (g_nameOff >= 0) order[n++] = g_nameOff;
    for (int i = 0; i < (int)(sizeof(g_nameOffTry) / sizeof(int)); ++i) order[n++] = g_nameOffTry[i];
    for (int i = 0; i < n; ++i) {
        const int off = order[i];
        int32_t cmpIdx = SafeI32((uint8_t*)fn + off);
        if (cmpIdx <= 0 || cmpIdx > 0x2000000) continue;
        FString fs{ nullptr, 0, 0 };
        bool ok = false;
        __try {
            toStr((uint8_t*)fn + off, &fs);
            if (fs.Data && fs.Num > 1 && fs.Num < 200) {
                wchar_t c0 = fs.Data[0];
                if (c0 > 32 && c0 < 127) {
                    int k = 0;
                    for (; k < fs.Num - 1 && k < cap - 1; ++k) out[k] = fs.Data[k];
                    out[k] = 0;
                    ok = true;
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
        if (fs.Data) { __try { freeFn(fs.Data); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
        if (ok) { g_nameOff = off; return true; }
    }
    return false;
}

static bool ClassNameOf(void* obj, wchar_t* out, int cap) {
    void* cls = SafePtr((uint8_t*)obj + OFF::UObject_Class);
    if (!cls || !SafePtr(cls)) return false;
    return ResolveFuncName(cls, out, cap);
}

// FName lookup WITHOUT creating the name (FNAME_Find = 0 -> NAME_None when unknown).
static bool FindNameKey(const wchar_t* s, FNameRaw* out) {
    typedef FNameRaw* (__fastcall* tCtor)(FNameRaw* out, const wchar_t* str, int findType);
    auto ctor = (tCtor)(g_base + RVA::FName_Ctor);
    FNameRaw k = { 0, 0, 0 };
    bool ok = false;
    __try { ctor(&k, s, 0); ok = true; } __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    if (!ok || k.Comparison == 0) return false;
    *out = k;
    return true;
}

// ---- liveness -------------------------------------------------------------
// GUObjectArray lookup the way the engine inlines it. In THIS build the item's
// object pointer is obfuscated, so the layout check fails and IsObjectAlive is
// useless (v59). MaybeAlive filters only when the layout was confirmed usable.
static int g_objArrayState = 0;   // 0 unknown, 1 usable, -1 layout mismatch

static bool IsObjectAlive(void* obj) {
    if (!obj) return false;
    int32_t idx = SafeI32((uint8_t*)obj + OFF::UObject_InternalIndex);
    int32_t num = SafeI32((void*)(g_base + RVA::GUObjectArray_NumElems));
    if (idx < 0 || idx >= num) return false;
    void* chunks = SafePtr((void*)(g_base + RVA::GUObjectArray_Chunks));
    if (!chunks) return false;
    uint8_t* chunk = (uint8_t*)SafePtr((uint8_t*)chunks + (size_t)(idx >> 16) * 8);
    if (!chunk) return false;
    uint8_t* item = chunk + (size_t)(idx & 0xFFFF) * OFF::UObjectItem_Size;
    if (SafePtr(item) != obj) return false;
    uint32_t flags = (uint32_t)SafeI32(item + OFF::UObjectItem_Flags);
    return (flags & 0x30000000u) == 0;
}
static bool MaybeAlive(void* obj) {
    if (!obj) return false;
    return (g_objArrayState > 0) ? IsObjectAlive(obj) : true;
}
static void CalibrateObjectArray(void* knownAlive) {
    if (g_objArrayState != 0) return;
    g_objArrayState = IsObjectAlive(knownAlive) ? 1 : -1;
    L("[ws] GUObjectArray-Layout %s (Probe mit World %p)",
      g_objArrayState > 0 ? "bestaetigt -> Lebendigkeitspruefung aktiv" : "ABWEICHEND -> nur Zeiger-Plausibilitaet", knownAlive);
}

// Does this look like a live UObject? First 8 bytes = vtable inside the module.
static bool LooksLikeActor(void* a) {
    if (!a || (uintptr_t)a < 0x10000) return false;
    void* vt = SafePtr(a);
    return vt && InModule(vt);
}

// Strict pointer sanity for the crash guards (v117/v126): address, the
// object's vtable, its class and the class's vtable all have to be plausible.
static bool PtrPlausible(const void* p) {
    const uintptr_t a = (uintptr_t)p;
    if (a < 0x10000)    return false;
    if ((a >> 47) != 0) return false;     // non-canonical -> GP fault
    if (a & 7)          return false;
    return true;
}
static bool ActorPtrSane(const void* p) {
    if (!PtrPlausible(p)) return false;
    uint8_t probe = 0;                    // RemoteRole, the first field the engine reads
    if (!SafeCopy((const uint8_t*)p + OFF::AActor_Role, &probe, 1)) return false;
    void* vt = SafePtr(p);
    if (!PtrPlausible(vt) || !InModule(vt)) return false;
    void* cls = SafePtr((const uint8_t*)p + OFF::UObject_Class);
    if (!PtrPlausible(cls)) return false;
    void* clsVt = SafePtr(cls);
    if (!PtrPlausible(clsVt) || !InModule(clsVt)) return false;
    return true;
}

// ---- actors ---------------------------------------------------------------
static bool Finite3(const float* v) {
    for (int i = 0; i < 3; ++i) if (!(v[i] == v[i]) || v[i] > 1e9f || v[i] < -1e9f) return false;
    return true;
}
static bool ActorLocation(void* actor, float* out) {
    uint8_t* root = (uint8_t*)SafePtr((uint8_t*)actor + OFF::AActor_RootComponent);
    if (!root) return false;
    return SafeCopy(root + OFF::USceneComp_Translation, out, 12) && Finite3(out);
}
static float Dist3(const float* a, const float* b) {
    const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

// IsChildOf through the FStructBaseChain, as the engine inlines it.
static bool IsChildOfClass(void* obj, void* base) {
    if (!obj || !base) return false;
    void* cls = SafePtr((uint8_t*)obj + OFF::UObject_Class);
    if (!cls) return false;
    int32_t want = SafeI32((uint8_t*)base + OFF::UStruct_NumBasesM1);
    int32_t have = SafeI32((uint8_t*)cls  + OFF::UStruct_NumBasesM1);
    if (want < 0 || want > have || want > 64) return false;
    void* chain = SafePtr((uint8_t*)cls + OFF::UStruct_BaseChain);
    if (!chain) return false;
    return SafePtr((uint8_t*)chain + (size_t)want * 8) == (uint8_t*)base + OFF::UStruct_BaseChain;
}
static bool IsPlayerController(void* obj) {
    void* pcClass = SafePtr((void*)(g_base + RVA::APlayerController_ClassPtr));
    return pcClass && IsChildOfClass(obj, pcClass);
}
// Walk SuperStruct up from an instance's class until the class is named `name`.
static void* NativeAncestorNamed(void* obj, const wchar_t* name) {
    void* cls = SafePtr((uint8_t*)obj + OFF::UObject_Class);
    for (int i = 0; cls && i < 16; ++i) {
        wchar_t nm[128];
        if (ResolveFuncName(cls, nm, 128) && wcscmp(nm, name) == 0) return cls;
        cls = SafePtr((uint8_t*)cls + OFF::UStruct_SuperStruct);
    }
    return nullptr;
}

// ULevel::Actors slots are obfuscated (handoff "ACTOR-POINTERS-ARE-OBFUSCATED",
// encoder at 0x3F80FD5): decode one slot. The upper 32 bits survive the
// low-dword complement (v58f).
static inline uint64_t Rol64(uint64_t v, int n) { return (v << n) | (v >> (64 - n)); }
static inline uint64_t Ror64(uint64_t v, int n) { return (v >> n) | (v << (64 - n)); }
static void* DecodeActorSlot(uint64_t raw) {
    uint64_t v = ~raw;
    v = Rol64(v, 1);
    v ^= RVA::ActorPtr_XorKey;
    v = (v & 0xFFFFFFFF00000000ULL) | (uint64_t)(uint32_t)(~(uint32_t)v);
    v += RVA::ActorPtr_AddKey;
    v = Ror64(v, 6);
    return (void*)v;
}
// A decoded slot that looks like an actor in the module, or null.
static void* DecodedActorAt(const uint64_t* slot) {
    uint64_t raw = 0;
    if (!SafeCopy(slot, &raw, 8) || raw == 0) return nullptr;
    void* a = DecodeActorSlot(raw);
    if ((uintptr_t)a < 0x10000 || (uintptr_t)a > 0x00007FFFFFFFFFFFULL) return nullptr;
    void* vt = SafePtr(a);
    if (!vt || !InModule(vt)) return nullptr;
    return a;
}

// ---- reflection function lists (v80/v82/v167) ----------------------------
// A class's function list is found by probing: the slot whose chain of
// UField::Next yields the longest run of resolvable names. Shared by the
// magazine push and the command file. NumParms/ParmsSize are calibrated
// against StartGame (8 bytes) vs Cheatable (0 bytes) -- v169.
static int g_childOff = -1, g_nextOff = -1;
static int g_fnNumParmsOff = -1, g_fnParmsSizeOff = -1;

static int ChainLength(void* head, int nextOff) {
    int n = 0;
    void* f = head;
    for (int i = 0; f && i < 512; ++i) {
        wchar_t nm[128] = L"";
        if (!ResolveFuncName(f, nm, 128)) break;
        ++n;
        f = SafePtr((uint8_t*)f + nextOff);
    }
    return n;
}
static void ProbeFunctionList(void* cls) {
    if (g_childOff >= 0 || !cls) return;
    int bestLen = 0;
    static const int kNext[2] = { 0x30, 0x28 };
    for (int off = 0x30; off <= 0xA0; off += 8) {
        if (off == (int)OFF::UStruct_SuperStruct) continue;
        void* head = SafePtr((uint8_t*)cls + off);
        if (!head || !MaybeAlive(head)) continue;
        for (int k = 0; k < 2; ++k) {
            const int len = ChainLength(head, kNext[k]);
            if (len > bestLen) { bestLen = len; g_childOff = off; g_nextOff = kNext[k]; }
        }
    }
    if (bestLen < 8) { g_childOff = -1; g_nextOff = -1; }
    else L("[cmd] Funktionsliste gefunden: UStruct+0x%02X, Next +0x%02X, %d Eintraege", g_childOff, g_nextOff, bestLen);
}
// The UFunction named `want` on the object's class or any superclass.
static void* FindFunctionNamed(void* obj, const wchar_t* want, int* numParms, int* parmsSize) {
    if (numParms)  *numParms  = -1;
    if (parmsSize) *parmsSize = -1;
    if (!obj || !want) return nullptr;
    void* cls = SafePtr((uint8_t*)obj + OFF::UObject_Class);
    if (!cls || !MaybeAlive(cls)) return nullptr;
    for (int depth = 0; cls && depth < 14; ++depth) {
        if (g_childOff < 0) ProbeFunctionList(cls);
        if (g_childOff >= 0) {
            void* f = SafePtr((uint8_t*)cls + g_childOff);
            for (int i = 0; f && i < 4096; ++i) {
                wchar_t nm[128] = L"";
                if (ResolveFuncName(f, nm, 128) && wcscmp(nm, want) == 0) {
                    if (numParms && g_fnNumParmsOff >= 0)   *numParms  = SafeI32((uint8_t*)f + g_fnNumParmsOff) & 0xFF;
                    if (parmsSize && g_fnParmsSizeOff >= 0) *parmsSize = SafeI32((uint8_t*)f + g_fnParmsSizeOff) & 0xFFFF;
                    return f;
                }
                f = SafePtr((uint8_t*)f + g_nextOff);
            }
        }
        void* sup = SafePtr((uint8_t*)cls + OFF::UStruct_SuperStruct);
        if (!sup || !MaybeAlive(sup)) break;
        cls = sup;
    }
    return nullptr;
}
// ProcessEvent through the object's own vtable. No mask handling here: the
// callers sit inside the MaskOff window already (or open one themselves).
static bool CallFunctionOn(void* obj, void* fn, void* params) {
    if (!obj || !fn) return false;
    void** vt = (void**)SafePtr(obj);
    if (!vt) return false;
    void* pe = SafePtr((uint8_t*)vt + RVA::VT_ProcessEvent);
    if (!pe || !InModule(pe)) return false;
    bool ok = true;
    __try { ((tProcessEvent)pe)(obj, fn, params); }
    __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    return ok;
}
// ForceNetUpdate through the vtable (used for the zone and the GameState).
static bool ForceNetUpdateOn(void* a) {
    if (!a) return false;
    void** vt = (void**)SafePtr(a);
    if (!vt || !InModule(vt)) return false;
    void* fn = SafePtr((uint8_t*)vt + RVA::AActor_ForceNetUpdate_VT);
    if (!fn || !InModule(fn)) return false;
    __try { ((void (__fastcall*)(void*))fn)(a); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Per-UFunction verdict cache: resolve a function's name once, then decide by
// pointer. Used everywhere ProcessEvent is watched (v134 pattern).
struct FnVerdict { void* fn; uint8_t verdict; };
template <int N>
static uint8_t FnVerdictLookup(FnVerdict (&cache)[N], void* fn, uint8_t (*classify)(const wchar_t*)) {
    const uintptr_t h = ((uintptr_t)fn >> 4) * 2654435761u;
    for (int i = 0; i < 4; ++i) {
        FnVerdict& c = cache[(h + i) & (N - 1)];
        if (c.fn == fn) return c.verdict;
        if (!c.fn) {
            wchar_t nm[128] = L"";
            uint8_t v = ResolveFuncName(fn, nm, 128) ? classify(nm) : 0;
            c.verdict = v;
            c.fn = fn;                 // written last, so a half-filled slot reads as unseen
            return v;
        }
    }
    return 0;
}

// ============================================================================
//  == 5. the standalone mask (v33)
//
//  The exe is a CLIENT target: UWorld::InternalGetNetMode is folded to
//  "NetDriver ? NM_Client : DemoNetDriver ? Demo->GetNetMode() : NM_Standalone"
//  (proof byte at RVA::NetModeFoldProof). So the WORLD hides its NetDriver:
//  World->NetDriver and the LevelCollections' NetDriver are null for the game
//  logic and the driver is only shown inside the engine paths that need it
//  (NotifyAcceptingConnection, NotifyControlMessage, TickFlush, the dormancy
//  calls). The driver itself stays fully registered and ticking.
// ============================================================================
void*  g_world = nullptr;     // the listen world (set in MyListen)
static void*  g_drv   = nullptr;     // its UNetDriver
static bool          g_maskActive = false;
static volatile LONG g_maskDepth  = 0;
static volatile LONG g_maskOffCnt = 0;

static void SetWorldDriver(void* d) {
    if (!g_world) return;
    SafeStore((uint8_t*)g_world + OFF::UWorld_NetDriver, d);
    void*   lcData = SafePtr((uint8_t*)g_world + OFF::UWorld_LevelCollections);
    int32_t lcNum  = SafeI32((uint8_t*)g_world + OFF::UWorld_LevelCollections + 8);
    for (int32_t i = 0; lcData && i < lcNum && i < 8; ++i)
        SafeStore((uint8_t*)lcData + (size_t)i * OFF::LevelCollectionStride + OFF::LevelCollection_NetDriver, d);
}
static void MaskOff() {                      // show the driver
    if (!g_maskActive) return;
    InterlockedIncrement(&g_maskOffCnt);
    if (InterlockedIncrement(&g_maskDepth) == 1) SetWorldDriver(g_drv);
}
static void MaskOn() {                       // hide it again
    if (!g_maskActive) return;
    if (InterlockedDecrement(&g_maskDepth) == 0) SetWorldDriver(nullptr);
}
static bool InListenWindow() { return g_maskActive && g_drv && g_world; }
static int  ClientCount() { return g_drv ? SafeI32((uint8_t*)g_drv + OFF::UNetDriver_ClientConnNum) : 0; }

static void* GameStateOfWorld() { return g_world ? SafePtr((uint8_t*)g_world + OFF::UWorld_GameState) : nullptr; }
static void* GameModeOfWorld()  { return g_world ? SafePtr((uint8_t*)g_world + OFF::UWorld_AuthGameMode) : nullptr; }

// The HOST's own PlayerController (never the spawn override).
static void* RealFirstPlayerController() {
    if (!g_world) return nullptr;
    if (SafeI32((uint8_t*)g_world + OFF::UWorld_ControllerList + 8) <= 0) return nullptr;
    void* data = SafePtr((uint8_t*)g_world + OFF::UWorld_ControllerList);
    if (!data) return nullptr;
    void* pc = nullptr;
    __try { pc = ((void* (__fastcall*)(void*))(g_base + RVA::WeakObjectPtr_Get))(data); }
    __except (EXCEPTION_EXECUTE_HANDLER) { pc = nullptr; }
    return pc;
}

// ============================================================================
//  == 6. remote player registry (v55, per-player since v112)
//
//  A remote player's controller has RemoteRole == ROLE_AutonomousProxy (the
//  host's own does not). Its pawn is recorded from APawn::PossessedBy and is
//  the anchor for streaming, loot and the weapon work.
// ============================================================================
struct RemotePawn {
    void*   pawn;
    void*   controller;
    void*   weapon;        // current weapon, refreshed 4x/s
    int32_t lastMag, lastBp;
};
static RemotePawn g_remote[24];
static int        g_remoteCount = 0;
static volatile LONG g_tfCount = 0;   // TickFlush counter (section 20), used as a coarse clock

// v173: a listen host only animates skeletal meshes it RENDERS. The game
// ships the pawn mesh with VisibilityBasedAnimTickOption = OnlyTickPoseWhenRendered,
// so a remote player who is out of the host's view never plays his montages on
// the host -- and every anim-notify driven action (door kick trace, reload
// finish, bolt) silently never happens server-side. A dedicated server forces
// AlwaysTickPose; we do the same for every remote pawn (and switch the update
// rate optimisation off, which skips frames for unseen meshes).
static volatile LONG g_animTickFixed = 0;
static void FixAnimTick(void* pawn, const char* when) {
    if (!g_cfg.animTick || !pawn) return;
    uint8_t* mesh = (uint8_t*)SafePtr((uint8_t*)pawn + OFF::ACharacter_Mesh);
    if (!mesh || !InModule(SafePtr(mesh))) return;
    uint8_t opt = 0xFF, uro = 0;
    if (!SafeCopy(mesh + OFF::SkinnedMesh_AnimTickOption, &opt, 1) || !SafeCopy(mesh + OFF::SkinnedMesh_UROByte, &uro, 1)) return;
    if (opt > 3) return;                                   // not the field we expect -> leave it alone
    const bool needOpt = (opt != 0), needUro = (uro & 0x02) != 0;
    if (!needOpt && !needUro) return;
    const uint8_t zero = 0; const uint8_t uroNew = (uint8_t)(uro & ~0x02);
    if (needOpt) SafeCopy(&zero, mesh + OFF::SkinnedMesh_AnimTickOption, 1);
    if (needUro) SafeCopy(&uroNew, mesh + OFF::SkinnedMesh_UROByte, 1);
    LONG n = InterlockedIncrement(&g_animTickFixed);
    if (n <= 30) L("[at] Remote-Pawn %p Mesh %p (%s): VisibilityBasedAnimTickOption %d -> 0 (AlwaysTickPoseAndRefreshBones), UpdateRateOptimizations %s -- #%ld",
                   pawn, mesh, when, opt, needUro ? "AUS" : "war schon aus", n);
}
static void RegisterRemotePawn(void* pawn, void* controller) {
    for (int i = 0; i < g_remoteCount; ++i)
        if (g_remote[i].controller == controller) { g_remote[i].pawn = pawn; FixAnimTick(pawn, "PossessedBy (erneut)"); return; }
    FixAnimTick(pawn, "PossessedBy");
    if (g_remoteCount < 24) g_remote[g_remoteCount++] = { pawn, controller, nullptr, -1, -1 };
    else                    g_remote[7] = { pawn, controller, nullptr, -1, -1 };
}
static int RemoteOfWeapon(void* w) {
    if (!w) return -1;
    for (int i = 0; i < g_remoteCount; ++i) if (g_remote[i].weapon == w) return i;
    return -1;
}
// Root-component location of a registered pawn, or false if the pawn is gone
// or no longer belongs to that controller.
static bool RemotePawnLocation(const RemotePawn& rp, float* out) {
    if (!rp.pawn) return false;
    if (g_objArrayState > 0 && !IsObjectAlive(rp.pawn)) return false;
    if (SafePtr((uint8_t*)rp.pawn + OFF::APawn_Controller) != rp.controller) return false;
    uint8_t* root = (uint8_t*)SafePtr((uint8_t*)rp.pawn + OFF::AActor_RootComponent);
    if (!root) return false;
    if (!SafeCopy(root + OFF::USceneComp_Translation, out, 12)) return false;
    if (!Finite3(out)) return false;
    if (out[0] == 0.f && out[1] == 0.f && out[2] == 0.f) return false;
    return true;
}
// Nearest registered remote pawn within `reach` of a point, or -1.
static int RemoteWithin(const float* loc, float reach) {
    for (int i = 0; i < g_remoteCount; ++i) {
        float p[3];
        if (!RemotePawnLocation(g_remote[i], p)) continue;
        if (Dist3(p, loc) <= reach) return i;
    }
    return -1;
}

// Actors the remote players OWN (weapon, inventory items, PlayerState, ...),
// collected by the level sweep. The hash set is a fast filter for the
// GetNetMode hook (~6000 calls/s); every hit is verified against the real
// Owner chain, so a stale entry can never widen the answer (v58u).
static void* g_ownedActors[256];
static int   g_ownedCount = 0;
static void* g_ownedSet[2048];

static void OwnedSetAdd(void* a) {
    if (!a) return;
    uintptr_t h = ((uintptr_t)a >> 4) * 2654435761u;
    for (int i = 0; i < 8; ++i) {
        const int slot = (int)((h + i) & 2047);
        if (!g_ownedSet[slot] || g_ownedSet[slot] == a) { g_ownedSet[slot] = a; return; }
    }
}
static bool OwnedSetMayHave(void* a) {
    uintptr_t h = ((uintptr_t)a >> 4) * 2654435761u;
    for (int i = 0; i < 8; ++i) {
        const int slot = (int)((h + i) & 2047);
        if (!g_ownedSet[slot]) return false;
        if (g_ownedSet[slot] == a) return true;
    }
    return false;
}
// Does this actor's Owner chain end at a remote pawn or controller?
static bool OwnedByRemote(void* a) {
    void* cur = a;
    for (int depth = 0; depth < 6 && cur; ++depth) {
        for (int i = 0; i < g_remoteCount; ++i)
            if (cur == g_remote[i].pawn || cur == g_remote[i].controller) return depth > 0;
        cur = SafePtr((uint8_t*)cur + OFF::AActor_Owner);
    }
    return false;
}

// ============================================================================
//  == 7. start-up byte patches
// ============================================================================
// Steam ownership: "je 0x12BA7F4" (0F 84 B8 00 00 00) -> 6 x NOP, so the game
// also starts on accounts without the licence. Must run before the online
// subsystem initialises, i.e. at process start.
static bool SkipOwnershipCheck() {
    static const uint8_t expect[6] = { 0x0F, 0x84, 0xB8, 0x00, 0x00, 0x00 };
    static const uint8_t patch[6]  = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
    const bool ok = PatchBytes("i", "Steam-Besitzpruefung", RVA::SteamSubscribedCheck_JE, expect, patch, 6, 0, 6);
    if (ok) L("[+] Steam-Besitzpruefung entfernt -> Spiel laeuft auch ohne Lizenz");
    return ok;
}
// Steam P2P passthrough: "je" -> "jmp", so InitListen always opens a real UDP socket.
static bool ForceIpSockets() {
    static const uint8_t expect[6] = { 0x0F, 0x84, 0xD2, 0x00, 0x00, 0x00 };
    static const uint8_t patch[6]  = { 0xE9, 0xD3, 0x00, 0x00, 0x00, 0x90 };
    const bool ok = PatchBytes("i", "Steam-Passthrough-Sprung", RVA::SteamPassthrough_JE, expect, patch, 6, 0, 6);
    if (ok) L("[+] SteamNetDriver auf Passthrough gezwungen -> echte UDP-Sockets");
    else    L("[!] Steam-P2P bleibt aktiv!");
    return ok;
}
// v48: references into streaming sublevels are always sent instead of None
// (the MovementBase the client stands on -- "inside a building while walking").
static bool ForceLevelReferences() {
    static const uint8_t expect[2] = { 0x75, 0x6A };
    static const uint8_t patch[2]  = { 0xEB, 0x6A };
    const bool ok = PatchBytes("lv", "Level-Sichtbarkeitssprung", RVA::PackageMap_LevelVisibleJne, expect, patch, 2, 0, 2);
    if (ok) L("[+] Referenzen in Streaming-Sublevels werden immer gesendet (kein 'None' mehr)");
    return ok;
}
// v142: a join carrying an EncryptionToken takes the same path as one without
// (the DS module that answered tokens is not in the client build).
static bool IgnoreEncryptionToken() {
    static const uint8_t expect[10]  = { 0x83, 0x7D, 0x88, 0x01, 0x7F, 0x0D, 0x48, 0x8B, 0xCE, 0xE8 };
    static const uint8_t patched[10] = { 0x83, 0x7D, 0x88, 0x01, 0x90, 0x90, 0x48, 0x8B, 0xCE, 0xE8 };
    const bool ok = PatchBytes("et", "Token-Pruefung in NMT_Hello", RVA::Hello_TokenCheck, expect, patched, 10, 4, 2);
    if (ok) L("[+] [et] EncryptionToken im NMT_Hello wird ignoriert -> Join aus der Lobby-Suche laeuft unverschluesselt wie 'open ip:port'");
    else    L("[!] [et] Joins aus der Lobby-Suche werden abgewiesen");
    return ok;
}
// v146: in-round gold/material payments: the unbound-delegate path behaves
// like Standalone ("jne +4" -> "jne +0x22", both land on 'mov dl,1').
static bool PatchPayResult(const char* name, uintptr_t jneRva) {
    static const uint8_t expect[8]  = { 0x84, 0xDB, 0x75, 0x04, 0x33, 0xD2, 0xEB, 0x23 };
    static const uint8_t patched[8] = { 0x84, 0xDB, 0x75, 0x22, 0x33, 0xD2, 0xEB, 0x23 };
    const bool ok = PatchBytes("cp", name, jneRva - 2, expect, patched, 8, 3, 1);
    if (ok) L("[+] [cp] %s: Ergebnis wird sofort geliefert (wie Standalone)", name);
    else    L("[!] [cp] %s: Klassenwahl/Kauf mit Gold bleibt haengen", name);
    return ok;
}

// ============================================================================
//  == 8. net mode
// ============================================================================
// ---- AActor::GetNetMode (v42/v46/v58u) --------------------------------------
// With the mask on, actors would see NM_Standalone. That breaks PossessedBy
// (no AutonomousProxy for the joiner's pawn), IsLocalController (host runs the
// joiner's input path -> rubber band) and every "we are a server, tell the
// owning client" branch on the joiner's own actors. So: actors with
// RemoteRole == AutonomousProxy and actors OWNED by a remote player answer
// NM_ListenServer. Everything else (loot spawners, geometry, GameMode) keeps
// NM_Standalone, which is what the host's own floor loot needs (v45).
typedef int (__fastcall* tGetNetMode)(void* actor);
static tGetNetMode   g_origGetNetMode = nullptr;
static volatile LONG g_nmCalls = 0, g_nmSubst = 0, g_ownModeSubst = 0, g_aircraftSubst = 0, g_bpGateForced = 0;

static int __fastcall MyActorGetNetMode(void* actor) {
    int r = g_origGetNetMode ? g_origGetNetMode(actor) : OFF::NM_Standalone;
    InterlockedIncrement(&g_nmCalls);
    // v58f: the spawn timers of buildings / vehicle spawners only start in
    // BeginPlay when GetNetMode() == NM_Standalone, whatever the mask state.
    if (g_cfg.beginPlayFix && g_world && g_drv) {
        const uintptr_t ra = (uintptr_t)_ReturnAddress();
        const bool bld = ra == g_base + RVA::BuildingBeginPlay_GetNetModeRet;
        if (bld || ra == g_base + RVA::VehicleBeginPlay_GetNetModeRet) {
            if (r != OFF::NM_Standalone) {
                LONG n = InterlockedIncrement(&g_bpGateForced);
                if (n <= 8) L("[bp] BeginPlay von %s %p sah NetMode %d -> NM_Standalone, damit der Spawn-Timer startet (#%ld)",
                              bld ? "Gebaeude" : "Fahrzeug-Spawner", actor, r, n);
                return OFF::NM_Standalone;
            }
        }
    }
    if (!g_cfg.actorMode || !g_maskActive || !g_world || !g_drv) return r;
    if (r != OFF::NM_Standalone) return r;          // driver visible -> already right
    uint8_t remote = 0;
    if (!SafeCopy((uint8_t*)actor + OFF::AActor_RemoteRole, &remote, 1)) return r;
    const bool isAutonomous = (remote == OFF::ROLE_AutonomousProxy);
    bool isOwnedByRemote = false;
    if (!isAutonomous) {
        if (!g_cfg.ownMode || g_remoteCount == 0) return r;
        if (!OwnedSetMayHave(actor))               return r;
        if (!OwnedByRemote(actor))                 return r;   // verified, not just hashed
        isOwnedByRemote = true;
    }
    // Only actors of OUR world (GetWorld() is the same virtual the original used).
    void** vt = (void**)SafePtr(actor);
    if (!vt || !InModule(vt)) return r;
    void* fn = SafePtr((uint8_t*)vt + RVA::AActor_GetWorld_VT);
    if (!fn || !InModule(fn)) return r;
    void* world = nullptr;
    __try { world = ((void* (__fastcall*)(void*))fn)(actor); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return r; }
    if (world != g_world) return r;
    InterlockedIncrement(&g_nmSubst);
    if (isOwnedByRemote) {
        LONG n = InterlockedIncrement(&g_ownModeSubst);
        if (n <= 15) {
            wchar_t cn[128] = L"?"; ClassNameOf(actor, cn, 128);
            L("[om] %ls %p gehoert dem Remote-Spieler -> NM_ListenServer statt NM_Standalone (Aufrufer +0x%llX) -- #%ld",
              cn, actor, (unsigned long long)((uintptr_t)_ReturnAddress() - g_base), n);
        }
        return OFF::NM_ListenServer;
    }
    // v58: DoInAircraft only sends ClientInAircraft under NM_DedicatedServer.
    if (g_cfg.aircraftFix && (uintptr_t)_ReturnAddress() == g_base + RVA::DoInAircraft_GetNetModeRet) {
        LONG n = InterlockedIncrement(&g_aircraftSubst);
        if (n <= 5) L("[ac] DoInAircraft fuer Remote-Pawn %p -> NM_DedicatedServer, damit ClientInAircraft rausgeht (#%ld)", actor, n);
        return OFF::NM_DedicatedServer;
    }
    return OFF::NM_ListenServer;
}

// ---- APawn::PossessedBy (v42/v55) ------------------------------------------
// Registers the remote player's pawn and, belt and braces, repeats the role
// setup (SetReplicates + SetAutonomousProxy) if the engine skipped it.
typedef void (__fastcall* tPossessedBy)(void* pawn, void* controller);
static tPossessedBy g_origPossessedBy = nullptr;

static const char* RoleName(uint8_t r) {
    switch (r) { case 0: return "None"; case 1: return "SimulatedProxy";
                 case 2: return "AutonomousProxy"; case 3: return "Authority"; default: return "?"; }
}
static void __fastcall MyPossessedBy(void* pawn, void* controller) {
    if (g_origPossessedBy) g_origPossessedBy(pawn, controller);
    static int n = 0;
    uint8_t remote = 0, role = 0, rep = 0;
    SafeCopy((uint8_t*)pawn + OFF::AActor_RemoteRole,  &remote, 1);
    SafeCopy((uint8_t*)pawn + OFF::AActor_Role,        &role,   1);
    SafeCopy((uint8_t*)pawn + OFF::AActor_bReplicates, &rep,    1);
    const bool pc = IsPlayerController(controller);
    if (pc && g_cfg.streamRemote) {
        uint8_t ctrlRemote = 0;
        SafeCopy((uint8_t*)controller + OFF::AActor_RemoteRole, &ctrlRemote, 1);
        if (ctrlRemote == OFF::ROLE_AutonomousProxy) {
            RegisterRemotePawn(pawn, controller);
            L("[ws] Remote-Spieler: Controller %p besitzt Pawn %p -> Streaming-Anker #%d", controller, pawn, g_remoteCount);
        } else if (n < 40) {
            L("[ws] Controller %p hat RemoteRole=%s -> kein Streaming-Anker (Host selbst)", controller, RoleName(ctrlRemote));
        }
    }
    if (n >= 40) return;
    L("[pb] PossessedBy pawn=%p ctrl=%p (%s) -> Role=%s RemoteRole=%s bReplicates=%d",
      pawn, controller, pc ? "PlayerController" : "other", RoleName(role), RoleName(remote), rep & 1);
    ++n;
    if (g_maskActive && pc && role == OFF::ROLE_Authority && remote != OFF::ROLE_AutonomousProxy) {
        auto setRep  = (void (__fastcall*)(void*, bool))(g_base + RVA::AActor_SetReplicates);
        auto setAuto = (void (__fastcall*)(void*, bool, bool))(g_base + RVA::AActor_SetAutonomousProxy);
        __try {
            setRep(pawn, true);
            setAuto(pawn, true, true);
            SafeCopy((uint8_t*)pawn + OFF::AActor_RemoteRole, &remote, 1);
            L("[pb]   Rollen-Setup war uebersprungen -> wiederholt, RemoteRole jetzt %s", RoleName(remote));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            L("[pb]   Wiederholung des Rollen-Setups ist GEFAULTET -> belassen");
        }
    }
}

// ---- AActor::GetFunctionCallspace (v37/v45) --------------------------------
// Under the mask the world says NM_Standalone and every RPC would be "Local".
// Client and multicast RPCs get the Remote bit OR-ed on (additive: the host
// keeps its own). Callspace values are bit flags: Remote=1, Local=2 (v45).
static tCallspace    g_origCallspace = nullptr;
static volatile LONG g_rpcClient = 0, g_rpcMulti = 0;

// v174: every distinct net function that passes here is logged once with the
// engine's answer and ours ([cs] lines) -- this is where a multicast such as
// BravoHotelHIDestructibleComponent::MulticastOnDestructComponent (the window
// break sound) either leaves the host or dies.
static void* g_csSeen[2048];
static volatile LONG g_csLogged = 0;
static void CsLogOnce(void* actor, void* fn, uint32_t flags, int orig, int ret) {
    if (g_csLogged >= 400) return;
    uintptr_t h = ((uintptr_t)fn >> 4) * 2654435761u;
    for (int i = 0; i < 16; ++i) {
        const int slot = (int)((h + i) & 2047);
        if (g_csSeen[slot] == fn) return;
        if (!g_csSeen[slot]) { g_csSeen[slot] = fn; break; }
    }
    InterlockedIncrement(&g_csLogged);
    wchar_t nm[128] = L"?", cn[128] = L"?";
    ResolveFuncName(fn, nm, 128); ClassNameOf(actor, cn, 128);
    L("[cs] %ls %p -> %ls  Flags 0x%08X (%s)  Engine %d -> wir %d", cn, actor, nm, flags,
      (flags & OFF::FUNC_NetMulticast) ? "Multicast" : (flags & OFF::FUNC_NetClient) ? "Client" : "Server", orig, ret);
}
static int __fastcall MyGetFunctionCallspace(void* actor, void* fn, void* stack) {
    int r = g_origCallspace ? g_origCallspace(actor, fn, stack) : OFF::CS_Local;
    if (!g_cfg.rpcFix || !g_maskActive || !g_drv || !fn) return r;
    if (ClientCount() <= 0) return r;
    uint32_t flags = (uint32_t)SafeI32((uint8_t*)fn + OFF::UFunction_FunctionFlags);
    void* f = fn;                                   // net flags sit on the top of the Super chain
    for (int i = 0; i < 4; ++i) {
        void* sup = SafePtr((uint8_t*)f + OFF::UStruct_SuperStruct);
        if (!sup || !LooksLikeActor(sup)) break;
        f = sup;
        flags |= (uint32_t)SafeI32((uint8_t*)f + OFF::UFunction_FunctionFlags);
    }
    if (!(flags & OFF::FUNC_Net)) return r;
    int ret = r;
    if (flags & OFF::FUNC_NetMulticast)   { InterlockedIncrement(&g_rpcMulti);  ret = r | OFF::CS_Remote; }
    else if (flags & OFF::FUNC_NetClient) { InterlockedIncrement(&g_rpcClient); ret = g_cfg.rpcRemoteOnly ? OFF::CS_Remote : (r | OFF::CS_Remote); }
    CsLogOnce(actor, fn, flags, r, ret);            // server RPCs: local execution is right
    return ret;
}

// ============================================================================
//  == 9. world: origin pin (v44), replay recorder (v43), FindPlayerStart (v24)
// ============================================================================
static volatile LONG g_rebaseBlocked = 0;
// LV-OrbIsland uses World Composition origin rebasing. A dedicated server
// never rebases and all server-side game code assumes local == absolute; our
// host has a local player and would rebase (vault teleported the joiner by
// one origin, 2.4 km). Keep the origin at {0,0,0}. Returning false is the
// engine's own answer when a level is pending visibility.
static bool __fastcall MySetNewWorldOrigin(void* world, void* newOrigin) {
    InterlockedIncrement(&g_rebaseBlocked);
    int32_t cur[3] = { 0, 0, 0 }, req[3] = { 0, 0, 0 };
    SafeCopy((uint8_t*)world + OFF::UWorld_OriginLocation, cur, sizeof(cur));
    SafeCopy(newOrigin, req, sizeof(req));
    SafeCopy(cur, (uint8_t*)world + OFF::UWorld_RequestedOriginLocation, sizeof(cur));   // "nothing requested"
    static int n = 0;
    if (n < 10) { ++n; L("[or] Origin-Rebasing blockiert: World %p bleibt bei {%d, %d, %d}, angefordert war {%d, %d, %d}",
                        world, cur[0], cur[1], cur[2], req[0], req[1], req[2]); }
    return false;
}
// The crash-dump replay recorder records all net actors and dies in
// DeltaSerializeFastArrayProperty once the joiner replicates fully. A private
// listen server needs no replay: the function is a no-op.
static void __fastcall MyStartRecordingReplay(void* /*gameInstance*/) {
    static int n = 0;
    if (n++ < 5) L("[rp] StartRecordingReplay unterdrueckt (kein Crash-Replay-Recorder auf dem Host)");
}
// LV-OrbIsland has no PlayerStarts for late joiners ("NO PLAYERSTART with
// positive rating" -> spawn at the origin, in the sea). Remember the last
// start the engine found for the host and hand it to late joiners.
typedef void* (__fastcall* tFindPlayerStart)(void*, void*, void*);
static tFindPlayerStart g_origFindPlayerStart = nullptr;
static void* g_lastGoodStart = nullptr;

static void* __fastcall MyFindPlayerStart(void* gameMode, void* controller, void* incomingName) {
    void* r = g_origFindPlayerStart ? g_origFindPlayerStart(gameMode, controller, incomingName) : nullptr;
    static int n = 0, m = 0;
    if (LooksLikeActor(r)) {
        g_lastGoodStart = r;
        if (n < 10) { L("[fps] Original liefert %p -> gemerkt", r); ++n; }
        return r;
    }
    if (LooksLikeActor(g_lastGoodStart)) {
        if (m < 20) { L("[fps] Original liefert %p -> ersetzt durch %p", r, g_lastGoodStart); ++m; }
        return g_lastGoodStart;
    }
    if (m < 20) { L("[fps] Original liefert %p, kein Ersatz vorhanden", r); ++m; }
    return r;
}

// ============================================================================
//  == 10. streaming around remote players (v55, v58d, v58i, v58q, v58r, v58s)
//
//  The server only requests World Composition tiles around its LOCAL players.
//  Remote pawns are appended as streaming points -- and since v58s they go
//  FIRST: every point after index 0 is a "sub point" whose base-level distance
//  is scaled down to 15-45 %, and in standalone the one point is the player.
//  The host's own point is dropped while he is in the aircraft (z > 12000).
// ============================================================================
typedef void (__fastcall* tUpdStream)(void* comp, const float* locs, const float* dirs, int32_t num, int32_t lod, float scale);
static tUpdStream    g_origUpdStream = nullptr;
static volatile LONG g_streamCalls = 0, g_streamExtra = 0, g_subScaleWrites = 0;
static float         g_lastRemoteLoc[3] = { 0.f, 0.f, 0.f };

static void __fastcall MyUpdateStreamingState(void* comp, const float* locs, const float* dirs, int32_t num, int32_t lod, float scale) {
    if (!g_origUpdStream) return;
    InterlockedIncrement(&g_streamCalls);
    if (!g_cfg.streamRemote || !g_drv || !g_world || g_remoteCount == 0 || num < 0 || num > 12 ||
        SafePtr((uint8_t*)comp + OFF::UWorldComposition_World) != g_world) {
        g_origUpdStream(comp, locs, dirs, num, lod, scale);
        return;
    }
    CalibrateObjectArray(g_world);
    enum { MAXPTS = 20 };
    float L3[MAXPTS * 3], D3[MAXPTS * 3];
    int n = 0, added = 0;
    float hostPt[3] = { 0, 0, 0 }, hostDir[3] = { 0, 0, 0 };
    const bool haveHost = (num > 0) && locs && SafeCopy(locs, hostPt, 12);
    if (haveHost && (!dirs || !SafeCopy(dirs, hostDir, 12))) memset(hostDir, 0, 12);
    const bool hostOnGround = haveHost && hostPt[2] < 12000.0f;
    const bool keepHost     = g_cfg.hostPoint || !g_cfg.remoteFirst || hostOnGround;
    for (int i = 0; i < g_remoteCount && n < MAXPTS; ++i) {
        float loc[3];
        if (!RemotePawnLocation(g_remote[i], loc)) continue;
        memcpy(&L3[n * 3], loc, 12);
        memset(&D3[n * 3], 0, 12);           // no direction bias for remote anchors
        ++n; ++added;
        memcpy(g_lastRemoteLoc, loc, 12);
    }
    int hostSlot = -1;
    if (added == 0 || !g_cfg.remoteFirst) {
        // old order: the engine's own points first, remote pawns behind them
        n = 0;
        for (int i = 0; i < num && n < MAXPTS; ++i, ++n) {
            if (!locs || !SafeCopy(locs + i * 3, &L3[n * 3], 12)) memset(&L3[n * 3], 0, 12);
            if (!dirs || !SafeCopy(dirs + i * 3, &D3[n * 3], 12)) memset(&D3[n * 3], 0, 12);
        }
        hostSlot = (num > 0) ? 0 : -1;
        for (int i = 0; i < g_remoteCount && n < MAXPTS && !g_cfg.remoteFirst; ++i) {
            float loc[3];
            if (!RemotePawnLocation(g_remote[i], loc)) continue;
            memcpy(&L3[n * 3], loc, 12);
            memset(&D3[n * 3], 0, 12);
            ++n;
        }
    } else if (keepHost && haveHost && n < MAXPTS) {
        memcpy(&L3[n * 3], hostPt, 12);
        memcpy(&D3[n * 3], hostDir, 12);
        hostSlot = n;
        ++n;
    }
    {
        static int orderLog = 0; static bool lastKeep = true;
        if (g_cfg.remoteFirst && added > 0 && (orderLog < 3 || keepHost != lastKeep)) {
            L("[ws] Reihenfolge: Remote-Spieler ist Punkt 0 (Primaerpunkt), Host %s (Host-z %.0f) -- %d Punkte gesamt",
              (hostSlot > 0) ? "als Unterpunkt dahinter" : "hat gerade KEINEN eigenen Punkt (im Flieger)",
              haveHost ? hostPt[2] : 0.0f, n);
            lastKeep = keepHost; ++orderLog;
        }
    }
    for (int k = 0; k < added; ++k) InterlockedIncrement(&g_streamExtra);
    // v58d: sub points stream with half the distance by default; the CVar is
    // a float in .data the game may reset, so it is written whenever it differs.
    if (g_cfg.subScale && added > 0) {
        float cur = 0.0f, one = 1.0f;
        if (SafeCopy((void*)(g_base + RVA::CVar_SubPointDistanceScale), &cur, 4) && cur != 1.0f) {
            SafeCopy(&one, (void*)(g_base + RVA::CVar_SubPointDistanceScale), 4);
            if (InterlockedIncrement(&g_subScaleWrites) <= 3)
                L("[ws] s.SubPointLevelStreamingDistanceScale %.2f -> 1.0 (Remote-Anker bekommen die volle Streaming-Distanz)", cur);
        }
    }
    g_origUpdStream(comp, L3, D3, n, lod, scale);
}

// ---- tiles: the commit decision and the pull ------------------------------
// Tile geometry from the world composition (layout read off the tile lambda):
// tile stride 0xE0, int position at +0x2C/+0x30, bounds at +0x38/+0x44.
struct TileBox { float minx, miny, maxx, maxy; };
static bool TileBoxOf(void* comp, int32_t idx, TileBox* out) {
    uint8_t* tiles = (uint8_t*)SafePtr((uint8_t*)comp + OFF::WorldComp_Tiles);
    int32_t  n     = SafeI32((uint8_t*)comp + OFF::WorldComp_Tiles + 8);
    if (!tiles || idx < 0 || idx >= n) return false;
    uint8_t* t = tiles + (size_t)idx * OFF::TileStride;
    int32_t px = SafeI32(t + 0x2C), py = SafeI32(t + 0x30);
    float bmin[2], bmax[2];
    if (!SafeCopy(t + 0x38, bmin, 8) || !SafeCopy(t + 0x44, bmax, 8)) return false;
    out->minx = px + bmin[0]; out->miny = py + bmin[1];
    out->maxx = px + bmax[0]; out->maxy = py + bmax[1];
    if (out->maxx - out->minx > 50000.0f || out->maxy - out->miny > 50000.0f) return false;   // world-sized proxy tile
    return true;
}
static bool RemoteNearBox(const TileBox& b, float margin, float maxZ) {
    for (int i = 0; i < g_remoteCount; ++i) {
        float p[3];
        if (!RemotePawnLocation(g_remote[i], p)) continue;
        if (p[2] > maxZ) continue;
        if (p[0] > b.minx - margin && p[0] < b.maxx + margin && p[1] > b.miny - margin && p[1] < b.maxy + margin) return true;
    }
    return false;
}

typedef bool (__fastcall* tCommitTile)(void* comp, void* world, int32_t tileIdx, bool bShouldBeLoaded, bool bShouldBeVisible,
                                       bool bBlockOnLoad, int32_t lodIndex, bool a8, bool a9);
static tCommitTile   g_origCommitTile = nullptr;
static volatile LONG g_commitCalls = 0, g_commitForced = 0, g_blockUsed = 0, g_pulled = 0, g_pullSkipped = 0;
static const LONG    kBlockBudget = 24;

// v58i: the commit is where "this tile should be loaded/visible/at this LOD"
// becomes engine action. For a tile next to a remote player answer as the
// engine would for a local player; the tile he is STANDING on (v58q) loads
// blocking, at most 24 times per round, only below z = 12000.
static bool __fastcall MyCommitTile(void* comp, void* world, int32_t tileIdx, bool bShouldBeLoaded, bool bShouldBeVisible,
                                    bool bBlockOnLoad, int32_t lodIndex, bool a8, bool a9) {
    if (!g_origCommitTile) return false;
    InterlockedIncrement(&g_commitCalls);
    TileBox box;
    const bool haveBox = g_remoteCount > 0 && g_world && world == g_world && TileBoxOf(comp, tileIdx, &box);
    if (g_cfg.commitFix && g_drv && haveBox && RemoteNearBox(box, 25000.0f, 1e30f) &&
        (!bShouldBeLoaded || !bShouldBeVisible || lodIndex != -1)) {
        LONG c = InterlockedIncrement(&g_commitForced);
        if (c <= 12 || (c % 2000) == 0)
            L("[ct] Kachel #%d beim Remote-Spieler: Engine wollte geladen=%d sichtbar=%d LOD=%d -> geladen=1 sichtbar=1 LOD=-1 (#%ld)",
              tileIdx, bShouldBeLoaded, bShouldBeVisible, lodIndex, c);
        bShouldBeLoaded = true; bShouldBeVisible = true; lodIndex = -1;
    }
    if (g_cfg.blockLoad && g_cfg.commitFix && haveBox && !bBlockOnLoad && bShouldBeLoaded && bShouldBeVisible &&
        g_blockUsed < kBlockBudget && RemoteNearBox(box, 3000.0f, 12000.0f)) {
        bBlockOnLoad = true;
        LONG b = InterlockedIncrement(&g_blockUsed);
        if (b <= 20) L("[ct] Kachel #%d unter dem gelandeten Remote-Spieler wird blockierend geladen (#%ld von %d)", tileIdx, b, kBlockBudget);
    }
    return g_origCommitTile(comp, world, tileIdx, bShouldBeLoaded, bShouldBeVisible, bBlockOnLoad, lodIndex, a8, a9);
}

// v58r/v58s: tiles the engine never commits (its evaluation byte stays 3) are
// requested through the three setters the commit body itself calls, in its
// order: loaded, visible, LOD -1. Six per sweep (~2 s), 250 m around each
// remote player. Engine setters, not raw field writes (the v58h lesson).
static void ForceTilesAroundRemote() {
    if (!g_cfg.pullTiles || !g_world || g_remoteCount == 0) return;
    void* comp = SafePtr((uint8_t*)g_world + OFF::UWorld_WorldComposition);
    if (!comp) return;
    int32_t n  = SafeI32((uint8_t*)comp + OFF::WorldComp_Tiles + 8);
    void**  lvls = (void**)SafePtr((uint8_t*)comp + OFF::WorldComp_TilesStreaming);
    int32_t ln = SafeI32((uint8_t*)comp + OFF::WorldComp_TilesStreaming + 8);
    if (!lvls || n <= 0 || n > 20000) return;
    int budget = 6;
    for (int i = 0; i < n && i < ln && budget > 0; ++i) {
        TileBox box;
        if (!TileBoxOf(comp, i, &box) || !RemoteNearBox(box, 25000.0f, 1e30f)) continue;
        uint8_t* lvl = (uint8_t*)SafePtr(lvls + i);
        if (!lvl) continue;
        uint8_t state = 0; SafeCopy(lvl + OFF::LevelStreaming_CurState, &state, 1);
        const int32_t lodIdx = SafeI32(lvl + OFF::LevelStreaming_LODIndex);
        if (state == 6 && lodIdx == -1) { InterlockedIncrement(&g_pullSkipped); continue; }   // already there
        if (state == 3 || state == 5) continue;                                                 // busy
        --budget;
        void** lvt = (void**)SafePtr(lvl);
        void*  setLoaded = lvt ? SafePtr((uint8_t*)lvt + RVA::LevelStreaming_SetShouldBeLoadedVT) : nullptr;
        if (!setLoaded || !InModule(setLoaded)) continue;
        __try {
            ((void (__fastcall*)(void*, uint8_t))setLoaded)(lvl, 1);
            ((void (__fastcall*)(void*, uint8_t))(g_base + RVA::LevelStreaming_SetShouldBeVisible))(lvl, 1);
            ((void (__fastcall*)(void*, int32_t))(g_base + RVA::LevelStreaming_SetLevelLODIndex))(lvl, -1);
        } __except (EXCEPTION_EXECUTE_HANDLER) { continue; }
        LONG c = InterlockedIncrement(&g_pulled);
        if (c <= 40) {
            uint8_t* loaded = (uint8_t*)SafePtr(lvl + OFF::LevelStreaming_LoadedLvl);
            L("[fx] Kachel #%d (Zustand %d, LOD %d) beim Remote-Spieler nachgefordert -> Level %p mit %d Actors (#%ld)",
              i, state, lodIdx, loaded, loaded ? SafeI32(loaded + OFF::ULevel_Actors + 8) : -1, c);
        }
    }
}

// ============================================================================
//  == 11. dormancy
// ============================================================================
static bool IsDormantAllActor(void* actor) {
    if (!actor) return false;
    uint8_t d = 0;
    if (!SafeCopy((uint8_t*)actor + OFF::AActor_NetDormancy, &d, 1)) return false;
    return d == OFF::DORM_DormantAll || d == OFF::DORM_DormantPartial;
}
static bool IsNetStartup(void* actor) {
    uint8_t flags = 0;
    return SafeCopy((uint8_t*)actor + OFF::AActor_bNetStartup, &flags, 1) && (flags & 2);
}

// ---- AActor::SetNetDormancy (v154) ---------------------------------------
// The game puts weapons (and PlayerStates, drop boxes, fences) to
// DORM_DormantAll 3.5 s after they are equipped. Under the mask the wake path
// is a no-op and the v62 guard keeps the window shut for exactly these actors,
// so every later change (attachment, magazine, bolt) only reached the client
// on the next weapon switch. Refuse the request at its source for dynamic
// actors; level-placed actors keep the game's dormancy unless -dormlevel.
typedef void (__fastcall* tSetNetDormancy)(void* actor, uint8_t dorm);
static tSetNetDormancy g_origSetNetDormancy = nullptr;
static volatile LONG   g_dormBlocked = 0, g_dormBlockedAdd = 0;

static void __fastcall MySetNetDormancy(void* actor, uint8_t dorm) {
    if (g_cfg.dormBlock && actor && (dorm == OFF::DORM_DormantAll || dorm == OFF::DORM_DormantPartial)) {
        const bool startup = IsNetStartup(actor);
        bool skip = false;
        if (g_cfg.dormAllowWeapons) { wchar_t wn[128] = L""; skip = ClassNameOf(actor, wn, 128) && wcsstr(wn, L"Weapon") != nullptr; }
        if ((!startup || g_cfg.dormBlockLevel) && !skip) {
            LONG n = InterlockedIncrement(&g_dormBlocked);
            if (n <= 40) {
                wchar_t cn[128] = L"?"; ClassNameOf(actor, cn, 128);
                uint8_t cur = 0xFF; SafeCopy((uint8_t*)actor + OFF::AActor_NetDormancy, &cur, 1);
                L("[dz] SetNetDormancy(%d) fuer %ls %p unterdrueckt (war %d, %s) -- Actor bleibt wach -- #%ld",
                  dorm, cn, actor, cur, startup ? "im Level platziert" : "dynamisch", n);
            }
            return;
        }
    }
    if (g_origSetNetDormancy) g_origSetNetDormancy(actor, dorm);
}

// ---- FlushNetDormancy / ForceNetUpdate through the mask (v58c, v62) -------
// Both end in "drv = GetNetDriver(); if (drv) ..." and GetNetDriver reads
// World->NetDriver, which the mask nulls. They run with the driver visible --
// except for a DORM_DormantAll/Partial actor: the graph's dormancy branch then
// dereferences a per-connection context that only exists inside its own pass
// (four host crashes, v58w..v62). For those the window stays shut.
typedef void (__fastcall* tActorVoid)(void* actor);
static tActorVoid    g_origFlushNetDormancy = nullptr;
static tActorVoid    g_origForceNetUpdate   = nullptr;
static volatile LONG g_flushCalls = 0, g_forceCalls = 0, g_flushWindow = 0, g_dormRefused = 0;

static void __fastcall MyFlushNetDormancy(void* actor) {
    if (!g_origFlushNetDormancy) return;
    InterlockedIncrement(&g_flushCalls);
    bool win = InListenWindow();
    if (win && IsDormantAllActor(actor)) {
        win = false;
        LONG n = InterlockedIncrement(&g_dormRefused);
        if (n <= 30) { wchar_t rn[128] = L"?"; ClassNameOf(actor, rn, 128);
            L("[dm] FlushNetDormancy fuer %ls %p (DORM_DormantAll) -- Fenster bleibt ZU (v62-Crashschutz) -- #%ld", rn, actor, n); }
    }
    if (win) { MaskOff(); InterlockedIncrement(&g_flushWindow); }
    g_origFlushNetDormancy(actor);
    if (win) MaskOn();
}
static void __fastcall MyForceNetUpdate(void* actor) {
    if (!g_origForceNetUpdate) return;
    InterlockedIncrement(&g_forceCalls);
    bool win = InListenWindow();
    if (win && IsDormantAllActor(actor)) {
        win = false;
        LONG n = InterlockedIncrement(&g_dormRefused);
        if (n <= 20) L("[dm] ForceNetUpdate fuer einen DORM_DormantAll-Actor %p -- Fenster bleibt ZU (v62-Crashschutz) -- #%ld", actor, n);
    }
    if (win) MaskOff();
    g_origForceNetUpdate(actor);
    if (win) MaskOn();
}

// ============================================================================
//  == 12. replication graph
// ============================================================================
// ---- zone state shared between this section and 13 ------------------------
static void*         g_blueZone       = nullptr;   // ABravoHotelBlueZone instance
static uintptr_t     g_zoneRefOff     = 0;         // GameState slot holding the zone reference (measured: 0x8D0)
static void*         g_zoneClass      = nullptr;
static bool          g_haveZoneKey    = false;
static FNameRaw      g_zoneKey        = { 0, 0, 0 };
static bool          g_zoneCdoPatched = false;
static void*         g_zoneInfoPtr    = nullptr;   // the graph's FGlobalActorReplicationInfo of the zone
static volatile bool g_zoneChannelJustOpened = false;
static volatile LONG g_zoneChannelOpens = 0, g_zoneAddFixes = 0, g_zoneReadds = 0, g_zoneKicks = 0;

static bool IsKnownRepGraph(const void* vt) {
    if (!vt || !InModule(vt)) return false;
    void* fn = SafePtr((const uint8_t*)vt + RVA::RepDriver_ServerReplicateActors_VT);
    return fn == (void*)(g_base + RVA::RepGraph_ServerReplicateActors) || fn == (void*)(g_base + RVA::RealRepGraph_ServerReplicate);
}
static void* RepGraph() { return g_drv ? SafePtr((uint8_t*)g_drv + OFF::UNetDriver_RepDriver) : nullptr; }

// The graph's own per-actor record (FGlobalActorReplicationInfo), created
// from the class settings when the actor is added. Its cull copy at +0x90/+0x94
// is what the replicate loop checks; cull <= 0 disables the distance check.
static void* GraphInfoFor(void* actor) {
    void* graph = RepGraph();
    if (!graph || !actor) return nullptr;
    if (!IsKnownRepGraph(SafePtr(graph))) return nullptr;
    typedef void* (__fastcall* tGet)(void* map, void** actorRef);
    void* ref = actor; void* info = nullptr;
    __try { info = ((tGet)(g_base + RVA::GlobalActorInfoMap_Get))((uint8_t*)graph + OFF::RepGraph_GlobalInfoMap, &ref); }
    __except (EXCEPTION_EXECUTE_HANDLER) { info = nullptr; }
    return info;
}
static bool ZoneGraphCullToZero(void* actor, const char* when) {
    void* info = GraphInfoFor(actor);
    if (!info) { L("[bz] Graph-Info des Zonen-Actors nicht erhalten (%s)", when); return false; }
    float cull = 0.0f, cullSq = 0.0f, zero = 0.0f; uint8_t wantsDormant = 0, off = 0;
    SafeCopy((uint8_t*)info + OFF::GlobalInfo_CullDistance, &cull, 4);
    SafeCopy((uint8_t*)info + OFF::GlobalInfo_CullDistance + 4, &cullSq, 4);
    SafeCopy((uint8_t*)info + 0x14, &wantsDormant, 1);
    SafeCopy(&zero, (uint8_t*)info + OFF::GlobalInfo_CullDistance, 4);
    SafeCopy(&zero, (uint8_t*)info + OFF::GlobalInfo_CullDistance + 4, 4);
    SafeCopy(&off,  (uint8_t*)info + 0x14, 1);        // bWantsToBeDormant = false
    L("[bz] Graph-Info %p des Zonen-Actors (%s): CullDistance %.0f / Sq %.0f -> 0 / 0, bWantsToBeDormant %d -> 0", info, when, cull, cullSq, wantsDormant);
    g_zoneInfoPtr = info;
    return true;
}
// v58c self-heal: if the zone's graph entry was replaced (the actor left and
// re-entered the graph with fresh class defaults), add it once more through
// our own AddNetworkActor hook; otherwise keep the cull copy at zero.
static void ZoneGraphSelfHeal() {
    if (!g_cfg.zoneRelevant || !g_blueZone || !g_drv) return;
    void* info = GraphInfoFor(g_blueZone);
    if (!info) return;
    if (info == g_zoneInfoPtr) {
        float sq = 0.0f, zero = 0.0f;
        if (SafeCopy((uint8_t*)info + OFF::GlobalInfo_CullDistance + 4, &sq, 4) && sq != 0.0f) {
            SafeCopy(&zero, (uint8_t*)info + OFF::GlobalInfo_CullDistance, 4);
            SafeCopy(&zero, (uint8_t*)info + OFF::GlobalInfo_CullDistance + 4, 4);
            static int n = 0;
            if (n++ < 10) L("[bz] Graph-Info %p: CullDistanceSquared war wieder %.0f -> 0", info, sq);
        }
        return;
    }
    if (g_zoneReadds >= 30) return;
    LONG n = InterlockedIncrement(&g_zoneReadds);
    L("[bz] Graph-Eintrag des Zonen-Actors ist NEU (%p statt %p) -> Actor wird erneut hinzugefuegt (#%ld)", info, g_zoneInfoPtr, n);
    __try { ((void (__fastcall*)(void*, void*))(g_base + RVA::NetDriver_AddNetworkActor))(g_drv, g_blueZone); }
    __except (EXCEPTION_EXECUTE_HANDLER) { L("[bz] [!] erneutes AddNetworkActor ist GEFAULTET"); }
}
// Same one-value repair for the remote players' own actors (v59c): the graph's
// cull copy goes to zero, so the actor is never distance-culled for them.
static volatile LONG g_cullZeroed = 0;
static void OwnedCullToZero(void* actor, const wchar_t* what) {
    if (!g_cfg.ownRelevant || !actor) return;
    void* info = GraphInfoFor(actor);
    if (!info) return;
    float sq = 0.0f;
    if (!SafeCopy((uint8_t*)info + OFF::GlobalInfo_CullDistance + 4, &sq, 4) || sq == 0.0f) return;
    float cull = 0.0f, zero = 0.0f;
    SafeCopy((uint8_t*)info + OFF::GlobalInfo_CullDistance, &cull, 4);
    SafeCopy(&zero, (uint8_t*)info + OFF::GlobalInfo_CullDistance, 4);
    SafeCopy(&zero, (uint8_t*)info + OFF::GlobalInfo_CullDistance + 4, 4);
    LONG n = InterlockedIncrement(&g_cullZeroed);
    if (n <= 20) L("[wl] %ls %p: Graph-Cull %.0f m -> 0 -- der Actor wird fuer die Verbindung nicht mehr distanzgecullt -- #%ld", what, actor, cull / 100.0f, n);
}

static bool MarkAlwaysRelevant(void* actor, const char* what) {
    uint8_t f = 0;
    if (!SafeCopy((uint8_t*)actor + OFF::AActor_bAlwaysRelevant, &f, 1)) return false;
    const uint8_t was = f;
    if (!(f & 1)) { f |= 1; if (!SafeCopy(&f, (uint8_t*)actor + OFF::AActor_bAlwaysRelevant, 1)) return false; }
    float cull = 0.0f; const float kHuge = 1.0e18f;
    SafeCopy((uint8_t*)actor + OFF::AActor_NetCullDistanceSquared, &cull, 4);
    if (cull == 225000000.0f) SafeCopy(&kHuge, (uint8_t*)actor + OFF::AActor_NetCullDistanceSquared, 4);
    L("[bz] %s %p: bAlwaysRelevant %s (Flagbyte +0x1CC war 0x%02X), NetCullDistanceSquared war %.1f",
      what, actor, (was & 1) ? "war schon gesetzt" : "GESETZT", was, cull);
    return true;
}
static bool IsZoneActor(void* actor) {
    void* cls = SafePtr((uint8_t*)actor + OFF::UObject_Class);
    if (!cls) return false;
    if (g_zoneClass) return cls == g_zoneClass;
    if (!g_haveZoneKey) g_haveZoneKey = FindNameKey(L"BP_BlueZone_C", &g_zoneKey);   // class may load late
    if (g_haveZoneKey) {
        FNameRaw n;
        return SafeCopy((uint8_t*)cls + OFF::UObject_Name, &n, sizeof(n)) && n.Comparison == g_zoneKey.Comparison;
    }
    wchar_t nm[128];
    return ClassNameOf(actor, nm, 128) && (wcsstr(nm, L"BlueZone") || wcsstr(nm, L"PlayZone"));
}

// ---- UNetDriver::AddNetworkActor (v58, v96, v154) --------------------------
// Three things happen BEFORE the driver forwards an actor to the graph:
//  * the zone class/instance becomes bAlwaysRelevant (AlwaysRelevant node),
//    and its per-actor cull copy is zeroed right after the add;
//  * an actor that arrives already DormantAll/Partial (class default) is set
//    to DORM_Awake (v154 -- the block above cannot see constructor values);
//  * a LEVEL actor at DORM_Initial is set to DORM_Awake (v96): the graph's
//    IsActorValidForReplicationGather rejects "DORM_Initial && bNetStartup"
//    outright, and under the mask nothing ever wakes doors, windows and
//    level loot. Same single byte the zone has used since v58c.
typedef void (__fastcall* tAddNetActor)(void* drv, void* actor);
static tAddNetActor  g_origAddNetActor = nullptr;
static volatile LONG g_addActorCalls = 0, g_dormWoken = 0;

static void __fastcall MyAddNetworkActor(void* drv, void* actor) {
    InterlockedIncrement(&g_addActorCalls);
    const bool zone = g_cfg.zoneRelevant && actor && IsZoneActor(actor);
    if (zone) {
        if (!g_zoneCdoPatched) {
            void* cls = SafePtr((uint8_t*)actor + OFF::UObject_Class);
            void* cdo = cls ? SafePtr((uint8_t*)cls + OFF::UClass_DefaultObject) : nullptr;
            if (cdo && SafePtr((uint8_t*)cdo + OFF::UObject_Class) == cls) {
                if (!g_zoneClass) g_zoneClass = cls;
                g_zoneCdoPatched = MarkAlwaysRelevant(cdo, "CDO BP_BlueZone_C");
            } else L("[bz] CDO der Zonenklasse nicht gefunden (cls=%p cdo=%p)", cls, cdo);
        }
        uint8_t f = 0;
        SafeCopy((uint8_t*)actor + OFF::AActor_bAlwaysRelevant, &f, 1);
        if (!(f & 1)) InterlockedIncrement(&g_zoneAddFixes);
        MarkAlwaysRelevant(actor, (f & 1) ? "Zonen-Actor (Flag schon da)" : "Zonen-Actor (in AddNetworkActor)");
        uint8_t dorm = 0;
        if (SafeCopy((uint8_t*)actor + OFF::AActor_NetDormancy, &dorm, 1) && dorm != OFF::DORM_Awake) {
            const uint8_t awake = OFF::DORM_Awake;
            SafeCopy(&awake, (uint8_t*)actor + OFF::AActor_NetDormancy, 1);
            L("[bz] Zonen-Actor NetDormancy %d -> 1 (Awake) vor dem Eintritt in den Graphen", dorm);
        }
        if (!g_blueZone) g_blueZone = actor;
        // v172: the graph routes a class by its policy byte (0 NotRouted,
        // 1 RelevantAllConnections, 2 Spatialize_Static, 3 Spatialize_Dynamic,
        // 4 Spatialize_Dormancy). bAlwaysRelevant on the instance does not
        // change the routing -- only the byte does. Read it, and with
        // -zonepolicy set it to 1 BEFORE the actor enters the graph.
        {
            static bool policyDone = false;
            void* cls = SafePtr((uint8_t*)actor + OFF::UObject_Class);
            void* graph = RepGraph();
            if (!policyDone && cls && graph && IsKnownRepGraph(SafePtr(graph))) {
                typedef void* (__fastcall* tLookup)(void* map, void* cls);
                void* pol = nullptr;
                __try { pol = ((tLookup)(g_base + RVA::RepGraph_ClassPolicyLookup))((uint8_t*)graph + OFF::RepGraph_ClassPolicyMap, cls); }
                __except (EXCEPTION_EXECUTE_HANDLER) { pol = nullptr; }
                uint8_t b = 0xFF;
                if (pol) SafeCopy(pol, &b, 1);
                if (pol && b != 0xFF) {
                    policyDone = true;
                    if (g_cfg.zonePolicy && b != 1) {
                        const uint8_t one = 1;
                        SafeCopy(&one, pol, 1);
                        L("[bz] Klassen-Policy der Zone im Graphen: %d -> 1 (RelevantAllConnections) -- -zonepolicy", b);
                    } else L("[bz] Klassen-Policy der Zone im Graphen: %d (0 NotRouted, 1 RelevantAll, 2 SpatialStatic, 3 SpatialDynamic, 4 SpatialDormancy)%s", b,
                             (b != 1 && !g_cfg.zonePolicy) ? " -- mit -zonepolicy wuerde sie auf 1 gesetzt" : "");
                } else L("[bz] Klassen-Policy der Zone: kein Eintrag im Graphen (Lookup %p) -- Routing kommt aus der Klassenhierarchie", pol);
            }
        }
        L("[bz] AddNetworkActor fuer Zonen-Actor %p auf Treiber %p (g_drv=%p)", actor, drv, g_drv);
    }
    if (g_cfg.dormBlock && actor) {
        uint8_t dorm = 0;
        if (SafeCopy((uint8_t*)actor + OFF::AActor_NetDormancy, &dorm, 1)
         && (dorm == OFF::DORM_DormantAll || dorm == OFF::DORM_DormantPartial)
         && (!IsNetStartup(actor) || g_cfg.dormBlockLevel)) {
            const uint8_t awake = OFF::DORM_Awake;
            if (SafeCopy(&awake, (uint8_t*)actor + OFF::AActor_NetDormancy, 1)) {
                LONG n = InterlockedIncrement(&g_dormBlockedAdd);
                if (n <= 25) { wchar_t an[128] = L"?"; ClassNameOf(actor, an, 128);
                    L("[dz] %ls %p kam schon mit Dormancy %d in den Graphen -> DORM_Awake -- #%ld", an, actor, dorm, n); }
            }
        }
    }
    if (g_cfg.dormWake && actor) {
        uint8_t dorm = 0, rep = 0;
        if (SafeCopy((uint8_t*)actor + OFF::AActor_NetDormancy, &dorm, 1) && dorm == OFF::DORM_Initial
         && IsNetStartup(actor)
         && SafeCopy((uint8_t*)actor + OFF::AActor_bReplicates, &rep, 1) && (rep & 1)) {
            if (g_cfg.wakeDoorsOnly) {
                wchar_t wn[128] = L"";
                if (!ClassNameOf(actor, wn, 128)
                 || !(wcsstr(wn, L"Door") || wcsstr(wn, L"Window") || wcsstr(wn, L"Glass") || wcsstr(wn, L"Break") || wcsstr(wn, L"Destruct")))
                    { if (g_origAddNetActor) g_origAddNetActor(drv, actor); return; }
            }
            const uint8_t awake = OFF::DORM_Awake;
            if (SafeCopy(&awake, (uint8_t*)actor + OFF::AActor_NetDormancy, 1)) {
                LONG n = InterlockedIncrement(&g_dormWoken);
                if (n <= 25) { wchar_t an[128] = L"?"; ClassNameOf(actor, an, 128);
                    L("[dw] %ls %p war DORM_Initial und im Level platziert -- auf DORM_Awake gesetzt, damit der Graph ihn annimmt -- #%ld", an, actor, n); }
            }
        }
    }
    if (g_origAddNetActor) g_origAddNetActor(drv, actor);
    if (zone) ZoneGraphCullToZero(actor, "nach AddNetworkActor");
}

// ---- FNetViewer (v58g) -----------------------------------------------------
// The grid node gathers cells around ONE point per connection, the viewer's
// ViewLocation. For a connection that belongs to a registered remote player it
// is moved onto that player's pawn when more than 50 m off.
typedef void* (__fastcall* tNetViewerCtor)(void* out, void* conn, float dt);
static tNetViewerCtor g_origNetViewer = nullptr;
static volatile LONG  g_viewCalls = 0, g_viewFixes = 0;

static void* __fastcall MyNetViewerCtor(void* out, void* conn, float dt) {
    void* r = g_origNetViewer ? g_origNetViewer(out, conn, dt) : out;
    if (!out || !conn) return r;
    InterlockedIncrement(&g_viewCalls);
    void* pc  = SafePtr((uint8_t*)conn + OFF::UNetConn_PlayerController);
    void* own = SafePtr((uint8_t*)conn + OFF::UNetConn_OwningActor);
    int idx = -1;
    for (int i = 0; i < g_remoteCount; ++i)
        if (g_remote[i].controller == pc || g_remote[i].pawn == own) { idx = i; break; }
    if (idx < 0) return r;
    float loc[3] = { 0.f, 0.f, 0.f }, pawn[3];
    SafeCopy((uint8_t*)out + OFF::FNetViewer_ViewLocation, loc, 12);
    if (!RemotePawnLocation(g_remote[idx], pawn)) return r;
    const float dist = Dist3(pawn, loc);
    if (g_cfg.viewFix && dist > 5000.0f) {
        SafeCopy(pawn, (uint8_t*)out + OFF::FNetViewer_ViewLocation, 12);
        void* vt = SafePtr((uint8_t*)out + OFF::FNetViewer_ViewTarget);
        if (vt != g_remote[idx].pawn) { void* np = g_remote[idx].pawn; SafeCopy(&np, (uint8_t*)out + OFF::FNetViewer_ViewTarget, 8); }
        LONG n = InterlockedIncrement(&g_viewFixes);
        if (n <= 6 || (n % 500) == 0)
            L("[vw] Blickpunkt des Remote-Spielers war %.0f Einheiten daneben -> auf den Pawn {%.0f, %.0f, %.0f} gesetzt (#%ld)", dist, pawn[0], pawn[1], pawn[2], n);
    }
    return r;
}

// ---- UActorChannel::SetChannelActor (v108/v111/v122) -----------------------
// The moment the zone gets a channel on a connection is the moment the
// GameState's zone reference becomes resolvable for that client. The flag is
// raised here and acted on from the driver tick (rule 2).
typedef void (__fastcall* tSetChannelActor)(void* channel, void* actor, int32_t flags);
static tSetChannelActor g_origSetChannelActor = nullptr;
static volatile LONG    g_channelsOpened = 0;

// v172: remember the zone channels so a CLOSE (SetChannelActor(ch, null)) can
// be logged with the caller -- a second "GEOEFFNET" per connection means the
// first channel was closed and the client destroyed its zone actor.
static void* g_zoneChanRing[8] = {};
static int   g_zoneChanRingN = 0;
static volatile LONG g_zoneChanClosed = 0;
static volatile LONG g_chDoors = 0, g_chWindows = 0, g_chPickups = 0;   // v174: channels opened per class
enum : uint8_t { CV_None = 0, CV_Door = 1, CV_Destructible = 2, CV_Pickup = 3 };
static uint8_t ClassVerdictOf(void* obj);
static void __fastcall MySetChannelActor(void* channel, void* actor, int32_t flags) {
    if (!actor && channel) {
        for (int i = 0; i < 8; ++i) if (g_zoneChanRing[i] == channel) {
            g_zoneChanRing[i] = nullptr;
            LONG n = InterlockedIncrement(&g_zoneChanClosed);
            uint8_t d = 0; if (g_blueZone) SafeCopy((uint8_t*)g_blueZone + OFF::AActor_NetDormancy, &d, 1);
            if (n <= 20) L("[zc] *** Zonen-Kanal %p GESCHLOSSEN -- Zone %p NetDormancy=%d, Aufrufer base+0x%llX -- #%ld ***",
                           channel, g_blueZone, d, (unsigned long long)((uintptr_t)_ReturnAddress() - g_base), n);
            break;
        }
    }
    if (actor) {
        InterlockedIncrement(&g_channelsOpened);
        if (g_remoteCount > 0) {
            const uint8_t cv = ClassVerdictOf(actor);
            if (cv == CV_Door) InterlockedIncrement(&g_chDoors);
            else if (cv == CV_Destructible) InterlockedIncrement(&g_chWindows);
            else if (cv == CV_Pickup) InterlockedIncrement(&g_chPickups);
        }
        if (actor == g_blueZone) {
            g_zoneChannelJustOpened = true;
            InterlockedIncrement(&g_zoneChannelOpens);
            g_zoneChanRing[g_zoneChanRingN & 7] = channel; ++g_zoneChanRingN;
            uint8_t d = 0; SafeCopy((uint8_t*)actor + OFF::AActor_NetDormancy, &d, 1);
            L("[zc] Zonen-Kanal %p GEOEFFNET -- Zone %p NetDormancy=%d", channel, actor, d);
        }
        if (actor == g_blueZone || actor == GameStateOfWorld()) {
            void* gs  = GameStateOfWorld();
            void* ref = (gs && g_zoneRefOff) ? SafePtr((uint8_t*)gs + g_zoneRefOff) : nullptr;
            L("[jo] t=%lu ms  KANAL fuer %s %p auf Verbindung %p | Zonen-Referenz auf der GameState = %p %s",
              (unsigned long)GetTickCount(), actor == g_blueZone ? "die BlueZone" : "die GameState", actor, channel, ref,
              !g_zoneRefOff ? "(Offset noch unbekannt)" : ((ref && ref == g_blueZone) ? "(gesetzt)" : "(NULL -- der Client bekaeme nichts)"));
        }
    }
    if (g_origSetChannelActor) g_origSetChannelActor(channel, actor, flags);
}

// ---- ReplicateNow (v27/v29) -------------------------------------------------
// The client build's TickFlush has no WITH_SERVER_CODE block, so
// ServerReplicateActors is called here, after the original TickFlush, with the
// same guards the engine uses: our driver, at least one client, a graph whose
// slot holds a known implementation.
static void ReplicateNow(void* drv, float dt) {
    if (drv != g_drv) return;
    if (ClientCount() <= 0) return;
    void* rd = SafePtr((uint8_t*)drv + OFF::UNetDriver_RepDriver);
    if (!rd) return;
    void** vt = (void**)SafePtr(rd);
    if (!vt || !InModule(vt)) return;
    void* fn = SafePtr((uint8_t*)vt + RVA::RepDriver_ServerReplicateActors_VT);
    if (fn != (void*)(g_base + RVA::RepGraph_ServerReplicateActors) && fn != (void*)(g_base + RVA::RealRepGraph_ServerReplicate)) {
        static int warned = 0;
        if (!warned++) L("[rg] VTable+0x2F0 = base+0x%llX -- keine bekannte ServerReplicateActors -> KEIN Aufruf", (unsigned long long)((uintptr_t)fn - g_base));
        return;
    }
    ((int32_t (__fastcall*)(void*, float))fn)(rd, dt);
    static int calls = 0, logged = 0;
    if (++calls % 600 == 0 && logged < 12) {
        ++logged;
        L("[rg] ServerReplicateActors: %d Aufrufe | Verbindungen im Graphen %d, im Treiber %d", calls, SafeI32((uint8_t*)rd + 0x48), ClientCount());
    }
}

// ============================================================================
//  == 13. blue zone
//
//  The client learns the zone through a replicated OBJECT REFERENCE on the
//  GameState (+0x8D0). A reference only resolves if the zone actor has a
//  channel on that connection when it arrives, and a property that never
//  changes again is never resent. So (v122): when the zone channel opens,
//  blank the reference for ONE frame and put it back, plus the same one-frame
//  change on the zone actor's SelectedPlayZoneInfoIndex and phase-list count,
//  so the client receives a real delta. The zone is kept DORM_Awake (v120),
//  always relevant with a zero cull copy (v58), and nudged with ForceNetUpdate
//  once a second together with the GameState during prematch (v47/v106).
// ============================================================================
static bool     g_zoneScanDone = false;
static void*    g_toggleSaved  = nullptr;
static int      g_togglePhase  = 0;
static int32_t  g_zoneIdxSaved = (-2147483647 - 1);
static int32_t  g_zoneListSaved = 0;
static volatile LONG g_zoneToggles = 0, g_zonePinHits = 0, g_gsKicks = 0, g_zoneResendN = 0;
static const int32_t ZONE_IDX_NONE = (-2147483647 - 1);

// Scan the GameState for the zone reference: every slot that points at a
// UObject whose class name contains BlueZone/PlayZone. The slot offset is the
// reference the client depends on. Runs once a second; the heavy log only once.
static void FindBlueZone() {
    void* gs = GameStateOfWorld();
    if (!gs) return;
    static void* lastGs = nullptr;
    if (gs != lastGs) {
        lastGs = gs;
        wchar_t gsn[128];
        if (ClassNameOf(gs, gsn, 128)) L("[bz] GameState %p, Klasse %ls", gs, gsn);
    }
    wchar_t nm[128];
    for (size_t o = 0; o + 8 <= 0xA00; o += 8) {
        void* p = SafePtr((uint8_t*)gs + o);
        if (!p || (uintptr_t)p < 0x10000) continue;
        void* vt = SafePtr(p);
        if (!vt || !InModule(vt)) continue;
        if (!ClassNameOf(p, nm, 128)) continue;
        if (wcsstr(nm, L"BlueZone") || wcsstr(nm, L"PlayZone")) {
            g_blueZone   = p;
            g_zoneRefOff = (uintptr_t)o;
            if (g_zoneScanDone) return;
            g_zoneScanDone = true;
            uint8_t role = 0, remote = 0, rep = 0, ar = 0;
            SafeCopy((uint8_t*)p + OFF::AActor_Role, &role, 1);
            SafeCopy((uint8_t*)p + OFF::AActor_RemoteRole, &remote, 1);
            SafeCopy((uint8_t*)p + OFF::AActor_bReplicates, &rep, 1);
            SafeCopy((uint8_t*)p + OFF::AActor_bAlwaysRelevant, &ar, 1);
            L("[bz] *** BlueZone gefunden *** GameState+0x%03llX -> %p  Klasse %ls  Role=%s RemoteRole=%s bReplicates=%d bAlwaysRelevant=%d",
              (unsigned long long)o, p, nm, RoleName(role), RoleName(remote), rep & 1, ar & 1);
            return;
        }
    }
}
// v50: classic-path relevancy -- raise the untouched engine default cull.
static void EnsureZoneRelevance() {
    if (!g_blueZone) return;
    const float kHuge = 1.0e18f;
    float cull = 0.0f;
    if (!SafeCopy((uint8_t*)g_blueZone + OFF::AActor_NetCullDistanceSquared, &cull, 4)) return;
    if (cull == kHuge) return;
    if (cull != 225000000.0f) {
        static int warned = 0;
        if (!warned++) L("[bz] NetCullDistanceSquared = %.1f -- nicht der Engine-Default, wird NICHT angefasst", cull);
        return;
    }
    if (SafeCopy(&kHuge, (uint8_t*)g_blueZone + OFF::AActor_NetCullDistanceSquared, 4)) {
        static int done = 0;
        if (!done++) L("[bz] *** NetCullDistanceSquared +0x2D4: 225000000.0 -> 1e18 -- BlueZone ueberall netzrelevant ***");
    }
}
static void KickBlueZone() {
    if (!g_blueZone) return;
    if (ForceNetUpdateOn(g_blueZone)) InterlockedIncrement(&g_zoneKicks);
}
// v106: the zone reference and the match info ride on the GameState. Capped
// to ~2 minutes of prematch.
static void KickGameState() {
    if (!g_cfg.gsKick) return;
    if (g_gsKicks >= 120) return;
    void* gs = GameStateOfWorld();
    if (!gs || !ForceNetUpdateOn(gs)) return;
    LONG n = InterlockedIncrement(&g_gsKicks);
    if (n <= 5 || (n % 30) == 0) {
        void* ref = g_zoneRefOff ? SafePtr((uint8_t*)gs + g_zoneRefOff) : nullptr;
        L("[gz] GameState %p angestossen (#%ld) -- Zonen-Referenz bei +0x%03llX = %p %s", gs, n, (unsigned long long)g_zoneRefOff, ref,
          (g_zoneRefOff && ref && ref == g_blueZone) ? "(zeigt auf die BlueZone)" : (g_zoneRefOff ? "(NICHT die BlueZone)" : "(Offset noch unbekannt)"));
    }
}

// The one-frame reference change (v122..v125). Frame 1: blank the GameState
// reference, bump the zone's SelectedPlayZoneInfoIndex, shorten the phase
// list by one (count only). Frame 2: put everything back and ForceNetUpdate
// both actors. Runs in front of the once-a-second gate, so the null window is
// one tick.
static void ZoneToggleStep() {
    if (!g_cfg.zoneToggle || !g_zoneRefOff || !g_blueZone) return;
    void* gs = GameStateOfWorld();
    if (!gs) return;
    if (g_togglePhase == 1) {
        if (g_zoneIdxSaved != ZONE_IDX_NONE) SafeCopy(&g_zoneIdxSaved, (uint8_t*)g_blueZone + OFF::Zone_SelectedInfoIndex, 4);
        if (g_zoneListSaved > 0) { SafeCopy(&g_zoneListSaved, (uint8_t*)g_blueZone + OFF::Zone_PhaseListNum, 4); g_zoneListSaved = 0; }
        KickBlueZone();
        void* z = g_toggleSaved;
        if (z && SafeCopy(&z, (uint8_t*)gs + g_zoneRefOff, 8)) {
            ForceNetUpdateOn(gs);
            L("[zt] Zonen-Referenz zurueckgeschrieben (%p), SelectedPlayZoneInfoIndex wieder %d -- der Client bekommt jetzt eine ECHTE Aenderung (#%ld)",
              z, (int)(g_zoneIdxSaved == ZONE_IDX_NONE ? -999 : g_zoneIdxSaved), g_zoneToggles);
            g_zoneIdxSaved = ZONE_IDX_NONE;
        }
        g_togglePhase = 0;
        g_toggleSaved = nullptr;
    } else if (g_zoneChannelJustOpened) {
        g_zoneChannelJustOpened = false;
        void* cur = SafePtr((uint8_t*)gs + g_zoneRefOff);
        if (!cur) return;
        g_toggleSaved = cur;
        void* nul = nullptr;
        if (!SafeCopy(&nul, (uint8_t*)gs + g_zoneRefOff, 8)) return;
        int32_t idx = 0;
        if (SafeCopy((uint8_t*)g_blueZone + OFF::Zone_SelectedInfoIndex, &idx, 4)) {
            g_zoneIdxSaved = idx;
            int32_t bump = idx + 1;
            SafeCopy(&bump, (uint8_t*)g_blueZone + OFF::Zone_SelectedInfoIndex, 4);
        }
        int32_t ln = 0;
        if (SafeCopy((uint8_t*)g_blueZone + OFF::Zone_PhaseListNum, &ln, 4) && ln >= 2) {
            g_zoneListSaved = ln;
            int32_t shorter = ln - 1;
            SafeCopy(&shorter, (uint8_t*)g_blueZone + OFF::Zone_PhaseListNum, 4);
        }
        g_togglePhase = 1;
        InterlockedIncrement(&g_zoneToggles);
        L("[zt] Zonen-Kanal ist offen -> Referenz fuer EINEN Frame auf null gesetzt, damit die Eigenschaft wirklich als geaendert gilt (#%ld)", g_zoneToggles);
    }
}

// Game thread, from MyTickFlush (driver visible). The toggle runs every tick,
// the rest about once a second.
static void ZoneTick() {
    if (!g_cfg.zoneFix || !g_world || !g_drv) return;
    static int frames = 0;
    if (ClientCount() <= 0) {
        // v173: find the zone BEFORE the first client joins. In the one round
        // where the zone rendered, the GameState channel opened with the zone
        // reference already known; in the rounds where it did not, the join
        // came first and the reference was still unknown.
        if (++frames >= 60) { frames = 0; FindBlueZone(); }
        return;
    }
    ZoneToggleStep();
    if (++frames < 60) return;
    frames = 0;
    FindBlueZone();
    if (g_cfg.zonePin && g_blueZone) {                  // v120: never let the zone's channel close over dormancy
        uint8_t d = 0;
        if (SafeCopy((uint8_t*)g_blueZone + OFF::AActor_NetDormancy, &d, 1) && d > OFF::DORM_Awake) {
            const uint8_t awake = OFF::DORM_Awake;
            SafeCopy(&awake, (uint8_t*)g_blueZone + OFF::AActor_NetDormancy, 1);
            LONG n = InterlockedIncrement(&g_zonePinHits);
            if (n <= 10 || (n % 50) == 0) L("[zp] Zone %p war NetDormancy=%d -> auf DORM_Awake zurueckgeschrieben (#%ld)", g_blueZone, d, n);
        }
    }
    EnsureZoneRelevance();
    KickBlueZone();
    KickGameState();
    if (g_zoneChannelJustOpened && !g_cfg.zoneToggle) {   // v111 path, only when the toggle is off
        g_zoneChannelJustOpened = false;
        void* gs = GameStateOfWorld();
        if (gs && ForceNetUpdateOn(gs)) {
            LONG n = InterlockedIncrement(&g_zoneResendN);
            if (n <= 10) L("[zf] Zonen-Kanal ist aufgegangen -> GameState erneut angestossen (#%ld)", n);
        }
    }
    if (g_blueZone && g_cfg.zoneRelevant) {
        if (!g_zoneInfoPtr) ZoneGraphCullToZero(g_blueZone, "ZoneTick (kein Add gesehen)");
        else                ZoneGraphSelfHeal();
    }
}

// ---- play zone row (v152/v153) ---------------------------------------------
// InitTableSetting(-1) picks a random row out of all 156 rows of
// TBL-PlayZone_OrbIsland. Row IDs per MaxPlayerCount (ID - 520100000):
static const int kZone16[]  = { 273,274,275,276,277,278,279,280,281,282,283,284,285,286,287,288,289,290,291,292,293,294,295,296,297,298,299,300,301,302,307,311,312,313 };
static const int kZone24[]  = { 243,244,245,246,247,248,249,250,251,252,253,254,255,256,257,258,259,260,261,262,263,264,265,266,267,268,269,270,271,272,310,331,332,333,334,335,336,337,338,339,340,341,351 };
static const int kZone40[]  = { 223,224,225,226,227,228,229,230,231,232,233,234,235,236,237,238,239,240,241,242,305,309,324,325,326,327,328,329,330,419 };
static const int kZone64[]  = { 203,204,205,206,207,208,209,210,211,212,213,214,215,216,217,218,219,220,221,222,304,308,323,414,417,418 };
static const int kZone80[]  = { 314,315,316,317,318,319,320,321,322,342,349,350,409,410,411,412,413,425,426,427,428,429 };
static const int kZone100[] = { 303 };
static bool ZoneList(int players, const int** list, int* n) {
    switch (players) {
    case 16:  *list = kZone16;  *n = (int)(sizeof(kZone16)  / sizeof(int)); return true;
    case 24:  *list = kZone24;  *n = (int)(sizeof(kZone24)  / sizeof(int)); return true;
    case 40:  *list = kZone40;  *n = (int)(sizeof(kZone40)  / sizeof(int)); return true;
    case 64:  *list = kZone64;  *n = (int)(sizeof(kZone64)  / sizeof(int)); return true;
    case 80:  *list = kZone80;  *n = (int)(sizeof(kZone80)  / sizeof(int)); return true;
    case 100: *list = kZone100; *n = (int)(sizeof(kZone100) / sizeof(int)); return true;
    }
    return false;
}
static int ZoneTierFor(int passengers) {
    for (int i = 0; i < g_zoneTierN; ++i) if (passengers <= g_zoneTiers[i].maxPassengers) return g_zoneTiers[i].rows;
    return g_zoneTiers[g_zoneTierN - 1].rows;
}
static int ZonePickFrom(int players) {
    const int* list = nullptr; int n = 0;
    if (!ZoneList(players, &list, &n) || n <= 0) return -1;
    static bool seeded = false;
    if (!seeded) { srand((unsigned)(time(nullptr) ^ GetCurrentProcessId())); seeded = true; }
    return list[rand() % n];
}
static int g_zoneTierCur = -1;
static int ZonePick() {
    if (g_cfg.zoneId >= 0) return g_cfg.zoneId >= RVA::PlayZone_IdBase ? g_cfg.zoneId - RVA::PlayZone_IdBase : g_cfg.zoneId;
    if (g_cfg.zoneScale) { g_zoneTierCur = ZoneTierFor(1); return ZonePickFrom(g_zoneTierCur); }   // map load: host alone
    return ZonePickFrom(g_cfg.zonePlayers);
}
typedef void* (__fastcall* tInitTableSetting)(void*, int, void*, void*, void*, void*);
static tInitTableSetting g_origInitTableSetting = nullptr;

static void* __fastcall MyInitTableSetting(void* gm, int idx, void* a3, void* a4, void* a5, void* a6) {
    int use = idx;
    if (idx < 0) {
        int pick = ZonePick();
        if (pick >= 0) {
            use = pick;
            L("[bz] Zonenwahl: Spiel wollte Index %d (Zufall ueber alle 156 Zeilen) -> Zeile ID %d (%s)", idx, RVA::PlayZone_IdBase + pick,
              g_cfg.zoneId >= 0 ? "-zoneid" : g_cfg.zoneScale ? "Stufe fuer 1 Passagier, waechst mit den Beitritten" : "Spielerzahl-Tabelle");
        } else L("[bz] Zonenwahl: Index %d bleibt (-zoneplayers=0 oder unbekannte Spielerzahl)", idx);
    } else L("[bz] Zonenwahl: Spiel gibt Index %d selbst vor -- unveraendert", idx);
    return g_origInitTableSetting ? g_origInitTableSetting(gm, use, a3, a4, a5, a6) : nullptr;
}

// PlayerState census for the tiers: walks the decoded level actor lists.
static void*   g_clsSeen[512];
static uint8_t g_clsIsPs[512];
static int     g_clsSeenN = 0;
static bool ClassIsPlayerState(void* a, void* cls) {
    for (int i = 0; i < g_clsSeenN; ++i) if (g_clsSeen[i] == cls) return g_clsIsPs[i] != 0;
    wchar_t nm[128] = L"";
    const bool ps = ClassNameOf(a, nm, 128) && wcsstr(nm, L"PlayerState") != nullptr;
    if (g_clsSeenN < 512) { g_clsSeen[g_clsSeenN] = cls; g_clsIsPs[g_clsSeenN] = ps ? 1 : 0; ++g_clsSeenN; }
    return ps;
}
static int CountPlayerStates() {
    if (!g_world) return -1;
    void**  levels = (void**)SafePtr((uint8_t*)g_world + OFF::UWorld_Levels);
    int32_t nLv    = SafeI32((uint8_t*)g_world + OFF::UWorld_Levels + 8);
    if (!levels || nLv <= 0 || nLv > 4096) return -1;
    int n = 0;
    for (int li = 0; li < nLv; ++li) {
        uint8_t* lvl = (uint8_t*)SafePtr(levels + li);
        if (!lvl) continue;
        uint64_t* acts = (uint64_t*)SafePtr(lvl + OFF::ULevel_Actors);
        int32_t   nA   = SafeI32(lvl + OFF::ULevel_Actors + 8);
        if (!acts || nA <= 0 || nA > 200000) continue;
        for (int ai = 0; ai < nA; ++ai) {
            void* a = DecodedActorAt(acts + ai);
            if (!a) continue;
            void* cls = SafePtr((uint8_t*)a + OFF::UObject_Class);
            if (cls && ClassIsPlayerState(a, cls)) ++n;
        }
    }
    return n;
}
// ~every 2 s until the countdown is over: re-run InitTableSetting with a row
// of the tier that fits the current passenger count.
static void ZoneScaleTick() {
    if (!g_cfg.zoneScale || !g_origInitTableSetting || !g_world) return;
    void* gs = GameStateOfWorld();
    void* gm = GameModeOfWorld();
    if (!gs || !gm) return;
    uint8_t state = 0xFF; SafeCopy((uint8_t*)gs + OFF::GameState_Phase, &state, 1);
    if (state == 0xFF || state >= 3) return;
    int passengers = CountPlayerStates();
    if (passengers <= 0) return;
    int rows = ZoneTierFor(passengers);
    if (rows == g_zoneTierCur) return;
    int pick = ZonePickFrom(rows);
    if (pick < 0) return;
    g_zoneTierCur = rows;
    static int reruns = 0;
    L("[bz] Zonenstufe: %d Passagiere -> %d-Spieler-Tabelle, Zeile ID %d wird NEU eingespielt (Match-Phase %d, #%d)", passengers, rows, RVA::PlayZone_IdBase + pick, state, ++reruns);
    __try { g_origInitTableSetting(gm, pick, nullptr, nullptr, nullptr, nullptr); }
    __except (EXCEPTION_EXECUTE_HANDLER) { L("[!] [bz] Ausnahme beim erneuten InitTableSetting -- Zonenstufen AUS"); g_cfg.zoneScale = false; }
}

// ---- view type (v151): -fpp / -tpp ------------------------------------------
static void ViewTypeTick() {
    if (!g_cfg.forceView || !g_world) return;
    const uint8_t want = (uint8_t)g_cfg.forceView;
    static int writes = 0;
    if (void* gs = GameStateOfWorld()) {
        uint8_t cur = 0xFF; SafeCopy((uint8_t*)gs + OFF::GameState_GameViewType, &cur, 1);
        if (cur != want && cur != 0xFF) { bool ok = SafeWriteU8((uint8_t*)gs + OFF::GameState_GameViewType, want);
            if (++writes <= 6) L("[vt] GameState %p GameViewType %d -> %d %s", gs, cur, want, ok ? "" : "(Schreiben fehlgeschlagen)"); }
    }
    if (void* gm = GameModeOfWorld()) {
        uint8_t cur = 0xFF; SafeCopy((uint8_t*)gm + OFF::GameMode_GameViewType, &cur, 1);
        if (cur != want && cur != 0xFF) { bool ok = SafeWriteU8((uint8_t*)gm + OFF::GameMode_GameViewType, want);
            if (++writes <= 6) L("[vt] GameMode %p GameViewType %d -> %d %s", gm, cur, want, ok ? "" : "(Schreiben fehlgeschlagen)"); }
    }
}

// ============================================================================
//  == 14. loot around remote players (v58 .. v58s, v96)
//
//  Floor loot is spawned per building by ABravoHotelBuilding::
//  CheckSpawnByStandalone, on a timer, and only around the HOST's pawn: the
//  check takes UWorld::GetFirstPlayerController(), its pawn, the distance and
//  the "in-bound levels loaded" byte at PC+0x1928. Three things make it work
//  for remote players:
//   * after the original ran for the host, it runs once more per remote pawn
//     in reach with GetFirstPlayerController redirected to that controller;
//   * every ~2 s the level actor lists are walked: actors near a remote player
//     that carry a UBravoHotelDetectItemSpawnBoxComponent are fired directly
//     (the game's own test), each source once; buildings/vehicle spawners
//     without a running timer get the manual check;
//   * dormant replicated actors near a remote player are flushed and
//     registered, once each (never DormantAll/Partial -- rule 3).
//  Nothing is spawned before match phase 4 (v58p).
// ============================================================================
typedef void  (__fastcall* tCheckSpawn)(void* actor);
static tCheckSpawn   g_origBuildingSpawn = nullptr;
static tCheckSpawn   g_origVehicleSpawn  = nullptr;
static void*         g_spawnOverridePC   = nullptr;   // game thread only
static int           g_spawnDepth        = 0;
static bool          g_firstPcHooked     = false;
static void*         g_buildingNative    = nullptr;   // UClass ABravoHotelBuilding
static void*         g_vehicleNative     = nullptr;   // UClass ABravoHotelVehicleSpawnActor
static volatile LONG g_spawnCalls = 0, g_spawnChecks = 0, g_spawnFlagForced = 0, g_spawnLobbySkipped = 0, g_spawnListGrew = 0;
static volatile LONG g_sweepCalls = 0, g_wokenActors = 0, g_wakeSkipped = 0, g_ownFound = 0;
static volatile LONG g_lootSources = 0, g_lootOpen = 0, g_lootFired = 0, g_lootDone = 0;
static volatile LONG g_lootGate[7] = { 0, 0, 0, 0, 0, 0, 0 };

// Replacement for UWorld::GetFirstPlayerController (21 bytes, re-implemented).
static void* __fastcall MyGetFirstPlayerController(void* world) {
    if (g_spawnOverridePC) return g_spawnOverridePC;
    if (*(int32_t*)((uint8_t*)world + OFF::UWorld_ControllerList + 8) <= 0) return nullptr;
    void* data = *(void**)((uint8_t*)world + OFF::UWorld_ControllerList);
    return ((void* (__fastcall*)(void*))(g_base + RVA::WeakObjectPtr_Get))(data);
}
static void* GameStateOf(void* actor) {
    void* gs = nullptr;
    __try { gs = ((void* (__fastcall*)(void*))(g_base + RVA::Actor_GameState))(actor); }
    __except (EXCEPTION_EXECUTE_HANDLER) { gs = nullptr; }
    return gs;
}
static bool MatchStarted(void* actorInWorld) {
    uint8_t* gs = (uint8_t*)GameStateOf(actorInWorld);
    uint8_t ph = 0;
    return gs && SafeCopy(gs + OFF::GameState_Phase, &ph, 1) && ph >= 4;
}

// The nested check for every remote pawn within reach.
static void RunSpawnForRemotePawns(tCheckSpawn orig, void* actor, const char* what, float reach) {
    if (!g_cfg.spawnRemote || !g_firstPcHooked || !g_drv || g_remoteCount == 0) return;
    if (!MatchStarted(actor)) { InterlockedIncrement(&g_spawnLobbySkipped); return; }
    float aloc[3];
    if (!ActorLocation(actor, aloc)) return;
    static int logged = 0;
    for (int i = 0; i < g_remoteCount; ++i) {
        float p[3];
        if (!RemotePawnLocation(g_remote[i], p)) continue;
        const float d = Dist3(p, aloc);
        if (d > reach) continue;
        uint8_t* pc = (uint8_t*)g_remote[i].controller;
        // PC+0x1928 is set by the client's ServerInBoundLevelsAreLoaded; if it
        // is still 0 it is lifted for this one check (the tiles are ours, v55).
        uint8_t flag = 0; bool forced = false;
        SafeCopy(pc + OFF::PC_InBoundLevelsLoaded, &flag, 1);
        if (flag == 0) { uint8_t one = 1; forced = SafeCopy(&one, pc + OFF::PC_InBoundLevelsLoaded, 1); if (forced) InterlockedIncrement(&g_spawnFlagForced); }
        uint8_t* gs = (uint8_t*)GameStateOf(actor);
        const int32_t listBefore = gs ? SafeI32(gs + OFF::GameState_SpawnedNum) : -1;
        g_spawnOverridePC = pc;
        ++g_spawnDepth;
        __try { orig(actor); } __except (EXCEPTION_EXECUTE_HANDLER) { L("[ls] [!] Spawn-Pruefung (%s) fuer Remote-Pawn %p ist GEFAULTET", what, g_remote[i].pawn); }
        --g_spawnDepth;
        g_spawnOverridePC = nullptr;
        if (forced) { uint8_t z = 0; SafeCopy(&z, pc + OFF::PC_InBoundLevelsLoaded, 1); }
        const int32_t listAfter = gs ? SafeI32(gs + OFF::GameState_SpawnedNum) : -1;
        const bool grew = gs && listAfter > listBefore;
        if (grew) InterlockedIncrement(&g_spawnListGrew);
        InterlockedIncrement(&g_spawnChecks);
        if (logged < 10 || (grew && logged < 60)) {
            ++logged;
            L("[ls] Spawn-Pruefung an %s %p fuer Remote-Pawn %p (Abstand %.0f, PC-Flag 0x1928=%d%s, Spawn-Liste %d->%d%s)",
              what, actor, g_remote[i].pawn, d, flag, forced ? " erzwungen" : "", listBefore, listAfter, grew ? ", GESPAWNT" : "");
        }
    }
}
static void __fastcall MyBuildingCheckSpawn(void* building) {
    if (!g_origBuildingSpawn) return;
    InterlockedIncrement(&g_spawnCalls);
    if (!g_buildingNative) { g_buildingNative = NativeAncestorNamed(building, L"BravoHotelBuilding"); L("[ls] Klasse BravoHotelBuilding = %p", g_buildingNative); }
    g_origBuildingSpawn(building);                       // host, as before
    if (g_spawnDepth == 0) RunSpawnForRemotePawns(g_origBuildingSpawn, building, "Gebaeude", 40000.0f);
}
static void __fastcall MyVehicleCheckSpawn(void* spawner) {
    if (!g_origVehicleSpawn) return;
    InterlockedIncrement(&g_spawnCalls);
    if (!g_vehicleNative) { g_vehicleNative = NativeAncestorNamed(spawner, L"BravoHotelVehicleSpawnActor"); L("[ls] Klasse BravoHotelVehicleSpawnActor = %p", g_vehicleNative); }
    g_origVehicleSpawn(spawner);
    if (g_spawnDepth == 0) RunSpawnForRemotePawns(g_origVehicleSpawn, spawner, "Fahrzeug-Spawner", 60000.0f);
}

// ---- loot sources the way the game finds them (v58o/v58q) --------------------
static void* g_spawnCompClass = nullptr;
static void* g_firedSet[8192];      // sources we have fired (the component has no such flag)
static int   g_firedCount = 0;
static bool AlreadyFired(void* c) {
    if (!c) return true;
    uintptr_t h = ((uintptr_t)c >> 4) * 2654435761u;
    for (int i = 0; i < 8; ++i) {
        const int slot = (int)((h + i) & 8191);
        if (!g_firedSet[slot]) {
            if (g_firedCount >= 7000) return false;
            g_firedSet[slot] = c; ++g_firedCount; return false;
        }
        if (g_firedSet[slot] == c) return true;
    }
    return false;
}
static void* SpawnComponentOf(void* actor) {
    if (!g_spawnCompClass) {
        __try { g_spawnCompClass = ((void* (__fastcall*)())(g_base + RVA::ItemSpawnBoxComp_StaticClass))(); }
        __except (EXCEPTION_EXECUTE_HANDLER) { g_spawnCompClass = nullptr; }
        if (g_spawnCompClass) { wchar_t nm[128] = L"?"; ResolveFuncName(g_spawnCompClass, nm, 128); L("[lq] Loot-Komponentenklasse = %p (%ls)", g_spawnCompClass, nm); }
        if (!g_spawnCompClass) return nullptr;
    }
    void** vt = (void**)SafePtr(actor);
    if (!vt || !InModule(vt)) return nullptr;
    void* fn = SafePtr((uint8_t*)vt + RVA::AActor_GetComponentByClass_VT);
    if (!fn || !InModule(fn)) return nullptr;
    void* comp = nullptr;
    __try { comp = ((void* (__fastcall*)(void*, void*))fn)(actor, g_spawnCompClass); }
    __except (EXCEPTION_EXECUTE_HANDLER) { comp = nullptr; }
    return comp;
}
// The outer spawn function 0x2142DA0 has six gates (world, gamemode,
// gamestate, phase >= 4, owner, owner Authority); the inner 0x2141FD0 has
// none. Gate 4 is never stepped over; the others are, by doing the outer
// function's own tail by hand (mode bytes, timestamp) and calling the inner.
static const char* const kGateName[7] = { "-", "keine World", "kein GameMode", "kein GameState", "Match-Phase < 4", "kein Owner", "Owner ist nicht Authority" };
static bool FireSpawnComponent(void* comp, void* owner) {
    if (AlreadyFired(comp)) { InterlockedIncrement(&g_lootDone); return false; }
    InterlockedIncrement(&g_lootOpen);
    if (!g_cfg.directSpawn) return false;
    int gate = 0;
    uint8_t* world = (uint8_t*)SafePtr((uint8_t*)comp + OFF::UActorComp_World);
    uint8_t* gm    = world ? (uint8_t*)SafePtr(world + OFF::UWorld_AuthGameMode) : nullptr;
    uint8_t* gs    = gm    ? (uint8_t*)SafePtr(gm + OFF::GameMode_GameState) : nullptr;
    uint8_t  phase = 0, role = 0;
    uint8_t* own   = (uint8_t*)SafePtr((uint8_t*)comp + OFF::UActorComp_Owner);
    if      (!world) gate = 1;
    else if (!gm)    gate = 2;
    else if (!gs)    gate = 3;
    else if (!SafeCopy(gs + OFF::GameState_Phase, &phase, 1) || phase < 4) gate = 4;
    else if (!own)   gate = 5;
    else if (!SafeCopy(own + OFF::AActor_Role, &role, 1) || role != OFF::ROLE_Authority) gate = 6;
    if (gate) InterlockedIncrement(&g_lootGate[gate]);
    if (gate == 4 || !MatchStarted(owner)) { InterlockedIncrement(&g_spawnLobbySkipped); return false; }
    bool ok = false;
    if (gate == 0 || !g_cfg.gateBypass) {
        __try { ((void (__fastcall*)(void*, float, uint8_t))(g_base + RVA::ItemSpawnComp_Spawn))(comp, 0.0f, 2); ok = true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    } else {
        const uint8_t zero = 0;
        SafeCopy(&zero, (uint8_t*)comp + OFF::SpawnComp_ModeA, 1);
        SafeCopy(&zero, (uint8_t*)comp + OFF::SpawnComp_ModeB, 1);
        if (world) { uint32_t now = 0; if (SafeCopy(world + OFF::UWorld_TimeSeconds, &now, 4)) SafeCopy(&now, (uint8_t*)comp + OFF::SpawnComp_Time, 4); }
        __try { ((void (__fastcall*)(void*, uint8_t))(g_base + RVA::ItemSpawnComp_SpawnInner))(comp, 2); ok = true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    }
    if (!ok) return false;
    LONG n = InterlockedIncrement(&g_lootFired);
    if (n <= 25) { wchar_t nm[128] = L"?"; ClassNameOf(owner, nm, 128);
        L("[lq] Loot-Quelle %p an %ls %p ausgeloest (Match-Phase %d, %s) -- #%ld", comp, nm, owner, phase, gate ? kGateName[gate] : "alle Tore offen", n); }
    return true;
}

// ---- wake the dormant world around the remote player (v58k) -------------------
static void* g_woken[8192];
static int   g_wokenCount = 0;
static bool AlreadyWoken(void* a) {
    if (g_wokenCount >= 7000) return true;
    size_t h = ((size_t)a >> 4) & 8191;
    for (int i = 0; i < 8; ++i) {
        void*& slot = g_woken[(h + i) & 8191];
        if (slot == a) return true;
        if (slot == nullptr) { slot = a; ++g_wokenCount; return false; }
    }
    return true;
}
static void WakeActorForClients(void* actor) {
    if (!g_drv || IsDormantAllActor(actor)) return;   // rule 3
    if (g_origFlushNetDormancy) { __try { g_origFlushNetDormancy(actor); } __except (EXCEPTION_EXECUTE_HANDLER) { return; } }
    if (g_origAddNetActor)      { __try { g_origAddNetActor(g_drv, actor); } __except (EXCEPTION_EXECUTE_HANDLER) { return; } }
    InterlockedIncrement(&g_wokenActors);
}

// The level walk, ~every 2 s from MyTickFlush (driver visible).
static void LootSweep() {
    if (!g_cfg.spawnRemote || !g_origBuildingSpawn || !g_firstPcHooked || !g_world || g_remoteCount == 0) return;
    if (!g_buildingNative && !g_vehicleNative) return;   // learned from the first hooked calls
    InterlockedIncrement(&g_sweepCalls);
    void**  levels = (void**)SafePtr((uint8_t*)g_world + OFF::UWorld_Levels);
    int32_t nLv    = SafeI32((uint8_t*)g_world + OFF::UWorld_Levels + 8);
    if (!levels || nLv <= 0 || nLv > 4096) return;
    static int reports = 0;
    int nearLoot = 0, lootSrc = 0, buildings = 0, vehicles = 0, nearRemote = 0;
    for (int li = 0; li < nLv; ++li) {
        uint8_t* lvl = (uint8_t*)SafePtr(levels + li);
        if (!lvl) continue;
        uint64_t* acts = (uint64_t*)SafePtr(lvl + OFF::ULevel_Actors);
        int32_t   nA   = SafeI32(lvl + OFF::ULevel_Actors + 8);
        if (!acts || nA <= 0 || nA > 200000) continue;
        for (int ai = 0; ai < nA; ++ai) {
            void* a = DecodedActorAt(acts + ai);
            if (!a) continue;
            // (1) dormant replicated actors within 300 m of a remote player
            if (g_cfg.wakeUp && g_drv) {
                uint8_t rep = 0, dorm = 0;
                if (SafeCopy((uint8_t*)a + OFF::AActor_bReplicates, &rep, 1) && (rep & 1) &&
                    SafeCopy((uint8_t*)a + OFF::AActor_NetDormancy, &dorm, 1) && dorm > OFF::DORM_Awake) {
                    float wl[3];
                    if (ActorLocation(a, wl) && RemoteWithin(wl, 30000.0f) >= 0) {
                        if (AlreadyWoken(a)) InterlockedIncrement(&g_wakeSkipped);
                        else {
                            WakeActorForClients(a);
                            if (g_wokenActors <= 10) { wchar_t wn[128] = L"?"; ClassNameOf(a, wn, 128);
                                L("[wk] %ls %p (Dormancy %d) im Umkreis des Remote-Spielers geweckt", wn, a, dorm); }
                        }
                    }
                }
            }
            // (2) the remote players' OWN actors (weapon, items, PlayerState):
            //     collected for the net-mode substitution and the cull repair.
            //     Never the short-lived classes -- a dead pointer here is what
            //     killed the host in v58t.
            if (g_cfg.ownMode && g_drv && g_ownedCount < 256) {
                bool already = false;
                for (int k = 0; k < g_ownedCount && !already; ++k) if (g_ownedActors[k] == a) already = true;
                uint8_t repFlag = 0;
                const bool repl = !already && SafeCopy((uint8_t*)a + OFF::AActor_bReplicates, &repFlag, 1) && (repFlag & 1);
                if (repl && OwnedByRemote(a)) {
                    wchar_t tn[128] = L"?";
                    ClassNameOf(a, tn, 128);
                    const bool transient = wcsstr(tn, L"Projectile") || wcsstr(tn, L"Bullet") || wcsstr(tn, L"Camera") || wcsstr(tn, L"Emitter");
                    if (!transient) {
                        g_ownedActors[g_ownedCount++] = a;
                        OwnedSetAdd(a);
                        LONG f = InterlockedIncrement(&g_ownFound);
                        if (f <= 30) { uint8_t od = 0; SafeCopy((uint8_t*)a + OFF::AActor_NetDormancy, &od, 1);
                            L("[ow] Eigener Actor des Remote-Spielers: %ls %p (Dormancy %d) -- #%ld", tn, a, od, f); }
                    }
                }
            }
            // (3) loot sources within 300 m of a remote player
            if (g_cfg.lootScan && nearLoot < 4000) {
                float ll[3];
                if (ActorLocation(a, ll) && RemoteWithin(ll, 30000.0f) >= 0) {
                    ++nearLoot;
                    if (void* comp = SpawnComponentOf(a)) { ++lootSrc; InterlockedIncrement(&g_lootSources); FireSpawnComponent(comp, a); }
                }
            }
            // (4) buildings / vehicle spawners without a running timer
            const bool b = IsChildOfClass(a, g_buildingNative);
            const bool v = !b && IsChildOfClass(a, g_vehicleNative);
            if (!b && !v) continue;
            if (b) ++buildings; else ++vehicles;
            float loc[3];
            if (!ActorLocation(a, loc) || RemoteWithin(loc, b ? 40000.0f : 60000.0f) < 0) continue;
            ++nearRemote;
            uint8_t* root = (uint8_t*)SafePtr((uint8_t*)a + OFF::AActor_RootComponent);
            const bool registered = root && SafePtr(root + OFF::UActorComp_World) != nullptr;
            if (!registered) continue;
            if (b) {
                uint64_t timer = 0; SafeCopy((uint8_t*)a + OFF::Building_SpawnTimer, &timer, 8);
                if (timer != 0) continue;                          // the hook path sees these 10x/s anyway
                InterlockedIncrement(&g_spawnCalls); RunSpawnForRemotePawns(g_origBuildingSpawn, a, "Gebaeude(Sweep)", 40000.0f);
            } else if (g_origVehicleSpawn) {
                InterlockedIncrement(&g_spawnCalls); RunSpawnForRemotePawns(g_origVehicleSpawn, a, "Fahrzeug-Spawner(Sweep)", 60000.0f);
            }
        }
    }
    if (g_cfg.lootScan && (reports < 15 || (reports % 5) == 0))
        L("[lq] %d Actors im Umkreis 300 m geprueft, %d Loot-Quellen | gesamt: %ld gefunden, %ld versucht, %ld ausgeloest, %ld schon erledigt | Tore: World %ld, GameMode %ld, GameState %ld, Phase %ld, Owner %ld, Authority %ld",
          nearLoot, lootSrc, g_lootSources, g_lootOpen, g_lootFired, g_lootDone,
          g_lootGate[1], g_lootGate[2], g_lootGate[3], g_lootGate[4], g_lootGate[5], g_lootGate[6]);
    if (reports < 12 || (reports % 30) == 0)
        L("[sw] Level-Walk #%ld: %d Levels, %d Gebaeude, %d Fahrzeug-Spawner, %d davon nahe am Remote-Spieler | geweckt %ld (uebersprungen %ld) | eigene Actors %d",
          g_sweepCalls, nLv, buildings, vehicles, nearRemote, g_wokenActors, g_wakeSkipped, g_ownedCount);
    ++reports;
}

// ============================================================================
//  == 15. weapons
//
//  What the host has to do for a remote player's weapon, in the order it was
//  found: know which weapon he holds (GetCurrentWeapon, 4x/s); count his
//  shots off the host's Magazine because the Blueprint decrement does not run
//  server-side here (v86); push an INCREASED magazine with the game's own
//  ClientSetMagazine RPC (v60/v159); keep the graph from distance-culling his
//  actors (v59c); set bIsServerChanged on the held weapon so the client's
//  CanReload passes (v157), and pulse it 1->0->1 while a weapon is held so
//  the permission the client's own tick clears comes back (v165/v166);
//  finish a chambering action or reload the host never ends (v164).
// ============================================================================
typedef void* (__fastcall* tGetCurrentWeapon)(void* character);
static volatile LONG g_magSpent = 0, g_scSet = 0, g_scCleared = 0, g_epDone = 0, g_bfDone = 0, g_rfDone = 0;
static int g_magPushed = 0;

// ---- current weapon per remote player (v58x/v112) ----------------------------
static void RefreshWeapons() {
    for (int i = 0; i < g_remoteCount; ++i) {
        void* pawn = g_remote[i].pawn;
        if (!pawn || !MaybeAlive(pawn)) continue;
        void* w = nullptr;
        __try { w = ((tGetCurrentWeapon)(g_base + RVA::Character_GetCurrentWeapon))(pawn); }
        __except (EXCEPTION_EXECUTE_HANDLER) { continue; }
        if (!w || !MaybeAlive(w)) continue;
        if (g_remote[i].weapon != w) {
            g_remote[i].weapon = w;
            g_remote[i].lastMag = g_remote[i].lastBp = -1;
            OwnedSetAdd(w);
            static int shown = 0;
            if (++shown <= 24) { wchar_t cn[128] = L"?"; ClassNameOf(w, cn, 128); L("[ww] Spieler %d hat jetzt %ls %p", i, cn, w); }
        }
    }
}

// ---- ClientSetMagazine (v60/v80/v82) -----------------------------------------
// The UFunction is found by name in the weapon class's function list and only
// used if its flags carry FUNC_Net | FUNC_NetClient. The getter at
// RVA::Weapon_GetClientSetMagazineFn hands back the wrong object in this
// build (v62/v80), so it is not used.
static void* g_setMagFn = nullptr;
static bool  g_setMagTried = false;
static void* FindClientSetMagazine(void* weapon) {
    if (g_setMagTried) return g_setMagFn;
    g_setMagTried = true;
    void* fn = FindFunctionNamed(weapon, L"ClientSetMagazine", nullptr, nullptr);
    if (!fn) { L("[mp] ClientSetMagazine NICHT in der Funktionsliste -- kein Push"); return nullptr; }
    const uint32_t fl = (uint32_t)SafeI32((uint8_t*)fn + OFF::UFunction_FunctionFlags);
    L("[mp] ClientSetMagazine gefunden: UFunction %p, FunctionFlags %#x -- Net %s, NetClient %s",
      fn, fl, (fl & OFF::FUNC_Net) ? "JA" : "nein", (fl & OFF::FUNC_NetClient) ? "JA" : "nein");
    if ((fl & OFF::FUNC_Net) && (fl & OFF::FUNC_NetClient)) { g_setMagFn = fn; return fn; }
    L("[mp] -> Flags passen nicht, wird NICHT benutzt");
    return nullptr;
}
static void PushMagazineToClient(void* weapon, int32_t mag) {
    if (!g_cfg.magPush || !weapon) return;
    if (mag <= 0 || mag > 500) return;             // never a garbage read, never 0 (v156)
    void* fn = FindClientSetMagazine(weapon);
    if (!fn) return;
    int cs = -1;                                   // must route Remote, or it would run on the host (v61)
    __try { cs = ((tCallspace)(g_base + RVA::GetFunctionCallspace))(weapon, fn, nullptr); }
    __except (EXCEPTION_EXECUTE_HANDLER) { cs = -1; }
    if (!(cs & OFF::CS_Remote)) {
        static int warned = 0;
        if (!warned++) L("[mp] Callspace %d fuer ClientSetMagazine -- nicht Remote, es wird NICHT gesendet", cs);
        return;
    }
    struct { int32_t NewMagazine; } params; params.NewMagazine = mag;
    const bool win = InListenWindow();
    if (win) MaskOff();
    const bool ok = CallFunctionOn(weapon, fn, &params);
    if (win) MaskOn();
    if (!ok) { L("[mp] ProcessEvent(ClientSetMagazine) hat geworfen -- Push abgeschaltet"); g_cfg.magPush = false; return; }
    if (++g_magPushed <= 60) L("[mp] ClientSetMagazine(%d) an den besitzenden Client gesendet (Waffe %p, Callspace %d) -- #%d", mag, weapon, cs, g_magPushed);
}

// ---- magazine watch + cull repair, 4x/s (v59c/v112/v159) ----------------------
static void WeaponTick() {
    if (!g_drv || g_remoteCount == 0) return;
    RefreshWeapons();
    for (int ri = 0; ri < g_remoteCount; ++ri)             // v173: the game may reset the mesh option (aircraft, respawn)
        if (g_remote[ri].pawn && MaybeAlive(g_remote[ri].pawn)) FixAnimTick(g_remote[ri].pawn, "4x/s-Kontrolle");
    for (int ri = 0; ri < g_remoteCount; ++ri) {
        void* rw = g_remote[ri].weapon;
        if (!rw || !MaybeAlive(rw)) continue;
        int32_t mag = SafeI32((uint8_t*)rw + OFF::Weapon_Magazine);
        int32_t bp  = SafeI32((uint8_t*)rw + OFF::Weapon_BackPackAmmo);
        if (mag == g_remote[ri].lastMag && bp == g_remote[ri].lastBp) continue;
        static int mags = 0;
        if (++mags <= 300) L("[mag] t=%lu ms  Spieler %d, Waffe %p: Magazin %d  Rucksack %d  Kapazitaet %d",
                             (unsigned long)GetTickCount(), ri, rw, mag, bp, SafeI32((uint8_t*)rw + OFF::Weapon_MagazineCapacity));
        // v159: push only INCREASES (a reload the host executed); a decrease is
        // the client's own shot, already applied locally.
        if (mag > g_remote[ri].lastMag && g_remote[ri].lastMag >= 0) PushMagazineToClient(rw, mag);
        g_remote[ri].lastMag = mag; g_remote[ri].lastBp = bp;
    }
    static int relTick = 0;
    if (++relTick % 8 != 0) return;                 // ~every two seconds
    for (int i = 0; i < g_remoteCount; ++i) {
        if (g_remote[i].weapon) OwnedCullToZero(g_remote[i].weapon, L"Waffe des Remote-Spielers");
        OwnedCullToZero(g_remote[i].pawn, L"Remote-Pawn");
        if (void* ps = SafePtr((uint8_t*)g_remote[i].controller + OFF::AController_PlayerState)) OwnedCullToZero(ps, L"PlayerState des Remote-Spielers");
    }
    for (int k = 0; k < g_ownedCount; ++k) if (g_ownedActors[k]) OwnedCullToZero(g_ownedActors[k], L"Eigener Actor des Remote-Spielers");
}

// ---- bIsServerChanged (v157) and the reload permission pulse (v165/v166) -----
// Weapon+0x94C: bit0 bIsEquipped, bit1 bIsServerChanged (both Net+RepNotify).
// On a client CanReload demands weapon+0x21E0 != 0, which only
// OnRep_IsEquipped and OnRep_IsServerChanged set and the weapon's own tick
// clears again. A RepNotify only runs on a CHANGE, so the bit is pulsed.
static void* g_scLast[24];
static int   g_epPhase[24];
static unsigned long long g_epNext[24];
static void* g_epLast[24];

static void ServerChangedTick() {
    if (!g_cfg.servChanged) return;
    for (int i = 0; i < g_remoteCount && i < 24; ++i) {
        void* w = g_remote[i].weapon;
        void* prev = g_scLast[i];
        if (prev && prev != w && MaybeAlive(prev)) {           // put away: clear bit1 so the next equip is a fresh 0->1
            uint8_t f = 0;
            if (SafeCopy((uint8_t*)prev + OFF::Weapon_EquipFlags, &f, 1) && (f & 2)) {
                f &= ~2; SafeWriteU8((uint8_t*)prev + OFF::Weapon_EquipFlags, f);
                LONG n = InterlockedIncrement(&g_scCleared);
                if (n <= 40) L("[sc] bIsServerChanged geloescht auf abgelegter Waffe %p (Spieler %d) -- #%ld", prev, i, n);
            }
        }
        g_scLast[i] = w;
        if (!w || !MaybeAlive(w)) continue;
        if (g_epPhase[i] == 1) continue;                       // a pulse has the bit down on purpose
        uint8_t f = 0;
        if (!SafeCopy((uint8_t*)w + OFF::Weapon_EquipFlags, &f, 1)) continue;
        if ((f & 1) && !(f & 2)) {                             // equipped on the host, not yet confirmed
            f |= 2;
            if (SafeWriteU8((uint8_t*)w + OFF::Weapon_EquipFlags, f)) {
                LONG n = InterlockedIncrement(&g_scSet);
                if (n <= 40) { wchar_t wn[128] = L"?"; ClassNameOf(w, wn, 128);
                    L("[sc] bIsServerChanged GESETZT auf %ls %p (Spieler %d) -> Client: OnRep_IsServerChanged, Nachladen frei -- #%ld", wn, w, i, n); }
            }
        }
    }
}
static void EmptyPulseTick() {
    if (!g_cfg.emptyPulse) return;
    const unsigned long long now = GetTickCount64();
    for (int i = 0; i < g_remoteCount && i < 24; ++i) {
        void* w = g_remote[i].weapon;
        if (g_epLast[i] != w) { g_epLast[i] = w; g_epPhase[i] = 0; g_epNext[i] = 0; }
        if (!w || !MaybeAlive(w)) continue;
        uint8_t f = 0;
        if (!SafeCopy((uint8_t*)w + OFF::Weapon_EquipFlags, &f, 1)) continue;
        if (!(f & 1)) { g_epPhase[i] = 0; continue; }         // not equipped on the host
        if (g_epPhase[i] == 1) {                               // second half: the 0 -> 1 the RepNotify needs
            if (SafeWriteU8((uint8_t*)w + OFF::Weapon_EquipFlags, (uint8_t)(f | 2))) {
                g_epPhase[i] = 0;
                g_epNext[i]  = now + (unsigned long long)g_cfg.emptyPulseMs;
                LONG n = InterlockedIncrement(&g_epDone);
                if (n <= 60) L("[ep] bIsServerChanged 0 -> 1 gepulst (Waffe %p, Spieler %d, Magazin %d) -- #%ld", w, i, SafeI32((uint8_t*)w + OFF::Weapon_Magazine), n);
            }
            continue;
        }
        const int32_t cap = SafeI32((uint8_t*)w + OFF::Weapon_MagazineCapacity);
        const int32_t mag = SafeI32((uint8_t*)w + OFF::Weapon_Magazine);
        if (cap <= 0 || cap > 500) continue;                   // grenade / melee / sensor
        if (g_cfg.pulseOnlyEmpty && mag > 0) { g_epNext[i] = 0; continue; }
        if (g_epNext[i] && now < g_epNext[i]) continue;
        if (f & 2) { if (SafeWriteU8((uint8_t*)w + OFF::Weapon_EquipFlags, (uint8_t)(f & ~2))) g_epPhase[i] = 1; }
        else       g_epPhase[i] = 1;                           // already down, just set it next tick
    }
}
// v164: the host never runs the bolt timer for a remote player's weapon, so
// bPendingBoltAction (Net + RepNotify) would stay up and the client would sit
// in EWS_BoltAction with CanReload false. Clear it ONCE after boltHoldMs;
// bPendingReload the same with a long timeout, as a safety net.
static unsigned long long g_bfSince[24], g_rfSince[24];
static void* g_bfLast[24];
static void BoltFinishTick() {
    if (!g_cfg.boltFinish && !g_cfg.reloadFinish) return;
    const unsigned long long now = GetTickCount64();
    for (int i = 0; i < g_remoteCount && i < 24; ++i) {
        void* w = g_remote[i].weapon;
        if (g_bfLast[i] != w) { g_bfSince[i] = 0; g_rfSince[i] = 0; g_bfLast[i] = w; }
        if (!w || !MaybeAlive(w)) continue;
        if (g_cfg.boltFinish) {
            uint8_t b = 0;
            if (SafeCopy((uint8_t*)w + OFF::Weapon_PendingBolt, &b, 1)) {
                if (b & 1) {
                    if (g_bfSince[i] == 0) g_bfSince[i] = now;
                    else if (now - g_bfSince[i] >= (unsigned long long)g_cfg.boltHoldMs) {
                        if (SafeWriteU8((uint8_t*)w + OFF::Weapon_PendingBolt, (uint8_t)(b & ~1))) {
                            g_bfSince[i] = 0;
                            LONG n = InterlockedIncrement(&g_bfDone);
                            if (n <= 60) L("[bf] Repetieren beendet: bPendingBoltAction nach %d ms geloescht (Waffe %p, Spieler %d, Magazin %d) -- #%ld",
                                           g_cfg.boltHoldMs, w, i, SafeI32((uint8_t*)w + OFF::Weapon_Magazine), n);
                        }
                    }
                } else g_bfSince[i] = 0;
            }
        }
        if (g_cfg.reloadFinish) {
            uint8_t r = 0;
            if (SafeCopy((uint8_t*)w + OFF::Weapon_PendingReload, &r, 1)) {
                if (r & 1) {
                    if (g_rfSince[i] == 0) g_rfSince[i] = now;
                    else if (now - g_rfSince[i] >= 6000ULL) {
                        if (SafeWriteU8((uint8_t*)w + OFF::Weapon_PendingReload, (uint8_t)(r & ~1))) {
                            g_rfSince[i] = 0;
                            LONG n = InterlockedIncrement(&g_rfDone);
                            if (n <= 40) L("[bf] bPendingReload hing seit 6000 ms und wurde geloescht (Waffe %p, Spieler %d) -- #%ld", w, i, n);
                        }
                    }
                } else g_rfSince[i] = 0;
            }
        }
    }
}

// ---- AActor::ProcessEvent (v83/v86/v87/v112/v148) ----------------------------
// Two duties and a little logging:
//  * ServerFireProjectile on a remote player's weapon or pawn = one bullet:
//    the host's Magazine goes down by one (the game's Blueprint decrement does
//    not run server-side here). Locked to whichever object kind emits it
//    first so a shot is never counted twice.
//  * ShowMatchEndFinalResult (client RPC) -> gold settlement reserve path.
//  * [pe]: whitelisted RPC names on remote weapon/pawn, [rr]: reload events
//    with the host's magazine numbers. Off with -nopelog.
// Names are resolved once per UFunction and cached; after that the hot path is
// one hash probe.
typedef void (__fastcall* tPE)(void* obj, void* fn, void* params);
static tPE   g_origProcessEvent = nullptr;
static int   g_fireSrcKind = 0;          // 1 = weapon emits ServerFireProjectile, 2 = pawn
static volatile LONG g_peLogged = 0, g_rrLogged = 0;
static void GoldOnMatchEndResult(void* obj, void* params);   // section 16

enum : uint8_t { FV_None = 0, FV_Fire = 1, FV_Reload = 2, FV_Keep = 3, FV_MatchEnd = 4, FV_MoveIn = 5, FV_MoveOut = 6, FV_Perk = 7, FV_KickDoor = 8, FV_MoveAdjust = 9 };
static FnVerdict g_fnv[2048];
// v171: movement round-trip counters. Every ServerMove* the client sends should
// be answered by ClientAckGoodMove / ClientAdjustPosition*; the ratio is
// printed in the [ls] line (issue #3, "Hit limit of 96 saved moves").
static volatile LONG g_moveIn = 0, g_moveOut = 0, g_moveAdjust = 0;   // v173: corrections counted apart from good-move acks
static uint8_t ClassifyWeaponFn(const wchar_t* nm) {
    if (wcsncmp(nm, L"ServerMove", 10) == 0) return FV_MoveIn;
    if (wcsncmp(nm, L"ClientAddPerkLevel", 18) == 0 || wcsncmp(nm, L"ClientPerkSpinEvent", 19) == 0
     || wcsncmp(nm, L"ClientPhasePerkLevelUpReady", 27) == 0 || wcsncmp(nm, L"ClientAddPerkUIEvent", 20) == 0) return FV_Perk;
    if (wcscmp(nm, L"ServerStartKickDoor") == 0) return FV_KickDoor;
    if (wcsncmp(nm, L"ClientAckGoodMove", 17) == 0) return FV_MoveOut;
    if (wcsncmp(nm, L"ClientAdjust", 12) == 0 || wcsncmp(nm, L"ClientVeryShortAdjust", 21) == 0) return FV_MoveAdjust;
    if (wcscmp(nm, L"ServerFireProjectile") == 0) return FV_Fire;
    if (wcsstr(nm, L"ShowMatchEndFinalResult")) return FV_MatchEnd;
    if (wcsstr(nm, L"Reload")) return FV_Reload;
    if (wcsstr(nm, L"Fire") || wcsstr(nm, L"Ammo") || wcsstr(nm, L"Magazine") || wcsstr(nm, L"Weapon") || wcsstr(nm, L"Equip")
     || wcsstr(nm, L"PickUp") || wcsstr(nm, L"Combine") || wcsstr(nm, L"Bullet") || wcsstr(nm, L"Shoot")
     || wcsstr(nm, L"Door") || wcsstr(nm, L"Window") || wcsstr(nm, L"Glass") || wcsstr(nm, L"Break") || wcsstr(nm, L"Kick")
     || wcsstr(nm, L"Interact") || wcsstr(nm, L"Vault") || wcsstr(nm, L"Sound") || wcsstr(nm, L"Open") || wcsstr(nm, L"Destruct")
     || wcsstr(nm, L"Bolt") || wcsstr(nm, L"Chamber") || wcsstr(nm, L"Stop")) return FV_Keep;
    return FV_None;
}
static void SpendOneRound(void* weapon) {
    if (!g_cfg.magTrack || !weapon) return;
    int32_t mag = SafeI32((uint8_t*)weapon + OFF::Weapon_Magazine);
    if (mag <= 0 || mag > 500) return;
    const int32_t next = mag - 1;
    if (!SafeCopy(&next, (uint8_t*)weapon + OFF::Weapon_Magazine, 4)) return;
    LONG n = InterlockedIncrement(&g_magSpent);
    if (n <= 30) L("[ms] Schuss gezaehlt: Magazin %d -> %d (Waffe %p) -- #%ld", mag, next, weapon, n);
}
// v171 -rpcnames: every distinct UFunction seen on a remote player's objects is
// logged once, and every distinct function anywhere whose name is in the
// FV_Keep set (Window/Glass/Break/Destruct/...) is logged once with the class
// of the object it ran on. Pure diagnostics, pointer set, no per-call name work.
static void* g_rnSeen[4096];
static volatile LONG g_rnLogged = 0;
static bool RnSeenFirst(void* fn) {
    uintptr_t h = ((uintptr_t)fn >> 4) * 2654435761u;
    for (int i = 0; i < 16; ++i) {
        const int slot = (int)((h + i) & 4095);
        if (g_rnSeen[slot] == fn) return false;
        if (!g_rnSeen[slot]) { g_rnSeen[slot] = fn; return true; }
    }
    return false;
}
static void RnLog(void* obj, void* fn, const char* who) {
    if (g_rnLogged >= 800 || !RnSeenFirst(fn)) return;
    InterlockedIncrement(&g_rnLogged);
    wchar_t nm[128] = L"?", cn[128] = L"?";
    ResolveFuncName(fn, nm, 128); ClassNameOf(obj, cn, 128);
    L("[rn] t=%lu ms  %s %ls %p -> %ls", (unsigned long)GetTickCount(), who, cn, obj, nm);
}
// v172 -doorpush: doors, destructibles and broken-window actors are level
// actors the client already has; the host changes their state (kicked open,
// shattered) but the change was observed not to reach the client. Whenever one
// of them runs a function on the host near a remote player, or a remote
// player kicks a door, the actor gets the weapon treatment: graph cull 0,
// FlushNetDormancy, ForceNetUpdate. Class verdict cached per UClass*.
static FnVerdict g_clsv[1024];
static uint8_t ClassifyActorClass(const wchar_t* nm) {
    if (wcsstr(nm, L"Door")) return CV_Door;
    if (wcsstr(nm, L"Destructible") || wcsstr(nm, L"BrokenWindow") || wcsstr(nm, L"Window") || wcsstr(nm, L"Glass")) return CV_Destructible;
    if (wcsstr(nm, L"Pickup")) return CV_Pickup;
    return CV_None;
}
static uint8_t ClassVerdictOf(void* obj) {
    void* cls = SafePtr((uint8_t*)obj + OFF::UObject_Class);
    return cls ? FnVerdictLookup(g_clsv, cls, ClassifyActorClass) : (uint8_t)CV_None;
}
static void*  g_pushSeen[512];      // actor -> last push tick (open addressing, pointer + tick)
static LONG   g_pushTick[512];
static volatile LONG g_doorPushes = 0;
static LONG   g_doorSweepUntil = 0;  // TickFlush count until which the door sweep around the kicking pawn runs
static void*  g_doorSweepPawn = nullptr;
static void DoorPush(void* actor, const char* why) {
    if (!actor || !InListenWindow()) return;
    const LONG now = g_tfCount;
    uintptr_t h = ((uintptr_t)actor >> 4) * 2654435761u;
    int slot = -1;
    for (int i = 0; i < 8; ++i) {
        const int k = (int)((h + i) & 511);
        if (g_pushSeen[k] == actor) { if (now - g_pushTick[k] < 15) return; slot = k; break; }   // at most 2x/s per actor
        if (!g_pushSeen[k]) { slot = k; break; }
    }
    if (slot < 0) slot = (int)(h & 511);
    g_pushSeen[slot] = actor; g_pushTick[slot] = now;
    uint8_t rep = 0, dorm = 0;
    SafeCopy((uint8_t*)actor + OFF::AActor_bReplicates, &rep, 1);
    SafeCopy((uint8_t*)actor + OFF::AActor_NetDormancy, &dorm, 1);
    OwnedCullToZero(actor, L"Tuer/Fenster");
    if (!IsDormantAllActor(actor)) MyFlushNetDormancy(actor);
    const bool forced = ForceNetUpdateOn(actor);
    LONG n = InterlockedIncrement(&g_doorPushes);
    if (n <= 60) { wchar_t cn[128] = L"?"; ClassNameOf(actor, cn, 128);
        uint8_t doorState = 0xFF; float hp = -1.f;                       // ABravoHotelDoor: DoorState +0x490 (rep), CurrentHP +0x398 (rep) -- SDK
        if (ClassVerdictOf(actor) == CV_Door) { SafeCopy((uint8_t*)actor + 0x490, &doorState, 1); SafeCopy((uint8_t*)actor + 0x398, &hp, 4); }
        L("[dp] %ls %p (%s): bReplicates=%d NetDormancy=%d DoorState=%d HP=%.0f Graph-Info %p -> Cull 0, Flush, ForceNetUpdate %s -- #%ld",
          cn, actor, why, rep & 1, dorm, doorState, hp, GraphInfoFor(actor), forced ? "OK" : "FEHLT", n); }
}
// Door sweep after a kick: every door actor within 10 m of the kicking pawn, for ~3 s.
static void DoorSweepTick() {
    if (!g_cfg.doorPush || !g_world || g_tfCount > g_doorSweepUntil || !g_doorSweepPawn) return;
    float pl[3];
    { bool ok = false; for (int i = 0; i < g_remoteCount; ++i) if (g_remote[i].pawn == g_doorSweepPawn && RemotePawnLocation(g_remote[i], pl)) { ok = true; break; }
      if (!ok) { g_doorSweepPawn = nullptr; return; } }
    void**  levels = (void**)SafePtr((uint8_t*)g_world + OFF::UWorld_Levels);
    int32_t nLv    = SafeI32((uint8_t*)g_world + OFF::UWorld_Levels + 8);
    if (!levels || nLv <= 0 || nLv > 4096) return;
    for (int li = 0; li < nLv; ++li) {
        uint8_t* lvl = (uint8_t*)SafePtr(levels + li);
        if (!lvl) continue;
        uint64_t* acts = (uint64_t*)SafePtr(lvl + OFF::ULevel_Actors);
        int32_t   nA   = SafeI32(lvl + OFF::ULevel_Actors + 8);
        if (!acts || nA <= 0 || nA > 200000) continue;
        for (int ai = 0; ai < nA; ++ai) {
            void* a = DecodedActorAt(acts + ai);
            if (!a) continue;
            uint8_t rep = 0;
            if (!SafeCopy((uint8_t*)a + OFF::AActor_bReplicates, &rep, 1) || !(rep & 1)) continue;
            const uint8_t cv = ClassVerdictOf(a);
            if (cv != CV_Door && cv != CV_Destructible) continue;
            float al[3];
            if (!ActorLocation(a, al) || Dist3(al, pl) > 1000.0f) continue;
            DoorPush(a, "Tuer-Sweep nach Tritt");
        }
    }
}
static volatile LONG g_perkPushes = 0;
static void __fastcall MyProcessEvent(void* obj, void* fn, void* params) {
    if (obj && fn && g_remoteCount > 0) {
        int  shooter  = RemoteOfWeapon(obj);
        bool isWeapon = shooter >= 0;
        bool isPawn   = false;
        if (!isWeapon) for (int i = 0; i < g_remoteCount && !isPawn; ++i) if (obj == g_remote[i].pawn) { isPawn = true; shooter = i; }
        bool isCtrl = false;
        if (!isWeapon && !isPawn && (g_cfg.goldGain || g_cfg.rpcNames))
            for (int i = 0; i < g_remoteCount && !isCtrl; ++i) {
                void* c = g_remote[i].controller;
                if (obj == c || (c && obj == SafePtr((uint8_t*)c + OFF::AController_PlayerState))) isCtrl = true;
            }
        if (isWeapon || isPawn || isCtrl) {
            const uint8_t v = FnVerdictLookup(g_fnv, fn, ClassifyWeaponFn);
            if (g_cfg.rpcNames) RnLog(obj, fn, isWeapon ? "Waffe" : isPawn ? "Pawn" : "Controller/PlayerState");
            if (v == FV_MoveIn)       InterlockedIncrement(&g_moveIn);
            else if (v == FV_MoveOut) InterlockedIncrement(&g_moveOut);
            else if (v == FV_MoveAdjust) InterlockedIncrement(&g_moveAdjust);
            else if (v == FV_Perk) {
                // v172: let the RPC go out first, then force a property delta for
                // everything the perk/level HUD reads (pawn, controller, PlayerState).
                if (g_origProcessEvent) g_origProcessEvent(obj, fn, params);
                if (g_cfg.perkPush && shooter >= 0) {
                    void* c  = g_remote[shooter].controller;
                    void* ps = c ? SafePtr((uint8_t*)c + OFF::AController_PlayerState) : nullptr;
                    int n = 0;
                    if (ForceNetUpdateOn(g_remote[shooter].pawn)) ++n;
                    if (ForceNetUpdateOn(c)) ++n;
                    if (ForceNetUpdateOn(ps)) ++n;
                    LONG k = InterlockedIncrement(&g_perkPushes);
                    if (k <= 20) { wchar_t nm[128] = L"?"; ResolveFuncName(fn, nm, 128);
                        L("[pk] t=%lu ms  %ls fuer Spieler %d -> ForceNetUpdate auf %d Actors (Pawn, Controller, PlayerState) -- #%ld", (unsigned long)GetTickCount(), nm, shooter, n, k); }
                } else if (g_cfg.perkPush) {
                    // controller-side event: find the player by controller
                    for (int i = 0; i < g_remoteCount; ++i) if (obj == g_remote[i].controller) {
                        ForceNetUpdateOn(g_remote[i].pawn); ForceNetUpdateOn(obj);
                        ForceNetUpdateOn(SafePtr((uint8_t*)obj + OFF::AController_PlayerState)); break; }
                }
                return;
            }
            else if (v == FV_KickDoor && g_cfg.doorPush && isPawn) {
                g_doorSweepPawn  = obj;
                g_doorSweepUntil = g_tfCount + 90;   // ~3 s
                L("[dp] ServerStartKickDoor von Pawn %p -> Tueren im Umkreis von 10 m werden 3 s lang nachgeschoben", obj);
            }
            else if (v == FV_MatchEnd && g_cfg.goldGain) {
                __try { GoldOnMatchEndResult(obj, params); } __except (EXCEPTION_EXECUTE_HANDLER) { L("[gg] Ausnahme im Ergebnis-Decoder"); }
            } else if (v == FV_Fire && (isWeapon || isPawn)) {
                const int kind = isWeapon ? 1 : 2;
                if (g_fireSrcKind == 0) { g_fireSrcKind = kind; L("[ms] Schuss-RPC kommt vom %s", kind == 1 ? "Waffen-Objekt" : "Pawn-Objekt"); }
                if (kind == g_fireSrcKind) SpendOneRound(g_remote[shooter].weapon);
            } else if (v == FV_Reload && g_cfg.peLog && g_rrLogged < 300) {
                InterlockedIncrement(&g_rrLogged);
                void* wpn = isWeapon ? obj : (shooter >= 0 ? g_remote[shooter].weapon : nullptr);
                int32_t mag = -1, bp = -1, cap = -1;
                if (wpn) { mag = SafeI32((uint8_t*)wpn + OFF::Weapon_Magazine); bp = SafeI32((uint8_t*)wpn + OFF::Weapon_BackPackAmmo); cap = SafeI32((uint8_t*)wpn + OFF::Weapon_MagazineCapacity); }
                wchar_t nm[128] = L"?"; ResolveFuncName(fn, nm, 128);
                L("[rr] t=%lu ms  %ls  Waffe %p | Magazin %d / Kapazitaet %d, Rucksack %d%s", (unsigned long)GetTickCount(), nm, wpn, mag, cap, bp,
                  (mag >= 0 && cap > 0 && mag >= cap) ? "   <<< der Host haelt die Waffe fuer VOLL >>>" : "");
            } else if (v == FV_Keep && g_cfg.peLog && (isWeapon || isPawn) && g_peLogged < 400) {
                InterlockedIncrement(&g_peLogged);
                wchar_t nm[128] = L"?"; ResolveFuncName(fn, nm, 128);
                L("[pe] t=%lu ms  %p%s -> %ls", (unsigned long)GetTickCount(), obj, isWeapon ? " (Waffe)" : " (Pawn)", nm);
            }
        } else {
            // any other object
            if (g_cfg.rpcNames && FnVerdictLookup(g_fnv, fn, ClassifyWeaponFn) == FV_Keep) RnLog(obj, fn, "Actor");
            if (g_cfg.doorPush) {
                const uint8_t cv = ClassVerdictOf(obj);
                if (cv == CV_Door || cv == CV_Destructible) {
                    float al[3];
                    if (ActorLocation(obj, al) && RemoteWithin(al, 1500.0f) >= 0) {
                        if (g_origProcessEvent) g_origProcessEvent(obj, fn, params);
                        DoorPush(obj, cv == CV_Door ? "Tuer-Funktion nahe Remote-Spieler" : "Fenster/Destructible-Funktion nahe Remote-Spieler");
                        return;
                    }
                }
            }
        }
    }
    if (g_origProcessEvent) g_origProcessEvent(obj, fn, params);
}

// ============================================================================
//  == 16. gold and loadout (v143..v149)
//
//  A network join carries no loadout. -loadout injects the join options the
//  backend wrote to loadouts.txt ("<UID>\t<pc_info json>?AccountGold=G?...")
//  in front of the connection's options before InitNewPlayer reads them.
//  Gold: start value after InitNewPlayer, purchases via OnPayResult, the
//  round's ledger rows off the PlayerState at Logout; the result is ONE line
//  "[gg] #n t=.. CommitRequest[CurrencyGain] (...)" that tools/sp-listen-alive.js
//  books in the backend. A loss is logged, never booked.
// ============================================================================
typedef void* (__fastcall* tInitNewPlayer)(void*, void*, void*, void*, void*, void*);
typedef void  (__fastcall* tSetGold)(void*, int, void*, void*, void*, void*);
typedef void* (__fastcall* tPayResult)(void*, uint8_t, int, int, void*, void*);
typedef void* (__fastcall* tLogout)(void*, void*, void*, void*, void*, void*);
static tInitNewPlayer g_origInitNewPlayer = nullptr;
static tSetGold       g_origSetGold   = nullptr;
static tPayResult     g_origPayResult = nullptr;
static tLogout        g_origLogout    = nullptr;
static wchar_t        g_loadoutPath[MAX_PATH] = { 0 };

struct GoldTrack { void* pc; void* ps; char uid[48]; int start; int last; int paid; int changes; bool active; int coins; int bonus; bool resultSeen; };
static GoldTrack g_gold[64];
static int       g_ggSeq = 0;

static bool ExtractUidFromOptions(const wchar_t* opts, int num, char* out, int outsz) {
    if (!opts || num <= 4 || outsz < 2) return false;
    for (int i = 0; i + 4 <= num; ++i) {
        wchar_t a = opts[i], b = opts[i + 1], c = opts[i + 2], d = opts[i + 3];
        if ((a == 'U' || a == 'u') && (b == 'I' || b == 'i') && (c == 'D' || c == 'd') && d == '=') {
            int j = i + 4, k = 0;
            while (j < num && opts[j] && opts[j] != '?' && k < outsz - 1) {
                wchar_t ch = opts[j++];
                if (ch < 32 || ch > 126) return false;
                out[k++] = (char)ch;
            }
            out[k] = 0;
            return k > 0;
        }
    }
    return false;
}
static bool LookupLoadout(const char* uid, char* out, int outsz) {
    if (!g_loadoutPath[0] || !uid || !uid[0]) return false;
    FILE* f = _wfsopen(g_loadoutPath, L"r", _SH_DENYNO);
    if (!f) return false;
    static char line[9000];
    bool found = false;
    const size_t ulen = strlen(uid);
    while (fgets(line, (int)sizeof(line), f)) {
        char* tab = strchr(line, '\t');
        if (!tab) continue;
        *tab = 0;
        if (strncmp(line, uid, ulen) == 0 && line[ulen] == 0) {
            char* val = tab + 1;
            size_t vl = strlen(val);
            while (vl && (val[vl - 1] == '\n' || val[vl - 1] == '\r')) val[--vl] = 0;
            if (vl > 0 && (int)vl < outsz) { memcpy(out, val, vl + 1); found = true; }
            break;
        }
    }
    fclose(f);
    return found;
}
// Rewrite the options FString to "?" + opts + original, with the game's own
// allocator. The leading '?' is what the option extractor (0x424ED40) needs.
static bool InjectOptions(void* obj, const char* asciiOpts) {
    if (!obj || !asciiOpts) return false;
    auto* pData = (wchar_t**)((uint8_t*)obj + OFF::Options_Data);
    auto* pNum  = (int32_t*)((uint8_t*)obj + OFF::Options_Num);
    auto* pMax  = (int32_t*)((uint8_t*)obj + OFF::Options_Max);
    wchar_t* origData; int origNum;
    if (!SafeCopy(pData, &origData, 8) || !SafeCopy(pNum, &origNum, 4)) return false;
    if (!origData || origNum < 1 || origNum > 8192) return false;
    static wchar_t orig[8200];
    if (!SafeCopy(origData, orig, (size_t)origNum * 2)) return false;
    orig[origNum - 1] = 0;
    const int prefixLen = (int)strlen(asciiOpts);
    if (prefixLen < 1 || prefixLen > 8000) return false;
    const bool needSep = (orig[0] != L'?');
    const int head = 1 + prefixLen + (needSep ? 1 : 0);
    const int newNum = head + origNum;
    wchar_t* nd = nullptr;
    __try { nd = (wchar_t*)((void* (__fastcall*)(void*, size_t, uint32_t))(g_base + RVA::FMemory_Realloc))(origData, (size_t)newNum * 2, 0); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    if (!nd) return false;
    memmove(nd + head, nd, (size_t)origNum * 2);
    int k = 0;
    nd[k++] = L'?';
    for (int i = 0; i < prefixLen; ++i) nd[k++] = (wchar_t)(uint8_t)asciiOpts[i];
    if (needSep) nd[k++] = L'?';
    nd[newNum - 1] = 0;
    *pData = nd; *pNum = newNum; *pMax = newNum;
    return true;
}

static int GoldRead(void* ps) {
    void* w = ps ? SafePtr((uint8_t*)ps + OFF::PS_Wallet) : nullptr;
    return w ? SafeI32((uint8_t*)w + OFF::Wallet_Gold) : -1;
}
static GoldTrack* GoldByPc(void* pc) { for (auto& g : g_gold) if (g.active && g.pc == pc) return &g; return nullptr; }
static GoldTrack* GoldByPs(void* ps) { for (auto& g : g_gold) if (g.active && g.ps == ps) return &g; return nullptr; }
static void GoldRegister(void* pc, const char* uid) {
    if (!g_cfg.goldGain || !pc || !uid || !uid[0]) return;
    void* ps = SafePtr((uint8_t*)pc + OFF::AController_PlayerState);
    int gold = GoldRead(ps);
    if (!ps || gold < 0) { L("[gg] UID %hs: kein PlayerState/Gold lesbar (ps=%p) -- nicht verfolgt", uid, ps); return; }
    if (GoldTrack* old = GoldByPc(pc)) { L("[gg] UID %hs: PC %p erneut initialisiert (Start %d, jetzt %d) -- Verfolgung neu", uid, pc, old->start, gold); old->active = false; }
    GoldTrack* slot = nullptr;
    for (auto& g : g_gold) if (!g.active) { slot = &g; break; }
    if (!slot) { L("[gg] UID %hs: keine freie Spur (64 aktiv?)", uid); return; }
    memset(slot, 0, sizeof(*slot));
    slot->pc = pc; slot->ps = ps; slot->start = gold; slot->last = gold; slot->active = true;
    strncpy_s(slot->uid, uid, _TRUNCATE);
    L("[gg] UID %hs: Start %d Gold (PC %p, PS %p)", uid, gold, pc, ps);
}
static void __fastcall MySetGold(void* ps, int value, void* a3, void* a4, void* a5, void* a6) {
    if (GoldTrack* g = GoldByPs(ps)) {
        if (g->changes < 8) L("[gg] UID %hs: Gold %d -> %d", g->uid, g->last, value);
        else if (g->changes == 8) L("[gg] UID %hs: weitere Aenderungen still", g->uid);
        g->changes++;
        g->last = value;
    }
    if (g_origSetGold) g_origSetGold(ps, value, a3, a4, a5, a6);
}
static void* __fastcall MyPayResult(void* pc, uint8_t ok, int amount, int action, void* a5, void* a6) {
    if (ok && amount > 0)
        if (GoldTrack* g = GoldByPc(pc)) { g->paid += amount; L("[gg] UID %hs: bezahlt %d (Aktion %d), gesamt %d", g->uid, amount, action, g->paid); }
    return g_origPayResult ? g_origPayResult(pc, ok, amount, action, a5, a6) : nullptr;
}
static const char* const kLedgerNames[17] = { "None","RankPoint","KillPoint","DMGPoint","SurvivalPoint","SupplyBoxOpen","DropCoin",
    "AcquireCoin","DropRecipe","AcquireRecipe","ChangeDeck","Resuscitation","RequestResuscitation","RandomGold","RandomRankGold","SelectDeckMode","ChangeDeckList" };
// The round's ledger rows on the PlayerState (v149): coins = DropCoin+AcquireCoin, bonus = RandomGold+RandomRankGold.
static int GoldReadLedger(void* ps, int* coins, int* bonus, char* desc, int descCap) {
    desc[0] = 0; *coins = 0; *bonus = 0;
    void* led = ps ? SafePtr((uint8_t*)ps + OFF::PS_Ledger) : nullptr;
    if (!led) { sprintf_s(desc, (size_t)descCap, "kein Ledger-Objekt an PS+0x%zX", (size_t)OFF::PS_Ledger); return 0; }
    void* rows = SafePtr((uint8_t*)led + OFF::Ledger_Rows);
    int num = SafeI32((uint8_t*)led + OFF::Ledger_Rows + 8);
    int max = SafeI32((uint8_t*)led + OFF::Ledger_Rows + 12);
    if (num <= 0 || num > 64 || max < num) { sprintf_s(desc, (size_t)descCap, "Ledger %p leer/unplausibel (Num %d, Max %d)", led, num, max); return 0; }
    struct Row { int32_t value; uint8_t type; uint8_t pad[3]; } r[64];
    if (!rows || !SafeCopy(rows, r, (size_t)num * sizeof(Row))) { sprintf_s(desc, (size_t)descCap, "Ledger-Zeilen %p nicht lesbar", rows); return 0; }
    int len = 0;
    for (int i = 0; i < num; ++i) {
        const char* nm = r[i].type < 17 ? kLedgerNames[r[i].type] : "?";
        if (len < descCap - 40) len += sprintf_s(desc + len, (size_t)(descCap - len), "%s%s(%d)=%d", i ? " " : "", nm, r[i].type, r[i].value);
        if (r[i].type == 6 || r[i].type == 7)   *coins += r[i].value;
        if (r[i].type == 13 || r[i].type == 14) *bonus += r[i].value;
    }
    return num;
}
static void GoldSettle(void* pc) {
    GoldTrack* g = GoldByPc(pc);
    if (!g) return;
    int end = GoldRead(g->ps);
    if (end < 0) end = g->last;
    { int lc = 0, lb = 0; char ld[512];
      int rows = GoldReadLedger(g->ps, &lc, &lb, ld, sizeof(ld));
      if (rows > 0) { g->coins = lc; g->bonus = lb; g->resultSeen = true; L("[gg] UID %hs: Ledger (%d Zeilen): %s", g->uid, rows, ld); }
      else            L("[gg] UID %hs: Ledger nicht lesbar: %s", g->uid, ld); }
    long long looted = (long long)end - g->start + g->paid + g->coins + g->bonus;
    g->active = false;
    if (!g->resultSeen) L("[gg] UID %hs: weder Ledger noch ShowMatchEndFinalResult -> nur Kontogold-Differenz", g->uid);
    if (looted > 0) {
        ++g_ggSeq;
        L("[gg] UID %hs: Start %d, Ende %d, bezahlt %d, Muenzen %d, Bonus %d -> Beute %lld (wird verbucht)", g->uid, g->start, end, g->paid, g->coins, g->bonus, looted);
        L("[gg] #%d t=%llu CommitRequest[CurrencyGain] (UID:%hs,CurrencyIndex:230000001,Amount:%lld,ActionCode:0)",
          g_ggSeq, (unsigned long long)time(nullptr), g->uid, looted);
    } else if (looted < 0) {
        L("[gg] UID %hs: Start %d, Ende %d, bezahlt %d, Muenzen %d, Bonus %d -> Verlust %lld (NICHT verbucht)", g->uid, g->start, end, g->paid, g->coins, g->bonus, -looted);
    } else {
        L("[gg] UID %hs: Start %d, Ende %d, bezahlt %d, Muenzen %d, Bonus %d -> keine Beute", g->uid, g->start, end, g->paid, g->coins, g->bonus);
    }
}
// v148 reserve path: decode the {ENormalType, value} rows of ShowMatchEndFinalResult heuristically.
static bool GoldDecodeResult(const uint8_t* prm, size_t prmLen, int* coins, int* bonus, char* desc, int descCap) {
    for (size_t q = 0; q + 16 <= prmLen; q += 8) {
        const uint8_t* arr = *(const uint8_t* const*)(prm + q);
        int num = *(const int32_t*)(prm + q + 8);
        int max = *(const int32_t*)(prm + q + 12);
        if (!arr || num < 1 || num > 32 || max < num || max > 256) continue;
        static const int sizes[5] = { 8, 12, 16, 20, 24 };
        for (int si = 0; si < 5; ++si) {
            const int es = sizes[si];
            uint8_t rows[32 * 24];
            if (!SafeCopy(arr, rows, (size_t)num * es)) break;
            for (int tw = 1; tw <= 4; tw <<= 1) {
                for (int vo = 4; vo + 4 <= es; vo += 4) {
                    bool ok = true; int c = 0, b = 0, seen = 0; int len = 0;
                    for (int i = 0; i < num && ok; ++i) {
                        const uint8_t* r = rows + i * es;
                        uint32_t t = tw == 1 ? r[0] : tw == 2 ? *(const uint16_t*)r : *(const uint32_t*)r;
                        int32_t v = *(const int32_t*)(r + vo);
                        if (t > 16 || v < -10000000 || v > 10000000) { ok = false; break; }
                        if (t == 7) { c += v; seen = 1; }
                        if (t == 13 || t == 14) { b += v; seen = 1; }
                        if (len < descCap - 40) len += sprintf_s(desc + len, (size_t)(descCap - len), "%s%s=%d", i ? " " : "", kLedgerNames[t], v);
                    }
                    if (ok && seen) { *coins = c; *bonus = b;
                        len += sprintf_s(desc + len, (size_t)(descCap - len), "  [Feld +0x%zX, %d Zeilen x %d Byte, Typ %d Byte, Wert +%d]", q, num, es, tw, vo);
                        return true; }
                }
            }
        }
    }
    desc[0] = 0;
    return false;
}
static void GoldOnMatchEndResult(void* obj, void* params) {
    GoldTrack* g = GoldByPc(obj);
    if (!g) g = GoldByPs(obj);
    if (!g) { L("[gg] ShowMatchEndFinalResult auf %p -- kein verfolgter Spieler", obj); return; }
    uint8_t prm[0x80]; size_t got = 0;
    for (size_t k = 0x80; k >= 0x20 && !got; k -= 0x20) if (params && SafeCopy(params, prm, k)) got = k;
    if (!got) { L("[gg] UID %hs: ShowMatchEndFinalResult ohne lesbare Parameter", g->uid); return; }
    int coins = 0, bonus = 0; char desc[512];
    if (GoldDecodeResult(prm, got, &coins, &bonus, desc, sizeof(desc))) {
        g->coins = coins; g->bonus = bonus; g->resultSeen = true;
        L("[gg] UID %hs: Rundenergebnis %s -> Muenzen %d, Bonusgold %d", g->uid, desc, coins, bonus);
    } else {
        g->resultSeen = true;
        L("[gg] UID %hs: Rundenergebnis NICHT entschluesselt -- Muenzen bleiben 0 (Ledger bei Logout entscheidet)", g->uid);
    }
}
static void* __fastcall MyLogout(void* gm, void* pc, void* a3, void* a4, void* a5, void* a6) {
    __try { GoldSettle(pc); } __except (EXCEPTION_EXECUTE_HANDLER) { L("[gg] Ausnahme beim Abrechnen (PC %p)", pc); }
    return g_origLogout ? g_origLogout(gm, pc, a3, a4, a5, a6) : nullptr;
}
static void* __fastcall MyInitNewPlayer(void* a1, void* obj, void* a3, void* a4, void* a5, void* a6) {
    char uidForGold[48] = { 0 };
    static int calls = 0;
    if ((g_cfg.loadout || g_cfg.goldGain) && obj) {
        if (calls < 20) { ++calls; L("[lo] InitNewPlayer #%d obj=%p", calls, obj); }
        __try {
            wchar_t* data; int num;
            if (SafeCopy((uint8_t*)obj + OFF::Options_Data, &data, 8) && SafeCopy((uint8_t*)obj + OFF::Options_Num, &num, 4)
                && data && num >= 4 && num <= 8192) {
                static wchar_t buf[8200];
                if (SafeCopy(data, buf, (size_t)num * 2)) {
                    buf[num - 1] = 0;
                    char uid[64] = { 0 };
                    const bool gotUid = (buf[0] != L'{') && ExtractUidFromOptions(buf, num, uid, sizeof(uid));
                    if (calls <= 20 && !gotUid) L("[lo]   keine UID in den Optionen (erste 60 Zeichen: %.60ls)", buf);
                    if (gotUid) strncpy_s(uidForGold, uid, _TRUNCATE);
                    if (gotUid && g_cfg.loadout) {
                        char opts[9000];
                        if (LookupLoadout(uid, opts, sizeof(opts))) {
                            if (InjectOptions(obj, opts)) L("[lo] UID %hs -> Loadout eingespielt (%d Zeichen Optionen, +%zu)", uid, num, strlen(opts));
                            else                          L("[lo] UID %hs: Injektion fehlgeschlagen (Optionen unveraendert)", uid);
                        } else L("[lo] UID %hs: kein Eintrag in loadouts.txt", uid);
                    }
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) { L("[lo] Ausnahme beim Einspielen -- Optionen unveraendert"); }
    }
    void* ret = g_origInitNewPlayer ? g_origInitNewPlayer(a1, obj, a3, a4, a5, a6) : nullptr;
    if (g_cfg.goldGain && uidForGold[0]) {
        __try { GoldRegister(obj, uidForGold); } __except (EXCEPTION_EXECUTE_HANDLER) { L("[gg] Ausnahme beim Registrieren"); }
    }
    return ret;
}

// ============================================================================
//  == 17. pills (v104, ported from PILLS-WORKING-HANDOFF.md)
//
//  Three failures fixed, nothing invented: (1) white pills say "max level"
//  because ItemAbility names Renewal buff 221000337, absent from the active
//  legacy buff table -> alias the two missing names onto the EXISTING legacy
//  rows 220000104/220000105, after verifying class, selector and counts;
//  (2) consumption uses a separate FName->index cache (manager+0x428) -> the
//  same alias, only from the exact apply call site; (3) only white pills
//  spawn because the live picker's Alltablet row has zero R/G/B weights ->
//  a private copy of the row with the shipped AI_NoCR weights, handed to the
//  picker only.
// ============================================================================
typedef void*    (__fastcall* tFindRowTyped)(void*, const FNameRaw*, const wchar_t*, bool);
typedef int32_t* (__fastcall* tFindIndex)(void*, int32_t*, const FNameRaw*);
static tFindRowTyped g_origFindBuffRow   = nullptr;
static tFindIndex    g_origFindBuffIndex = nullptr;
static tFindRowTyped g_origFindSpawnRow  = nullptr;
static void*    g_capsuleBuffTable = nullptr;
static void*    g_capsuleIndexMap  = nullptr;
static FNameRaw g_capsuleBuffNames[2]   = {};   // 221000337 / 221000338
static FNameRaw g_capsuleLegacyNames[2] = {};   // 220000104 / 220000105
static void*    g_capsuleBuffRows[2]    = {};
static volatile LONG g_capsuleHits[2]   = {};

static bool PillsReady() { return g_cfg.pills && InListenWindow(); }
static bool SameRawName(const FNameRaw* a, const FNameRaw* b) {
    uint64_t x = 0, y = 0;
    return SafeCopy(a, &x, 8) && SafeCopy(b, &y, 8) && x == y;
}
static bool RowStringEquals(uint8_t* row, size_t off, const wchar_t* expected) {
    FString v{};
    if (!SafeCopy(row + off, &v, sizeof(v))) return false;
    const size_t len = wcslen(expected) + 1;
    wchar_t copy[16] = {};
    return len <= 16 && v.Num == (int)len && v.Data && SafeCopy(v.Data, copy, len * sizeof(wchar_t)) && wcscmp(copy, expected) == 0;
}
static void PrepareCapsuleBuffFallback() {
    if (!PillsReady() || !g_origFindBuffRow) return;
    static bool attempted = false;
    if (attempted) return;
    attempted = true;
    __try {
        void* instance = ((void* (__fastcall*)())(g_base + PILL::Singleton))();
        void* manager  = instance ? SafePtr((uint8_t*)instance + 0x40) : nullptr;
        void* active   = manager ? ((void* (__fastcall*)(void*))(g_base + PILL::GetBuffTable))(manager) : nullptr;
        if (!active) { L("[pill-data] aktive Buff-Tabelle nicht verfuegbar -- Fallback bleibt aus"); return; }
        auto makeName = (void* (__fastcall*)(FNameRaw*, const wchar_t*, int))(g_base + PILL::MakeFName);
        makeName(&g_capsuleBuffNames[0], L"221000337", 1);
        makeName(&g_capsuleBuffNames[1], L"221000338", 1);
        bool missing[2] = {};
        for (int i = 0; i < 2; ++i) missing[i] = !g_origFindBuffRow(active, &g_capsuleBuffNames[i], L"sp_listen Kapsel-Pruefung", false);
        L("[pill-data] aktive Tabelle %p, weiss fehlt=%d, schwarz fehlt=%d", active, missing[0], missing[1]);
        if (!missing[0] && !missing[1]) { L("[pill-data] beide Renewal-Zeilen sind da -- nichts zu tun"); return; }
        if (!g_origFindBuffIndex) { L("[pill-data] Index-Hook fehlt -- Fallback bleibt aus"); return; }
        void* indexMap    = (uint8_t*)manager + 0x428;
        void* wantedClass = ((void* (__fastcall*)())(g_base + PILL::AddPerkExpClass))();
        void* rows[2] = {};
        for (int i = 0; i < 2; ++i) {
            if (!missing[i]) continue;
            makeName(&g_capsuleLegacyNames[i], i == 0 ? L"220000104" : L"220000105", 1);
            auto* row = (uint8_t*)g_origFindBuffRow(active, &g_capsuleLegacyNames[i], L"sp_listen Legacy-Kapsel-Pruefung", false);
            int32_t originalIndex = -1, legacyIndex = -1;
            g_origFindBuffIndex(indexMap, &originalIndex, &g_capsuleBuffNames[i]);
            g_origFindBuffIndex(indexMap, &legacyIndex,   &g_capsuleLegacyNames[i]);
            auto* entries = (uint8_t*)SafePtr(indexMap);
            uint16_t buffIndex = 0;
            if (entries && legacyIndex >= 0 && legacyIndex < SafeI32((uint8_t*)indexMap + 8))
                SafeCopy(entries + (size_t)legacyIndex * 0x18 + 0xC, &buffIndex, sizeof(buffIndex));
            if (!row || SafePtr(row + 0x18) != wantedClass || !RowStringEquals(row, 0x178, L"All")
             || !RowStringEquals(row, 0x188, i == 0 ? L"2" : L"3") || originalIndex != -1 || !buffIndex) {
                L("[pill-data] Legacy-Kapsel %d faellt durch die Pruefung (originalIndex=%d, legacyIndex=%d, buffIndex=%u) -- Fallback bleibt aus", i, originalIndex, legacyIndex, buffIndex);
                return;
            }
            rows[i] = row;
            L("[pill-data] Legacy-Kapsel %s -> %s, buffIndex=%u, Stufen=%d geprueft", i == 0 ? "221000337" : "221000338", i == 0 ? "220000104" : "220000105", buffIndex, i + 2);
        }
        g_capsuleBuffRows[0] = rows[0];
        g_capsuleBuffRows[1] = rows[1];
        g_capsuleIndexMap    = indexMap;
        g_capsuleBuffTable   = active;
        L("[pill-data] Legacy-Kapsel-Kompatibilitaet steht (weiss=%d schwarz=%d)", rows[0] != nullptr, rows[1] != nullptr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_capsuleBuffTable = nullptr;
        L("[pill-data] Vorbereitung hat geworfen -- Kapsel-Fallback bleibt aus");
    }
}
static void* __fastcall MyFindBuffRow(void* table, const FNameRaw* name, const wchar_t* ctx, bool warn) {
    int capsule = -1;
    if (PillsReady() && table == g_capsuleBuffTable && name) {
        uint64_t key = 0;
        if (SafeCopy(name, &key, sizeof(key)))
            for (int i = 0; i < 2; ++i) { uint64_t wanted = 0; memcpy(&wanted, &g_capsuleBuffNames[i], sizeof(wanted));
                if (key == wanted && g_capsuleBuffRows[i]) { capsule = i; break; } }
    }
    void* result = g_origFindBuffRow(table, name, ctx, capsule < 0 && warn);
    if (result || capsule < 0) return result;
    if (InterlockedIncrement(&g_capsuleHits[capsule]) <= 3)
        L("[pill-data] fehlender Buff %s aus der aktiven Legacy-Zeile aufgeloest (Abfrage %ld)", capsule == 0 ? "221000337" : "221000338", g_capsuleHits[capsule]);
    return g_capsuleBuffRows[capsule];
}
static int32_t* __fastcall MyFindBuffIndex(void* map, int32_t* out, const FNameRaw* name) {
    const uintptr_t caller = (uintptr_t)_ReturnAddress() - g_base;
    int32_t* result = g_origFindBuffIndex(map, out, name);
    if (!PillsReady() || caller != PILL::ConsumeRet || map != g_capsuleIndexMap || !out || *out != -1 || !name) return result;
    uint64_t key = 0;
    if (!SafeCopy(name, &key, sizeof(key))) return result;
    for (int i = 0; i < 2; ++i) {
        uint64_t wanted = 0; memcpy(&wanted, &g_capsuleBuffNames[i], sizeof(wanted));
        if (key != wanted || !g_capsuleBuffRows[i]) continue;
        result = g_origFindBuffIndex(map, out, &g_capsuleLegacyNames[i]);
        L("[pill-consume] Legacy-Alias %s -> %s, Cache-Platz=%d, Stufen=%d", i == 0 ? "221000337" : "221000338", i == 0 ? "220000104" : "220000105", *out, i + 2);
        return result;
    }
    return result;
}
// ---- coloured pills in the loot ----------------------------------------------
static const wchar_t* kCapsulePools[] = { L"Alltablet", L"pickonetablet", L"AIAlltablet", L"RSI_BaseSpawnPackage_Tablet_t1" };
static const wchar_t* kCapsuleItems[] = { L"Tablet_R", L"Tablet_G", L"Tablet_B", L"Tablet_White", L"Tablet_Black", L"None" };
static FNameRaw g_spawnPoolNames[4] = {}, g_spawnItemNames[6] = {};
static bool     g_spawnAuditReady = false;
static volatile LONG g_spawnPoolHits[4] = {};
alignas(16) static uint8_t g_capsuleRow[0x20] = {};
alignas(16) static uint8_t g_capsuleEntries[6 * 0x80] = {};
static void* g_capsuleLootTable = nullptr;
static void* g_capsuleSourceRow = nullptr;

static void LogCapsulePool(void* table, void* row, int pool, uintptr_t caller, const char* origin) {
    __try {
        L("[loot-pool] %s Pool=%ls Tabelle=%p Zeile=%p Aufrufer=0x%llX", origin, kCapsulePools[pool], table, row, (unsigned long long)caller);
        if (!row) return;
        auto* entries = (uint8_t*)SafePtr((uint8_t*)row + 0x10);
        const int count = SafeI32((uint8_t*)row + 0x18);
        uint8_t type = 0; SafeCopy((uint8_t*)row + 8, &type, 1);
        if (!entries || count < 1 || count > 64) { L("[loot-pool] Eintragsfeld ungueltig, count=%d", count); return; }
        for (int i = 0; i < count; ++i) {
            auto* e = entries + (size_t)i * 0x80;
            float w = 0; SafeCopy(e, &w, sizeof(w));
            const wchar_t* label = L"unbekannt";
            for (int j = 0; j < 6; ++j) if (SameRawName((FNameRaw*)(e + 0x10), &g_spawnItemNames[j])) { label = kCapsuleItems[j]; break; }
            L("[loot-pool]   Typ=%u Eintrag=%d Item=%ls Gewicht=%.6f Buendel=%d", type, i, label, w, SafeI32(e + 4));
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { L("[loot-pool] Diagnose-Lesen fehlgeschlagen -- Loot bleibt nativ"); }
}
static void PrepareColouredCapsulePool(void* table, void* row) {
    static const float observed[6] = { 0.f, 0.f, 0.f, 0.13f, 0.02f, 0.85f };
    static const float restored[6] = { 0.14f, 0.14f, 0.14f, 0.04f, 0.04f, 0.50f };
    if (!row || !g_capsuleBuffTable || SafeI32((uint8_t*)row + 0x18) != 6) return;
    uint8_t type = 0;
    if (!SafeCopy((uint8_t*)row + 8, &type, 1) || type != 2) return;
    auto* entries = (uint8_t*)SafePtr((uint8_t*)row + 0x10);
    if (!entries) return;
    for (int i = 0; i < 6; ++i) {
        auto* e = entries + i * 0x80;
        float w = -1.f;
        if (!SameRawName((FNameRaw*)(e + 0x10), &g_spawnItemNames[i]) || !SafeCopy(e, &w, sizeof(w)) || w != observed[i] || SafeI32(e + 4) != 1) {
            L("[loot-repair] Alltablet weicht vom geprueften Null-RGB-Pool ab -- unveraendert"); return;
        }
    }
    if (!SafeCopy(row, g_capsuleRow, sizeof(g_capsuleRow)) || !SafeCopy(entries, g_capsuleEntries, sizeof(g_capsuleEntries))) return;
    for (int i = 0; i < 6; ++i) memcpy(g_capsuleEntries + i * 0x80, &restored[i], sizeof(float));
    void* ownEntries = g_capsuleEntries;
    int capacity = 6;
    memcpy(g_capsuleRow + 0x10, &ownEntries, sizeof(ownEntries));
    memcpy(g_capsuleRow + 0x1C, &capacity, sizeof(capacity));
    g_capsuleSourceRow = row;
    g_capsuleLootTable = table;
    L("[loot-repair] Alltablet-Kopie bereit: R/G/B je .14, weiss .04, schwarz .04, nichts .50");
}
static void PrepareCapsuleLootAudit() {
    if (!PillsReady() || !g_origFindSpawnRow || g_spawnAuditReady) return;
    __try {
        auto makeName = (void* (__fastcall*)(FNameRaw*, const wchar_t*, int))(g_base + PILL::MakeFName);
        for (int i = 0; i < 4; ++i) makeName(&g_spawnPoolNames[i], kCapsulePools[i], 1);
        for (int i = 0; i < 6; ++i) makeName(&g_spawnItemNames[i], kCapsuleItems[i], 1);
        void* instance = ((void* (__fastcall*)())(g_base + PILL::Singleton))();
        void* manager  = instance ? SafePtr((uint8_t*)instance + 0x40) : nullptr;
        if (!manager) return;
        void* table = ((void* (__fastcall*)(void*))(g_base + PILL::GetSpawnTable))(manager);
        if (!table) return;
        g_spawnAuditReady = true;
        for (int i = 0; i < 4; ++i) {
            void* row = g_origFindSpawnRow(table, &g_spawnPoolNames[i], L"sp_listen Kapsel-Loot-Pruefung", false);
            LogCapsulePool(table, row, i, 0, "Bestandsaufnahme");
            if (i == 0) PrepareColouredCapsulePool(table, row);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { L("[loot-pool] Vorbereitung fehlgeschlagen -- Loot bleibt nativ"); }
}
static void* __fastcall MyFindSpawnRow(void* table, const FNameRaw* name, const wchar_t* ctx, bool warn) {
    const uintptr_t caller = (uintptr_t)_ReturnAddress() - g_base;
    void* row = g_origFindSpawnRow(table, name, ctx, warn);
    if (PillsReady() && g_spawnAuditReady && name) {
        for (int i = 0; i < 4; ++i) {
            if (!SameRawName(name, &g_spawnPoolNames[i])) continue;
            const bool replaced = (i == 0) && caller == PILL::PickerRet && table == g_capsuleLootTable && row && row == g_capsuleSourceRow;
            if (replaced) row = g_capsuleRow;
            if (InterlockedIncrement(&g_spawnPoolHits[i]) <= 3) {
                if (replaced) L("[loot-repair] der native Picker benutzt jetzt die AI_NoCR-Gewichte");
                LogCapsulePool(table, row, i, caller, replaced ? "Picker-ersetzt" : (caller == PILL::PickerRet ? "Picker" : "nativ"));
            }
            break;
        }
    }
    return row;
}
static void PillTick() { PrepareCapsuleBuffFallback(); PrepareCapsuleLootAudit(); }

// ============================================================================
//  == 18. command file (v167..v169) -- the admin panel reaches into the round
//
//  tools/sp-listen-alive.js appends "<id>\t<text>" lines to the file from
//  -cmdfile=<path> (default: sp_listen_cmd.txt next to this DLL) and reads the
//  answer from this log: "[cmd] <id> ok <text>" / "[cmd] <id> fail <reason>".
//  The reply text is English because it is shown in the panel unchanged.
//    cheatable             APlayerController::EnableCheats (if needed) +
//                          BravoHotelCheatManager::Cheatable() -- the game's gate
//    StartGame <s> <bool>  BravoHotelCheatManager::StartGame(InDelay, bUseAircraft)
//    <Name>                any PARAMETERLESS function of the cheat manager or controller
//  Everything goes through the reflection function list (section 4). The
//  UFunction NumParms/ParmsSize offsets are calibrated at StartGame (8 bytes)
//  vs Cheatable (0 bytes). On start the read pointer is placed at the file's
//  end, so a command from an earlier session never runs into this round.
// ============================================================================
static long long g_cmdOff   = -1;
static int       g_cmdCount = 0;

static void CalibrateUFunctionLayout(void* withParams, void* withoutParams) {
    if (g_fnParmsSizeOff >= 0 || !withParams || !withoutParams) return;
    int psCand[4], psN = 0, npCand[4], npN = 0;
    for (int off = 0x60; off <= 0x200; off += 2)
        if ((SafeI32((uint8_t*)withParams + off) & 0xFFFF) == 8 && (SafeI32((uint8_t*)withoutParams + off) & 0xFFFF) == 0 && psN < 4) psCand[psN++] = off;
    for (int off = 0x60; off <= 0x200; ++off)
        if ((SafeI32((uint8_t*)withParams + off) & 0xFF) == 2 && (SafeI32((uint8_t*)withoutParams + off) & 0xFF) == 0 && npN < 4) npCand[npN++] = off;
    char ps[64] = "", np[64] = "";
    for (int i = 0; i < psN; ++i) { char t[12]; sprintf_s(t, "%s+0x%02X", i ? " " : "", psCand[i]); strcat_s(ps, t); }
    for (int i = 0; i < npN; ++i) { char t[12]; sprintf_s(t, "%s+0x%02X", i ? " " : "", npCand[i]); strcat_s(np, t); }
    L("[cmd] Eichung: ParmsSize-Kandidaten [%s], NumParms-Kandidaten [%s]", psN ? ps : "keine", npN ? np : "keine");
    if (psN) {
        g_fnParmsSizeOff = psCand[0];
        g_fnNumParmsOff  = npN ? npCand[0] : -1;
        L("[cmd] UFunction-Layout geeicht: ParmsSize +0x%02X, NumParms %s", g_fnParmsSizeOff, npN ? "gefunden" : "nicht gefunden -- die Groesse allein entscheidet");
        if (psN > 1) L("[cmd] Achtung: %d ParmsSize-Kandidaten -- der erste wird benutzt", psN);
        return;
    }
    L("[cmd] UFunction-Layout NICHT eichbar -- Funktionen mit Parametern werden abgelehnt, parameterlose nur noch nach Namen");
}
static bool EqNoCase(const char* a, const char* b) {
    if (!a || !b) return false;
    for (;; ++a, ++b) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
        if (x != y) return false;
        if (!x) return true;
    }
}
static void* CheatManagerOf(void* pc) {
    if (!pc) return nullptr;
    void* cm = SafePtr((uint8_t*)pc + OFF::PC_CheatManager);
    return (cm && MaybeAlive(cm)) ? cm : nullptr;
}
static bool CmdEnableCheats(char* out, int cap) {
    void* pc = RealFirstPlayerController();
    if (!pc) { sprintf_s(out, cap, "no host PlayerController yet -- is the round loaded?"); return false; }
    void* cm = CheatManagerOf(pc);
    if (!cm) {
        int np = -1, ps = -1;
        void* fn = FindFunctionNamed(pc, L"EnableCheats", &np, &ps);
        if (!fn) { sprintf_s(out, cap, "no cheat manager, and EnableCheats is not in the controller's function list"); return false; }
        L("[cmd] EnableCheats: UFunction %p, NumParms %d, ParmsSize %d", fn, np, ps);
        if (!CallFunctionOn(pc, fn, nullptr)) { sprintf_s(out, cap, "EnableCheats threw"); return false; }
        cm = CheatManagerOf(pc);
        if (!cm) { sprintf_s(out, cap, "EnableCheats ran but PlayerController+0x620 is still null"); return false; }
    }
    wchar_t cn[128] = L"?"; ClassNameOf(cm, cn, 128);
    L("[cmd] CheatManager %p, Klasse %ls", cm, cn);
    int np2 = -1, ps2 = -1;
    void* ch = FindFunctionNamed(cm, L"Cheatable", &np2, &ps2);
    { int a = -1, b = -1; void* sg = FindFunctionNamed(cm, L"StartGame", &a, &b);
      if (sg && ch) CalibrateUFunctionLayout(sg, ch);
      if (ch && g_fnParmsSizeOff >= 0) {
          np2 = g_fnNumParmsOff >= 0 ? (SafeI32((uint8_t*)ch + g_fnNumParmsOff) & 0xFF) : -1;
          ps2 = SafeI32((uint8_t*)ch + g_fnParmsSizeOff) & 0xFFFF;
      } }
    if (!ch) { sprintf_s(out, cap, "cheat manager %p is there, but it has no Cheatable() -- the game's own gate stays shut", cm); return false; }
    L("[cmd] Cheatable: UFunction %p, NumParms %d, ParmsSize %d", ch, np2, ps2);
    if (!CallFunctionOn(cm, ch, nullptr)) { sprintf_s(out, cap, "Cheatable() threw"); return false; }
    sprintf_s(out, cap, "cheat manager %p, Cheatable() called -- the game should accept cheats now", cm);
    return true;
}
static bool CmdStartGame(float delay, bool aircraft, char* out, int cap) {
    void* pc = RealFirstPlayerController();
    if (!pc) { sprintf_s(out, cap, "no host PlayerController yet -- is the round loaded?"); return false; }
    void* cm = CheatManagerOf(pc);
    if (!cm) { sprintf_s(out, cap, "no cheat manager (run cheatable first)"); return false; }
    int np = -1, ps = -1;
    void* fn = FindFunctionNamed(cm, L"StartGame", &np, &ps);
    if (!fn) { sprintf_s(out, cap, "StartGame not found in the cheat manager's function list"); return false; }
    { void* ch = FindFunctionNamed(cm, L"Cheatable", nullptr, nullptr); if (ch) CalibrateUFunctionLayout(fn, ch); }
    if (g_fnParmsSizeOff >= 0 && (np < 0 || ps < 0)) {
        if (g_fnNumParmsOff >= 0) np = SafeI32((uint8_t*)fn + g_fnNumParmsOff) & 0xFF;
        ps = SafeI32((uint8_t*)fn + g_fnParmsSizeOff) & 0xFFFF;
    }
    L("[cmd] StartGame: UFunction %p, NumParms %d, ParmsSize %d (erwartet 2 / 8)", fn, np, ps);
    if (ps > 0 && ps <= 256 && ps != 8) { sprintf_s(out, cap, "StartGame wants %d parameter bytes, not the 8 the SDK dump states -- not calling it", ps); return false; }
    struct { float InDelay; uint8_t bUseAircraft; uint8_t pad[3]; } params{};
    params.InDelay = delay;
    params.bUseAircraft = aircraft ? 1 : 0;
    if (!CallFunctionOn(cm, fn, &params)) { sprintf_s(out, cap, "StartGame threw"); return false; }
    sprintf_s(out, cap, "StartGame %.0f %s sent to the cheat manager", (double)delay, aircraft ? "true" : "false");
    return true;
}
static bool CmdNamedFunction(const char* name, char* out, int cap) {
    wchar_t want[128] = L"";
    int k = 0;
    for (; name[k] && k < 127; ++k) want[k] = (wchar_t)(unsigned char)name[k];
    want[k] = 0;
    if (!k) { sprintf_s(out, cap, "empty command"); return false; }
    void* pc = RealFirstPlayerController();
    if (!pc) { sprintf_s(out, cap, "no host PlayerController yet -- is the round loaded?"); return false; }
    void* targets[2] = { CheatManagerOf(pc), pc };
    const char* where[2] = { "cheat manager", "controller" };
    for (int i = 0; i < 2; ++i) {
        if (!targets[i]) continue;
        int np = -1, ps = -1;
        void* fn = FindFunctionNamed(targets[i], want, &np, &ps);
        if (!fn) continue;
        if (ps < 0) {
            const bool knownEmpty = !wcscmp(want, L"Cheatable") || !wcscmp(want, L"EnableCheats") || !wcscmp(want, L"IWantToDie") || !wcscmp(want, L"ForceEndMatch");
            if (!knownEmpty) { sprintf_s(out, cap, "%s found, but this build's parameter fields are not calibrated yet -- run Enable cheats once, then try again", name); return false; }
        } else if (ps > 0 || np > 0) {
            sprintf_s(out, cap, "%s takes %d parameter(s), %d bytes -- only parameterless commands are allowed here", name, np, ps); return false;
        }
        L("[cmd] %s auf dem %s: UFunction %p, NumParms %d, ParmsSize %d", name, where[i], fn, np, ps);
        if (!CallFunctionOn(targets[i], fn, nullptr)) { sprintf_s(out, cap, "%s threw", name); return false; }
        sprintf_s(out, cap, "%s ran on the %s", name, where[i]);
        return true;
    }
    sprintf_s(out, cap, "no parameterless function called \"%s\" on the cheat manager or the controller", name);
    return false;
}
static void RunCommandLine(const char* id, char* text) {
    char out[240] = "";
    bool ok = false;
    if (_strnicmp(text, "StartGame", 9) == 0 && (text[9] == ' ' || text[9] == 0)) {
        float delay = 10.0f; bool air = true;
        const char* p = text + 9;
        while (*p == ' ') ++p;
        if (*p) {
            delay = (float)atof(p);
            while (*p && *p != ' ') ++p;
            while (*p == ' ') ++p;
            if (*p) air = EqNoCase(p, "true") || EqNoCase(p, "1") || EqNoCase(p, "yes");
        }
        if (delay < 0.0f || delay > 600.0f) { sprintf_s(out, (int)sizeof(out), "delay %.0f is outside 0-600 seconds", (double)delay); ok = false; }
        else ok = CmdStartGame(delay, air, out, (int)sizeof(out));
    } else if (EqNoCase(text, "cheatable") || EqNoCase(text, "EnableCheats")) {
        ok = CmdEnableCheats(out, (int)sizeof(out));
    } else {
        ok = CmdNamedFunction(text, out, (int)sizeof(out));
    }
    L("[cmd] %s %s %s", id, ok ? "ok" : "fail", out[0] ? out : (ok ? "done" : "failed"));   // the line the panel reads
}
// Game thread, from MyTickFlush, ~2x per second.
static void CmdFileTick() {
    if (!g_cfg.cmdFile) return;
    if (!g_cfg.cmdPath[0]) {
        HMODULE self = nullptr;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&CmdFileTick, &self) && self) {
            GetModuleFileNameW(self, g_cfg.cmdPath, 500);
            wchar_t* bs = wcsrchr(g_cfg.cmdPath, L'\\');
            const size_t rem = bs ? (size_t)(512 - (bs + 1 - g_cfg.cmdPath)) : 0;
            if (bs && rem >= 24) wcscpy_s(bs + 1, rem, L"sp_listen_cmd.txt"); else g_cfg.cmdPath[0] = 0;
        }
        if (!g_cfg.cmdPath[0]) { g_cfg.cmdFile = false; L("[cmd] Pfad der eigenen DLL nicht lesbar -- Befehlsdatei AUS"); return; }
    }
    FILE* f = nullptr;
    if (_wfopen_s(&f, g_cfg.cmdPath, L"rb") != 0 || !f) return;
    _fseeki64(f, 0, SEEK_END);
    const long long size = _ftelli64(f);
    if (g_cmdOff < 0) { g_cmdOff = size; L("[cmd] Befehlsdatei %ls, Start bei Byte %lld", g_cfg.cmdPath, size); fclose(f); return; }
    if (size < g_cmdOff) { g_cmdOff = 0; L("[cmd] Befehlsdatei wurde gekuerzt -- lese wieder von vorn"); }
    if (size == g_cmdOff) { fclose(f); return; }
    long long want = size - g_cmdOff;
    if (want > 16384) want = 16384;
    static char buf[16384 + 1];
    _fseeki64(f, g_cmdOff, SEEK_SET);
    const size_t got = fread(buf, 1, (size_t)want, f);
    fclose(f);
    if (!got) return;
    buf[got] = 0;
    size_t lineStart = 0, consumed = 0;
    for (size_t i = 0; i < got; ++i) {
        if (buf[i] != '\n') continue;
        char line[1024];
        size_t len = i - lineStart;
        if (len > sizeof(line) - 1) len = sizeof(line) - 1;
        memcpy(line, buf + lineStart, len);
        line[len] = 0;
        while (len && (line[len - 1] == '\r' || line[len - 1] == ' ')) line[--len] = 0;
        lineStart = i + 1;
        consumed  = lineStart;
        char* tab = strchr(line, '\t');
        if (!tab || tab == line) continue;
        *tab = 0;
        char* text = tab + 1;
        while (*text == ' ') ++text;
        if (!*text) { L("[cmd] %s fail empty command", line); continue; }
        ++g_cmdCount;
        L("[cmd] #%d %s: %s", g_cmdCount, line, text);
        __try { RunCommandLine(line, text); }
        __except (EXCEPTION_EXECUTE_HANDLER) { L("[cmd] %s fail the DLL threw while running it", line); }
    }
    g_cmdOff += (long long)consumed;
}

// ============================================================================
//  == 19. crash guards (v110, v117/v126, v136) and the crash reporter (v113)
//
//  Every guard reproduces an early-out the engine itself already has for a
//  neighbouring case (null map -> "not found", null connection -> ret, null
//  actor -> return 0) and applies it to the dead value the graph hands over.
//  No engine state is written.
// ============================================================================
// ---- 0x11E8200: generic TMap find, crashed on a null owning object ("0x40") --
typedef int32_t* (__fastcall* tMapFind)(void* map, int32_t* out, void* key);
static tMapFind      g_origMapFind = nullptr;
static volatile LONG g_mapGuardHits = 0;
static int32_t* __fastcall MyMapFind(void* map, int32_t* out, void* key) {
    if (g_cfg.mapGuard && (uintptr_t)map < 0x1000) {
        LONG n = InterlockedIncrement(&g_mapGuardHits);
        if (n <= 10 || (n % 500) == 0)
            L("[mg] *** Null-Map abgefangen (rcx=%p) -- Aufrufer base+0x%llX, Treffer #%ld. Wir liefern -1 wie der Nicht-gefunden-Zweig. ***",
              map, (unsigned long long)((uintptr_t)_ReturnAddress() - g_base), n);
        if (out) *out = -1;
        return out;
    }
    return g_origMapFind(map, out, key);
}
// ---- 0x3F81A20: actor -> channel layer; a dead actor returns like a null connection --
typedef void (__fastcall* tActorChanPrep)(void* actor, void* conn);
static tActorChanPrep g_origActorChanPrep = nullptr;
static volatile LONG  g_chanGuardHits = 0;
static void __fastcall MyActorChanPrep(void* actor, void* conn) {
    if (!conn) return;                    // the original's own first check (reproduced, the jump is not in the trampoline)
    if (g_cfg.chanGuard && !ActorPtrSane(actor)) {
        LONG n = InterlockedIncrement(&g_chanGuardHits);
        if (n <= 10 || (n % 500) == 0)
            L("[cg] *** Unbrauchbarer Actor %p an die Kanal-Schicht abgefangen (VTable %p, Klasse %p) -- Aufrufer base+0x%llX, Treffer #%ld. OHNE das waere der Host hier abgestuerzt. ***",
              actor, SafePtr(actor), SafePtr((uint8_t*)actor + OFF::UObject_Class), (unsigned long long)((uintptr_t)_ReturnAddress() - g_base), n);
        return;
    }
    if (g_origActorChanPrep) g_origActorChanPrep(actor, conn);
}
// ---- 0x13C3DB0: the graph's per-actor function; a dead actor returns 0 -------
typedef void* (__fastcall* tGraphPerActor)(void*, void*, void*, void*, void*, void*);
static tGraphPerActor g_origGraphPerActor = nullptr;
static volatile LONG  g_graphGuardHits = 0, g_graphGuardSeen = 0;
static volatile LONG g_gpDoors = 0, g_gpDestr = 0, g_gpZone = 0, g_gpPickups = 0;   // v172: what the graph considers
static void* __fastcall MyGraphPerActor(void* a, void* actor, void* c, void* d, void* e, void* f) {
    if (g_cfg.graphGuard && actor) {
        InterlockedIncrement(&g_graphGuardSeen);
        if (actor == g_blueZone) InterlockedIncrement(&g_gpZone);
        else if (g_remoteCount > 0 && ActorPtrSane(actor)) {
            const uint8_t cv = ClassVerdictOf(actor);
            if (cv == CV_Door) InterlockedIncrement(&g_gpDoors);
            else if (cv == CV_Destructible) InterlockedIncrement(&g_gpDestr);
            else if (cv == CV_Pickup) InterlockedIncrement(&g_gpPickups);
        }
        if (!ActorPtrSane(actor)) {
            LONG n = InterlockedIncrement(&g_graphGuardHits);
            if (n <= 10 || (n % 500) == 0)
                L("[gd] *** Toter Actor %p in der Actor-Schleife des Graphen abgefangen (VTable %p, Klasse %p) -- Treffer #%ld. Wir geben 0 zurueck wie der Graph bei einem Null-Actor. ***",
                  actor, SafePtr(actor), SafePtr((uint8_t*)actor + OFF::UObject_Class), n);
            return nullptr;
        }
    }
    return g_origGraphPerActor ? g_origGraphPerActor(a, actor, c, d, e, f) : nullptr;
}

// ---- crash reporter: first-chance vectored handler, read-only --------------
static PVOID     g_vehHandle = nullptr;
uintptr_t        g_selfBase  = 0;
static uintptr_t g_selfEnd   = 0;
static uintptr_t g_exeEnd    = 0;
static volatile LONG g_crashCount = 0;

static uintptr_t ImageEndOf(uintptr_t mod) {
    if (!mod) return 0;
    __try {
        const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)mod;
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
        const IMAGE_NT_HEADERS64* nt = (const IMAGE_NT_HEADERS64*)(mod + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
        return mod + nt->OptionalHeader.SizeOfImage;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
static void NameAddr(uintptr_t a, char* buf, size_t n) {
    if (g_base && g_exeEnd && a >= g_base && a < g_exeEnd)                 sprintf_s(buf, n, "base+0x%llX", (unsigned long long)(a - g_base));
    else if (g_selfBase && g_selfEnd && a >= g_selfBase && a < g_selfEnd)  sprintf_s(buf, n, "sp_listen+0x%llX", (unsigned long long)(a - g_selfBase));
    else                                                                    sprintf_s(buf, n, "%p", (void*)a);
}
static LONG CALLBACK MyVeh(EXCEPTION_POINTERS* ep) {
    if (!g_cfg.crashLog || !ep || !ep->ExceptionRecord || !ep->ContextRecord) return EXCEPTION_CONTINUE_SEARCH;
    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION && code != EXCEPTION_PRIV_INSTRUCTION &&
        code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != EXCEPTION_STACK_OVERFLOW && code != EXCEPTION_IN_PAGE_ERROR)
        return EXCEPTION_CONTINUE_SEARCH;
    const uintptr_t rip = (uintptr_t)ep->ExceptionRecord->ExceptionAddress;
    if (g_selfBase && rip >= g_selfBase && rip < g_selfEnd) return EXCEPTION_CONTINUE_SEARCH;   // our own SafeCopy probes
    uintptr_t frame[40];
    int frames = 0;
    __try {
        CONTEXT w = *ep->ContextRecord;
        for (; frames < 40 && w.Rip; ++frames) {
            frame[frames] = (uintptr_t)w.Rip;
            DWORD64 imgBase = 0;
            PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(w.Rip, &imgBase, nullptr);
            if (!fn) { if (!w.Rsp) { ++frames; break; } w.Rip = *(DWORD64*)w.Rsp; w.Rsp += 8; continue; }
            PVOID handlerData = nullptr; DWORD64 establisher = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, imgBase, w.Rip, fn, &w, &handlerData, &establisher, nullptr);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    for (int i = 1; i < frames && i <= 2; ++i)          // one of our own guarded engine calls -> handled
        if (g_selfBase && frame[i] >= g_selfBase && frame[i] < g_selfEnd) return EXCEPTION_CONTINUE_SEARCH;
    {   // budget per fault site
        static uintptr_t site[16] = {}; static int siteHits[16] = {}; static int siteN = 0;
        static CRITICAL_SECTION cs; static bool csReady = false;
        if (!csReady) { InitializeCriticalSection(&cs); csReady = true; }
        EnterCriticalSection(&cs);
        int idx = -1;
        for (int i = 0; i < siteN; ++i) if (site[i] == rip) { idx = i; break; }
        if (idx < 0 && siteN < 16) { idx = siteN++; site[idx] = rip; siteHits[idx] = 0; }
        const bool skip = (idx < 0) || (++siteHits[idx] > 2);
        LeaveCriticalSection(&cs);
        if (skip) return EXCEPTION_CONTINUE_SEARCH;
    }
    if (InterlockedIncrement(&g_crashCount) > 40) return EXCEPTION_CONTINUE_SEARCH;
    char nm[80];
    NameAddr(rip, nm, sizeof(nm));
    L("[ax] *** Ausnahme 0x%08lX an %s (Thread %lu)", (unsigned long)code, nm, GetCurrentThreadId());
    if ((code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR) && ep->ExceptionRecord->NumberParameters >= 2) {
        const ULONG_PTR op = ep->ExceptionRecord->ExceptionInformation[0];
        L("[ax] %s an Adresse %p", op == 0 ? "Lesen" : (op == 1 ? "Schreiben" : "Ausfuehren"), (void*)ep->ExceptionRecord->ExceptionInformation[1]);
    }
    const CONTEXT* c = ep->ContextRecord;
    L("[ax] rax=%016llX rbx=%016llX rcx=%016llX rdx=%016llX", (unsigned long long)c->Rax, (unsigned long long)c->Rbx, (unsigned long long)c->Rcx, (unsigned long long)c->Rdx);
    L("[ax] rsi=%016llX rdi=%016llX rbp=%016llX rsp=%016llX", (unsigned long long)c->Rsi, (unsigned long long)c->Rdi, (unsigned long long)c->Rbp, (unsigned long long)c->Rsp);
    L("[ax] r8 =%016llX r9 =%016llX r10=%016llX r11=%016llX", (unsigned long long)c->R8, (unsigned long long)c->R9, (unsigned long long)c->R10, (unsigned long long)c->R11);
    L("[ax] r12=%016llX r13=%016llX r14=%016llX r15=%016llX", (unsigned long long)c->R12, (unsigned long long)c->R13, (unsigned long long)c->R14, (unsigned long long)c->R15);
    for (int i = 0; i < frames; ++i) { NameAddr(frame[i], nm, sizeof(nm)); L("[ax]   #%02d  %s", i, nm); }
    L("[ax] --- Ende des Berichts ---");
    return EXCEPTION_CONTINUE_SEARCH;
}

// ============================================================================
//  == 20. Listen(), notify redirect, TickFlush, watchdog
// ============================================================================
static uint8_t*  g_capEngine = nullptr;      // capture stub holding GEngine at +0x80
static uintptr_t g_notifyOff = 0;            // FNetworkNotify subobject offset inside UWorld
static HANDLE    g_watchdog  = nullptr;
typedef void (__fastcall* tTickFlush)(void* drv, float dt);
static tTickFlush    g_origTickFlush = nullptr;

// The FNetworkNotify subobject: the slot whose vtable has NotifyControlMessage
// in its first 8 entries (the primary UWorld vtable has it at ~92).
static uintptr_t FindNotifyOffset(void* world) {
    const uintptr_t want = g_base + RVA::NotifyControlMessage;
    for (uintptr_t o = 8; o < 0x400; o += 8) {
        void* vt = SafePtr((uint8_t*)world + o);
        if (!vt || !InModule(vt)) continue;
        for (int k = 0; k < 8; ++k)
            if ((uintptr_t)SafePtr((uint8_t*)vt + k * 8) == want) {
                L("  FNetworkNotify-Subobjekt: World+0x%llX (VTable %p, NCM bei Index %d)", (unsigned long long)o, vt, k);
                return o;
            }
    }
    return 0;
}
// The two engine paths that dereference World->NetDriver unchecked run with
// the driver visible. Vtable swap, no code patch.
typedef int  (__fastcall* tNotifyAccept)(void* notify);
typedef void (__fastcall* tNotifyCtrl)(void* notify, void* conn, uint8_t msg, void* bunch);
static tNotifyAccept g_origNotifyAccept = nullptr;
static tNotifyCtrl   g_origNotifyCtrl   = nullptr;
static int __fastcall MyNotifyAccept(void* notify) {
    MaskOff();
    int r = g_origNotifyAccept ? g_origNotifyAccept(notify) : 0;
    MaskOn();
    return r;
}
static void __fastcall MyNotifyCtrl(void* notify, void* conn, uint8_t msg, void* bunch) {
    if (msg == 0) {                                   // NMT_Hello: every join attempt starts here
        static int hellos = 0;
        if (hellos < 200) { ++hellos; L("[et] NMT_Hello #%d von Verbindung %p (Token-Patch %s)", hellos, conn, g_cfg.ignoreEncToken ? "AN" : "AUS"); }
    }
    MaskOff();
    if (g_origNotifyCtrl) g_origNotifyCtrl(notify, conn, msg, bunch);
    MaskOn();
}
static void HookNotify(void* world) {
    if (!g_notifyOff) { L("  [nm] kein FNetworkNotify-Offset -> Maske waere unsicher"); return; }
    void** vt = (void**)SafePtr((uint8_t*)world + g_notifyOff);
    if (!vt || !InModule(vt)) { L("  [nm] Notify-VTable unbrauchbar"); return; }
    void* wantA = (void*)(g_base + RVA::NotifyAcceptingConnection);
    void* wantC = (void*)(g_base + RVA::NotifyControlMessage);
    for (int k = 0; k < 8; ++k) {
        void* cur = SafePtr(vt + k);
        if (cur == wantA && !g_origNotifyAccept) {
            g_origNotifyAccept = (tNotifyAccept)cur; void* mine = (void*)&MyNotifyAccept;
            L(Poke(vt + k, &mine, 8) ? "  [nm] NotifyAcceptingConnection (Index %d) umgehaengt" : "  [nm] NotifyAcceptingConnection (Index %d) NICHT schreibbar", k);
        } else if (cur == wantC && !g_origNotifyCtrl) {
            g_origNotifyCtrl = (tNotifyCtrl)cur; void* mine = (void*)&MyNotifyCtrl;
            L(Poke(vt + k, &mine, 8) ? "  [nm] NotifyControlMessage (Index %d) umgehaengt" : "  [nm] NotifyControlMessage (Index %d) NICHT schreibbar", k);
        }
    }
    if (!g_origNotifyAccept) L("  [nm] NotifyAcceptingConnection NICHT gefunden");
    if (!g_origNotifyCtrl)   L("  [nm] NotifyControlMessage NICHT gefunden");
}

// ---- UNetDriver::TickFlush: the game-thread heartbeat ----------------------
// Everything that touches engine state runs from here, with the driver
// visible. Cadences are the v169 ones.
static void __fastcall MyTickFlush(void* drv, float dt) {
    InterlockedIncrement(&g_tfCount);
    const bool ours = (drv == g_drv);
    if (ours) MaskOff();
    if (g_origTickFlush) g_origTickFlush(drv, dt);
    ReplicateNow(drv, dt);
    if (ours) {
        ZoneTick();
        ViewTypeTick();
        static int zs = 0, sc = 0, bf = 0, ep = 0, wt = 0, fx = 0, sw = 0, pi = 0, cf = 0;
        if (++zs >= 120) { zs = 0; ZoneScaleTick(); }            // ~2 s
        if (++sc >= 6)   { sc = 0; ServerChangedTick(); DoorSweepTick(); }   // ~10x/s
        if (++bf >= 3)   { bf = 0; BoltFinishTick(); }           // ~20x/s
        if (++ep >= 3)   { ep = 0; EmptyPulseTick(); }           // ~20x/s
        if (++wt >= 15)  { wt = 0; WeaponTick(); }               // ~4x/s
        if (++fx >= 30)  { fx = 0; ForceTilesAroundRemote(); }   // ~2x/s
        if (++sw >= 120) { sw = 0; LootSweep(); }                // ~2 s
        if (++pi >= 60)  { pi = 0; PillTick(); }                 // ~1 s, self-disarming
        if (++cf >= 30)  { cf = 0; CmdFileTick(); }              // ~2x/s
        MaskOn();
    }
}
static void HookTickFlush(void* drv) {
    void** vt = (void**)SafePtr(drv);
    if (!vt) { L("  [tf] Treiber-VTable unlesbar"); return; }
    void* cur  = SafePtr(vt + RVA::TickFlush_VT);
    void* want = (void*)(g_base + RVA::UNetDriver_TickFlush);
    L("  [tf] VTable[%zu] = %p (erwartet %p)", RVA::TickFlush_VT, cur, want);
    if (cur != want) { L("  [tf] passt nicht -> NICHT gehakt"); return; }
    g_origTickFlush = (tTickFlush)cur;
    void* mine = (void*)&MyTickFlush;
    L(Poke(vt + RVA::TickFlush_VT, &mine, 8) ? "  [tf] TickFlush gehakt" : "  [tf] VTable nicht beschreibbar");
}
// v26: SetWorld once more when the world is complete, so the NetworkObjectList
// is rebuilt from ALL actors. The mask is re-applied afterwards.
static void ReRegisterWorld(const char* when) {
    if (!g_world || !g_drv) return;
    ((void (__fastcall*)(void*, void*))(g_base + RVA::UNetDriver_SetWorld))(g_drv, g_world);
    SetWorldDriver(g_maskActive ? nullptr : g_drv);
    L("[re] %s: SetWorld erneut gerufen, World->NetDriver=%p%s", when, SafePtr((uint8_t*)g_world + OFF::UWorld_NetDriver), g_maskActive ? "  (Maske aktiv -> absichtlich null)" : "");
}
// Reporting only (rule 2): one status line every 20 s, the match phase on change.
static DWORD WINAPI WatchdogThread(LPVOID) {
    int ticks = 0;
    bool reRegistered = false;
    while (ticks < 7200) {
        Sleep(500); ++ticks;
        if (!g_world || !g_drv) continue;
        if (!reRegistered && ticks >= 10) { reRegistered = true; ReRegisterWorld("t=5s"); }
        {   // match phase, immediately on change and every 20 s
            static int prevPhase = -1;
            uint8_t ph = 0;
            void* gs = GameStateOfWorld();
            if (gs && SafeCopy((uint8_t*)gs + OFF::GameState_Phase, &ph, 1)) {
                const bool changed = ((int)ph != prevPhase);
                if (changed || (ticks % 40) == 0) {
                    L("[gs] t=%.0fs  Match-Phase = %d%s | Clients %d | Remote-Spieler %d | Zone %p (Kanal geoeffnet %ldx)",
                      ticks * 0.5, ph, changed ? "  *** GEWECHSELT ***" : "", ClientCount(), g_remoteCount, g_blueZone, g_zoneChannelOpens);
                    // v171 -perkwake: the hero perk is rolled while the pawns sit in
                    // the aircraft (phase 3) and the HUD stays at Lv.0 until the jump.
                    // Force a fresh property delta for everything the remote player
                    // owns at the moment the phase turns 3 and again at 4.
                    if (changed && g_cfg.perkWake && (ph == 3 || ph == 4) && InListenWindow()) {
                        int n = 0;
                        for (int i = 0; i < g_remoteCount; ++i) {
                            void* c  = g_remote[i].controller;
                            void* ps = c ? SafePtr((uint8_t*)c + OFF::AController_PlayerState) : nullptr;
                            if (ForceNetUpdateOn(g_remote[i].pawn)) ++n;
                            if (ForceNetUpdateOn(c)) ++n;
                            if (ForceNetUpdateOn(ps)) ++n;
                        }
                        for (int k = 0; k < g_ownedCount; ++k) if (g_ownedActors[k] && ForceNetUpdateOn(g_ownedActors[k])) ++n;
                        L("[pk] Phase %d: ForceNetUpdate fuer %d Actors der Remote-Spieler (Pawn, Controller, PlayerState, eigene Actors) -- Perk/Level-Anzeige", ph, n);
                    }
                    prevPhase = ph;
                }
            }
        }
        if ((ticks % 40) == 0) {
            static LONG prevTf = 0; const LONG tf = g_tfCount;
            L("[ls] t=%.0fs  TickFlush %ld (~%.1f/s) | Maske aktiv %d, Einblendungen %ld | RPC raus: Client %ld, Multicast %ld | GetNetMode %ld (ListenServer %ld, eigene %ld, Flieger %ld, BeginPlay %ld)"
              " | Streaming %ld (+Anker %ld) | Kacheln: Commits %ld korrigiert %ld blockierend %ld, nachgefordert %ld | AddNetworkActor %ld, geweckt DORM_Initial %ld, Dormancy abgewiesen %ld/%ld"
              " | Flush %ld (Fenster %ld), Force %ld, verweigert %ld | Spawn-Pruefungen %ld (remote %ld, gespawnt %ld, Lobby %ld) | Loot ausgeloest %ld | Zone: Kicks %ld, Toggles %ld, Pins %ld, Re-Adds %ld"
              " | Blickpunkt %ld/%ld | Schuesse %ld, Push %d, ServChanged %ld, Puls %ld, Bolt %ld | Bewegung: ServerMove %ld, Acks %ld, Korrekturen %ld | Anim-Tick %ld | Graph sah: Tueren %ld, Fenster %ld, Zone %ld, Pickups %ld | Kanaele: Tueren %ld, Fenster %ld, Pickups %ld | Tuer-Push %ld, Perk-Push %ld, Zonenkanal zu %ld | Schutz: Map %ld, Kanal %ld, Graph %ld/%ld | Origin blockiert %ld",
              ticks * 0.5, tf, (tf - prevTf) / 20.0, g_maskActive ? 1 : 0, g_maskOffCnt, g_rpcClient, g_rpcMulti,
              g_nmCalls, g_nmSubst, g_ownModeSubst, g_aircraftSubst, g_bpGateForced,
              g_streamCalls, g_streamExtra, g_commitCalls, g_commitForced, g_blockUsed, g_pulled,
              g_addActorCalls, g_dormWoken, g_dormBlocked, g_dormBlockedAdd,
              g_flushCalls, g_flushWindow, g_forceCalls, g_dormRefused,
              g_spawnCalls, g_spawnChecks, g_spawnListGrew, g_spawnLobbySkipped, g_lootFired,
              g_zoneKicks, g_zoneToggles, g_zonePinHits, g_zoneReadds, g_viewFixes, g_viewCalls,
              g_magSpent, g_magPushed, g_scSet, g_epDone, g_bfDone, g_moveIn, g_moveOut, g_moveAdjust, g_animTickFixed,
              g_gpDoors, g_gpDestr, g_gpZone, g_gpPickups, g_chDoors, g_chWindows, g_chPickups, g_doorPushes, g_perkPushes, g_zoneChanClosed,
              g_mapGuardHits, g_chanGuardHits, g_graphGuardHits, g_graphGuardSeen, g_rebaseBlocked);
            prevTf = tf;
        }
    }
    return 0;
}

// ---- UWorld::Listen replacement (v9..v33) -------------------------------------
// Creates the GameNetDriver the way the engine does, registers it at the
// world (SetWorld + LevelCollections), calls InitListen, then arms the mask.
static bool __fastcall MyListen(void* world, void* url) {
    L(""); L("=== World->Listen(URL)   World=%p  URL=%p ===", world, url);
    void* engine = g_capEngine ? *(void**)(g_capEngine + 0x80) : nullptr;
    if (!engine) { L("  [!] GEngine fehlt -> nichts getan."); return false; }
    L("  GEngine = %p", engine);
    void*   defsData = SafePtr((uint8_t*)engine + OFF::UEngine_NetDriverDefs);
    int32_t defsNum  = SafeI32((uint8_t*)engine + OFF::UEngine_NetDriverDefs + 8);
    if (!defsData || defsNum < 1) { L("  [!] keine NetDriverDefinitions"); return false; }
    FNameRaw defName{};
    if (!SafeCopy(defsData, &defName, sizeof(defName))) { L("  [!] DefName unlesbar"); return false; }
    typedef bool  (__fastcall* tCreateNamedNetDriverW)(void* engine, void* world, FNameRaw* name, FNameRaw* def);
    typedef void* (__fastcall* tGetWorldContext)(void* engine, void* world);
    typedef bool  (__fastcall* tInitListen)(void* drv, void* notify, void* url, bool reuse, FString* err);
    FNameRaw nameArg = defName, defArg = defName;
    const bool created = ((tCreateNamedNetDriverW)(g_base + RVA::CreateNamedNetDrv_World))(engine, world, &nameArg, &defArg);
    L("  CreateNamedNetDriver(UWorld*) -> %s", created ? "OK" : "FEHLER");
    void* ctx = ((tGetWorldContext)(g_base + RVA::GetWorldContextFromW))(engine, world);
    void* drv = nullptr;
    if (ctx) {
        void*   aData = SafePtr((uint8_t*)ctx + OFF::Ctx_ActiveNetDrivers);
        int32_t aNum  = SafeI32((uint8_t*)ctx + OFF::Ctx_ActiveNetDrivers + 8);
        for (int i = 0; aData && i < aNum && i < 64; ++i) {
            void* d = SafePtr((uint8_t*)aData + i * OFF::NamedNetDriverStride);
            if (!d) continue;
            FNameRaw dn{}; SafeCopy((uint8_t*)d + OFF::UNetDriver_Name, &dn, sizeof(dn));
            if (dn.Comparison == defName.Comparison) drv = d;
        }
        if (!drv && aData && aNum > 0) drv = SafePtr((uint8_t*)aData + (aNum - 1) * OFF::NamedNetDriverStride);
    }
    if (!drv) { L("  [!] kein NetDriver gefunden -> Abbruch."); return false; }
    L("  NetDriver = %p (Kontext %p)", drv, ctx);
    Poke((uint8_t*)world + OFF::UWorld_NetDriver, &drv, 8);
    ((void (__fastcall*)(void*, void*))(g_base + RVA::UNetDriver_SetWorld))(drv, world);   // registers the tick delegates
    void*   lcData = SafePtr((uint8_t*)world + OFF::UWorld_LevelCollections);
    int32_t lcNum  = SafeI32((uint8_t*)world + OFF::UWorld_LevelCollections + 8);
    for (int32_t i = 0; lcData && i < lcNum && i < 8; ++i)
        Poke((uint8_t*)lcData + (size_t)i * OFF::LevelCollectionStride + OFF::LevelCollection_NetDriver, &drv, 8);
    L("  SetWorld() gerufen, Driver->World=%p, %d LevelCollections gesetzt", SafePtr((uint8_t*)drv + OFF::UNetDriver_World), lcNum);
    if (!g_notifyOff) g_notifyOff = FindNotifyOffset(world);
    void* notify = g_notifyOff ? (void*)((uint8_t*)world + g_notifyOff) : world;
    if (!g_notifyOff) L("  [!] FNetworkNotify nicht gefunden -> nehme World (kann schiefgehen)");
    // InitListen: the driver class overrides it; find the known address in the vtable.
    void* vt = SafePtr(drv);
    if (!vt) { L("  [!] Driver-VTable unlesbar"); return false; }
    const uintptr_t cand[2] = { g_base + RVA::DriverInitListen_Override, g_base + RVA::IpNetDriver_InitListen };
    tInitListen init = nullptr;
    for (int k = 0; k < 200 && !init; ++k) {
        uintptr_t v = (uintptr_t)SafePtr((uint8_t*)vt + k * 8);
        for (uintptr_t c : cand) if (v == c) { init = (tInitListen)v; L("  InitListen gefunden: VTable-Index %d -> base+0x%llX", k, (unsigned long long)(v - g_base)); break; }
    }
    if (!init) { uintptr_t v = (uintptr_t)SafePtr((uint8_t*)vt + RVA::InitListen_VT); L("  [i] Fallback VTable+0x%llX -> %p", (unsigned long long)RVA::InitListen_VT, (void*)v); init = (tInitListen)v; }
    if (!init || !InModule((void*)init)) { L("  [!] InitListen-Ziel %p liegt NICHT im Modul -> Abbruch statt Absturz.", (void*)init); return false; }
    if (g_cfg.zoneRelevant && !g_haveZoneKey) {          // v58: the graph snapshots class settings inside InitListen
        g_haveZoneKey = FindNameKey(L"BP_BlueZone_C", &g_zoneKey);
        L("[bz] FName BP_BlueZone_C %s", g_haveZoneKey ? "bekannt (Klasse geladen)" : "noch nicht in der Namenstabelle");
    }
    FString err{ nullptr, 0, 0 };
    const bool ok = init(drv, notify, url, false, &err);
    L("  InitListen -> %s", ok ? "OK" : "FEHLER");
    if (err.Data && err.Num > 0) L("  Error: %.200ws", err.Data);
    if (!ok) return false;
    L("  *** LISTEN-SERVER AKTIV ***");
    g_world = world; g_drv = drv;
    // Arm the mask: first the notify redirect, THEN hide the driver (the other
    // order runs the first incoming connection into a null driver).
    HookNotify(world);
    if (!g_cfg.mask) L("  [nm] -nomask -> Maske aus.");
    else if (!g_origNotifyAccept || !g_origNotifyCtrl) L("  [nm] Notify-Umleitung unvollstaendig -> Maske BLEIBT AUS (zu riskant).");
    else {
        g_maskDepth = 0;
        g_maskActive = true;
        SetWorldDriver(nullptr);
        L("  [nm] *** MASKE AKTIV *** World->NetDriver und LevelCollections auf null (Spiellogik sieht NM_Standalone).");
    }
    if (!g_watchdog) g_watchdog = CreateThread(nullptr, 0, WatchdogThread, nullptr, 0, nullptr);
    if (!g_origTickFlush) HookTickFlush(drv);
    return true;
}
// Redirect the call site inside LoadMap ("call UWorld::Listen") to MyListen
// through a trampoline within +-2 GB.
static bool PatchLoadMapCall() {
    const uintptr_t site = g_base + RVA::LoadMap_CallListen;
    static const uint8_t expect[5] = { 0xE8, 0x0A, 0x15, 0xA6, 0xFC };
    auto* p = (uint8_t*)site;
    L("[i] Aufrufstelle: %02X %02X %02X %02X %02X", p[0], p[1], p[2], p[3], p[4]);
    if (memcmp(p, expect, 5) != 0) { L("[!] weicht ab -> nicht gepatcht"); return false; }
    void* tr = nullptr; SYSTEM_INFO si; GetSystemInfo(&si);
    for (uintptr_t d = si.dwAllocationGranularity; d < 0x40000000ULL && !tr; d += si.dwAllocationGranularity)
        for (int dir = 0; dir < 2 && !tr; ++dir) {
            uintptr_t a = (dir ? site + d : site - d) & ~(uintptr_t)(si.dwAllocationGranularity - 1);
            tr = VirtualAlloc((void*)a, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        }
    if (!tr) { L("[!] kein Speicher in +-2GB"); return false; }
    auto* tb = (uint8_t*)tr;
    tb[0] = 0xFF; tb[1] = 0x25; *(uint32_t*)(tb + 2) = 0;
    void* h = (void*)&MyListen; memcpy(tb + 6, &h, 8);
    int64_t rel = (int64_t)(uintptr_t)tr - (int64_t)(site + 5);
    if (rel > INT32_MAX || rel < INT32_MIN) { L("[!] rel32 zu weit"); return false; }
    DWORD old;
    if (!VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old)) return false;
    *(int32_t*)(p + 1) = (int32_t)rel;
    VirtualProtect(p, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
    L("[+] LoadMap-Aufrufstelle -> Trampolin %p -> MyListen %p", tr, h);
    return true;
}

// ============================================================================
//  == 21. Main / DllMain -- hook installation, in the v169 order
//
//  Every prologue below was read off this exact executable: position
//  independent bytes ending on an instruction boundary. InstallJmp compares
//  the first 14, the trampoline copies all of them.
// ============================================================================
static DWORD WINAPI Main(LPVOID) {
    char path[MAX_PATH]; GetModuleFileNameA(nullptr, path, MAX_PATH);
    char* s = strrchr(path, '\\'); if (s) strcpy_s(s + 1, 32, "sp_listen.log");
    g_log = _fsopen(path, "w", _SH_DENYWR);      // readable while the game runs
    HMODULE hm = GetModuleHandleA(nullptr);
    MODULEINFO mi{}; GetModuleInformation(GetCurrentProcess(), hm, &mi, sizeof(mi));
    g_base = (uintptr_t)mi.lpBaseOfDll; g_end = g_base + mi.SizeOfImage;
    L("[+] sp_listen v174 (v173 + [cs]-Callspace-Diagnose je RPC, Kanalzaehler je Klasse, DoorState-Sonde; v173 = Animations-Tick der Remote-Pawns, Zone vor dem ersten Beitritt gesucht, Korrektur-Zaehler; v172 = Perk-Push nach den Perk-RPCs, Tuer/Fenster-Push, Zonen-Policy-Test, Zonenkanal-Schliess-Diagnose; v171 = Perk-Weckruf beim Phasenwechsel, Bewegungszaehler, -rpcnames; v170 = saubere Neuordnung von v169: gleiche Hooks, gleiche RVAs, gleiche Vorgaben; Messcode und abgeschaltete Experimente entfernt -- siehe SP_LISTEN_NOTES.md) base=%p pid=%lu t0=%llu",
      (void*)g_base, (unsigned long)GetCurrentProcessId(), (unsigned long long)time(nullptr));
    ParseCommandLine();

    // crash reporter first, so nothing below can fault unseen
    {
        HMODULE self = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&MyVeh, &self);
        g_selfBase = (uintptr_t)self;
        g_selfEnd  = ImageEndOf(g_selfBase);
        g_exeEnd   = ImageEndOf(g_base);
        if (g_cfg.crashLog) {
            g_vehHandle = AddVectoredExceptionHandler(1, &MyVeh);
            L(g_vehHandle ? "[ax] Absturz-Melder aktiv -- jede Zugriffsverletzung landet MIT Aufrufkette in dieser Datei" : "[ax] [!] Absturz-Melder konnte nicht installiert werden");
        }
    }
    {   // the analysis behind the mask must hold for this build
        const uint8_t* p = (const uint8_t*)(g_base + RVA::NetModeFoldProof);
        L("[i] NetMode-Faltung @base+0x%llX: %02X %02X -> %s", (unsigned long long)RVA::NetModeFoldProof, p[0], p[1],
          (p[0] == 0xB8 && p[1] == 0x03) ? "bestaetigt (NM_Client konstant)" : "ABWEICHUNG - Maske mit Vorsicht");
    }
    SkipOwnershipCheck();
    static const uint8_t lmProlog[14] = { 0x48,0x8B,0xC4, 0x48,0x89,0x58,0x20, 0x55, 0x56, 0x57, 0x41,0x54, 0x41,0x55 };
    g_capEngine = MakeCapture(g_base + RVA::LoadMap, lmProlog, 14);
    L(g_capEngine ? "[+] GEngine-Abgriff an LoadMap aktiv" : "[!] GEngine-Abgriff FEHLGESCHLAGEN");
    ForceIpSockets();
    if (g_cfg.ignoreEncToken) IgnoreEncryptionToken(); else L("[et] -noenctoken -> Lobby-Joins werden abgewiesen.");
    if (g_cfg.payPatch) { PatchPayResult("CurrencyPay", RVA::CurrencyPay_Jne); PatchPayResult("MaterialPay", RVA::MaterialPay_Jne); }
    else L("[cp] -nopay -> CurrencyPay/MaterialPay unveraendert.");

    // InitNewPlayer: loadout injection and the gold start value
    if (g_cfg.loadout || g_cfg.goldGain) {
        if (g_cfg.loadout) {
            GetModuleFileNameW((HMODULE)g_selfBase, g_loadoutPath, MAX_PATH);
            wchar_t* bs = wcsrchr(g_loadoutPath, L'\\');
            if (bs) wcscpy_s(bs + 1, 32, L"loadouts.txt"); else g_loadoutPath[0] = 0;
        }
        static const uint8_t inpProlog[14] = { 0x48,0x89,0x5C,0x24,0x18, 0x48,0x89,0x74,0x24,0x20, 0x55, 0x57, 0x41,0x54 };
        g_origInitNewPlayer = (tInitNewPlayer)HookFunction("ABattleRoyaleGameMode::InitNewPlayer", RVA::GM_InitNewPlayer, (void*)&MyInitNewPlayer, inpProlog, sizeof(inpProlog));
        if (g_origInitNewPlayer) L(g_cfg.loadout ? "[+] [lo] Loadout-Hook aktiv, Datei: %ls" : "[+] [lo] InitNewPlayer-Hook aktiv (nur Gold-Verfolgung) %ls", g_loadoutPath[0] ? g_loadoutPath : L"");
        else L("[!] [lo] InitNewPlayer-Hook FEHLGESCHLAGEN -> Loadout/Gold-Start werden NICHT erfasst");
    }
    // dormancy block
    if (g_cfg.dormBlock) {
        static const uint8_t prolog[15] = { 0x48,0x89,0x5C,0x24,0x18, 0x56, 0x48,0x83,0xEC,0x30, 0x8B,0xF2, 0x48,0x8B,0xD9 };
        g_origSetNetDormancy = (tSetNetDormancy)HookFunction("AActor::SetNetDormancy", RVA::AActor_SetNetDormancy, (void*)&MySetNetDormancy, prolog, sizeof(prolog));
        if (g_origSetNetDormancy) L("[+] [dz] Dormancy-Sperre aktiv: DormantAll/DormantPartial wird fuer %s abgewiesen", g_cfg.dormBlockLevel ? "ALLE Actors" : "dynamische Actors");
        else { L("[!] [dz] SetNetDormancy-Hook fehlgeschlagen -- Dormancy-Sperre AUS"); g_cfg.dormBlock = false; }
    } else L("[dz] -nodormblock -> Dormancy-Sperre aus.");
    // zone row pick
    if (g_cfg.zoneScale || g_cfg.zonePlayers || g_cfg.zoneId >= 0) {
        static const uint8_t prolog[14] = { 0x48,0x89,0x5C,0x24,0x18, 0x55, 0x56, 0x57, 0x41,0x54, 0x41,0x55, 0x41,0x56 };
        g_origInitTableSetting = (tInitTableSetting)HookFunction("ABattleRoyaleGameMode::InitTableSetting", RVA::GM_InitTableSetting, (void*)&MyInitTableSetting, prolog, sizeof(prolog));
        L(g_origInitTableSetting ? "[+] [bz] Zonenwahl aktiv: %s" : "[!] [bz] InitTableSetting-Hook fehlgeschlagen -> Zone bleibt Zufall %s",
          g_cfg.zoneId >= 0 ? "feste Zeile (-zoneid)" : "Zufall innerhalb der Spielerzahl-Tabelle");
    } else L("[bz] -zoneplayers=0 -> die Zone bleibt die Zufallswahl des Spiels ueber alle 156 Zeilen.");
    // gold
    if (g_cfg.goldGain) {
        static const uint8_t sgProlog[16] = { 0x40,0x53, 0x48,0x83,0xEC,0x20, 0x48,0x8B,0x81,0xE0,0x05,0x00,0x00, 0x48,0x8B,0xD9 };
        static const uint8_t prProlog[16] = { 0x40,0x55, 0x53, 0x56, 0x57, 0x41,0x55, 0x41,0x56, 0x41,0x57, 0x48,0x8D,0x6C,0x24,0xD9 };
        static const uint8_t loProlog[14] = { 0x48,0x89,0x5C,0x24,0x18, 0x55, 0x56, 0x57, 0x41,0x54, 0x41,0x55, 0x41,0x56 };
        g_origSetGold   = (tSetGold)  MakeTrampoline(g_base + RVA::PS_SetGold,   sgProlog, sizeof(sgProlog));
        g_origPayResult = (tPayResult)MakeTrampoline(g_base + RVA::PC_PayResult, prProlog, sizeof(prProlog));
        g_origLogout    = (tLogout)   MakeTrampoline(g_base + RVA::GM_Logout,    loProlog, sizeof(loProlog));
        if (!g_origSetGold || !g_origPayResult || !g_origLogout) {
            L("[!] [gg] Trampolin FEHLGESCHLAGEN (SetGold %p, PayResult %p, Logout %p) -> Beute wird NICHT verbucht", (void*)g_origSetGold, (void*)g_origPayResult, (void*)g_origLogout);
            g_origSetGold = nullptr; g_origPayResult = nullptr; g_origLogout = nullptr; g_cfg.goldGain = false;
        } else {
            const bool ok = InstallJmp("PlayerState::SetGold", RVA::PS_SetGold, (void*)&MySetGold, sgProlog)
                         && InstallJmp("PlayerController::OnPayResult", RVA::PC_PayResult, (void*)&MyPayResult, prProlog)
                         && InstallJmp("ABattleRoyaleGameMode::Logout", RVA::GM_Logout, (void*)&MyLogout, loProlog);
            L(ok ? "[+] [gg] Beute-Abrechnung aktiv: Start bei InitNewPlayer, Ende bei Logout" : "[!] [gg] Hook unvollstaendig -- Beute wird evtl. NICHT verbucht");
        }
    } else L("[gg] -nogoldgain -> Beute aus der Runde wird nicht verbucht.");
    if (g_cfg.levelVis) ForceLevelReferences(); else L("[lv] -nolevelvis -> Sublevel-Referenzen werden weiter durch None ersetzt.");
    // FindPlayerStart
    {
        static const uint8_t prolog[14] = { 0x48,0x89,0x5C,0x24,0x20, 0x55, 0x56, 0x57, 0x41,0x54, 0x41,0x55, 0x41,0x56 };
        g_origFindPlayerStart = (tFindPlayerStart)HookFunction("FindPlayerStart", RVA::FindPlayerStart, (void*)&MyFindPlayerStart, prolog, sizeof(prolog));
        if (!g_origFindPlayerStart) L("[!] FindPlayerStart-Hook FEHLGESCHLAGEN -> Nachzuegler koennen im Meer landen");
    }
    // actor net mode
    if (g_cfg.actorMode) {
        static const uint8_t prolog[16] = { 0x48,0x89,0x5C,0x24,0x10, 0x57, 0x48,0x83,0xEC,0x30, 0x48,0x8B,0x01, 0x48,0x8B,0xD9 };
        g_origGetNetMode = (tGetNetMode)HookFunction("AActor::GetNetMode", RVA::AActor_GetNetMode, (void*)&MyActorGetNetMode, prolog, sizeof(prolog));
        if (!g_origGetNetMode) { L("[!] AActor::GetNetMode-Hook FEHLGESCHLAGEN -> Actor-Netzmodus bleibt Standalone"); g_cfg.actorMode = false; }
    }
    L("[i] Konfiguration: Actor-Netzmodus %s, RPC-Weiche %s",
      g_cfg.actorMode ? "NM_ListenServer fuer AutonomousProxy und eigene Actors der Remote-Spieler" : "durchgehend NM_Standalone (-noactormode)",
      g_cfg.rpcFix ? "AN (Client-/Multicast-RPCs gehen zusaetzlich raus)" : "AUS");
    // PossessedBy
    {
        static const uint8_t prolog[16] = { 0x48,0x89,0x5C,0x24,0x08, 0x48,0x89,0x6C,0x24,0x10, 0x48,0x89,0x74,0x24,0x18, 0x57 };
        g_origPossessedBy = (tPossessedBy)HookFunction("APawn::PossessedBy", RVA::APawn_PossessedBy, (void*)&MyPossessedBy, prolog, sizeof(prolog));
        if (!g_origPossessedBy) L("[!] APawn::PossessedBy-Hook FEHLGESCHLAGEN -> keine Remote-Spieler-Registrierung!");
    }
    // ProcessEvent: shot accounting, gold result, [pe] log
    {
        static const uint8_t prolog[15] = { 0x48,0x89,0x5C,0x24,0x10, 0x48,0x89,0x6C,0x24,0x18, 0x57, 0x48,0x83,0xEC,0x20 };
        g_origProcessEvent = (tPE)HookFunction("AActor::ProcessEvent", RVA::AActor_ProcessEvent, (void*)&MyProcessEvent, prolog, sizeof(prolog));
        L(g_origProcessEvent ? "[pe] ProcessEvent gehakt (Schusszaehlung, Rundenergebnis%s)" : "[!] [pe] ProcessEvent-Hook FEHLGESCHLAGEN -> Magazin wird nicht mitgezaehlt",
          g_cfg.peLog ? ", [pe]/[rr]-Zeilen" : "");
    }
    // replay recorder off
    if (g_cfg.blockReplay) {
        static const uint8_t prolog[14] = { 0x48,0x89,0x5C,0x24,0x08, 0x48,0x89,0x74,0x24,0x10, 0x48,0x89,0x7C,0x24 };
        InstallJmp("UGameInstance::StartRecordingReplay", RVA::GameInstance_StartRecordingReplay, (void*)&MyStartRecordingReplay, prolog);
    } else L("[rp] -allowreplay -> Replay-Recorder bleibt aktiv (kann in DeltaSerializeFastArrayProperty crashen).");
    // origin pin
    if (g_cfg.noRebase) {
        static const uint8_t prolog[14] = { 0x48,0x8B,0xC4, 0x48,0x89,0x58,0x18, 0x55, 0x56, 0x57, 0x41,0x54, 0x41,0x55 };
        if (InstallJmp("UWorld::SetNewWorldOrigin", RVA::UWorld_SetNewWorldOrigin, (void*)&MySetNewWorldOrigin, prolog))
            L("[or] Welt-Ursprung bleibt bei {0,0,0} -> Serverkoordinaten == absolute Koordinaten");
        else L("[!] [or] Origin-Hook fehlgeschlagen -> Rebasing bleibt aktiv (Vault-Teleport moeglich)");
    } else L("[or] -allowrebase -> Origin-Rebasing bleibt aktiv.");
    // streaming
    if (g_cfg.streamRemote) {
        static const uint8_t prolog[15] = { 0x48,0x8B,0xC4, 0x48,0x89,0x58,0x18, 0x44,0x89,0x48,0x20, 0x48,0x89,0x50,0x10 };
        g_origUpdStream = (tUpdStream)HookFunction("UWorldComposition::UpdateStreamingState", RVA::WorldComp_UpdateStreamingState, (void*)&MyUpdateStreamingState, prolog, sizeof(prolog));
        if (g_origUpdStream) L("[ws] Host laedt die Welt-Kacheln zusaetzlich um jeden Remote-Spieler");
        else { L("[!] [ws] Streaming-Hook FEHLGESCHLAGEN -> Host streamt nur um sich selbst"); g_cfg.streamRemote = false; }
    } else L("[ws] -nostreamremote -> Host streamt nur um sich selbst.");
    // AddNetworkActor (zone + dormancy); installed as in v169 only with zoneRelevant
    if (g_cfg.zoneRelevant) {
        static const uint8_t prolog[16] = { 0x48,0x89,0x5C,0x24,0x08, 0x57, 0x48,0x83,0xEC,0x20, 0x48,0x8B,0xDA, 0x48,0x8B,0xF9 };
        g_origAddNetActor = (tAddNetActor)HookFunction("UNetDriver::AddNetworkActor", RVA::NetDriver_AddNetworkActor, (void*)&MyAddNetworkActor, prolog, sizeof(prolog));
        L(g_origAddNetActor ? "[bz] AddNetworkActor gehakt: Zone bAlwaysRelevant, DORM_Initial-Level-Actors und schlafende Actors werden wach gemacht"
                            : "[!] [bz] AddNetworkActor-Hook FEHLGESCHLAGEN");
    }
    // loot around remote pawns
    if (g_cfg.spawnRemote) {
        static const uint8_t fpcBytes[14] = { 0x83,0xB9,0x78,0x02,0x00,0x00,0x00, 0x7E,0x0C, 0x48,0x8B,0x89,0x70,0x02 };
        g_firstPcHooked = InstallJmp("UWorld::GetFirstPlayerController", RVA::World_GetFirstPlayerController, (void*)&MyGetFirstPlayerController, fpcBytes);
        static const uint8_t bsProlog[15] = { 0x48,0x8B,0xC4, 0x48,0x89,0x58,0x08, 0x48,0x89,0x70,0x10, 0x48,0x89,0x78,0x18 };
        g_origBuildingSpawn = (tCheckSpawn)HookFunction("ABravoHotelBuilding::CheckSpawnByStandalone", RVA::Building_CheckSpawn, (void*)&MyBuildingCheckSpawn, bsProlog, sizeof(bsProlog));
        static const uint8_t vsProlog[16] = { 0x48,0x89,0x74,0x24,0x18, 0x57, 0x48,0x83,0xEC,0x40, 0x48,0x8B,0x01, 0x48,0x8B,0xF9 };
        g_origVehicleSpawn = (tCheckSpawn)HookFunction("ABravoHotelVehicleSpawnActor::CheckSpawnByStandalone", RVA::Vehicle_CheckSpawn, (void*)&MyVehicleCheckSpawn, vsProlog, sizeof(vsProlog));
        L("[ls] Loot-Spawn um Remote-Spieler: GetFirstPlayerController %s, Gebaeude %s, Fahrzeuge %s",
          g_firstPcHooked ? "OK" : "FEHLT", g_origBuildingSpawn ? "OK" : "FEHLT", g_origVehicleSpawn ? "OK" : "FEHLT");
    } else L("[ls] -nospawnremote -> Loot-Spawn-Pruefung nur fuer den Host.");
    // tile commit
    {
        static const uint8_t prolog[22] = { 0x48,0x89,0x5C,0x24,0x08, 0x48,0x89,0x6C,0x24,0x10, 0x48,0x89,0x74,0x24,0x18, 0x48,0x89,0x7C,0x24,0x20, 0x41,0x54 };
        g_origCommitTile = (tCommitTile)HookFunction("UWorldComposition::CommitTileStreamingState", RVA::WorldComp_CommitTile, (void*)&MyCommitTile, prolog, sizeof(prolog));
        L(g_origCommitTile ? "[ct] Kachel-Entscheidungen der Engine werden fuer den Remote-Spieler korrigiert" : "[!] [ct] CommitTileStreamingState-Hook FEHLGESCHLAGEN");
    }
    // channel open (zone reference toggle trigger)
    {
        static const uint8_t prolog[16] = { 0x48,0x89,0x5C,0x24,0x18, 0x55, 0x56, 0x57, 0x41,0x54, 0x41,0x55, 0x41,0x56, 0x41,0x57 };
        g_origSetChannelActor = (tSetChannelActor)HookFunction("UActorChannel::SetChannelActor", RVA::ActorChannel_SetChannelActor, (void*)&MySetChannelActor, prolog, sizeof(prolog));
        if (!g_origSetChannelActor) L("[!] [zc] SetChannelActor-Hook FEHLGESCHLAGEN -> der Zonen-Toggle hat keinen Ausloeser");
    }
    // viewer
    {
        static const uint8_t prolog[15] = { 0x48,0x89,0x5C,0x24,0x08, 0x48,0x89,0x74,0x24,0x10, 0x57, 0x48,0x83,0xEC,0x40 };
        g_origNetViewer = (tNetViewerCtor)HookFunction("FNetViewer::FNetViewer", RVA::FNetViewer_Ctor, (void*)&MyNetViewerCtor, prolog, sizeof(prolog));
        L(g_origNetViewer ? (g_cfg.viewFix ? "[vw] Blickpunkt der Remote-Spieler wird auf den Pawn korrigiert" : "[vw] Blickpunkt wird nur gemessen (-noviewfix)") : "[!] [vw] FNetViewer-Hook FEHLGESCHLAGEN");
    }
    // guards
    if (g_cfg.mapGuard) {
        static const uint8_t prolog[16] = { 0x48,0x89,0x5C,0x24,0x08, 0x48,0x89,0x7C,0x24,0x10, 0x8B,0x41,0x08, 0x49,0x8B,0xD8 };
        g_origMapFind = (tMapFind)HookFunction("Null-Map-Schutz (0x11E8200)", RVA::MapFind_Guarded, (void*)&MyMapFind, prolog, sizeof(prolog));
        if (!g_origMapFind) { g_cfg.mapGuard = false; L("[mg] [!] Haken fehlgeschlagen -- Schutz AUS"); } else L("[mg] Null-Map-Schutz aktiv");
    } else L("[mg] -nomapguard -> der 0x40-Absturz bleibt scharf");
    if (g_cfg.chanGuard) {
        static const uint8_t entry[14] = { 0x48,0x85,0xD2, 0x0F,0x84,0x84,0x01,0x00,0x00, 0x48,0x89,0x5C,0x24,0x08 };
        static const uint8_t tail[10]  = { 0x48,0x89,0x5C,0x24,0x08, 0x48,0x89,0x6C,0x24,0x10 };
        g_origActorChanPrep = (tActorChanPrep)MakeTrampoline(g_base + RVA::ActorChanPrep_Body, tail, sizeof(tail));   // built after the rel32 jump
        if (g_origActorChanPrep && !InstallJmp("Kanal-Schutz (0x3F81A20)", RVA::ActorChanPrep, (void*)&MyActorChanPrep, entry)) g_origActorChanPrep = nullptr;
        if (!g_origActorChanPrep) { g_cfg.chanGuard = false; L("[cg] [!] Haken fehlgeschlagen -- Schutz AUS"); } else L("[cg] Kanal-Schutz aktiv");
    } else L("[cg] -nochanguard -> der Kanal-Absturz bleibt scharf");
    if (g_cfg.graphGuard) {
        static const uint8_t body[18] = { 0x40,0x55, 0x53, 0x56, 0x57, 0x41,0x54, 0x41,0x55, 0x41,0x56, 0x41,0x57, 0x48,0x8D,0x6C,0x24,0xF9 };
        g_origGraphPerActor = (tGraphPerActor)HookFunction("Graph-Actor-Schutz (0x13C3DB0)", RVA::GraphPerActor, (void*)&MyGraphPerActor, body, sizeof(body));
        if (!g_origGraphPerActor) { g_cfg.graphGuard = false; L("[gd] [!] Haken fehlgeschlagen -- Schutz AUS"); } else L("[gd] Graph-Actor-Schutz aktiv");
    } else L("[gd] -nographguard -> der Absturz in der Actor-Schleife bleibt scharf");
    // pills
    if (g_cfg.pills) {
        static const uint8_t buffRowProlog[14]  = { 0x48,0x89,0x5C,0x24,0x10, 0x48,0x89,0x6C,0x24,0x18, 0x56, 0x57, 0x41,0x56 };
        static const uint8_t buffIdxProlog[15]  = { 0x48,0x89,0x5C,0x24,0x08, 0x48,0x89,0x6C,0x24,0x10, 0x48,0x89,0x74,0x24,0x18 };
        static const uint8_t spawnRowProlog[14] = { 0x48,0x89,0x5C,0x24,0x10, 0x48,0x89,0x6C,0x24,0x18, 0x56, 0x57, 0x41,0x56 };
        g_origFindBuffRow   = (tFindRowTyped)HookFunction("Pillen: FindBuffRow",   PILL::FindBuffRow,   (void*)&MyFindBuffRow,   buffRowProlog,  sizeof(buffRowProlog));
        g_origFindBuffIndex = (tFindIndex)   HookFunction("Pillen: FindBuffIndex", PILL::FindBuffIndex, (void*)&MyFindBuffIndex, buffIdxProlog,  sizeof(buffIdxProlog));
        g_origFindSpawnRow  = (tFindRowTyped)HookFunction("Pillen: FindSpawnRow",  PILL::FindSpawnRow,  (void*)&MyFindSpawnRow,  spawnRowProlog, sizeof(spawnRowProlog));
        L("[pill] Haken gesetzt: FindBuffRow %s, FindBuffIndex %s, FindSpawnRow %s", g_origFindBuffRow ? "OK" : "FEHLT", g_origFindBuffIndex ? "OK" : "FEHLT", g_origFindSpawnRow ? "OK" : "FEHLT");
        if (!g_origFindBuffRow || !g_origFindBuffIndex) { g_cfg.pills = false; L("[pill] [!] ohne beide Buff-Haken waere nur die halbe Kette da -- Pillen-Code AUS"); }
    } else L("[pill] -nopills -> Pillen bleiben im Auslieferungszustand");
    // dormancy wake-ups through the mask
    if (g_cfg.dormFix) {
        static const uint8_t fndProlog[17] = { 0x40,0x53, 0x48,0x83,0xEC,0x20, 0x48,0x8B,0xD9, 0x33,0xD2, 0x8B,0x89,0xC0,0x01,0x00,0x00 };
        static const uint8_t fnuProlog[16] = { 0x40,0x53, 0x48,0x83,0xEC,0x20, 0x80,0xB9,0x0F,0x02,0x00,0x00,0x03, 0x48,0x8B,0xD9 };
        g_origFlushNetDormancy = (tActorVoid)HookFunction("AActor::FlushNetDormancy", RVA::AActor_FlushNetDormancy, (void*)&MyFlushNetDormancy, fndProlog, sizeof(fndProlog));
        g_origForceNetUpdate   = (tActorVoid)HookFunction("AActor::ForceNetUpdate",   RVA::AActor_ForceNetUpdate,   (void*)&MyForceNetUpdate,   fnuProlog, sizeof(fnuProlog));
        L("[dm] Dormancy-Weckrufe durch die Maske: FlushNetDormancy %s, ForceNetUpdate %s", g_origFlushNetDormancy ? "OK" : "FEHLT", g_origForceNetUpdate ? "OK" : "FEHLT");
    } else L("[dm] -nodormfix -> FlushNetDormancy/ForceNetUpdate bleiben unter der Maske wirkungslos.");
    // RPC switch
    {
        static const uint8_t prolog[19] = { 0x48,0x89,0x5C,0x24,0x18, 0x55, 0x56, 0x57, 0x41,0x56, 0x41,0x57, 0x48,0x81,0xEC,0x90,0x00,0x00,0x00 };
        g_origCallspace = (tCallspace)HookFunction("GetFunctionCallspace", RVA::GetFunctionCallspace, (void*)&MyGetFunctionCallspace, prolog, sizeof(prolog));
        if (!g_origCallspace) L("[!] GetFunctionCallspace-Hook FEHLGESCHLAGEN -> RPC-Weiche aus");
    }
    PatchLoadMapCall();
    L("[+] bereit.");
    return 0;
}
BOOL WINAPI DllMain(HINSTANCE hm, DWORD r, LPVOID) {
    if (r == DLL_PROCESS_ATTACH) { DisableThreadLibraryCalls(hm); CreateThread(nullptr, 0, Main, nullptr, 0, nullptr); }
    return TRUE;
}
