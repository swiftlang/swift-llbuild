"""PyObjC Lit <-> XCTest Adaptor"""

import objc
import os
import re

import lit
import lit.discovery

# Ensure we have the appropriate environment variables
built_products_dir = os.environ.get("BUILT_PRODUCTS_DIR")
if not built_products_dir or not os.path.exists(built_products_dir):
   raise RuntimeError("invalid BUILT_PRODUCTS_DIR: %r" % (built_products_dir,))

# Load the Lit test suite using the unittest style discovery.
test_suite = lit.discovery.load_test_suite([os.path.join(built_products_dir,
                                                         "tests")])

# Inject test methods for each test.
def injectTestMethod(klass, test):
    test_name = test.id()
    
    # Mangle the test name into a valid name.
    method_name = "test" + re.sub(r"[^A-Za-z_0-9]", "_", test_name)
    
    # Inject the method.
    def runTest(obj):
        print "Running Lit test: %s" % (test.id(),)
        result = test.defaultTestResult()
        test.run(result)
        
        # Report an XCTest failure, if the test failed.
        if not result.wasSuccessful():
            # Get the underlying Lit result object.
            lit_result = test._test.result
            obj.recordFailureWithDescription_inFile_atLine_expected_(lit_result.output, test._test.getSourcePath(), 1, True)

    objc.classAddMethod(klass, method_name, runTest)

# Inject a test method for each test.
klass = objc.lookUpClass("LitTests")
for test in test_suite:
    injectTestMethod(klass, test)
