// No Vulkan headers in the build image: the structures used are spelled out with the layout and
// sType values of vulkan_core.h.
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int32_t VkResult;
typedef uint32_t VkBool32;
typedef uint64_t VkDeviceSize;
typedef void *VkInstance, *VkPhysicalDevice, *VkDevice, *VkQueue, *VkCommandBuffer;
typedef uint64_t VkBuffer, VkDeviceMemory, VkCommandPool, VkFence;
typedef void (*PFN_vkVoidFunction)(void);
typedef PFN_vkVoidFunction (*PFN_vkGetInstanceProcAddr)(VkInstance, const char *);

typedef struct { int32_t sType; const void *pNext; const char *pApplicationName; uint32_t applicationVersion;
                 const char *pEngineName; uint32_t engineVersion; uint32_t apiVersion; } VkApplicationInfo;
typedef struct { int32_t sType; const void *pNext; uint32_t flags; const VkApplicationInfo *pApplicationInfo;
                 uint32_t enabledLayerCount; const char *const *ppEnabledLayerNames;
                 uint32_t enabledExtensionCount; const char *const *ppEnabledExtensionNames; } VkInstanceCreateInfo;
typedef struct { uint32_t apiVersion, driverVersion, vendorID, deviceID; int32_t deviceType;
                 char deviceName[256]; uint8_t pipelineCacheUUID[16]; uint8_t rest[1024]; } VkPhysicalDeviceProperties;
typedef struct { int32_t sType; void *pNext; VkPhysicalDeviceProperties properties; } VkPhysicalDeviceProperties2;
typedef struct { int32_t sType; void *pNext; int32_t driverID; char driverName[256]; char driverInfo[256];
                 uint8_t conformanceVersion[4]; } VkPhysicalDeviceDriverProperties;
typedef struct { uint32_t queueFlags, queueCount, timestampValidBits, granularity[3]; } VkQueueFamilyProperties;
typedef struct { uint32_t propertyFlags, heapIndex; } VkMemoryType;
typedef struct { VkDeviceSize size; uint32_t flags; } VkMemoryHeap;
typedef struct { uint32_t memoryTypeCount; VkMemoryType memoryTypes[32];
                 uint32_t memoryHeapCount; VkMemoryHeap memoryHeaps[16]; } VkPhysicalDeviceMemoryProperties;
typedef struct { int32_t sType; const void *pNext; uint32_t flags; uint32_t queueFamilyIndex; uint32_t queueCount;
                 const float *pQueuePriorities; } VkDeviceQueueCreateInfo;
typedef struct { int32_t sType; const void *pNext; uint32_t flags; uint32_t queueCreateInfoCount;
                 const VkDeviceQueueCreateInfo *pQueueCreateInfos; uint32_t enabledLayerCount;
                 const char *const *ppEnabledLayerNames; uint32_t enabledExtensionCount;
                 const char *const *ppEnabledExtensionNames; const void *pEnabledFeatures; } VkDeviceCreateInfo;
typedef struct { int32_t sType; const void *pNext; uint32_t flags; VkDeviceSize size; uint32_t usage;
                 int32_t sharingMode; uint32_t queueFamilyIndexCount; const uint32_t *pQueueFamilyIndices; } VkBufferCreateInfo;
typedef struct { VkDeviceSize size, alignment; uint32_t memoryTypeBits; } VkMemoryRequirements;
typedef struct { int32_t sType; const void *pNext; VkDeviceSize allocationSize; uint32_t memoryTypeIndex; } VkMemoryAllocateInfo;
typedef struct { int32_t sType; const void *pNext; uint32_t flags; uint32_t queueFamilyIndex; } VkCommandPoolCreateInfo;
typedef struct { int32_t sType; const void *pNext; VkCommandPool commandPool; int32_t level;
                 uint32_t commandBufferCount; } VkCommandBufferAllocateInfo;
typedef struct { int32_t sType; const void *pNext; uint32_t flags; const void *pInheritanceInfo; } VkCommandBufferBeginInfo;
typedef struct { int32_t sType; const void *pNext; uint32_t flags; } VkFenceCreateInfo;
typedef struct { int32_t sType; const void *pNext; uint32_t waitSemaphoreCount; const void *pWaitSemaphores;
                 const uint32_t *pWaitDstStageMask; uint32_t commandBufferCount; const VkCommandBuffer *pCommandBuffers;
                 uint32_t signalSemaphoreCount; const void *pSignalSemaphores; } VkSubmitInfo;

enum {
    ST_APPLICATION_INFO = 0, ST_INSTANCE_CREATE_INFO = 1, ST_DEVICE_QUEUE_CREATE_INFO = 2, ST_DEVICE_CREATE_INFO = 3,
    ST_SUBMIT_INFO = 4, ST_MEMORY_ALLOCATE_INFO = 5, ST_FENCE_CREATE_INFO = 8, ST_BUFFER_CREATE_INFO = 12,
    ST_COMMAND_POOL_CREATE_INFO = 39, ST_COMMAND_BUFFER_ALLOCATE_INFO = 40, ST_COMMAND_BUFFER_BEGIN_INFO = 42,
    ST_PHYSICAL_DEVICE_PROPERTIES_2 = 1000059001, ST_PHYSICAL_DEVICE_DRIVER_PROPERTIES = 1000196000,
};
#define API_VERSION_1_3 ((1u << 22) | (3u << 12))
#define QUEUE_GRAPHICS 0x1u
#define QUEUE_COMPUTE 0x2u
#define BUFFER_USAGE_TRANSFER_DST 0x2u
#define MEMORY_HOST_VISIBLE 0x2u
#define MEMORY_HOST_COHERENT 0x4u
#define FILL_WORDS 1024u
#define FILL_VALUE 0xC0FFEE42u

static PFN_vkGetInstanceProcAddr gipa;
static VkInstance instance;
#define FN(name) PFN_##name name = (PFN_##name) gipa(instance, #name)
#define FAIL(...) do { printf("vkprobe: FAILED: " __VA_ARGS__); printf("\n"); return 1; } while (0)
#define CHECK(call) do { VkResult r_ = (call); if (r_ != 0) FAIL("%s -> VkResult %d", #call, r_); } while (0)

typedef VkResult (*PFN_vkCreateInstance)(const VkInstanceCreateInfo *, const void *, VkInstance *);
typedef VkResult (*PFN_vkEnumeratePhysicalDevices)(VkInstance, uint32_t *, VkPhysicalDevice *);
typedef void (*PFN_vkGetPhysicalDeviceProperties2)(VkPhysicalDevice, VkPhysicalDeviceProperties2 *);
typedef void (*PFN_vkGetPhysicalDeviceQueueFamilyProperties)(VkPhysicalDevice, uint32_t *, VkQueueFamilyProperties *);
typedef void (*PFN_vkGetPhysicalDeviceMemoryProperties)(VkPhysicalDevice, VkPhysicalDeviceMemoryProperties *);
typedef VkResult (*PFN_vkCreateDevice)(VkPhysicalDevice, const VkDeviceCreateInfo *, const void *, VkDevice *);
typedef PFN_vkVoidFunction (*PFN_vkGetDeviceProcAddr)(VkDevice, const char *);
typedef void (*PFN_vkGetDeviceQueue)(VkDevice, uint32_t, uint32_t, VkQueue *);
typedef VkResult (*PFN_vkCreateBuffer)(VkDevice, const VkBufferCreateInfo *, const void *, VkBuffer *);
typedef void (*PFN_vkGetBufferMemoryRequirements)(VkDevice, VkBuffer, VkMemoryRequirements *);
typedef VkResult (*PFN_vkAllocateMemory)(VkDevice, const VkMemoryAllocateInfo *, const void *, VkDeviceMemory *);
typedef VkResult (*PFN_vkBindBufferMemory)(VkDevice, VkBuffer, VkDeviceMemory, VkDeviceSize);
typedef VkResult (*PFN_vkMapMemory)(VkDevice, VkDeviceMemory, VkDeviceSize, VkDeviceSize, uint32_t, void **);
typedef VkResult (*PFN_vkCreateCommandPool)(VkDevice, const VkCommandPoolCreateInfo *, const void *, VkCommandPool *);
typedef VkResult (*PFN_vkAllocateCommandBuffers)(VkDevice, const VkCommandBufferAllocateInfo *, VkCommandBuffer *);
typedef VkResult (*PFN_vkBeginCommandBuffer)(VkCommandBuffer, const VkCommandBufferBeginInfo *);
typedef void (*PFN_vkCmdFillBuffer)(VkCommandBuffer, VkBuffer, VkDeviceSize, VkDeviceSize, uint32_t);
typedef VkResult (*PFN_vkEndCommandBuffer)(VkCommandBuffer);
typedef VkResult (*PFN_vkCreateFence)(VkDevice, const VkFenceCreateInfo *, const void *, VkFence *);
typedef VkResult (*PFN_vkQueueSubmit)(VkQueue, uint32_t, const VkSubmitInfo *, VkFence);
typedef VkResult (*PFN_vkWaitForFences)(VkDevice, uint32_t, const VkFence *, VkBool32, uint64_t);
typedef VkResult (*PFN_vkDeviceWaitIdle)(VkDevice);
typedef void (*PFN_vkDestroyDevice)(VkDevice, const void *);
typedef void (*PFN_vkDestroyInstance)(VkInstance, const void *);

static const char *device_type(int32_t t) {
    static const char *names[] = { "other", "integrated", "discrete", "virtual", "cpu" };
    return t >= 0 && t <= 4 ? names[t] : "?";
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    const char *files = getenv("VK_DRIVER_FILES") ? getenv("VK_DRIVER_FILES") : getenv("VK_ICD_FILENAMES");
    printf("vkprobe: driver manifest %s\n", files ? files : "(loader default search)");
    void *loader = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!loader) FAIL("dlopen libvulkan.so.1: %s", dlerror());
    gipa = (PFN_vkGetInstanceProcAddr) dlsym(loader, "vkGetInstanceProcAddr");
    if (!gipa) FAIL("no vkGetInstanceProcAddr in the loader");

    FN(vkCreateInstance);
    VkApplicationInfo app = { .sType = ST_APPLICATION_INFO, .pApplicationName = "droiddeck-vkprobe", .apiVersion = API_VERSION_1_3 };
    VkInstanceCreateInfo ici = { .sType = ST_INSTANCE_CREATE_INFO, .pApplicationInfo = &app };
    CHECK(vkCreateInstance(&ici, NULL, &instance));
    printf("vkprobe: instance created\n");

    FN(vkEnumeratePhysicalDevices);
    FN(vkGetPhysicalDeviceProperties2);
    FN(vkGetPhysicalDeviceQueueFamilyProperties);
    FN(vkGetPhysicalDeviceMemoryProperties);
    FN(vkCreateDevice);
    FN(vkGetDeviceProcAddr);
    FN(vkDestroyInstance);
    uint32_t count = 0;
    CHECK(vkEnumeratePhysicalDevices(instance, &count, NULL));
    printf("vkprobe: %u physical device(s)\n", count);
    if (count == 0) FAIL("the driver offers no physical device");
    VkPhysicalDevice pds[8];
    if (count > 8) count = 8;
    CHECK(vkEnumeratePhysicalDevices(instance, &count, pds));
    for (uint32_t i = 0; i < count; i++) {
        VkPhysicalDeviceDriverProperties drv = { .sType = ST_PHYSICAL_DEVICE_DRIVER_PROPERTIES };
        VkPhysicalDeviceProperties2 p2 = { .sType = ST_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &drv };
        vkGetPhysicalDeviceProperties2(pds[i], &p2);
        const VkPhysicalDeviceProperties *p = &p2.properties;
        printf("vkprobe: [%u] %s (%s, vendor 0x%04x device 0x%04x, Vulkan %u.%u.%u) driver %s / %s\n", i,
               p->deviceName, device_type(p->deviceType), p->vendorID, p->deviceID, p->apiVersion >> 22,
               (p->apiVersion >> 12) & 0x3ff, p->apiVersion & 0xfff, drv.driverName, drv.driverInfo);
    }

    VkPhysicalDevice pd = pds[0];
    VkQueueFamilyProperties qf[16];
    uint32_t nqf = 16;
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &nqf, qf);
    uint32_t family = UINT32_MAX;
    for (uint32_t i = 0; i < nqf; i++) {
        printf("vkprobe: queue family %u: flags 0x%x x%u\n", i, qf[i].queueFlags, qf[i].queueCount);
        if (family == UINT32_MAX && (qf[i].queueFlags & (QUEUE_GRAPHICS | QUEUE_COMPUTE))) family = i;
    }
    if (family == UINT32_MAX) FAIL("no graphics or compute queue");

    float priority = 1.0f;
    VkDeviceQueueCreateInfo qci = { .sType = ST_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = family,
                                    .queueCount = 1, .pQueuePriorities = &priority };
    VkDeviceCreateInfo dci = { .sType = ST_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1, .pQueueCreateInfos = &qci };
    VkDevice dev;
    CHECK(vkCreateDevice(pd, &dci, NULL, &dev));
    printf("vkprobe: device created (queue family %u)\n", family);

#define DFN(name) PFN_##name name = (PFN_##name) vkGetDeviceProcAddr(dev, #name)
    DFN(vkGetDeviceQueue); DFN(vkCreateBuffer); DFN(vkGetBufferMemoryRequirements); DFN(vkAllocateMemory);
    DFN(vkBindBufferMemory); DFN(vkMapMemory); DFN(vkCreateCommandPool); DFN(vkAllocateCommandBuffers);
    DFN(vkBeginCommandBuffer); DFN(vkCmdFillBuffer); DFN(vkEndCommandBuffer); DFN(vkCreateFence);
    DFN(vkQueueSubmit); DFN(vkWaitForFences); DFN(vkDeviceWaitIdle); DFN(vkDestroyDevice);

    VkBufferCreateInfo bci = { .sType = ST_BUFFER_CREATE_INFO, .size = FILL_WORDS * 4, .usage = BUFFER_USAGE_TRANSFER_DST };
    VkBuffer buf;
    CHECK(vkCreateBuffer(dev, &bci, NULL, &buf));
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(dev, buf, &req);
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(pd, &mp);
    uint32_t type = UINT32_MAX;
    const uint32_t want = MEMORY_HOST_VISIBLE | MEMORY_HOST_COHERENT;
    for (uint32_t i = 0; i < mp.memoryTypeCount && type == UINT32_MAX; i++)
        if ((req.memoryTypeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want) type = i;
    if (type == UINT32_MAX) FAIL("no host-visible coherent memory type for the buffer");
    VkMemoryAllocateInfo mai = { .sType = ST_MEMORY_ALLOCATE_INFO, .allocationSize = req.size, .memoryTypeIndex = type };
    VkDeviceMemory mem;
    CHECK(vkAllocateMemory(dev, &mai, NULL, &mem));
    CHECK(vkBindBufferMemory(dev, buf, mem, 0));
    uint32_t *map;
    CHECK(vkMapMemory(dev, mem, 0, req.size, 0, (void **) &map));
    memset(map, 0, FILL_WORDS * 4);
    printf("vkprobe: buffer allocated (memory type %u)\n", type);

    VkCommandPoolCreateInfo cpci = { .sType = ST_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = family };
    VkCommandPool pool;
    CHECK(vkCreateCommandPool(dev, &cpci, NULL, &pool));
    VkCommandBufferAllocateInfo cbai = { .sType = ST_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = pool, .commandBufferCount = 1 };
    VkCommandBuffer cmd;
    CHECK(vkAllocateCommandBuffers(dev, &cbai, &cmd));
    VkCommandBufferBeginInfo cbbi = { .sType = ST_COMMAND_BUFFER_BEGIN_INFO };
    CHECK(vkBeginCommandBuffer(cmd, &cbbi));
    vkCmdFillBuffer(cmd, buf, 0, FILL_WORDS * 4, FILL_VALUE);
    CHECK(vkEndCommandBuffer(cmd));
    VkFenceCreateInfo fci = { .sType = ST_FENCE_CREATE_INFO };
    VkFence fence;
    CHECK(vkCreateFence(dev, &fci, NULL, &fence));
    VkQueue queue;
    vkGetDeviceQueue(dev, family, 0, &queue);
    VkSubmitInfo si = { .sType = ST_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd };
    CHECK(vkQueueSubmit(queue, 1, &si, fence));
    printf("vkprobe: fill submitted, waiting up to 5 s\n");
    CHECK(vkWaitForFences(dev, 1, &fence, 1, 5000000000ull));

    uint32_t good = 0;
    for (uint32_t i = 0; i < FILL_WORDS; i++) good += map[i] == FILL_VALUE;
    if (good != FILL_WORDS) FAIL("GPU fill incomplete: %u of %u words written (first word 0x%08x)", good, FILL_WORDS, map[0]);
    printf("vkprobe: OK - the GPU executed a fill of %u bytes and the CPU read it back\n", FILL_WORDS * 4);

    vkDeviceWaitIdle(dev);
    vkDestroyDevice(dev, NULL);
    vkDestroyInstance(instance, NULL);
    return 0;
}
