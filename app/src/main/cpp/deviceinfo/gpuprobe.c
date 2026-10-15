#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <jni.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <drm/drm.h>
#include <drm/amdgpu_drm.h>
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#define LINE(...) do { if (used < sizeof(out)) used += snprintf(out + used, sizeof(out) - used, __VA_ARGS__); } while (0)

// <drm/drm.h>'s struct drm_version, spelled out so no libdrm headers are needed.
struct dd_drm_version {
    int version_major, version_minor, version_patchlevel;
    size_t name_len; char *name;
    size_t date_len; char *date;
    size_t desc_len; char *desc;
};
#define DD_DRM_IOCTL_VERSION _IOWR('d', 0x00, struct dd_drm_version)

JNIEXPORT jstring JNICALL
Java_com_droiddeck_launcher_gpu_GpuProbe_nativeDrmVersion(JNIEnv *env, jclass clazz, jstring jpath) {
    static char out[1024];
    size_t used = 0;
    out[0] = 0;
    const char *path = (*env)->GetStringUTFChars(env, jpath, NULL);
    int fd = open(path, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        LINE("open=failed (%s)", strerror(errno));
        int ro = open(path, O_RDONLY | O_CLOEXEC);
        if (ro >= 0) { LINE(", read-only open works"); close(ro); }
    } else {
        char name[64] = {0}, date[64] = {0}, desc[256] = {0};
        struct dd_drm_version v = { .name_len = sizeof(name) - 1, .name = name,
                                    .date_len = sizeof(date) - 1, .date = date,
                                    .desc_len = sizeof(desc) - 1, .desc = desc };
        if (ioctl(fd, DD_DRM_IOCTL_VERSION, &v) == 0) {
            LINE("open=ok, driver=%s %d.%d.%d (%s, %s)", name, v.version_major, v.version_minor,
                 v.version_patchlevel, desc, date);
        } else {
            LINE("open=ok, DRM_IOCTL_VERSION failed (%s)", strerror(errno));
        }
        close(fd);
    }
    (*env)->ReleaseStringUTFChars(env, jpath, path);
    return (*env)->NewStringUTF(env, out);
}

static int amdgpu_query(int fd, uint32_t query, void *out, uint32_t size, uint32_t ip_type) {
    struct drm_amdgpu_info req;
    memset(&req, 0, sizeof(req));
    memset(out, 0, size);
    req.return_pointer = (uint64_t) (uintptr_t) out;
    req.return_size = size;
    req.query = query;
    req.query_hw_ip.type = ip_type;
    return ioctl(fd, DRM_IOCTL_AMDGPU_INFO, &req);
}

JNIEXPORT jstring JNICALL
Java_com_droiddeck_launcher_gpu_GpuProbe_nativeAmdgpuInfo(JNIEnv *env, jclass clazz, jstring jpath) {
    static char out[4096];
    size_t used = 0;
    out[0] = 0;
    const char *path = (*env)->GetStringUTFChars(env, jpath, NULL);
    int fd = open(path, O_RDWR | O_CLOEXEC);
    (*env)->ReleaseStringUTFChars(env, jpath, path);
    if (fd < 0) { LINE("error=open failed (%s)\n", strerror(errno)); return (*env)->NewStringUTF(env, out); }

    struct drm_amdgpu_info_device dev;
    if (amdgpu_query(fd, AMDGPU_INFO_DEV_INFO, &dev, sizeof(dev), 0) == 0) {
        LINE("device_id=0x%04x\nchip_rev=%u\nexternal_rev=%u (0x%x)\npci_rev=%u\nfamily_id=%u (0x%x)\n",
             dev.device_id, dev.chip_rev, dev.external_rev, dev.external_rev, dev.pci_rev, dev.family, dev.family);
        LINE("shader_engines=%u\nshader_arrays_per_engine=%u\ncu_active=%u\ncu_per_sh=%u\nrb_pipes=%u\n",
             dev.num_shader_engines, dev.num_shader_arrays_per_engine, dev.cu_active_number, dev.num_cu_per_sh,
             dev.num_rb_pipes);
        LINE("wave_front_size=%u\nmax_engine_clock_khz=%" PRIu64 "\nvram_type=%u\nvram_bit_width=%u\n",
             dev.wave_front_size, (uint64_t) dev.max_engine_clock, dev.vram_type, dev.vram_bit_width);
        LINE("ids_flags=0x%" PRIx64 "\nva_offset=0x%" PRIx64 "\nva_max=0x%" PRIx64 "\ngart_page_size=%u\npte_fragment_size=%u\n",
             (uint64_t) dev.ids_flags, (uint64_t) dev.virtual_address_offset, (uint64_t) dev.virtual_address_max,
             dev.gart_page_size, dev.pte_fragment_size);
    } else {
        LINE("dev_info=failed (%s)\n", strerror(errno));
    }

    static const struct { uint32_t type; const char *name; } ips[] = {
        { AMDGPU_HW_IP_GFX, "gfx" }, { AMDGPU_HW_IP_COMPUTE, "compute" }, { AMDGPU_HW_IP_DMA, "sdma" },
        { AMDGPU_HW_IP_UVD, "uvd" }, { AMDGPU_HW_IP_VCE, "vce" }, { AMDGPU_HW_IP_VCN_DEC, "vcn_dec" },
        { AMDGPU_HW_IP_VCN_ENC, "vcn_enc" }, { AMDGPU_HW_IP_VCN_JPEG, "vcn_jpeg" },
    };
    for (size_t i = 0; i < sizeof(ips) / sizeof(ips[0]); i++) {
        struct drm_amdgpu_info_hw_ip ip;
        if (amdgpu_query(fd, AMDGPU_INFO_HW_IP_INFO, &ip, sizeof(ip), ips[i].type) == 0)
            LINE("ip_%s=%u.%u rings=0x%x\n", ips[i].name, ip.hw_ip_version_major, ip.hw_ip_version_minor, ip.available_rings);
        else
            LINE("ip_%s=query failed (%s)\n", ips[i].name, strerror(errno));
    }

    struct drm_amdgpu_memory_info mem;
    if (amdgpu_query(fd, AMDGPU_INFO_MEMORY, &mem, sizeof(mem), 0) == 0) {
        LINE("vram_mb=%" PRIu64 "\nvram_cpu_visible_mb=%" PRIu64 "\ngtt_mb=%" PRIu64 "\n",
             (uint64_t) mem.vram.total_heap_size >> 20, (uint64_t) mem.cpu_accessible_vram.total_heap_size >> 20,
             (uint64_t) mem.gtt.total_heap_size >> 20);
    } else {
        LINE("memory=failed (%s)\n", strerror(errno));
    }

    // RADV refuses a kernel without syncobjs; timeline ones it emulates when missing.
    static const struct { uint64_t cap; const char *name; } caps[] = {
        { DRM_CAP_SYNCOBJ, "cap_syncobj" }, { DRM_CAP_SYNCOBJ_TIMELINE, "cap_syncobj_timeline" },
        { DRM_CAP_PRIME, "cap_prime" },
    };
    for (size_t i = 0; i < sizeof(caps) / sizeof(caps[0]); i++) {
        struct drm_get_cap cap = { .capability = caps[i].cap };
        if (ioctl(fd, DRM_IOCTL_GET_CAP, &cap) == 0) LINE("%s=%" PRIu64 "\n", caps[i].name, (uint64_t) cap.value);
        else LINE("%s=query failed (%s)\n", caps[i].name, strerror(errno));
    }
    close(fd);
    return (*env)->NewStringUTF(env, out);
}

#define LINE_N(...) do { if (used < size) used += snprintf(out + used, size - used, __VA_ARGS__); } while (0)
static void append_maps(char *out, size_t size, size_t *usedp) {
    size_t used = *usedp;
    FILE *maps = fopen("/proc/self/maps", "re");
    if (!maps) { LINE_N("maps=unreadable\n"); *usedp = used; return; }
    static char seen[256][160];
    int nseen = 0;
    char line[512];
    while (fgets(line, sizeof(line), maps)) {
        char *p = strchr(line, '/');
        if (!p) continue;
        p[strcspn(p, "\n")] = 0;
        if (strncmp(p, "/vendor/", 8) && strncmp(p, "/odm/", 5) && strncmp(p, "/apex/", 6)) continue;
        if (!strstr(p, ".so")) continue;
        int dup = 0;
        for (int i = 0; i < nseen && !dup; i++) dup = !strcmp(seen[i], p);
        if (dup || nseen >= 256) continue;
        snprintf(seen[nseen++], sizeof(seen[0]), "%s", p);
        LINE_N("lib=%s\n", p);
    }
    fclose(maps);
    *usedp = used;
}

JNIEXPORT jstring JNICALL
Java_com_droiddeck_launcher_gpu_GpuProbe_nativeVendorLibs(JNIEnv *env, jclass clazz) {
    static char out[16384];
    size_t used = 0;
    out[0] = 0;
    void *lib = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    PFN_vkGetInstanceProcAddr gipa = lib ? (PFN_vkGetInstanceProcAddr) dlsym(lib, "vkGetInstanceProcAddr") : NULL;
    PFN_vkCreateInstance create = gipa ? (PFN_vkCreateInstance) gipa(NULL, "vkCreateInstance") : NULL;
    if (!create) { LINE("error=no Vulkan loader\n"); if (lib) dlclose(lib); return (*env)->NewStringUTF(env, out); }
    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "DroidDeck gpu probe",
                              .apiVersion = VK_API_VERSION_1_1 };
    VkInstanceCreateInfo info = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app };
    VkInstance instance = VK_NULL_HANDLE;
    if (create(&info, NULL, &instance) != VK_SUCCESS) { LINE("error=vkCreateInstance failed\n"); dlclose(lib); return (*env)->NewStringUTF(env, out); }
    PFN_vkEnumeratePhysicalDevices enumerate = (PFN_vkEnumeratePhysicalDevices) gipa(instance, "vkEnumeratePhysicalDevices");
    PFN_vkDestroyInstance destroy = (PFN_vkDestroyInstance) gipa(instance, "vkDestroyInstance");
    uint32_t count = 0;
    if (enumerate) enumerate(instance, &count, NULL);
    LINE("physical_devices=%u\n", count);
    append_maps(out, sizeof(out), &used);
    if (destroy) destroy(instance, NULL);
    dlclose(lib);
    return (*env)->NewStringUTF(env, out);
}
