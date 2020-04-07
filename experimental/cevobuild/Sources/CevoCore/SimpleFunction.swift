// This source file is part of the Swift.org open source project
//
// Copyright (c) 2020 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors

import NIO

public class SimpleFunction: Function {
    let action: (_ fi: FunctionInterface, _ key: Key) -> EventLoopFuture<Value>

    public init(action: @escaping (_ fi: FunctionInterface, _ key: Key) -> EventLoopFuture<Value>) {
        self.action = action
    }

    public func compute(key: Key, _ fi: FunctionInterface) -> EventLoopFuture<Value> {
        return action(fi, key)
    }
}
