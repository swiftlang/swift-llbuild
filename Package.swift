// swift-tools-version:4.0

// This file defines Swift package manager support for llbuild. See:
//  https://github.com/apple/swift-package-manager/tree/master/Documentation
//
// You can build using:
//
// ```shell
// swift build -Xlinker -lsqlite3 -Xlinker -lncurses
// ```

import PackageDescription

let package = Package(
    name: "llbuild",
    products: [
        .library(
            name: "libllbuild",
            targets: ["libllbuild"]),
        .library(
            name: "llbuildSwift",
            targets: ["llbuildSwift"]),
    ],
    targets: [
        /// The llbuild testing tool.
        .target(
            name: "llbuild",
            dependencies: ["llbuildCommands"],
            path: "products/llbuild"
        ),

        /// The custom build tool used by the Swift package manager.
        .target(
            name: "swift-build-tool",
            dependencies: ["llbuildBuildSystem"],
            path: "products/swift-build-tool"
        ),

        /// The custom build tool used by the Swift package manager.
        .target(
            name: "llbuildSwift",
            dependencies: ["libllbuild"],
            path: "products/llbuildSwift",
            exclude: ["llbuild.swift"]
        ),

        /// The public llbuild API.
        .target(
            name: "libllbuild",
            dependencies: ["llbuildCore", "llbuildBuildSystem"],
            path: "products/libllbuild"
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
            path: "lib/Core"
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

        // MARK: Test Targets

        .target(
            name: "llbuildBasicTests",
            dependencies: ["llbuildBasic", "gtest"],
            path: "unittests/Basic"),
        .target(
            name: "llbuildCoreTests",
            dependencies: ["llbuildCore", "gtest"],
            path: "unittests/Core"),
        .target(
            name: "llbuildBuildSystemTests",
            dependencies: ["llbuildBuildSystem", "gtest"],
            path: "unittests/BuildSystem"),
        .target(
            name: "llbuildNinjaTests",
            dependencies: ["llbuildNinja", "gtest"],
            path: "unittests/Ninja"),
        
        // MARK: GoogleTest

        .target(
            name: "gtest",
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
            name: "llvmSupport",
            path: "lib/llvm/Support"
        ),
    ],
    cxxLanguageStandard: .cxx14
)
