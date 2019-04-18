//
//  main.swift
//  BuildDBSwiftBindingTest
//
//  Copyright © 2019 Apple Inc. All rights reserved.
//

import llbuild
import Foundation

let path = "/Users/benjaminherzog/Library/Developer/Xcode/DerivedData/TEMPApp-bairfqxgkpxvqbglrdmnhebkyovk/Build/Intermediates.noindex/XCBuildData/build.db"

do {
  let db = try BuildDB(path: path, clientSchemaVersion: 9)
  
  for key in try db.getKeys() {
    print(key)
  }
  
} catch {
  dump(error)
}


