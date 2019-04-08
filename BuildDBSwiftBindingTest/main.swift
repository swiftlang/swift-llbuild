//
//  main.swift
//  BuildDBSwiftBindingTest
//
//  Created by Benjamin Herzog on 4/5/19.
//  Copyright © 2019 Apple Inc. All rights reserved.
//

import llbuild

class DBDelegate: BuildDBDelegate {
  func getKeyID(key: KeyType) -> KeyID {
    return 1
  }
  
  func getKey(id: KeyID) -> KeyType {
    return "foobar"
  }
}


let delegate = DBDelegate()

let path = "/Users/benjaminherzog/Library/Developer/Xcode/DerivedData/MyLibrary-dlfxkvgmyqjkrzewapdqjktwlglm/Build/Intermediates.noindex/XCBuildData/build.db"

do {
  let db = try BuildDB(path: path, clientSchemaVersion: 9, delegate: delegate)
  
  for element in try db.getKeys() {
    dump(element)
  }
} catch {
  dump(error)
}


