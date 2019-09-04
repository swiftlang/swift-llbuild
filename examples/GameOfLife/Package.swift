// swift-tools-version:4.0

import PackageDescription

let package = Package(
    name: "GameOfLife",
    products: [
        .executable(
            name: "game-of-life",
            targets: ["game-of-life"])
    ],
    dependencies: [
        .package(url: "../..", .branch("master")),
    ],
    targets: [
        .target(
            name: "game-of-life",
            dependencies: ["GameOfLife"]),

        .target(
            name: "GameOfLife",
            dependencies: ["llbuildSwift"]),
    ]
)
