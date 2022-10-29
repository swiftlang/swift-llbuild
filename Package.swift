// swift-tools-version:5.2

// This file defines Swift package manager support for llbuild. See:
//  https://github.com/apple/swift-package-manager/tree/master/Documentation

import PackageDescription

let package = Package(
    name: "llbuild",
    platforms: [
        .macOS(.v10_10), .iOS(.v9),
    ],
    products: [
        .executable(
            name: "llbuild",
            targets: ["llbuild"]),

            .library(
                name: "libllbuild",
                targets: ["libllbuild"]),
        .library(
            name: "llbuildSwift",
            targets: ["llbuildSwift"]),
        .library(
            name: "llbuildSwiftDynamic",
            type: .dynamic,
            targets: ["llbuildSwift"]),
        .library(
            name: "llbuildAnalysis",
            targets: ["llbuildAnalysis"]),
    ],
    targets: [
        // MARK: Products

        /// The llbuild multitool (primarily for testing).
        .target(
            name: "llbuild",
            dependencies: ["llbuildCommands"],
            path: "products/llbuild",
            linkerSettings: [
                .linkedLibrary("dl", .when(platforms: [.linux])),
                .linkedLibrary("pthread", .when(platforms: [.linux]))]
        ),

        /// The custom build tool used by the Swift package manager (SwiftPM).
        ///
        /// SwiftPM has now switched to using llbuild's Swift bindings API to
        /// build, but this tool is still used for SwiftPM's bootstrapping. Once
        /// that step has been eliminated, this tool can be removed.
            .target(
                name: "swift-build-tool",
                dependencies: ["llbuildBuildSystem"],
                path: "products/swift-build-tool",
                linkerSettings: [
                    .linkedLibrary("dl", .when(platforms: [.linux])),
                    .linkedLibrary("pthread", .when(platforms: [.linux]))]
            ),

        /// The public llbuild C API.
        .target(
            name: "libllbuild",
            dependencies: ["llbuildCore", "llbuildBuildSystem", "llbuildNinja"],
            path: "products/libllbuild"
        ),

        /// The public llbuild Swift API.
        .target(
            name: "llbuildSwift",
            dependencies: ["libllbuild"],
            path: "products/llbuildSwift",
            exclude: []
        ),

        // MARK: Components

            .target(
                name: "llbuildBasic",
                dependencies: ["llvmSupport"],
                path: "lib/Basic"
            ),
        .target(
            name: "llbuildCore",
            dependencies: ["llbuildBasic"],
            path: "lib/Core",
            linkerSettings: [.linkedLibrary("sqlite3")]
        ),
        .target(
            name: "llbuildBuildSystem",
            dependencies: ["llbuildCore"],
            path: "lib/BuildSystem"
        ),
        .target(
            name: "llbuildEvo",
            dependencies: ["llbuildCore"],
            path: "lib/Evo"
        ),
        .target(
            name: "llbuildNinja",
            dependencies: ["llbuildBasic"],
            path: "lib/Ninja"
        ),
        .target(
            name: "llbuildCommands",
            dependencies: ["llbuildCore", "llbuildBuildSystem", "llbuildEvo", "llbuildNinja"],
            path: "lib/Commands"
        ),

        // MARK: Analysis Components

            .target(
                name: "llbuildAnalysis",
                dependencies: ["llbuildSwift"],
                path: "lib/Analysis"
            ),

        // MARK: Test Targets

            .target(
                name: "llbuildBasicTests",
                dependencies: ["llbuildBasic", "gtestlib"],
                path: "unittests/Basic",
                linkerSettings: [
                    .linkedLibrary("dl", .when(platforms: [.linux])),
                    .linkedLibrary("pthread", .when(platforms: [.linux]))]),
        .target(
            name: "llbuildCoreTests",
            dependencies: ["llbuildCore", "gtestlib"],
            path: "unittests/Core",
            linkerSettings: [
                .linkedLibrary("dl", .when(platforms: [.linux])),
                .linkedLibrary("pthread", .when(platforms: [.linux]))]),
        .target(
            name: "llbuildBuildSystemTests",
            dependencies: ["llbuildBuildSystem", "gtestlib"],
            path: "unittests/BuildSystem",
            linkerSettings: [
                .linkedLibrary("dl", .when(platforms: [.linux])),
                .linkedLibrary("pthread", .when(platforms: [.linux]))]),
        .target(
            name: "llbuildNinjaTests",
            dependencies: ["llbuildNinja", "gtestlib"],
            path: "unittests/Ninja",
            linkerSettings: [
                .linkedLibrary("dl", .when(platforms: [.linux])),
                .linkedLibrary("pthread", .when(platforms: [.linux]))]),
        .testTarget(
            name: "llbuildSwiftTests",
            dependencies: ["llbuildSwift", "llbuildTestSupport"],
            path: "unittests/Swift",
            linkerSettings: [
                .linkedLibrary("dl", .when(platforms: [.linux])),
                .linkedLibrary("pthread", .when(platforms: [.linux]))]),
        .testTarget(
            name: "AnalysisTests",
            dependencies: ["llbuildAnalysis"],
            path: "unittests/Analysis",
            linkerSettings: [
                .linkedLibrary("dl", .when(platforms: [.linux])),
                .linkedLibrary("pthread", .when(platforms: [.linux]))]),

            .target(
                name: "llbuildTestSupport",
                path: "unittests/TestSupport"),

        // MARK: GoogleTest

            .target(
                name: "gtestlib",
                path: "utils/unittest/googletest/src",
                exclude: [
                    "gtest-death-test.cc",
                    "gtest-filepath.cc",
                    "gtest-port.cc",
                    "gtest-printers.cc",
                    "gtest-test-part.cc",
                    "gtest-typed-test.cc",
                    "gtest.cc",
                ]),

        // MARK: Ingested LLVM code.
        .target(
            name: "llvmDemangle",
            path: "lib/llvm/Demangle"
        ),

            .target(
                name: "llvmSupport",
                dependencies: ["llvmDemangle"],
                path: "lib/llvm/Support",
                exclude: ["BLAKE3/blake3_neon.c"],
                cSettings: [
                    .headerSearchPath("lib/llvm/Support"),
                    .define("BLAKE3_USE_NEON", to: "0"),
                    .define("BLAKE3_NO_AVX512", to: "1"),
                    .define("BLAKE3_NO_AVX2", to: "1"),
                    .define("BLAKE3_NO_SSE41", to: "1"),
                    .define("BLAKE3_NO_SSE2", to: "1")
                ], linkerSettings: [
                    .linkedLibrary("m", .when(platforms: [.linux])),
                    .linkedLibrary("ncurses", .when(platforms: [.linux, .macOS, .android]))]
            ),
    ], cxxLanguageStandard: .cxx14)

// FIXME: Conditionalize these flags since SwiftPM 5.3 and earlier will crash for platforms they don't know about.
#if os(Windows)

do {
    let llvmTargets: Set<String> = [
        "llvmDemangle",
        "llvmSupport",

        "llbuild",
        "llbuildBasic",
        "llbuildBuildSystem",
        "llbuildCommands",
        "llbuildNinja",

        "llbuildBasicTests",
        "llbuildBuildSystemTests",
        "llbuildCoreTests",

        "swift-build-tool",
    ]

    package.targets.filter({ llvmTargets.contains($0.name) }).forEach { target in
        target.cxxSettings = [ .define("LLVM_ON_WIN32", .when(platforms: [.windows])) ]
    }
}

package.targets.first { $0.name == "libllbuild" }?.cxxSettings = [
    .define("LLVM_ON_WIN32", .when(platforms: [.windows])),
    // FIXME: we need to define `libllbuild_EXPORTS` to ensure that the
    // symbols are exported from the DLL that is being built here until
    // static linking is supported on Windows.
    .define("libllbuild_EXPORTS", .when(platforms: [.windows])),
]

package.targets.first { $0.name == "llbuildBasic" }?.linkerSettings = [
    .linkedLibrary("ShLwApi", .when(platforms: [.windows]))
]

#endif

// FIXME: when the SupportedPlatforms availability directive is updated and
// the platform port is in sync with this directive, these conditions can
// be folded up with .when(platforms:_) clauses.
#if os(OpenBSD)
if let target = package.targets.first(where: { $0.name == "llbuildCore"}) {
    target.cSettings = [.unsafeFlags(["-I/usr/local/include"])] 
    target.linkerSettings = [
        .linkedLibrary("sqlite3"),
        .unsafeFlags(["-L/usr/local/lib"])
    ]
}
#endif
