// This source file is part of the Swift.org open source project
//
// Copyright 2019 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for Swift project authors

public enum ParseError: Swift.Error, Equatable {
    case cantParse(description: String)
    case cantFindOption(option: ArgumentsParser.Option, args: [String])
    case tooManyArguments(rest: [String])
}

public protocol OptionParseable {
    static func construct(from raw: String) throws -> Self
}

extension Int: OptionParseable {
    public static func construct(from raw: String) throws -> Int {
        guard let value = Int(raw) else { throw ParseError.cantParse(description: #"Can't construct integer value from "\#(raw)"."#) }
        return value
    }
}

extension String: OptionParseable {
    public static func construct(from raw: String) throws -> String { raw }
}

extension Bool: OptionParseable {
    public static func construct(from raw: String) throws -> Bool {
        if raw.isEmpty { return false }
        if let number = Int(raw) { return number > 0 }
        return ["true", "yes"].contains(where: { $0 == raw.lowercased() })
    }
}

extension RawRepresentable where Self: CaseIterable, RawValue: OptionParseable {
    public static func construct(from raw: String) throws -> Self {
        guard let value = self.init(rawValue: try RawValue.construct(from: raw)) else {
            throw ParseError.cantParse(description: "Unknown raw value '\(raw)' for \(Self.self). Possible values: [\(Self.allCases.map({ "\($0.rawValue)" }).joined(separator: ", "))].")
        }
        return value
    }
}

public final class ArgumentsParser {
    
    public struct Option: Equatable {
        public enum Kind: Equatable {
            case short(short: String)
            case long(long: String)
            case both(short: String, long: String)
        }
        
        public let name: String
        public let required: Bool
        /// If `false`, the pure existence is enough, the raw value for init will then be empty string or the provided flag
        public let needsValue: Bool
        public let kind: Kind
        public let type: OptionParseable.Type
        
        public init(name: String, required: Bool = false, needsValue: Bool = true, kind: Kind, type: OptionParseable.Type) {
            self.name = name
            self.required = required
            self.needsValue = needsValue
            self.kind = kind
            self.type = type
        }
        
        public static func == (lhs: ArgumentsParser.Option, rhs: ArgumentsParser.Option) -> Bool {
            lhs.name == rhs.name && lhs.required == rhs.required && lhs.needsValue == rhs.needsValue && lhs.kind == rhs.kind && lhs.type == rhs.type
        }
    }
    
    public struct Values {
        private var storage: [String: Any] = [:]
        
        fileprivate mutating func insert(value: Any, for option: Option) {
            storage[option.name] = value
        }
        
        public subscript<T: OptionParseable>(_ option: Option) -> T {
            storage[option.name] as! T
        }
        
        public subscript<T: OptionParseable>(_ option: Option) -> T? {
            if option.required {
                return (storage[option.name] as! T)
            } else {
                return storage[option.name] as? T
            }
        }
    }
    
    public let options: [Option]
    
    public init(options: [Option]) {
        self.options = options
    }
    
    public func parse(args: [String]) throws -> Values {
        var argsCopy = args
        var results = Values()
        for option in options {
            let tryToFind: (inout [String], Option) throws -> Any? = { args, option in
                guard let index = args.firstIndex(of: option.kind) else { return nil }
                args.remove(at: index)
                if option.needsValue {
                    guard args.count > index else { return nil }
                    let rawValue = args.remove(at: index)
                    return try option.type.construct(from: rawValue)
                } else {
                    return try option.type.construct(from: "\(true)")
                }
            }
            
            if option.required {
                guard let value = try tryToFind(&argsCopy, option) else {
                    throw ParseError.cantFindOption(option: option, args: argsCopy)
                }
                results.insert(value: value, for: option)
            } else {
                if let value = try tryToFind(&argsCopy, option) {
                    results.insert(value: value, for: option)
                }
            }
        }
        guard argsCopy.isEmpty else {
            throw ParseError.tooManyArguments(rest: argsCopy)
        }
        return results
    }
}

private extension BidirectionalCollection where Element == String {
    func firstIndex(of kind: ArgumentsParser.Option.Kind) -> Self.Index? {
        switch kind {
        case let .short(short): return self.firstIndex(of: "-\(short)")
        case let .long(long): return self.firstIndex(of: "--\(long)")
        case let .both(short, long): return self.firstIndex(of: "-\(short)") ?? self.firstIndex(of: "--\(long)")
        }
    }
}
