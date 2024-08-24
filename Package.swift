// swift-tools-version:5.3

// This file defines Swift package manager support for llbuild. See:
//  https://github.com/swiftlang/swift-package-manager/tree/master/Documentation

import PackageDescription
import class Foundation.ProcessInfo

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
            exclude: ["CMakeLists.txt"]
        ),

        // MARK: Components
        
        .target(
            name: "llbuildBasic",
            dependencies: ["llvmSupport"],
            path: "lib/Basic"
        ),
        .target(
            name: "llbuildCore",
            dependencies: [
                "llbuildBasic",
                .product(name: "CSQLite", package: "swift-toolchain-sqlite", condition: .when(platforms: [.windows])),
            ],
            path: "lib/Core",
            linkerSettings: [
                .linkedLibrary("sqlite3", .when(platforms: [.macOS, .iOS, .tvOS, .watchOS, .visionOS, .macCatalyst, .linux]))
            ]
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
            dependencies: ["llbuildCore", "gmocklib"],
            path: "unittests/Core",
            cxxSettings: [
                .headerSearchPath("../../utils/unittest/googlemock/include"),
                .headerSearchPath("../../utils/unittest/googletest/include"),
            ],
            linkerSettings: [
                .linkedLibrary("dl", .when(platforms: [.linux])),
                .linkedLibrary("pthread", .when(platforms: [.linux]))]),
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
                .linkedLibrary("ncurses", .when(platforms: [.linux, .macOS, .android]))]
        ),
    ],
    cxxLanguageStandard: .cxx14
)

if ProcessInfo.processInfo.environment["SWIFTCI_USE_LOCAL_DEPS"] == nil {
    package.dependencies += [
        .package(url: "https://github.com/swiftlang/swift-toolchain-sqlite", from: "0.1.0"),
    ]
} else {
    package.dependencies += [
        .package(path: "../swift-toolchain-sqlite"),
    ]
}

// FIXME: Conditionalize these flags since SwiftPM 5.3 and earlier will crash for platforms they don't know about.
#if os(Windows)

do {
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

    package.targets.filter({ llvmTargets.contains($0.name) }).forEach { target in
        target.cxxSettings = (target.cxxSettings ?? []) + [
            .define("LLVM_ON_WIN32", .when(platforms: [.windows])),
            .define("_CRT_SECURE_NO_WARNINGS", .when(platforms: [.windows])),
            .define("_CRT_NONSTDC_NO_WARNINGS", .when(platforms: [.windows])),
        ]
    }
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
