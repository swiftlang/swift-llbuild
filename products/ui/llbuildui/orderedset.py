class orderedset(object):
    def __init__(self):
        self.items = []
        self.set = set()

    def add(self, item):
        if item in self.set:
            return False
        self.items.append(item)
        self.set.add(item)
        return True

    def pop(self):
        item = self.items.pop()
        self.set.remove(item)
        return item
