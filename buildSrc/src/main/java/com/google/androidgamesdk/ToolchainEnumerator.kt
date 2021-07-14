package com.google.androidgamesdk

import org.gradle.api.Project
import java.lang.reflect.UndeclaredThrowableException
import java.util.stream.Collectors

/**
 * Expose the toolchains to use to compile a library against all combinations
 * of architecture/STL/Android NDK/Android SDK version.
 */
class ToolchainEnumerator() {
    private val abis32Bits = listOf("armeabi-v7a", "x86")
    private val abis64Bits = listOf("arm64-v8a", "x86_64")
    val allAbis = abis32Bits + abis64Bits

    private val pre17STLs = listOf(
        "c++_static", "c++_shared",
        "gnustl_static", "gnustl_shared"
    )
    private val post17STLs = listOf("c++_static", "c++_shared")

    // For the AAR, only build the dynamic libraries against shared STL.
    private val aarPre17STLs = listOf("c++_shared", "gnustl_shared")
    private val aarPost17STLs = listOf("c++_shared")

    private val pre17NdkToSdks = mapOf(
        "r14" to listOf(14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24),
        "r15" to listOf(14, 15, 16, 17, 18, 19, 21, 22, 23, 24, 26),
        "r16" to listOf(14, 15, 16, 17, 18, 19, 21, 22, 23, 24, 26, 27),
        "r17" to listOf(14, 15, 16, 17, 18, 19, 21, 22, 23, 24, 26, 27, 28)
    )
    private val post17NdkToSdks = mapOf(
        "r18" to listOf(16, 17, 18, 19, 21, 22, 23, 24, 26, 27, 28),
        "r19" to listOf(16, 17, 18, 19, 21, 22, 23, 24, 26, 27, 28),
        "r20" to listOf(16, 17, 18, 19, 21, 22, 23, 24, 26, 27, 28),
        "r21" to listOf(16, 17, 18, 19, 21, 22, 23, 24, 26, 27, 28, 29)
    )

    // In the AAR, library search is handled by Prefab, that looks for API 21 for 64 bits architectures
    // even if a lower API level is requested. We need to build a different set of libraries for 32 and
    // 64 bits as a consequence.
    private val aar32BitsPre17NdkToSdks = pre17NdkToSdks
    private val aar32BitsPost17NdkToSdks = post17NdkToSdks
    private val aar64BitsPre17NdkToSdks = mapOf(
        "r14" to listOf(21, 22, 23, 24),
        "r15" to listOf(21, 22, 23, 24, 26),
        "r16" to listOf(21, 22, 23, 24, 26, 27),
        "r17" to listOf(21, 22, 23, 24, 26, 27, 28)
    )
    private val aar64BitsPost17NdkToSdks = mapOf(
        "r18" to listOf(21, 22, 23, 24, 26, 27, 28),
        "r19" to listOf(21, 22, 23, 24, 26, 27, 28),
        "r20" to listOf(21, 22, 23, 24, 26, 27, 28),
        "r21" to listOf(21, 22, 23, 24, 26, 27, 28, 29)
    )

    fun enumerate(toolchainSet: ToolchainSet, project: Project): List<EnumeratedToolchain> {
        return when ( toolchainSet ) {
            ToolchainSet.ALL -> enumerateAllToolchains(project)
            ToolchainSet.AAR -> enumerateAllAarToolchains(project)
        }
    }
    private fun enumerateAllToolchains(project: Project): List<EnumeratedToolchain> {
        val pre17Toolchains: List<EnumeratedToolchain> = allAbis.flatMap {
            abi ->
            pre17STLs.flatMap { stl ->
                enumerateToolchains(project, abi, stl, pre17NdkToSdks)
            }
        }
        val post17Toolchains: List<EnumeratedToolchain> = allAbis.flatMap {
            abi ->
            post17STLs.flatMap { stl ->
                enumerateToolchains(project, abi, stl, post17NdkToSdks)
            }
        }
        return pre17Toolchains + post17Toolchains
    }

    private fun enumerateAllAarToolchains(project: Project): List<EnumeratedToolchain> {
        return abis32Bits.flatMap { abi ->
            aarPre17STLs.flatMap { stl ->
                enumerateToolchains(project, abi, stl, aar32BitsPre17NdkToSdks)
            } + aarPost17STLs.flatMap { stl ->
                enumerateToolchains(project, abi, stl, aar32BitsPost17NdkToSdks)
            }
        } + abis64Bits.flatMap { abi ->
            aarPre17STLs.flatMap { stl ->
                enumerateToolchains(project, abi, stl, aar64BitsPre17NdkToSdks)
            } + aarPost17STLs.flatMap { stl ->
                enumerateToolchains(project, abi, stl, aar64BitsPost17NdkToSdks)
            }
        }
    }

    private fun enumerateToolchains(
        project: Project,
        abi: String,
        stl: String,
        ndksToSdks: Map<String, List<Int>>
    ): List<EnumeratedToolchain> {
        return ndksToSdks.flatMap { ndkToSdks ->
            val ndk: String = ndkToSdks.key
            ndkToSdks.value.map { sdk ->
                EnumeratedToolchain(
                    abi,
                    stl, SpecificToolchain(project, sdk.toString(), ndk)
                )
            }
        }
    }

    /**
     * Execute the specified function concurrently on the toolchains.
     * In case of an exception, it will be rethrown early (not waiting for
     * all tasks to finish).
     */
    @Throws(Exception::class)
    fun <T> parallelMap(
        toolchains: List<EnumeratedToolchain>,
        f: (EnumeratedToolchain) -> T
    ): List<T> {
        return toolchains.parallelStream().map { toolchain ->
            try {
                f(toolchain)
            } catch (e: UndeclaredThrowableException) {
                // Groovy thrown exceptions will be wrapped in a
                // UndeclaredThrowableException. Unwrap them.
                throw e.cause ?: e
            }
        }.collect(Collectors.toList())
    }

    data class EnumeratedToolchain(
        val abi: String,
        val stl: String,
        val toolchain: Toolchain
    )

    companion object {
        /**
         * Filter the libraries to keep only those that are compatible
         * with the Android version, NDK version and STL specified in the
         * toolchain/build options.
         */
        @JvmStatic
        fun filterBuiltLibraries(
            libraries: Collection<NativeLibrary>,
            buildOptions: BuildOptions,
            toolchain: Toolchain
        ): Collection<NativeLibrary> {
            return libraries.filter { nativeLibrary ->
                val androidVersion = toolchain.getAndroidVersion().toInt()
                val ndkVersion = toolchain.getNdkVersionNumber().toInt()
                if (nativeLibrary.minimumAndroidApiLevel != null &&
                    nativeLibrary.minimumAndroidApiLevel!! > androidVersion
                ) {
                    false
                } else if (nativeLibrary.minimumNdkVersion != null &&
                    nativeLibrary.minimumNdkVersion!! > ndkVersion
                ) {
                    false
                } else if (nativeLibrary.supportedStlVersions != null &&
                    !nativeLibrary.supportedStlVersions!!
                        .contains(buildOptions.stl)
                ) {
                    false
                } else {
                    true
                }
            }
        }
    }
}
