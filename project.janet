(declare-project
  :name "Watchful"
  :description "A file system watching utility"
  :author "Michael Camilleri"
  :license "MIT"
  :url "https://github.com/pyrmont/watchful"
  :repo "git+https://github.com/pyrmont/watchful"
  :dependencies ["https://github.com/pyrmont/testament"])


(def cflags
  [])


(def platform-cflags
  (case (os/which)
   :macos
   ["-mmacosx-version-min=10.12" "-DMACOS=1" "-framework" "CoreServices" "-Wno-unused-parameter" "-Wno-unused-command-line-argument"]

   :linux
   ["-DLINUX=1" "-pthread" "-Wno-unused-parameter"]

   ["-Wno-unused-parameter"]))


(def lflags
  [])


(def platform-lflags
  [])


(declare-native
  :name "_watchful"
  :cflags [;default-cflags ;cflags ;platform-cflags]
  :lflags [;default-lflags ;lflags ;platform-lflags]
  :headers @["src/watchful.h"
             "wrappers/janet/wrapper.h"]
  :source @["src/backends/fse.c"
            "src/backends/inotify.c"
            "src/wildmatch.c"
            "src/watchful.c"
            "wrappers/janet/functions.c"
            "wrappers/janet/monitor.c"])


(declare-source
  :source ["wrappers/janet/watchful.janet"])
