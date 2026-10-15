package com.droiddeck.launcher.gpu

import android.util.Log
import java.io.File
import java.io.RandomAccessFile
import java.nio.ByteBuffer
import java.nio.ByteOrder

/** Only ROM paths, ROM properties and kernel driver names: nothing that identifies the user. */
object GpuProbe {
    private const val TAG = "GpuProbe"

    @JvmStatic private external fun nativeDrmVersion(path: String): String
    @JvmStatic private external fun nativeVendorLibs(): String
    @JvmStatic private external fun nativeAmdgpuInfo(path: String): String

    fun write(target: File) {
        try {
            target.writeText(build())
        } catch (t: Throwable) {
            Log.w(TAG, "could not write $target", t)
        }
    }

    fun build(): String {
        val b = StringBuilder()
        fun h(title: String) {
            b.append('\n').append(title).append('\n').append("-".repeat(title.length)).append('\n')
        }
        fun k(key: String, value: Any?) {
            b.append(key.padEnd(28)).append(value ?: "unknown").append('\n')
        }
        val native = runCatching { System.loadLibrary("deviceinfo") }.isSuccess

        b.append("DroidDeck GPU probe\n")
        b.append("===================\n")

        h("Android's Vulkan driver (as the app sees it)")
        runCatching { VulkanInfo.query() }.getOrNull()?.forEach { (key, value) -> k(key, value) }

        h("System properties")
        for (name in listOf(
            "ro.hardware", "ro.hardware.vulkan", "ro.hardware.egl", "ro.hardware.gralloc",
            "ro.board.platform", "ro.soc.manufacturer", "ro.soc.model", "ro.product.first_api_level",
            "ro.vendor.build.version.sdk", "ro.vndk.version", "ro.gfx.driver.0", "ro.gfx.driver.1",
            "ro.gfx.angle.supported", "ro.hwui.use_vulkan", "debug.hwui.renderer",
        )) k(name, prop(name).ifEmpty { "(empty)" })

        h("Driver files")
        for (dir in listOf("/vendor/lib64/hw", "/vendor/lib64/egl", "/vendor/lib64")) {
            val names = File(dir).list()
            if (names == null) { k(dir, "cannot list"); continue }
            val hits = names.filter { n ->
                val l = n.lowercase()
                listOf("vulkan", "gpu", "egl", "gles", "gralloc", "mapper", "amd", "xclipse", "rdna", "llvm", "sc_").any { l.contains(it) }
            }.sorted()
            k(dir, "${names.size} entries, ${hits.size} GPU-related")
            for (n in hits) b.append("    ").append(n).append('\n')
        }
        val vulkanDrivers = listOf("/vendor/lib64/hw", "/odm/lib64/hw")
            .flatMap { d -> File(d).listFiles()?.filter { it.name.startsWith("vulkan.") }.orEmpty() }
        if (vulkanDrivers.isEmpty()) k("vulkan.*.so", "none found in vendor/odm hw dirs")
        for (f in vulkanDrivers) {
            k(f.path, "${f.length()} bytes, readable=${f.canRead()}")
            val needed = runCatching { elfNeeded(f) }
            needed.onSuccess { list -> for (n in list) b.append("    NEEDED ").append(n).append('\n') }
            needed.onFailure { b.append("    (NEEDED unreadable: ").append(it.message).append(")\n") }
        }

        h("Vendor libraries loaded with a Vulkan instance up")
        if (native) runCatching { nativeVendorLibs() }.onSuccess { b.append(it) }.onFailure { k("error", it.message) }
        else k("error", "deviceinfo library not loaded")

        h("Kernel GPU interface")
        val devDri = File("/dev/dri").listFiles()
        k("/dev/dri", devDri?.joinToString(" ") { it.name } ?: "absent or cannot list")
        val amdgpuRender = mutableListOf<File>()
        devDri?.sortedBy { it.name }?.forEach { node ->
            val version = if (native) runCatching { nativeDrmVersion(node.path) }.getOrElse { it.message } else "no native"
            k("  ${node.name}", version)
            if (node.name.startsWith("renderD") && version?.contains("driver=amdgpu") == true) amdgpuRender.add(node)
        }
        val gpuNodes = File("/dev").list()?.filter { n ->
            listOf("mali", "kgsl", "sgpu", "gpu", "pvr", "amd", "ion", "dma_heap").any { n.lowercase().contains(it) }
        }?.sorted()
        k("/dev GPU-ish nodes", gpuNodes?.joinToString(" ")?.ifEmpty { "none" } ?: "cannot list /dev")
        File("/dev/dma_heap").list()?.let { k("/dev/dma_heap", it.sorted().joinToString(" ")) }
        File("/sys/class/drm").listFiles()?.sortedBy { it.name }?.forEach { e ->
            val uevent = readSmall(File(e, "device/uevent"))?.lines()?.filter {
                it.startsWith("DRIVER=") || it.startsWith("OF_COMPATIBLE_0=") || it.startsWith("OF_NAME=")
            }?.joinToString(", ")
            k("/sys/class/drm/${e.name}", uevent ?: "(no readable uevent)")
        } ?: k("/sys/class/drm", "absent or cannot list")
        File("/sys/kernel/gpu").listFiles()?.sortedBy { it.name }?.let { files ->
            k("/sys/kernel/gpu", files.joinToString(" ") { it.name })
            for (n in listOf("gpu_model", "gpu_driver_version", "gpu_clock", "gpu_max_clock")) {
                readSmall(File("/sys/kernel/gpu", n))?.let { k("  $n", it.trim()) }
            }
        } ?: k("/sys/kernel/gpu", "absent or cannot list")
        readSmall(File("/sys/kernel/gpu/gpu_info"))?.let { k("  gpu_info", it.trim().replace("\n", " | ")) }

        for (node in amdgpuRender) {
            h("amdgpu device info (${node.name})")
            if (native) runCatching { nativeAmdgpuInfo(node.path) }.onSuccess { b.append(it) }.onFailure { k("error", it.message) }
        }

        h("Android linker (for libhybris)")
        for (p in listOf(
            "/apex/com.android.runtime/bin/linker64", "/system/bin/linker64", "/linkerconfig/ld.config.txt",
            "/system/lib64/libvulkan.so", "/system/lib64/libc.so", "/vendor/lib64/libc++.so",
            "/apex/com.android.vndk.current/lib64", "/system/lib64/vndk-sp-current",
        )) {
            val f = File(p)
            k(p, if (!f.exists()) "absent" else "present, readable=${f.canRead()}")
        }

        b.append('\n')
        return b.toString()
    }

    private fun prop(name: String): String = runCatching {
        Class.forName("android.os.SystemProperties").getMethod("get", String::class.java).invoke(null, name) as String
    }.getOrDefault("")

    private fun readSmall(f: File): String? = runCatching { f.readText().take(4096) }.getOrNull()

    /** DT_NEEDED entries of an ELF64 little-endian shared object, read without loading it. */
    private fun elfNeeded(f: File): List<String> = RandomAccessFile(f, "r").use { raf ->
        fun buf(off: Long, len: Int): ByteBuffer {
            val bytes = ByteArray(len)
            raf.seek(off)
            raf.readFully(bytes)
            return ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        }
        val eh = buf(0, 64)
        require(eh.getInt(0) == 0x464c457f && eh.get(4).toInt() == 2) { "not ELF64" }
        val phoff = eh.getLong(0x20)
        val phentsize = eh.getShort(0x36).toInt()
        val phnum = eh.getShort(0x38).toInt()
        data class Seg(val type: Int, val off: Long, val vaddr: Long, val filesz: Long)
        val segs = (0 until phnum).map { i ->
            val ph = buf(phoff + i.toLong() * phentsize, 56)
            Seg(ph.getInt(0), ph.getLong(8), ph.getLong(16), ph.getLong(32))
        }
        val dyn = segs.firstOrNull { it.type == 2 } ?: return emptyList()
        val d = buf(dyn.off, dyn.filesz.toInt())
        val needed = mutableListOf<Long>()
        var strtab = 0L
        var i = 0
        while (i + 16 <= dyn.filesz) {
            val tag = d.getLong(i)
            val value = d.getLong(i + 8)
            if (tag == 0L) break
            if (tag == 1L) needed.add(value)
            if (tag == 5L) strtab = value
            i += 16
        }
        val load = segs.firstOrNull { it.type == 1 && strtab >= it.vaddr && strtab < it.vaddr + it.filesz }
            ?: return listOf("(string table not in a loaded segment)")
        val strOff = strtab - load.vaddr + load.off
        needed.map { idx ->
            val s = buf(strOff + idx, 128).array()
            String(s, 0, s.indexOfFirst { it.toInt() == 0 }.let { if (it < 0) s.size else it })
        }
    }
}
