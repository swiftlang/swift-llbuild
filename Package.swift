// swift-tools-version:5.3

// This file defines Swift package manager support for llbuild. See:
//  https://github.com/swiftlang/swift-package-manager/tree/master/Documentation

import PackageDescription
import class Foundation.ProcessInfo

let isStaticBuild = ProcessInfo.processInfo.environment["LLBUILD_STATIC_LINK"] != nil
let useEmbeddedSqlite = isStaticBuild || ProcessInfo.processInfo.environment["LLBUILD_USE_EMBEDDED_SQLITE"] != nil
let useTerminfo = !isStaticBuild && ProcessInfo.processInfo.environment["LLBUILD_NO_TERMINFO"] == nil

let embeddedSqliteCondition: TargetDependencyCondition? = {
    if useEmbeddedSqlite {
        return nil
    }
    return .when(platforms: [.windows, .android])
}()

let externalSqliteLibraries: [LinkerSetting] = {
    if useEmbeddedSqlite {
        return []
    }
    return [.linkedLibrary("sqlite3", .when(platforms: [.macOS, .iOS, .tvOS, .watchOS, .visionOS, .macCatalyst, .linux]))] 
}()

let terminfoLibraries: [LinkerSetting] = {
    if !useTerminfo {
        return []
    }
    return [.linkedLibrary("ncurses", .when(platforms: [.linux, .macOS]))]
}()

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
            exclude: ["CMakeLists.txt"]
        ),

        // MARK: Components
        
        .target(
            name: "llbuildBasic",
            dependencies: ["llvmSupport"],
            path: "lib/Basic",
            linkerSettings: [
                .linkedLibrary("android-spawn", .when(platforms: [.android]))
            ]
        ),
        .target(
            name: "llbuildCore",
            dependencies: [
                "llbuildBasic",
                .product(name: "SwiftToolchainCSQLite", package: "swift-toolchain-sqlite", condition: embeddedSqliteCondition),
            ],
            path: "lib/Core",
            linkerSettings: externalSqliteLibraries
        ),
        .target(
            name: "llbuildBuildSystem",
            dependencies: ["llbuildCore"],
            path: "lib/BuildSystem"
        ),
        .target(
            name: "llbuildNinja",
            dependencies: ["llbuildBasic"],
            path: "lib/Ninja"
        ),
        .target(
            name: "llbuildCommands",
            dependencies: ["llbuildCore", "llbuildBuildSystem", "llbuildNinja"],
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
            dependencies: ["llbuildBasic", "gmocklib"],
            path: "unittests/Basic",
            cxxSettings: [
                .headerSearchPath("../../utils/unittest/googlemock/include"),
                .headerSearchPath("../../utils/unittest/googletest/include"),
            ],
            linkerSettings: [
                .linkedLibrary("dl", .when(platforms: [.linux])),
                .linkedLibrary("pthread", .when(platforms: [.linux]))]),
        .target(
            name: "llbuildCoreTests",
            dependencies: [
                "llbuildCore",
                "gmocklib",
                .product(name: "SwiftToolchainCSQLite", package: "swift-toolchain-sqlite", condition: embeddedSqliteCondition),
            ],
            path: "unittests/Core",
            cxxSettings: [
                .headerSearchPath("../../utils/unittest/googlemock/include"),
                .headerSearchPath("../../utils/unittest/googletest/include"),
            ],
            linkerSettings: [
                .linkedLibrary("dl", .when(platforms: [.linux])),
                .linkedLibrary("pthread", .when(platforms: [.linux])),
            ] + externalSqliteLibraries),
        .target(
            name: "llbuildBuildSystemTests",
            dependencies: ["llbuildBuildSystem", "gmocklib"],
            path: "unittests/BuildSystem",
            cxxSettings: [
                .headerSearchPath("../../utils/unittest/googlemock/include"),
                .headerSearchPath("../../utils/unittest/googletest/include"),
            ],
            linkerSettings: [
                .linkedLibrary("dl", .when(platforms: [.linux])),
                .linkedLibrary("pthread", .when(platforms: [.linux]))]),
        .target(
            name: "llbuildNinjaTests",
            dependencies: ["llbuildNinja", "gmocklib"],
            path: "unittests/Ninja",
            cxxSettings: [
                .headerSearchPath("../../utils/unittest/googlemock/include"),
                .headerSearchPath("../../utils/unittest/googletest/include"),
            ],
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
                "gtest-matchers.cc",
                "gtest-port.cc",
                "gtest-printers.cc",
                "gtest-test-part.cc",
                "gtest-typed-test.cc",
                "gtest.cc",
            ],
            cxxSettings: [
                .headerSearchPath(".."),
                .headerSearchPath("../include"),
            ]),

        .target(
            name: "gmocklib",
            dependencies: ["gtestlib"],
            path: "utils/unittest/googlemock/src",
            exclude: [
                "gmock-cardinalities.cc",
                "gmock-internal-utils.cc",
                "gmock-matchers.cc",
                "gmock-spec-builders.cc",
                "gmock.cc",
            ],
            cxxSettings: [
                .headerSearchPath(".."),
                .headerSearchPath("../include"),
                .headerSearchPath("../../googletest/include"),
            ],
            linkerSettings: [
                .linkedLibrary("swiftCore", .when(platforms: [.windows])), // for swift_addNewDSOImage
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
            linkerSettings: [
                .linkedLibrary("m", .when(platforms: [.linux])),
            ] + terminfoLibraries
        ),
    ],
    cxxLanguageStandard: .cxx14
)

if !isStaticBuild {
    package.products += [
        .library(
            name: "llbuildSwiftDynamic",
            type: .dynamic,
            targets: ["llbuildSwift"]),
    ]
}

if ProcessInfo.processInfo.environment["SWIFTCI_USE_LOCAL_DEPS"] == nil {
    package.dependencies += [
        .package(url: "https://github.com/swiftlang/swift-toolchain-sqlite", from: "1.0.0"),
    ]
} else {
    package.dependencies += [
        .package(path: "../swift-toolchain-sqlite"),
    ]
}

let llvmTargets: Set<String> = [
    "libllbuild",
    "llbuildCore",

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
    "llbuildNinjaTests",

    "swift-build-tool",
]

if !useTerminfo {
    package.targets.filter({ llvmTargets.contains($0.name) }).forEach { target in
        target.cxxSettings = (target.cxxSettings ?? []) + [
            .define("LLBUILD_NO_TERMINFO"),
        ]
    }
}

// FIXME: Conditionalize these flags since SwiftPM 5.3 and earlier will crash for platforms they don't know about.
#if os(Windows)
package.targets.filter({ llvmTargets.contains($0.name) }).forEach { target in
    target.cxxSettings = (target.cxxSettings ?? []) + [
        .define("LLVM_ON_WIN32", .when(platforms: [.windows])),
        .define("_CRT_SECURE_NO_WARNINGS", .when(platforms: [.windows])),
        .define("_CRT_NONSTDC_NO_WARNINGS", .when(platforms: [.windows])),
    ]
}

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
