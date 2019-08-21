Pod::Spec.new do |s|

  s.name         = "llbuild"
  s.version      = ENV['LLBUILD_PODSPEC_VERSION'] || "9999.0.0"
  s.summary      = "A low-level build system."

  s.description  = <<-DESC.strip_heredoc
                    **llbuild** is a set of libraries for building build systems. Unlike most build
                    system projects which focus on the syntax for describing the build, llbuild is
                    designed around a reusable, flexible, and scalable general purpose *build
                    engine* capable of solving many "build system"-like problems. The project also
                    includes additional libraries on top of that engine which provide support for
                    constructing *bespoke* build systems (like `swift build`) or for building from
                    Ninja manifests.
                   DESC

  s.homepage     = "https://github.com/apple/swift-llbuild"
  s.license      = { type: 'Apache 2.0', file: "LICENSE.txt" }

  s.documentation_url  = "https://llbuild.readthedocs.io/"
  s.author             = "Apple"

  s.ios.deployment_target     = "9.0"
  s.osx.deployment_target     = "10.10"

  s.source = { git: "https://github.com/apple/swift-llbuild.git",
               tag: s.version }

  s.default_subspecs = ['Swift']
  s.pod_target_xcconfig = { 
    'OTHER_CFLAGS' => '-I${PODS_TARGET_SRCROOT}/include', 
    'GCC_C_LANGUAGE_STANDARD' => 'c11',
    'CLANG_CXX_LANGUAGE_STANDARD' => 'c++14',
    'CLANG_CXX_LIBRARY' => 'libc++',
  }

  s.subspec 'Swift' do |sp|
    sp.source_files = 'products/llbuildSwift/**/*.swift'
    sp.dependency 'llbuild/Library'
  end

  s.subspec 'Library' do |sp|
    sp.source_files = 'products/libllbuild/**/*.cpp', 'products/libllbuild/include/llbuild/*.h'

    # the first is an 'umbrella header', the rest have to be public because 
    # otherwise modular header warnings abound
    sp.public_header_files = 'products/libllbuild/include/llbuild/llbuild.h', 'products/libllbuild/include/llbuild/*.h'
    sp.preserve_paths = 'products/libllbuild/BuildKey-C-API-Private.h'
    
    sp.dependency 'llbuild/Core'
    sp.osx.dependency 'llbuild/BuildSystem' 
  end

  s.subspec 'Core' do |sp|
    sp.source_files = 'lib/Core/**/*.cpp'
    # internal header files, used this way to prevent header clash between subspecs
    sp.preserve_paths = 'include/llbuild/Core', 'lib/Core/**/*.h'

    sp.libraries = 'sqlite3'
    sp.dependency 'llbuild/Basic'
  end

  s.subspec 'Basic' do |sp|
    sp.osx.source_files = 'lib/Basic/**/*.cpp'
    sp.ios.source_files = 'lib/Basic/**/{PlatformUtility,Tracing,Version}.cpp'

    # internal header files, used this way to prevent header clash between subspecs
    sp.preserve_paths = 'include/llbuild/Basic', 'lib/Basic/**/*.h'
    sp.exclude_files = 'include/llbuild/Basic/LeanWindows.h'

    sp.dependency 'llbuild/llvmSupport'
  end

  s.subspec 'BuildSystem' do |sp|
    sp.source_files = 'lib/BuildSystem/**/*.cpp'
    # internal header files, used this way to prevent header clash between subspecs
    sp.preserve_paths = 'include/llbuild/BuildSystem', 'lib/BuildSystem/**/*.h'
    sp.compiler_flags = '-I${PODS_TARGET_SRCROOT}/include'

    sp.dependency 'llbuild/Core'
  end

  s.subspec 'llvmSupport' do |sp|
    sp.source_files = 'lib/llvm/{Support,Demangle}/**/*.cpp'
    sp.ios.exclude_files = [
      'lib/llvm/Support/CommandLine.cpp',
      'lib/llvm/Support/YAMLParser.cpp',
      'lib/llvm/Support/SourceMgr.cpp',
      'lib/llvm/Support/Atomic.cpp',
    ]
    # internal header files, used this way to prevent header clash between subspecs
    sp.preserve_paths = 'include/llvm', 'include/llvm-c', 'lib/llvm/Support/Unix', 'lib/llvm/Demangle/**/*.h'

    s.osx.libraries = 'ncurses'
  end
end
