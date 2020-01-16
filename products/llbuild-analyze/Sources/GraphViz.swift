// This source file is part of the Swift.org open source project
//
// Copyright 2019 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for Swift project authors

import TSCUtility
import TSCBasic
import llbuildAnalysis
import llbuildSwift


protocol GraphVizNode {
    var graphVizName: String { get }
}

/// Struct to represent a directed edge in GraphViz from a -> b.
/// `hash()` and `==` only take the both edges into account, not
/// `isCritical`, so the graph can be represented as a `Set<DirectedEdge>`
/// and gurantee that there is only one edge between two verticies.
struct DirectedEdge: Hashable, Equatable {
    /// Source `BuildKey`
    let a: BuildKey

    /// Destination `BuildKey`
    let b: BuildKey

    /// Flag if the edge is on critical build path.
    let isCritical: Bool

    static func == (lhs: Self, rhs: Self) -> Bool {
        lhs.a == rhs.a && lhs.b == rhs.b
    }

    func hash(into hasher: inout Hasher) {
        a.hash(into: &hasher)
        b.hash(into: &hasher)
    }

    /// Style attributes for the edge.
    private var style: String {
        if isCritical {
            return "[style=bold]"
        }
        return ""
    }

    /// GraphViz representation of the Edge.
    var graphVizString: String {
        guard let a = a as? GraphVizNode, let b = b as? GraphVizNode else {
            fatalError("Both edges need to conform to GraphVizNode to generate a graphVizString for DirectedEdge \(self).")
        }
        return "\t\"\(a.graphVizName)\" -> \"\(b.graphVizName)\"\(style)\n"
    }

}

extension BuildKey.Command: GraphVizNode {
    var graphVizName: String {
        "Command:\(self.name)"
    }
}

extension BuildKey.CustomTask: GraphVizNode {
    var graphVizName: String {
        "CustomTask:\(self.name)"
    }
}

extension BuildKey.DirectoryContents: GraphVizNode {
    var graphVizName: String {
        "DirectoryContents:\(self.path)"
    }
}

extension BuildKey.FilteredDirectoryContents: GraphVizNode {
    var graphVizName: String {
        "FilteredDirectoryContents:\(self.path)"
    }
}

extension BuildKey.DirectoryTreeSignature: GraphVizNode {
    var graphVizName: String {
        "DirectoryTreeSignature:\(self.path)"
    }
}

extension BuildKey.DirectoryTreeStructureSignature: GraphVizNode {
    var graphVizName: String {
        "DirectoryTreeStructureSignature:\(self.path)"
    }
}

extension BuildKey.Node: GraphVizNode {
    var graphVizName: String {
        "Node:\(self.path)"
    }
}

extension BuildKey.Stat: GraphVizNode {
    var graphVizName: String {
        "Stat:\(self.path)"
    }
}

extension BuildKey.Target: GraphVizNode {
    var graphVizName: String {
        "Target:\(self.name)"
    }
}
