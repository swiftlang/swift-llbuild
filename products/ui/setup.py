import os
import setuptools
import sys

import llbuildui

# setuptools expects to be invoked from within the directory of setup.py, but it
# is nice to allow:
#   python path/to/setup.py install
# to work (for scripts, etc.)
os.chdir(os.path.dirname(os.path.abspath(__file__)))

setuptools.setup(
    name = "llbuild-ui",
    version = llbuildui.__version__,

    author = llbuildui.__author__,
    author_email = llbuildui.__email__,
    url = 'http://github.com/apple/swift-llbuild',
    license = 'Apache 2.0',

    description = "llbuild User Interface",
    keywords = 'buildsystem build systems llbuild swift',
    zip_safe = False,

    packages = setuptools.find_packages(),

    install_requires=['SQLAlchemy', 'Flask'],
)
