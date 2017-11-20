// swift-tools-version:4.0

import PackageDescription

let package = Package(
    name: "GameOfLife",
    dependencies: [
        .package(url: "https://github.com/swift-server/http", from: "0.0.0"),
        .package(url: "../..", .branch("master")),
    ],
    targets: [
        .target(
            name: "LifeServer",
            dependencies: ["GameOfLife", "HTTP"]),

        .target(
            name: "GameOfLife",
            dependencies: ["llbuildSwift"]),
    ]
)
