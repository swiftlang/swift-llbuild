#include <stdlib.h>
#include <string>
int main(int argc, char** argv) {
  std::string args;
  for (int i = 1; i < argc; i++) {
    args += " \"";
    args += argv[i];
    args += "\"";
  }

#if defined(_WIN32)
  system(("python SOURCEDIR/Inputs/pseudo-swiftc" + args).c_str());
#else
  system(("SOURCEDIR/Inputs/pseudo-swiftc" + args).c_str());
#endif
  return 0;
}
