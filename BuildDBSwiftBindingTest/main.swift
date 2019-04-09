//
//  main.swift
//  BuildDBSwiftBindingTest
//
//  Created by Benjamin Herzog on 4/5/19.
//  Copyright © 2019 Apple Inc. All rights reserved.
//

import llbuild
import Foundation

let path = "/Users/benjaminherzog/Library/Developer/Xcode/DerivedData/Xcode-dsrbecsbudaaencdxnuiisivqcqe/Build/Intermediates.noindex/XCBuildData/build.db"

class Delegate: BuildDBDelegate {
  var allKeys: BuildDBFetchKeysResult?
  
  func getKey(id: KeyID) -> KeyType {
    return allKeys?.getKey(id: id) ?? ""
  }
  func getKeyID(key: KeyType) -> KeyID {
    return allKeys?.getKeyID(key: key) ?? 0
  }
}

do {
  let delegate = Delegate()
  try Database(path: path, clientSchemaVersion: 9, delegate: delegate)
    .startSession { db in
      delegate.allKeys = try db.getKeys()
      dump(try db.lookupRuleResult(keyID: 2))
    }
  
} catch {
  dump(error)
}


