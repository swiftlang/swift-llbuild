import llbuild

typealias Compute = ([Int]) -> Int

class SimpleTask: Task {
  let inputs: [Key]
  var values: [Int]
  let compute: Compute
	
  init(_ inputs: [Key], compute: @escaping Compute) {
    self.inputs = inputs
    values = [Int](repeating: 0, count: inputs.count)
    self.compute = compute
  }
	
  func start(_ engine: TaskBuildEngine) {
    for (idx, input) in inputs.enumerated() {
      engine.taskNeedsInput(input, inputID: idx)
    }
  }
  
  func provideValue(_ engine: TaskBuildEngine, inputID: Int, value: Value) {
    values[inputID] = Int(value.toString())!
  }
  
  func inputsAvailable(_ engine: TaskBuildEngine) {
    let result = compute(values)
    engine.taskIsComplete(Value("\(result)"), forceChange: false)
  }
}

class SimpleBuildEngineDelegate: BuildEngineDelegate {
  var builtKeys = [Key]()
	
  func lookupRule(_ key: Key) -> Rule {
    switch key.toString() {
    case "A":
      return SimpleRule([]) { arr in
        precondition(self.builtKeys.isEmpty)
        self.builtKeys.append(key)
        return 2
      }
    case "B":
      return SimpleRule([]) { arr in
        precondition(self.builtKeys.count == 1)
        self.builtKeys.append(key)
        return 3
      }
    case "C":
      return SimpleRule([Key("A"), Key("B")]) { arr in 
        precondition(self.builtKeys.count == 2)
        precondition(self.builtKeys[0].toString() == "A")
        precondition(self.builtKeys[1].toString() == "B")
        self.builtKeys.append(key)
        return arr[0] * arr[1]
      }
      default: fatalError("Unexpected key \(key) lookup")
    }
  }
}

class SimpleRule: Rule {
  let inputs: [Key]
  let compute: Compute
  init(_ inputs: [Key], compute: @escaping Compute) { 
    self.inputs = inputs 
    self.compute = compute
  } 
  func createTask() -> Task {
    return SimpleTask(inputs, compute: compute)
  }
}

let delegate = SimpleBuildEngineDelegate()
var engine = BuildEngine(delegate: delegate)

// C depends on A and B
var result = engine.build(key: Key("C"))
print("\(result.toString())")

precondition(result.toString() == "6")

// Make sure building already built keys do not re-compute.
delegate.builtKeys.removeAll()
precondition(delegate.builtKeys.isEmpty)

result = engine.build(key: Key("A"))
precondition(result.toString() == "2")
precondition(delegate.builtKeys.isEmpty)

result = engine.build(key: Key("B"))
precondition(result.toString() == "3")
precondition(delegate.builtKeys.isEmpty)
