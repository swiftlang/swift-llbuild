// swift-tools-version:5.1

import PackageDescription

let package = Package(
    name: "cevobuild",
    platforms: [
       .macOS(.v10_15) 
    ],
    products: [
        .executable(
            name: "cevobuild",
            targets: ["cevobuild"]),
        .library(
            name: "CevoCore",
            targets: ["CevoCore"]),
        .library(
            name: "NinjaBuild",
            targets: ["NinjaBuild"]),
    ],
    dependencies: [
        .package(url: "https://github.com/apple/swift-argument-parser.git", from: "0.0.1"),
        .package(url: "https://github.com/apple/swift-crypto.git", from: "1.0.0"),
        .package(url: "https://github.com/apple/swift-llbuild.git", .branch("master")),
        .package(url: "https://github.com/apple/swift-nio.git", from: "2.8.0"),
        .package(url: "https://github.com/apple/swift-tools-support-core.git", from: "0.0.1"),
    ],
    targets: [
        .target(
            name: "cevobuild",
            dependencies: [
                "CevoCore", "NinjaBuild", "ArgumentParser"]),

        .target(
            name: "CevoCore",
            dependencies: ["Crypto", "NIO"]),
        .testTarget(
            name: "CevoCoreTests",
            dependencies: ["CevoCore"]),

        // Ninja Build support
        .target(
            name: "NinjaBuild",
            dependencies: [
                "CevoCore", "Ninja", "llbuild"]),
        .testTarget(
            name: "NinjaBuildTests",
            dependencies: [
                "NinjaBuild", "SwiftToolsSupport-auto"]),
    ]
)
