import orderedset

def find_cycle(nodes, successors):
    path = orderedset.orderedset()
    visited = set()
    
    def visit(node):
        # If the node is already in the current path, we have found a cycle.
        if not path.add(node):
            return (path, node)

        # If we have otherwise already visited this node, we don't need to visit
        # it again.
        if node in visited:
            item = path.pop()
            assert item == node
            return
        visited.add(node)
        
        # Otherwise, visit all the successors.
        for succ in successors(node):
            cycle = visit(succ)
            if cycle is not None:
                return cycle

        item = path.pop()
        assert item == node
        return None
            
    for node in nodes:
        cycle = visit(node)
        if cycle is not None:
            return cycle
        else:
            assert not path.items

    return None
