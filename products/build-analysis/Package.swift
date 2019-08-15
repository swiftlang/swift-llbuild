// swift-tools-version:5.1
// The swift-tools-version declares the minimum version of Swift required to build this package.

import PackageDescription

let package = Package(
    name: "BuildAnalysis",
    products: [
        .executable(
            name: "build-analysis",
            targets: ["BuildAnalysis"]),
    ],
    dependencies: [
        // We declare a local dependency on the llbuild Swift API
        .package(path: "../..")
    ],
    targets: [
        .target(
            name: "BuildAnalysis",
            dependencies: ["Commands"]),
        .target(
            name: "Commands",
            dependencies: ["Core", "llbuildAnalysis", "llbuildSwift"]),
        .target(
            name: "Core",
            dependencies: []),
        
        .testTarget(
            name: "CommandsTests",
            dependencies: ["Commands"]),
        .testTarget(
            name: "CoreTests",
            dependencies: ["Core"]),
    ]
)
