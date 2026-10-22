// swift-tools-version:6.2

// This file defines Swift package manager support for llbuild. See:
//  https://github.com/swiftlang/swift-package-manager/tree/main/Documentation

import PackageDescription

let isStaticBuild = Context.environment["LLBUILD_STATIC_LINK"] != nil
let useEmbeddedSqlite = isStaticBuild || Context.environment["LLBUILD_USE_EMBEDDED_SQLITE"] != nil
let useTerminfo = !isStaticBuild && Context.environment["LLBUILD_NO_TERMINFO"] == nil
let useLocalDependencies = Context.environment["SWIFTCI_USE_LOCAL_DEPS"] != nil

// `Platform.freebsd` is not yet available to manifests at this tools version,
// so refer to it by its platform name.
let freebsd: Platform = .custom("freebsd")

/// Platforms where SQLite is provided by the system rather than embedded.
let systemSqlitePlatforms: [Platform] = [
    .macOS, .iOS, .tvOS, .watchOS, .visionOS, .macCatalyst, .linux, .openbsd, freebsd,
]

/// Platforms which need `-ldl -lpthread` to be linked explicitly.
let dlPthreadPlatforms: [Platform] = [.linux, freebsd]

// MARK: Shared settings

/// Use the embedded SQLite where there is no system SQLite (or always, when requested).
let embeddedSqliteCondition: TargetDependencyCondition? =
    useEmbeddedSqlite ? nil : .when(platforms: [.windows, .android])

let sqliteSettings: (cSettings: [CSetting], linkerSettings: [LinkerSetting]) = {
    if useEmbeddedSqlite {
        return ([], [])
    }
    return (
        [
            // On the BSDs, SQLite is installed from ports/packages into /usr/local.
            .unsafeFlags(["-I/usr/local/include"], .when(platforms: [.openbsd, freebsd])),
        ],
        [
            .linkedLibrary("sqlite3", .when(platforms: systemSqlitePlatforms)),
            .unsafeFlags(["-L/usr/local/lib"], .when(platforms: [.openbsd, freebsd])),
        ]
    )
}()

let terminfoLibraries: [LinkerSetting] =
    useTerminfo ? [.linkedLibrary("ncurses", .when(platforms: [.linux, .macOS, .openbsd, freebsd]))] : []

let dlPthreadLibraries: [LinkerSetting] = [
    .linkedLibrary("dl", .when(platforms: dlPthreadPlatforms)),
    .linkedLibrary("pthread", .when(platforms: dlPthreadPlatforms)),
]

/// C++ settings shared by all of the targets built from llbuild's C++ sources
/// (and the imported LLVM sources).
let llbuildCXXSettings: [CXXSetting] = [
    .define("LLVM_ON_WIN32", .when(platforms: [.windows])),
    .define("_CRT_SECURE_NO_WARNINGS", .when(platforms: [.windows])),
    .define("_CRT_NONSTDC_NO_WARNINGS", .when(platforms: [.windows])),
] + (useTerminfo ? [] : [.define("LLBUILD_NO_TERMINFO")])

// MARK: Package

let package = Package(
    name: "llbuild",
    platforms: [
        .macOS(.v10_13), .iOS(.v12),
    ],
    products: [
        /// The llbuild multitool (primarily for testing).
        ///
        /// The product can't be named `llbuild` because that name belongs to
        /// the Swift API library product.
        .executable(
            name: "llbuild-tool",
            targets: ["llbuild"]),

        /// The public llbuild C API.
        .library(
            name: "cllbuild",
            targets: ["libllbuild"]),

        /// The public llbuild Swift API.
        .library(
            name: "llbuild",
            targets: ["llbuildSwift"]),

        .library(
            name: "llbuildAnalysis",
            targets: ["llbuildAnalysis"]),

        // MARK: Deprecated products
        //
        // These products are kept for compatibility with existing clients.
        // SwiftPM has no way to mark a product as deprecated, so new clients
        // should use the products above instead.

        /// Deprecated: use the `cllbuild` product instead.
        .library(
            name: "libllbuild",
            targets: ["libllbuild"]),

        /// Deprecated: use the `llbuild` product instead.
        .library(
            name: "llbuildSwift",
            targets: ["llbuildSwift"]),
    ],
    dependencies: [
        useLocalDependencies
            ? .package(path: "../swift-toolchain-sqlite")
            : .package(url: "https://github.com/swiftlang/swift-toolchain-sqlite", from: "1.0.0"),
    ],
    targets: [
        // MARK: Products

        /// The llbuild multitool (primarily for testing).
        .executableTarget(
            name: "llbuild",
            dependencies: ["llbuildCommands"],
            path: "products/llbuild",
            exclude: ["CMakeLists.txt"],
            cxxSettings: llbuildCXXSettings,
            linkerSettings: dlPthreadLibraries
        ),

        /// The custom build tool used by the Swift package manager (SwiftPM).
        ///
        /// SwiftPM has now switched to using llbuild's Swift bindings API to
        /// build, but this tool is still used for SwiftPM's bootstrapping. Once
        /// that step has been eliminated, this tool can be removed.
        .executableTarget(
            name: "swift-build-tool",
            dependencies: ["llbuildBuildSystem"],
            path: "products/swift-build-tool",
            exclude: ["CMakeLists.txt"],
            cxxSettings: llbuildCXXSettings,
            linkerSettings: dlPthreadLibraries
        ),

        /// The public llbuild C API.
        .target(
            name: "libllbuild",
            dependencies: ["llbuildCore", "llbuildBuildSystem", "llbuildNinja"],
            path: "products/libllbuild",
            exclude: ["CMakeLists.txt"],
            cxxSettings: llbuildCXXSettings
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
            exclude: ["CMakeLists.txt"],
            cxxSettings: llbuildCXXSettings,
            linkerSettings: [
                .linkedLibrary("ShLwApi", .when(platforms: [.windows])),
            ]
        ),
        .target(
            name: "llbuildCore",
            dependencies: [
                "llbuildBasic",
                .product(name: "SwiftToolchainCSQLite", package: "swift-toolchain-sqlite", condition: embeddedSqliteCondition),
            ],
            path: "lib/Core",
            exclude: ["CMakeLists.txt"],
            cSettings: sqliteSettings.cSettings,
            cxxSettings: llbuildCXXSettings,
            linkerSettings: sqliteSettings.linkerSettings
        ),
        .target(
            name: "llbuildBuildSystem",
            dependencies: ["llbuildCore"],
            path: "lib/BuildSystem",
            exclude: ["CMakeLists.txt"],
            cxxSettings: llbuildCXXSettings
        ),
        .target(
            name: "llbuildNinja",
            dependencies: ["llbuildBasic"],
            path: "lib/Ninja",
            exclude: ["CMakeLists.txt"],
            cxxSettings: llbuildCXXSettings
        ),
        .target(
            name: "llbuildCommands",
            dependencies: ["llbuildCore", "llbuildBuildSystem", "llbuildNinja"],
            path: "lib/Commands",
            exclude: ["CMakeLists.txt"],
            cxxSettings: llbuildCXXSettings
        ),

        // MARK: Analysis Components

        .target(
            name: "llbuildAnalysis",
            dependencies: ["llbuildSwift"],
            path: "lib/Analysis"
        ),

        // MARK: Test Targets

        .executableTarget(
            name: "llbuildBasicTests",
            dependencies: ["llbuildBasic", "llbuildUnitTestSupport", "gmocklib", "llbuildUnitTestMain"],
            path: "unittests/Basic",
            exclude: ["CMakeLists.txt"],
            cxxSettings: llbuildCXXSettings,
            linkerSettings: dlPthreadLibraries
        ),
        .executableTarget(
            name: "llbuildCoreTests",
            dependencies: [
                "llbuildCore",
                "gmocklib",
                "llbuildUnitTestMain",
                .product(name: "SwiftToolchainCSQLite", package: "swift-toolchain-sqlite", condition: embeddedSqliteCondition),
            ],
            path: "unittests/Core",
            exclude: ["CMakeLists.txt"],
            cSettings: sqliteSettings.cSettings,
            cxxSettings: llbuildCXXSettings,
            linkerSettings: dlPthreadLibraries + sqliteSettings.linkerSettings
        ),
        .executableTarget(
            name: "llbuildBuildSystemTests",
            dependencies: ["llbuildBuildSystem", "llbuildUnitTestSupport", "gmocklib", "llbuildUnitTestMain"],
            path: "unittests/BuildSystem",
            exclude: ["CMakeLists.txt"],
            cxxSettings: llbuildCXXSettings,
            linkerSettings: dlPthreadLibraries
        ),
        .executableTarget(
            name: "llbuildNinjaTests",
            dependencies: ["llbuildNinja", "gmocklib", "llbuildUnitTestMain"],
            path: "unittests/Ninja",
            exclude: ["CMakeLists.txt"],
            cxxSettings: llbuildCXXSettings,
            linkerSettings: dlPthreadLibraries
        ),
        .testTarget(
            name: "llbuildSwiftTests",
            dependencies: ["llbuildSwift", "llbuildTestSupport"],
            path: "unittests/Swift",
            linkerSettings: dlPthreadLibraries
        ),
        .testTarget(
            name: "AnalysisTests",
            dependencies: ["llbuildAnalysis"],
            path: "unittests/Analysis",
            linkerSettings: dlPthreadLibraries
        ),

        .target(
            name: "llbuildTestSupport",
            path: "unittests/TestSupport"
        ),

        /// Shared support code for the C++ unit tests.
        .target(
            name: "llbuildUnitTestSupport",
            dependencies: ["llbuildBasic"],
            path: "unittests/UnitTestSupport",
            cxxSettings: llbuildCXXSettings
        ),

        /// The `main` entry point shared by the C++ unit test executables.
        .target(
            name: "llbuildUnitTestMain",
            dependencies: ["llvmSupport", "gmocklib"],
            path: "utils/unittest/UnitTestMain",
            exclude: ["CMakeLists.txt"],
            // This target has no headers of its own.
            publicHeadersPath: ".",
            cxxSettings: llbuildCXXSettings
        ),

        // MARK: GoogleTest

        .target(
            name: "gtestlib",
            path: "utils/unittest/googletest",
            sources: ["src/gtest-all.cc"],
            cxxSettings: [
                .headerSearchPath("."),
            ]
        ),

        .target(
            name: "gmocklib",
            dependencies: ["gtestlib"],
            path: "utils/unittest/googlemock",
            sources: ["src/gmock-all.cc"],
            cxxSettings: [
                .headerSearchPath("."),
            ],
            linkerSettings: [
                .linkedLibrary("swiftCore", .when(platforms: [.windows])), // for swift_addNewDSOImage
            ]
        ),

        // MARK: Ingested LLVM code.

        .target(
            name: "llvmDemangle",
            path: "lib/llvm/Demangle",
            exclude: ["CMakeLists.txt"],
            cxxSettings: llbuildCXXSettings
        ),

        .target(
            name: "llvmSupport",
            dependencies: ["llvmDemangle"],
            path: "lib/llvm/Support",
            exclude: ["CMakeLists.txt"],
            cxxSettings: llbuildCXXSettings,
            linkerSettings: [
                .linkedLibrary("m", .when(platforms: [.linux, freebsd])),
                .linkedLibrary("pthread", .when(platforms: [freebsd])),
                .linkedLibrary("execinfo", .when(platforms: [.openbsd, freebsd])),
            ] + terminfoLibraries
        ),
    ],
    swiftLanguageModes: [.v5],
    cxxLanguageStandard: .cxx14
)

if !isStaticBuild {
    package.products += [
        /// Deprecated: kept for compatibility with existing clients.
        .library(
            name: "llbuildSwiftDynamic",
            type: .dynamic,
            targets: ["llbuildSwift"]),
    ]
}
