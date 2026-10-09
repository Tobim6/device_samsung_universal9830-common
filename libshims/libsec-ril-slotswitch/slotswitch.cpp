// Ported to z3s from exy2100/android_device_samsung_universal2100-common#5 (Apache-2.0).
#include <dlfcn.h>
#include <stddef.h>
#include <sys/system_properties.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <log/log.h>

#undef LOG_TAG
#define LOG_TAG "sec-ril-slotswitch"

namespace {

constexpr char kRealLib[] = "/vendor/lib64/libsec-ril-impl.so";
constexpr char kDoSetSlotMapping[] = "_ZN10SimManager16DoSetSlotMappingEP7Request";
constexpr char kExecuteSlotSwitch[] = "_ZN10SimManager17ExecuteSlotSwitchEP7Requesti";
constexpr char kSimManagerInstances[] = "_ZN10SimManager9mInstanceE";
constexpr int kEsimSlot = 1;

constexpr size_t kRequestDataOffset = 0x38;
constexpr size_t kSlotMapCountOffset = 0x0c;
constexpr size_t kSlotMapEntriesOffset = 0x10;
constexpr size_t kSimManagerSlotOffset = 0x14;
constexpr size_t kSimManagerTargetOffset = 0x1668;

constexpr int kTargetPsim = 1;
constexpr int kTargetEsim = 2;
constexpr int kActionStartSwitch = 0x21;

using DoSetSlotMappingFn = int (*)(void*, void*);
using ExecuteSlotSwitchFn = void (*)(void*, void*, int);

DoSetSlotMappingFn gRealDoSetSlotMapping = nullptr;
ExecuteSlotSwitchFn gExecuteSlotSwitch = nullptr;
void** gSimManagers = nullptr;

bool resolveReal() {
    if (gRealDoSetSlotMapping != nullptr && gExecuteSlotSwitch != nullptr) {
        return true;
    }
    void* handle = dlopen(kRealLib, RTLD_NOW | RTLD_NOLOAD);
    if (handle == nullptr) {
        ALOGE("dlopen(%s, RTLD_NOLOAD) failed: %s", kRealLib, dlerror());
        return false;
    }
    gRealDoSetSlotMapping =
            reinterpret_cast<DoSetSlotMappingFn>(dlsym(handle, kDoSetSlotMapping));
    gExecuteSlotSwitch = reinterpret_cast<ExecuteSlotSwitchFn>(dlsym(handle, kExecuteSlotSwitch));
    gSimManagers = reinterpret_cast<void**>(dlsym(handle, kSimManagerInstances));
    if (gRealDoSetSlotMapping == nullptr || gExecuteSlotSwitch == nullptr) {
        ALOGE("dlsym failed (DoSetSlotMapping=%p ExecuteSlotSwitch=%p)",
              reinterpret_cast<void*>(gRealDoSetSlotMapping),
              reinterpret_cast<void*>(gExecuteSlotSwitch));
        return false;
    }
    return true;
}

int getPropInt(const char* name, int def) {
    char value[PROP_VALUE_MAX] = {};
    if (__system_property_get(name, value) <= 0) {
        return def;
    }
    char* end = nullptr;
    long parsed = strtol(value, &end, 10);
    return end == value ? def : static_cast<int>(parsed);
}

bool isTsds2() {
    char value[PROP_VALUE_MAX] = {};
    if (__system_property_get("persist.radio.esim.slotswitch", value) <= 0) {
        return false;
    }
    return strcmp(value, "tsds2") == 0;
}

template <typename T>
T* at(void* base, size_t offset) {
    return reinterpret_cast<T*>(static_cast<uint8_t*>(base) + offset);
}

int getSwitchTarget(void* slotMap) {
    const int count = *at<int>(slotMap, kSlotMapCountOffset);
    const int* map = at<int>(slotMap, kSlotMapEntriesOffset);

    ALOGI("DoSetSlotMapping: count %d", count);
    for (int i = 0; i < count && i < 4; i++) {
        ALOGI("  logical %d -> phy %d port %d", i, map[2 * i], map[2 * i + 1]);
    }

    if (count != 2 || map[0] != 0 || map[1] != 0 || map[3] != 0) {
        return 0;
    }

    const bool esimActive =
            getPropInt("ril.simslottype1", 0) == 1 || getPropInt("ril.simslottype2", 0) == 1;
    if (!esimActive && (map[2] == 2 || map[2] == 1)) {
        return kTargetEsim;
    }
    if (esimActive && map[2] == 1) {
        return kTargetPsim;
    }
    return 0;
}

}  // namespace

extern "C" __attribute__((visibility("default"))) int _ZN10SimManager16DoSetSlotMappingEP7Request(
        void* self, void* request) {
    if (!resolveReal()) {
        abort();
    }

    void* slotMap = request != nullptr ? *at<void*>(request, kRequestDataOffset) : nullptr;
    if (slotMap == nullptr || !isTsds2()) {
        return gRealDoSetSlotMapping(self, request);
    }

    const int target = getSwitchTarget(slotMap);
    if (target == 0) {
        return gRealDoSetSlotMapping(self, request);
    }

    ALOGI("tsds2 slot switch to %s (SimManager slot %d, simslottype %d/%d)",
          target == kTargetEsim ? "eSIM" : "pSIM", *at<int>(self, kSimManagerSlotOffset),
          getPropInt("ril.simslottype1", 0), getPropInt("ril.simslottype2", 0));

    void* mgr = (gSimManagers != nullptr) ? gSimManagers[kEsimSlot] : nullptr;
    if (mgr == nullptr) {
        ALOGE("SimManager instance for slot %d not found (table %p); refusing switch",
              kEsimSlot, reinterpret_cast<void*>(gSimManagers));
        return gRealDoSetSlotMapping(self, request);
    }
    // z3s guard (not in the S21 original): offset 0x14 must read slot ids 0 and 1.
    void* mgr0 = gSimManagers[0];
    if (mgr0 == nullptr || *at<int>(mgr0, kSimManagerSlotOffset) != 0 ||
        *at<int>(mgr, kSimManagerSlotOffset) != kEsimSlot) {
        ALOGE("SimManager slot ids at +0x%zx are %d/%d, expected 0/%d; offsets do not match this "
              "RIL, forwarding to the real DoSetSlotMapping",
              kSimManagerSlotOffset, mgr0 != nullptr ? *at<int>(mgr0, kSimManagerSlotOffset) : -1,
              *at<int>(mgr, kSimManagerSlotOffset), kEsimSlot);
        return gRealDoSetSlotMapping(self, request);
    }
    ALOGI("using SimManager %p (slot field %d) instead of caller %p (slot field %d)", mgr,
          *at<int>(mgr, kSimManagerSlotOffset), self, *at<int>(self, kSimManagerSlotOffset));
    *at<int>(mgr, kSimManagerTargetOffset) = target;
    gExecuteSlotSwitch(mgr, request, kActionStartSwitch);
    return 0;
}
