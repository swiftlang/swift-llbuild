// This source file is part of the Swift.org open source project
//
// Copyright 2019 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for Swift project authors

/// OptionForm specifies if an option can be defined with a -short form, --longform or both.
public enum OptionForm: Equatable {
    case short(short: String)
    case long(long: String)
    case both(short: String, long: String)
}

public enum ParseError: Swift.Error, Equatable {
    case cantParse(description: String)
    case cantFindRequiredValue(form: OptionForm, args: [String])
    case cantFindOption(form: OptionForm, args: [String])
    case tooManyArguments(rest: [String])
}

/// Binder enables type safe bindings of options
public final class Binder<O: OptionsType> {
    fileprivate struct Binding {
        let form: OptionForm
        let required: Bool
        let isOption: Bool
        let typeName: String
        let binding: (inout O, String) throws -> Void
    }
    fileprivate private(set) var bindings: [Binding] = []
    
    /// Binds an option with the given arguments.
    /// - Parameter form: The form of option
    /// - Parameter keyPath: The keyPath that should be used after successful mapping
    /// - Parameter required: `true` if the option needs to be passed in every invocation
    /// - Parameter isOption: `true` if the binding doesn't need a positional value
    public func bind<Type>(form: OptionForm, keyPath: WritableKeyPath<O, Type>, required: Bool = false, isOption: Bool = false) where Type : OptionParsable {
        bindings.append(Binding(form: form, required: required, isOption: isOption, typeName: "\(Type.self)", binding: { options, input in
            options[keyPath: keyPath] = try Type.init(raw: input)
        }))
    }
    
    public static var usage: String {
        var binder = Binder()
        let options = O()
        options.bind(to: &binder)
        return binder.bindings.map { binding in
            let formUsage: String
            switch binding.form {
            case let .short(short): formUsage = "[ -\(short) ]"
            case let .long(long): formUsage = "[ --\(long) ]"
            case let .both(short, long): formUsage = "[ -\(short) | --\(long) ]"
            }
            return "\t\(formUsage) \(binding.isOption ? "" : "value") (\(binding.typeName)) \(binding.required ? "[required]" : "")"
        }.joined(separator: "\n")
    }
}

/// Parses the given command line into a type that conforms to OptionsType.
/// - Parameter args: the command line arguments without the command name
public func parseCommandLine<Options: OptionsType>(args: [String]) throws -> Options {
    var argsCopy = args
    var results = Options()
    var binder = Binder<Options>()
    results.bind(to: &binder)
    for binding in binder.bindings {
        
        guard let index = argsCopy.firstIndex(of: binding.form) else {
            if binding.required {
                throw ParseError.cantFindOption(form: binding.form, args: argsCopy)
            }
            continue
        }
        argsCopy.remove(at: index)
        let value: String
        if binding.isOption {
            value = "\(true)"
        } else {
            if argsCopy.count <= index {
                throw ParseError.cantFindRequiredValue(form: binding.form, args: argsCopy)
            }
            value = argsCopy.remove(at: index)
        }
        try binding.binding(&results, value)
    }
    guard argsCopy.isEmpty else {
        throw ParseError.tooManyArguments(rest: argsCopy)
    }
    return results
}

extension BidirectionalCollection where Element == String {
    func firstIndex(of option: OptionForm) -> Self.Index? {
        switch option {
        case let .short(short): return firstIndex(of: "-\(short)")
        case let .long(long): return firstIndex(of: "--\(long)")
        case let .both(short, long): return firstIndex(of: "-\(short)") ?? firstIndex(of: "--\(long)")
        }
    }
}
