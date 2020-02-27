// swift-tools-version:5.0

// This file defines Swift package manager support for llbuild-analyze. See:
//  https://github.com/apple/swift-package-manager/tree/master/Documentation

import PackageDescription

let package = Package(
    name: "llbuild-analyze",
    platforms: [
        .macOS(.v10_10), .iOS(.v9),
    ],
    products: [
        .executable(
            name: "llbuild-analyze",
            targets: ["llbuildAnalyzeTool"]),
    ],
    dependencies: [
        .package(url: "https://github.com/apple/swift-argument-parser.git", from: "0.0.1"),
        .package(url: "https://github.com/apple/swift-tools-support-core.git", .branch("master")),
        .package(path: "../../"),
    ],
    targets: [
        .target(
            name: "llbuildAnalyzeTool",
            dependencies: ["SwiftToolsSupport-auto", "llbuildAnalysis", "ArgumentParser"],
            path: "Sources"),
    ]
)
