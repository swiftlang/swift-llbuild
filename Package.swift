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
            name: "llbuild",
            targets: ["llbuildBasic"]),
    ],
    targets: [
        /// The llbuild testing tool.
        ///
        /// We have a name collision between this tool, and the library, so we
        /// have to give it a separate name when built as a Swift package.
        .target(
            name: "llbuild-tool",
            dependencies: ["llbuildCommands"],
            path: "products/llbuild"
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

        // MARK: Ingested LLVM code.
        .target(
            name: "llvmSupport",
            path: "lib/llvm/Support"
        ),
    ],
    cxxLanguageStandard: .cxx14
)
