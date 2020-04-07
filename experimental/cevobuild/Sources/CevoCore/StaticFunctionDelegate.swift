// This source file is part of the Swift.org open source project
//
// Copyright (c) 2020 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors

import NIO

extension String: Key {}

public class StaticFunctionDelegate: EngineDelegate {
    let keyMap: [String: Function]

    public init(keyMap: [String: Function]) {
        self.keyMap = keyMap
    }

    public func lookupFunction(forKey key: Key, group: EventLoopGroup) -> EventLoopFuture<Function> {
        let stringKey = key as! String
        return group.next().makeSucceededFuture(keyMap[stringKey]!)
    }
}
